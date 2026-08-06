#include "gfx_backend.h"
#include "logger.h"
#include "system_window.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openre::gfx
{
    namespace
    {
        // GPU backend: creates an SDL_GPU device, claims the game window and
        // presents a cleared swapchain (M2). The surface layer (M3) tracks
        // DirectDraw surfaces as SDL_GPU textures with CPU staging buffers and
        // handles CreateSurface/Lock/Unlock/Blt/GetSurfaceDesc. Device/scene/
        // draw methods arrive in later milestones and stay as stubs. Present
        // only runs while the GPU backend is the active backend, so the
        // DirectDraw primary surface keeps showing when the D3D reference
        // backend (0) is selected.
        class GfxBackendGPU final : public GfxBackend
        {
        private:
            // One entry per DirectDraw surface the game created. The GPU
            // texture holds the pixels; the CPU shadow plus two transfer
            // buffers implement Lock (readback) / Unlock (upload). Declared
            // first so the surface-layer helper signatures can reference it.
            struct SurfaceEntry
            {
                SDL_GPUTexture* texture = nullptr;               // GPU-side pixels
                SDL_GPUTransferBuffer* uploadBuffer = nullptr;   // shadow -> texture
                SDL_GPUTransferBuffer* downloadBuffer = nullptr; // texture -> shadow
                std::vector<uint8_t> shadow;                     // CPU copy in the game's pixel format
                Uint32 width = 0;
                Uint32 height = 0;
                Uint32 bpp = 0;   // 16 or 32
                Uint32 pitch = 0; // width * bpp / 8 (DirectDraw row pitch)
                SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
                bool textureCreated = false;
                bool hasContent = false; // texture written at least once
                bool locked = false;
            };

        public:
            bool init() override
            {
                mWindow = static_cast<SDL_Window*>(system::window::get_window());
                if (mWindow == nullptr)
                {
                    logging::logError("[gfx:gpu] init failed: no SDL window available");
                    return false;
                }

                mDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
                if (mDevice == nullptr)
                {
                    logging::logError("[gfx:gpu] SDL_CreateGPUDevice failed: {}", SDL_GetError());
                    return false;
                }
                logging::logInfo("[gfx:gpu] device created (driver={})", SDL_GetGPUDeviceDriver(mDevice));

                if (!SDL_ClaimWindowForGPUDevice(mDevice, mWindow))
                {
                    logging::logError("[gfx:gpu] SDL_ClaimWindowForGPUDevice failed: {}", SDL_GetError());
                    SDL_DestroyGPUDevice(mDevice);
                    mDevice = nullptr;
                    return false;
                }

                const auto format = SDL_GetGPUSwapchainTextureFormat(mDevice, mWindow);
                logging::logInfo(
                    "[gfx:gpu] window claimed, swapchain format={} (M2: swapchain ready)", static_cast<int>(format));
                return true;
            }

            void shutdown() override
            {
                if (mDevice != nullptr)
                {
                    // Every transfer is submitted synchronously and waited on,
                    // so no command buffer can still reference the resources;
                    // wait once anyway to be safe before releasing them.
                    SDL_WaitForGPUIdle(mDevice);
                    for (auto& pair : mSurfaces)
                        releaseSurface(pair.second);
                    mSurfaces.clear();
                    if (mWindow != nullptr)
                        SDL_ReleaseWindowFromGPUDevice(mDevice, mWindow);
                    SDL_DestroyGPUDevice(mDevice);
                    mDevice = nullptr;
                }
                mWindow = nullptr;
                logging::logInfo("[gfx:gpu] shutdown (surfaces released, swapchain released, device destroyed)");
            }

            void present() override
            {
                // While the D3D reference backend is active the DirectDraw
                // primary surface (the game's Blt in flip_blt) owns the window;
                // do not acquire/present the swapchain in that case.
                if (active_backend() != 1)
                    return;
                if (mDevice == nullptr || mWindow == nullptr)
                {
                    logging::logDebug("[gfx:gpu] present skipped (device/window not ready)");
                    return;
                }

                auto* commandBuffer = SDL_AcquireGPUCommandBuffer(mDevice);
                if (commandBuffer == nullptr)
                {
                    logging::logError("[gfx:gpu] SDL_AcquireGPUCommandBuffer failed: {}", SDL_GetError());
                    return;
                }

                SDL_GPUTexture* swapchainTexture = nullptr;
                Uint32 width = 0;
                Uint32 height = 0;
                if (!SDL_AcquireGPUSwapchainTexture(commandBuffer, mWindow, &swapchainTexture, &width, &height))
                {
                    logging::logError("[gfx:gpu] SDL_AcquireGPUSwapchainTexture failed: {}", SDL_GetError());
                    SDL_CancelGPUCommandBuffer(commandBuffer);
                    return;
                }
                if (swapchainTexture == nullptr)
                {
                    // Window minimized / swapchain being recreated on resize:
                    // nothing to render into this frame, just drop it.
                    logging::logDebug("[gfx:gpu] swapchain texture unavailable (minimized/resized), skipping frame");
                    SDL_CancelGPUCommandBuffer(commandBuffer);
                    return;
                }

                SDL_GPUColorTargetInfo target = {};
                target.texture = swapchainTexture;
                target.mip_level = 0;
                target.layer_or_depth_plane = 0;
                target.clear_color = { 0.0f, 0.0f, 0.0f, 1.0f }; // solid black until M3 content
                target.load_op = SDL_GPU_LOADOP_CLEAR;
                target.store_op = SDL_GPU_STOREOP_STORE;

                auto* renderPass = SDL_BeginGPURenderPass(commandBuffer, &target, 1, nullptr);
                SDL_EndGPURenderPass(renderPass);

                if (!SDL_SubmitGPUCommandBuffer(commandBuffer))
                {
                    logging::logError("[gfx:gpu] SDL_SubmitGPUCommandBuffer failed: {}", SDL_GetError());
                    return;
                }
                logging::logDebug("[gfx:gpu] present (swapchain cleared)");
            }

            // ---- surface layer ----

            void create_surface(IUnknown* surface, const DDSURFACEDESC* desc) override
            {
                if (mDevice == nullptr)
                    return;

                SurfaceEntry entry;
                entry.width = desc->dwWidth;
                entry.height = desc->dwHeight;
                entry.bpp = desc->ddpfPixelFormat.dwRGBBitCount;
                entry.pitch = entry.width * (entry.bpp / 8);

                // The primary surface is created with DDSD_CAPS only (width,
                // height and pixel format all zero) and the offscreen render
                // surface is created without a pixel format, so the real
                // dimensions/bit depth arrive via GetSurfaceDesc/Lock. Defer
                // the GPU texture until then.
                if (entry.width == 0 || entry.height == 0 || entry.bpp == 0)
                {
                    mSurfaces[surface] = std::move(entry);
                    logging::logInfo(
                        "[gfx:gpu] CreateSurface {}x{} bpp={} (texture deferred until size/bpp known)",
                        desc->dwWidth,
                        desc->dwHeight,
                        desc->ddpfPixelFormat.dwRGBBitCount);
                    return;
                }

                if (!ensureTexture(entry))
                {
                    logging::logInfo(
                        "[gfx:gpu] CreateSurface {}x{} bpp={} (no GPU texture)",
                        desc->dwWidth,
                        desc->dwHeight,
                        desc->ddpfPixelFormat.dwRGBBitCount);
                    mSurfaces[surface] = std::move(entry);
                    return;
                }
                logging::logInfo(
                    "[gfx:gpu] CreateSurface {}x{} bpp={} format={}",
                    entry.width,
                    entry.height,
                    entry.bpp,
                    static_cast<int>(entry.format));
                mSurfaces[surface] = std::move(entry);
            }

            void destroy_surface(IUnknown* surface) override
            {
                const auto it = mSurfaces.find(surface);
                if (it == mSurfaces.end())
                {
                    logging::logDebug("[gfx:gpu] DestroySurface {} (unknown)", static_cast<void*>(surface));
                    return;
                }
                logging::logDebug("[gfx:gpu] DestroySurface {}", static_cast<void*>(surface));
                releaseSurface(it->second);
                mSurfaces.erase(it);
            }

            HRESULT lock(IUnknown* surface, LPRECT /*rect*/, LPDDSURFACEDESC desc, DWORD /*flags*/, HANDLE /*event*/) override
            {
                if (mDevice == nullptr)
                    return S_OK;

                auto* entry = findSurface(surface);
                if (entry == nullptr)
                {
                    logging::logDebug("[gfx:gpu] Lock surface={} (unknown)", static_cast<void*>(surface));
                    return S_OK;
                }

                // The D3D reference backend already filled desc with the real
                // surface info; adopt it so zero-size primaries get their real
                // dimensions here too.
                adoptDesc(*entry, desc);

                // Only the active backend may hand the game its CPU pointer:
                // while the D3D reference is active the game must keep writing
                // into the real DirectDraw surface, so leave the desc the D3D
                // backend just filled untouched.
                if (active_backend() != 1)
                {
                    logging::logDebug(
                        "[gfx:gpu] Lock surface={} (inactive: D3D owns the pointer)", static_cast<void*>(surface));
                    return S_OK;
                }

                if (!ensureTexture(*entry))
                {
                    logging::logDebug("[gfx:gpu] Lock surface={} (no GPU texture)", static_cast<void*>(surface));
                    return S_OK;
                }

                if (entry->locked)
                {
                    // Re-entrant lock while already locked: hand out the same
                    // pointer again rather than re-downloading.
                    desc->lpSurface = entry->shadow.data();
                    desc->lPitch = static_cast<LONG>(entry->pitch);
                    return S_OK;
                }

                if (!downloadToShadow(*entry))
                {
                    logging::logDebug("[gfx:gpu] Lock surface={} (readback failed)", static_cast<void*>(surface));
                    return S_OK;
                }

                entry->locked = true;
                desc->lpSurface = entry->shadow.data();
                desc->lPitch = static_cast<LONG>(entry->pitch);
                logging::logDebug(
                    "[gfx:gpu] Lock surface={} {}x{} bpp={} pitch={} ptr={}",
                    static_cast<void*>(surface),
                    entry->width,
                    entry->height,
                    entry->bpp,
                    entry->pitch,
                    desc->lpSurface);
                return S_OK;
            }

            HRESULT unlock(IUnknown* surface, void* /*lpRect*/) override
            {
                if (mDevice == nullptr)
                    return S_OK;

                auto* entry = findSurface(surface);
                if (entry == nullptr || !entry->textureCreated || !entry->locked)
                {
                    logging::logDebug("[gfx:gpu] Unlock surface={} (no active lock)", static_cast<void*>(surface));
                    return S_OK;
                }

                if (uploadFromShadow(*entry, nullptr))
                    entry->hasContent = true;
                entry->locked = false;
                logging::logDebug(
                    "[gfx:gpu] Unlock surface={} {}x{} bpp={}",
                    static_cast<void*>(surface),
                    entry->width,
                    entry->height,
                    entry->bpp);
                return S_OK;
            }

            HRESULT blt(IUnknown* dst, LPRECT dstRect, IUnknown* src, LPRECT srcRect, DWORD flags, LPDDBLTFX fx) override
            {
                if (mDevice == nullptr)
                    return S_OK;

                auto* dstEntry = findSurface(dst);
                if (dstEntry == nullptr)
                {
                    logging::logDebug("[gfx:gpu] Blt dst={} (unknown)", static_cast<void*>(dst));
                    return S_OK;
                }

                if (active_backend() != 1)
                {
                    logging::logDebug(
                        "[gfx:gpu] Blt dst={} src={} flags={} (inactive)",
                        static_cast<void*>(dst),
                        static_cast<void*>(src),
                        flags);
                    return S_OK;
                }

                if (src != nullptr)
                {
                    auto* srcEntry = findSurface(src);
                    if (srcEntry == nullptr)
                    {
                        logging::logDebug(
                            "[gfx:gpu] Blt dst={} src={} (unknown src)", static_cast<void*>(dst), static_cast<void*>(src));
                        return S_OK;
                    }
                    blitCopy(*dstEntry, dstRect, *srcEntry, srcRect);
                    return S_OK;
                }

                if ((flags & DDBLT_COLORFILL) != 0)
                {
                    blitFill(*dstEntry, dstRect, fx != nullptr ? fx->dwFillColor : 0);
                    return S_OK;
                }

                logging::logDebug("[gfx:gpu] Blt dst={} flags={} (unhandled, no src)", static_cast<void*>(dst), flags);
                return S_OK;
            }

            HRESULT get_surface_desc(IUnknown* surface, LPDDSURFACEDESC desc) override
            {
                auto* entry = findSurface(surface);
                if (entry == nullptr)
                {
                    logging::logDebug("[gfx:gpu] GetSurfaceDesc surface={} (unknown)", static_cast<void*>(surface));
                    return E_FAIL;
                }

                // The D3D reference backend already filled desc with the real
                // DirectDraw values. Adopt them so the zero-size primary
                // (created with DDSD_CAPS only) learns its real size here, and
                // surfaces created without a pixel format learn their bpp.
                if (adoptDesc(*entry, desc))
                {
                    logging::logInfo(
                        "[gfx:gpu] GetSurfaceDesc surface={} adopted real size {}x{} bpp={}",
                        static_cast<void*>(surface),
                        entry->width,
                        entry->height,
                        entry->bpp);
                }

                if (entry->width == 0 || entry->height == 0)
                    return S_OK;

                if (!ensureTexture(*entry))
                    return S_OK;

                if (active_backend() == 1)
                {
                    desc->dwWidth = entry->width;
                    desc->dwHeight = entry->height;
                    desc->lPitch = static_cast<LONG>(entry->pitch);
                }
                return S_OK;
            }

            HRESULT is_lost(IUnknown* /*surface*/) override
            {
                // GPU textures are never lost; surfaces survive window resizes
                // and display mode changes.
                return DD_OK;
            }

            HRESULT restore(IUnknown* surface) override
            {
                logging::logDebug("[gfx:gpu] Restore surface={} (no-op: GPU surfaces never lost)", static_cast<void*>(surface));
                return S_OK;
            }

            HRESULT add_attached_surface(IUnknown* surface, IUnknown* attached) override
            {
                logging::logDebug(
                    "[gfx:gpu] AddAttachedSurface surface={} attached={} (stub)",
                    static_cast<void*>(surface),
                    static_cast<void*>(attached));
                return S_OK;
            }

            HRESULT set_color_key(IUnknown* surface, DWORD flags, const DDCOLORKEY* /*key*/) override
            {
                // Color key handling is deferred to the texture milestone.
                logging::logDebug("[gfx:gpu] SetColorKey surface={} flags={} (stub)", static_cast<void*>(surface), flags);
                return S_OK;
            }

            HRESULT set_palette(IUnknown* surface, IUnknown* palette) override
            {
                // Paletted surfaces are not used by the GPU backend yet.
                logging::logDebug(
                    "[gfx:gpu] SetPalette surface={} palette={} (stub)",
                    static_cast<void*>(surface),
                    static_cast<void*>(palette));
                return S_OK;
            }

            HRESULT set_clipper(IUnknown* surface, IUnknown* clipper) override
            {
                logging::logDebug(
                    "[gfx:gpu] SetClipper surface={} clipper={} (stub)",
                    static_cast<void*>(surface),
                    static_cast<void*>(clipper));
                return S_OK;
            }

            // ---- device / scene ----

            void create_device(IUnknown* device) override
            {
                logging::logInfo("[gfx:gpu] CreateDevice device={} (stub)", static_cast<void*>(device));
            }

            HRESULT set_render_target(IUnknown* device, IUnknown* surface, DWORD flags) override
            {
                logging::logDebug(
                    "[gfx:gpu] SetRenderTarget device={} surface={} (stub)",
                    static_cast<void*>(device),
                    static_cast<void*>(surface));
                return S_OK;
            }

            HRESULT set_current_viewport(IUnknown* device, IUnknown* viewport) override
            {
                logging::logDebug(
                    "[gfx:gpu] SetCurrentViewport device={} viewport={} (stub)",
                    static_cast<void*>(device),
                    static_cast<void*>(viewport));
                return S_OK;
            }

            HRESULT set_viewport(IUnknown* viewport, const D3DVIEWPORT2* vp) override
            {
                logging::logDebug(
                    "[gfx:gpu] SetViewport2 viewport={} dwX={} dwY={} (stub)", static_cast<void*>(viewport), vp->dwX, vp->dwY);
                return S_OK;
            }

            HRESULT set_background(IUnknown* viewport, D3DMATERIALHANDLE materialHandle) override
            {
                logging::logDebug(
                    "[gfx:gpu] SetBackground viewport={} handle={} (stub)", static_cast<void*>(viewport), materialHandle);
                return S_OK;
            }

            HRESULT begin_scene(IUnknown* device) override
            {
                logging::logDebug("[gfx:gpu] BeginScene device={} (stub)", static_cast<void*>(device));
                return S_OK;
            }

            HRESULT end_scene(IUnknown* device) override
            {
                logging::logDebug("[gfx:gpu] EndScene device={} (stub)", static_cast<void*>(device));
                return S_OK;
            }

            HRESULT set_render_state(IUnknown* device, D3DRENDERSTATETYPE state, DWORD value) override
            {
                logging::logDebug(
                    "[gfx:gpu] SetRenderState device={} state={} value={:#x} (stub)",
                    static_cast<void*>(device),
                    static_cast<int>(state),
                    value);
                return S_OK;
            }

            HRESULT clear(IUnknown* viewport, DWORD count, const D3DRECT* rects, DWORD flags) override
            {
                logging::logDebug(
                    "[gfx:gpu] Clear viewport={} count={} flags={:#x} (stub)", static_cast<void*>(viewport), count, flags);
                return S_OK;
            }

            HRESULT draw_primitive(
                IUnknown* device, D3DPRIMITIVETYPE primType, D3DVERTEXTYPE vertexType, const void* vertices, DWORD vertexCount,
                DWORD flags) override
            {
                logging::logDebug(
                    "[gfx:gpu] DrawPrimitive device={} type={} verts={} (stub)",
                    static_cast<void*>(device),
                    static_cast<int>(primType),
                    vertexCount);
                return S_OK;
            }

            HRESULT draw_indexed_primitive(
                IUnknown* device, D3DPRIMITIVETYPE primType, D3DVERTEXTYPE vertexType, const void* vertices, DWORD vertexCount,
                const void* indices, DWORD indexCount, DWORD flags) override
            {
                logging::logDebug(
                    "[gfx:gpu] DrawIndexedPrimitive device={} type={} verts={} idx={} (stub)",
                    static_cast<void*>(device),
                    static_cast<int>(primType),
                    vertexCount,
                    indexCount);
                return S_OK;
            }

            HRESULT set_transform(IUnknown* device, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix) override
            {
                logging::logDebug(
                    "[gfx:gpu] SetTransform device={} state={} (stub)", static_cast<void*>(device), static_cast<int>(state));
                return S_OK;
            }

            HRESULT multiply_transform(IUnknown* device, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix) override
            {
                logging::logDebug(
                    "[gfx:gpu] MultiplyTransform device={} state={} (stub)",
                    static_cast<void*>(device),
                    static_cast<int>(state));
                return S_OK;
            }

            HRESULT get_stats(IUnknown* /*device*/, D3DSTATS* /*stats*/) override
            {
                return S_OK;
            }

            // ---- surface layer helpers ----

            SurfaceEntry* findSurface(IUnknown* surface)
            {
                const auto it = mSurfaces.find(surface);
                return it == mSurfaces.end() ? nullptr : &it->second;
            }

            // Records the real width/height/bit depth into the entry from a
            // DDSURFACEDESC the D3D reference backend just filled (GetSurfaceDesc
            // or Lock). Returns true when anything changed. The row pitch is
            // recomputed as width * bytesPerPixel, matching what the game expects
            // from a DirectDraw surface.
            bool adoptDesc(SurfaceEntry& entry, const DDSURFACEDESC* desc)
            {
                bool changed = false;
                if (entry.width == 0 && desc->dwWidth != 0)
                {
                    entry.width = desc->dwWidth;
                    changed = true;
                }
                if (entry.height == 0 && desc->dwHeight != 0)
                {
                    entry.height = desc->dwHeight;
                    changed = true;
                }
                if (entry.bpp == 0 && desc->ddpfPixelFormat.dwRGBBitCount != 0)
                {
                    entry.bpp = desc->ddpfPixelFormat.dwRGBBitCount;
                    changed = true;
                }
                if (entry.width != 0 && entry.bpp != 0)
                {
                    const auto pitch = entry.width * (entry.bpp / 8);
                    if (entry.pitch != pitch)
                    {
                        entry.pitch = pitch;
                        changed = true;
                    }
                }
                return changed;
            }

            // Lazily creates the GPU texture (plus staging transfer buffers and
            // the CPU shadow) once the entry has real dimensions. No-op when
            // already created; returns false when dimensions are still unknown
            // or creation failed.
            bool ensureTexture(SurfaceEntry& entry)
            {
                if (entry.textureCreated)
                    return true;
                if (mDevice == nullptr)
                    return false;
                if (entry.width == 0 || entry.height == 0 || entry.bpp == 0)
                    return false;

                if (entry.bpp == 16)
                {
                    // DirectDraw RGB565 shares the classic 5-6-5 bit layout of
                    // SDL's B5G6R5_UNORM, so the pixels transfer byte-for-byte.
                    entry.format = SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM;
                }
                else if (entry.bpp == 32)
                {
                    // DirectDraw RGBX8888 (memory order B,G,R,X) is converted
                    // to R8G8B8A8 during the shadow <-> texture transfers.
                    entry.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
                }
                else
                {
                    logging::logWarning(
                        "[gfx:gpu] unsupported surface bpp={} ({}x{} not tracked)", entry.bpp, entry.width, entry.height);
                    return false;
                }

                auto usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
                if (!SDL_GPUTextureSupportsFormat(mDevice, entry.format, SDL_GPU_TEXTURETYPE_2D, usage))
                {
                    // Some backends cannot render to 16-bit formats; fall back
                    // to sampling only (blit source). Render targets are 32bpp.
                    usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
                    if (!SDL_GPUTextureSupportsFormat(mDevice, entry.format, SDL_GPU_TEXTURETYPE_2D, usage))
                    {
                        logging::logError(
                            "[gfx:gpu] texture format {} unsupported ({}x{} bpp={})",
                            static_cast<int>(entry.format),
                            entry.width,
                            entry.height,
                            entry.bpp);
                        return false;
                    }
                }

                SDL_GPUTextureCreateInfo info = {};
                info.type = SDL_GPU_TEXTURETYPE_2D;
                info.format = entry.format;
                info.usage = usage;
                info.width = entry.width;
                info.height = entry.height;
                info.layer_count_or_depth = 1;
                info.num_levels = 1;
                info.sample_count = SDL_GPU_SAMPLECOUNT_1;
                entry.texture = SDL_CreateGPUTexture(mDevice, &info);
                if (entry.texture == nullptr)
                {
                    logging::logError(
                        "[gfx:gpu] SDL_CreateGPUTexture failed ({}x{} bpp={}): {}",
                        entry.width,
                        entry.height,
                        entry.bpp,
                        SDL_GetError());
                    return false;
                }

                const auto size = static_cast<Uint32>(entry.pitch) * entry.height;
                SDL_GPUTransferBufferCreateInfo transferInfo = {};
                transferInfo.size = size;
                transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
                entry.downloadBuffer = SDL_CreateGPUTransferBuffer(mDevice, &transferInfo);
                transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                entry.uploadBuffer = SDL_CreateGPUTransferBuffer(mDevice, &transferInfo);
                if (entry.downloadBuffer == nullptr || entry.uploadBuffer == nullptr)
                {
                    logging::logError("[gfx:gpu] SDL_CreateGPUTransferBuffer failed: {}", SDL_GetError());
                    releaseSurface(entry);
                    return false;
                }

                entry.shadow.assign(size, 0);
                entry.textureCreated = true;
                return true;
            }

            void releaseSurface(SurfaceEntry& entry)
            {
                if (mDevice != nullptr)
                {
                    if (entry.texture != nullptr)
                    {
                        SDL_ReleaseGPUTexture(mDevice, entry.texture);
                        entry.texture = nullptr;
                    }
                    if (entry.uploadBuffer != nullptr)
                    {
                        SDL_ReleaseGPUTransferBuffer(mDevice, entry.uploadBuffer);
                        entry.uploadBuffer = nullptr;
                    }
                    if (entry.downloadBuffer != nullptr)
                    {
                        SDL_ReleaseGPUTransferBuffer(mDevice, entry.downloadBuffer);
                        entry.downloadBuffer = nullptr;
                    }
                }
                entry.shadow.clear();
                entry.textureCreated = false;
                entry.hasContent = false;
                entry.locked = false;
            }

            // Downloads the current GPU texture content into the CPU shadow so
            // a Lock reads back the latest pixels. The download is submitted
            // synchronously and waited on before the transfer buffer is mapped.
            bool downloadToShadow(SurfaceEntry& entry)
            {
                if (!entry.hasContent)
                {
                    // First lock of a fresh surface: nothing to read back yet,
                    // hand back a zeroed shadow.
                    std::memset(entry.shadow.data(), 0, entry.shadow.size());
                    return true;
                }

                auto* commandBuffer = SDL_AcquireGPUCommandBuffer(mDevice);
                if (commandBuffer == nullptr)
                {
                    logging::logError("[gfx:gpu] SDL_AcquireGPUCommandBuffer failed: {}", SDL_GetError());
                    return false;
                }
                auto* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
                SDL_GPUTextureRegion source = {};
                source.texture = entry.texture;
                source.w = entry.width;
                source.h = entry.height;
                source.d = 1;
                SDL_GPUTextureTransferInfo destination = {};
                destination.transfer_buffer = entry.downloadBuffer;
                destination.pixels_per_row = entry.width;
                destination.rows_per_layer = entry.height;
                SDL_DownloadFromGPUTexture(copyPass, &source, &destination);
                SDL_EndGPUCopyPass(copyPass);
                if (!SDL_SubmitGPUCommandBuffer(commandBuffer))
                {
                    logging::logError("[gfx:gpu] SDL_SubmitGPUCommandBuffer failed: {}", SDL_GetError());
                    return false;
                }
                // The readback is async; block until the transfer buffer holds
                // the data before mapping it (simplicity over latency).
                SDL_WaitForGPUIdle(mDevice);
                void* mapped = SDL_MapGPUTransferBuffer(mDevice, entry.downloadBuffer, false);
                if (mapped == nullptr)
                {
                    logging::logError("[gfx:gpu] SDL_MapGPUTransferBuffer failed: {}", SDL_GetError());
                    return false;
                }
                convertTextureToShadow(static_cast<const uint8_t*>(mapped), entry.shadow.data(), entry);
                SDL_UnmapGPUTransferBuffer(mDevice, entry.downloadBuffer);
                return true;
            }

            // Uploads the given region of the CPU shadow (nullptr = whole
            // surface) into the GPU texture. Used by Unlock and Blt colorfill.
            bool uploadFromShadow(SurfaceEntry& entry, const RECT* region)
            {
                RECT full = { 0, 0, static_cast<LONG>(entry.width), static_cast<LONG>(entry.height) };
                const RECT* r = region != nullptr ? region : &full;
                const auto left = std::clamp(r->left, 0L, static_cast<LONG>(entry.width));
                const auto top = std::clamp(r->top, 0L, static_cast<LONG>(entry.height));
                const auto right = std::clamp(r->right, left, static_cast<LONG>(entry.width));
                const auto bottom = std::clamp(r->bottom, top, static_cast<LONG>(entry.height));
                const auto w = static_cast<Uint32>(right - left);
                const auto h = static_cast<Uint32>(bottom - top);
                if (w == 0 || h == 0)
                    return false;

                void* mapped = SDL_MapGPUTransferBuffer(mDevice, entry.uploadBuffer, false);
                if (mapped == nullptr)
                {
                    logging::logError("[gfx:gpu] SDL_MapGPUTransferBuffer failed: {}", SDL_GetError());
                    return false;
                }
                auto* dst = static_cast<uint8_t*>(mapped);
                const auto texel = entry.bpp / 8;
                const auto rowPixels = static_cast<size_t>(entry.width);
                for (auto row = top; row < bottom; row++)
                {
                    const auto* srcRow
                        = entry.shadow.data() + static_cast<size_t>(row) * entry.pitch + static_cast<size_t>(left) * texel;
                    auto* dstRow = dst + (static_cast<size_t>(row) * rowPixels + left) * texel;
                    if (entry.format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM)
                        convertShadowToTexture(srcRow, dstRow, w);
                    else
                        std::memcpy(dstRow, srcRow, static_cast<size_t>(w) * texel);
                }
                SDL_UnmapGPUTransferBuffer(mDevice, entry.uploadBuffer);

                auto* commandBuffer = SDL_AcquireGPUCommandBuffer(mDevice);
                if (commandBuffer == nullptr)
                {
                    logging::logError("[gfx:gpu] SDL_AcquireGPUCommandBuffer failed: {}", SDL_GetError());
                    return false;
                }
                auto* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
                SDL_GPUTextureTransferInfo source = {};
                source.transfer_buffer = entry.uploadBuffer;
                source.pixels_per_row = entry.width;
                source.rows_per_layer = entry.height;
                SDL_GPUTextureRegion destination = {};
                destination.texture = entry.texture;
                destination.x = static_cast<Uint32>(left);
                destination.y = static_cast<Uint32>(top);
                destination.w = w;
                destination.h = h;
                destination.d = 1;
                SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
                SDL_EndGPUCopyPass(copyPass);
                if (!SDL_SubmitGPUCommandBuffer(commandBuffer))
                {
                    logging::logError("[gfx:gpu] SDL_SubmitGPUCommandBuffer failed: {}", SDL_GetError());
                    return false;
                }
                SDL_WaitForGPUIdle(mDevice);
                return true;
            }

            // Fills the given region of the CPU shadow with a DirectDraw fill
            // color in the surface's own pixel format (RGB565 / RGBX8888).
            void fillShadow(SurfaceEntry& entry, const RECT* region, uint32_t color)
            {
                const auto texel = entry.bpp / 8;
                uint8_t pixel[4] = {};
                if (entry.bpp == 16)
                {
                    const auto p = static_cast<uint16_t>(color & 0xFFFF);
                    pixel[0] = static_cast<uint8_t>(p & 0xFF);
                    pixel[1] = static_cast<uint8_t>(p >> 8);
                }
                else
                {
                    // DirectDraw RGBX8888 memory order: B, G, R, X.
                    pixel[0] = static_cast<uint8_t>(color & 0xFF);
                    pixel[1] = static_cast<uint8_t>((color >> 8) & 0xFF);
                    pixel[2] = static_cast<uint8_t>((color >> 16) & 0xFF);
                }
                const auto left = std::max(0L, region->left);
                const auto top = std::max(0L, region->top);
                const auto right = std::min(static_cast<LONG>(entry.width), region->right);
                const auto bottom = std::min(static_cast<LONG>(entry.height), region->bottom);
                for (auto y = top; y < bottom; y++)
                {
                    auto* row = entry.shadow.data() + static_cast<size_t>(y) * entry.pitch + static_cast<size_t>(left) * texel;
                    for (auto x = left; x < right; x++)
                    {
                        std::memcpy(row, pixel, texel);
                        row += texel;
                    }
                }
            }

            // surface -> surface blit: GPU texture-to-texture copy of the
            // srcRect region into dstRect. Both rects default to the full
            // surface. Scaling (dstRect larger than srcRect) is not supported
            // yet; the common game path is a full-size 1:1 copy.
            bool blitCopy(SurfaceEntry& dstEntry, LPRECT dstRect, SurfaceEntry& srcEntry, LPRECT srcRect)
            {
                if (!ensureTexture(dstEntry) || !ensureTexture(srcEntry))
                {
                    logging::logDebug("[gfx:gpu] Blt copy skipped (missing texture)");
                    return false;
                }
                if (dstEntry.format != srcEntry.format)
                {
                    // The game only blits between same-format surfaces; a
                    // mismatch means a surface we cannot faithfully copy yet.
                    logging::logDebug(
                        "[gfx:gpu] Blt copy skipped (format mismatch {} -> {})",
                        static_cast<int>(srcEntry.format),
                        static_cast<int>(dstEntry.format));
                    return false;
                }

                int srcX = 0;
                int srcY = 0;
                int dstX = 0;
                int dstY = 0;
                Uint32 w = srcEntry.width;
                Uint32 h = srcEntry.height;
                if (srcRect != nullptr)
                {
                    srcX = std::max(0L, srcRect->left);
                    srcY = std::max(0L, srcRect->top);
                    w = static_cast<Uint32>(std::max(0L, srcRect->right - srcRect->left));
                    h = static_cast<Uint32>(std::max(0L, srcRect->bottom - srcRect->top));
                }
                if (dstRect != nullptr)
                {
                    dstX = std::max(0L, dstRect->left);
                    dstY = std::max(0L, dstRect->top);
                }
                // Clamp the copy to the destination surface (and source).
                if (dstX + static_cast<int>(w) > static_cast<int>(dstEntry.width))
                    w = dstX < static_cast<int>(dstEntry.width) ? dstEntry.width - dstX : 0;
                if (dstY + static_cast<int>(h) > static_cast<int>(dstEntry.height))
                    h = dstY < static_cast<int>(dstEntry.height) ? dstEntry.height - dstY : 0;
                if (srcX + static_cast<int>(w) > static_cast<int>(srcEntry.width))
                    w = srcX < static_cast<int>(srcEntry.width) ? srcEntry.width - srcX : 0;
                if (srcY + static_cast<int>(h) > static_cast<int>(srcEntry.height))
                    h = srcY < static_cast<int>(srcEntry.height) ? srcEntry.height - srcY : 0;
                if (w == 0 || h == 0)
                    return false;

                auto* commandBuffer = SDL_AcquireGPUCommandBuffer(mDevice);
                if (commandBuffer == nullptr)
                {
                    logging::logError("[gfx:gpu] SDL_AcquireGPUCommandBuffer failed: {}", SDL_GetError());
                    return false;
                }
                auto* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
                SDL_GPUTextureLocation source = {};
                source.texture = srcEntry.texture;
                source.x = static_cast<Uint32>(srcX);
                source.y = static_cast<Uint32>(srcY);
                SDL_GPUTextureLocation destination = {};
                destination.texture = dstEntry.texture;
                destination.x = static_cast<Uint32>(dstX);
                destination.y = static_cast<Uint32>(dstY);
                SDL_CopyGPUTextureToTexture(copyPass, &source, &destination, w, h, 1, false);
                SDL_EndGPUCopyPass(copyPass);
                if (!SDL_SubmitGPUCommandBuffer(commandBuffer))
                {
                    logging::logError("[gfx:gpu] SDL_SubmitGPUCommandBuffer failed: {}", SDL_GetError());
                    return false;
                }
                SDL_WaitForGPUIdle(mDevice);
                dstEntry.hasContent = true;
                logging::logDebug("[gfx:gpu] Blt copy {}x{} at ({},{}) -> ({},{})", w, h, srcX, srcY, dstX, dstY);
                return true;
            }

            // Blt colorfill: fills dstRect (default whole surface) of the
            // destination with dwFillColor by writing the CPU shadow and
            // uploading only the affected region.
            bool blitFill(SurfaceEntry& dstEntry, LPRECT dstRect, uint32_t color)
            {
                if (!ensureTexture(dstEntry))
                {
                    logging::logDebug("[gfx:gpu] Blt colorfill skipped (no texture)");
                    return false;
                }
                RECT full = { 0, 0, static_cast<LONG>(dstEntry.width), static_cast<LONG>(dstEntry.height) };
                const RECT* region = dstRect != nullptr ? dstRect : &full;
                fillShadow(dstEntry, region, color);
                if (!uploadFromShadow(dstEntry, region))
                    return false;
                dstEntry.hasContent = true;
                logging::logDebug(
                    "[gfx:gpu] Blt colorfill color={} rect=({},{} - {},{})",
                    color,
                    region->left,
                    region->top,
                    region->right,
                    region->bottom);
                return true;
            }

            // 32bpp: converts DirectDraw RGBX8888 pixels (memory order
            // B,G,R,X) to the R8G8B8A8 texture layout, forcing alpha opaque.
            static void convertShadowToTexture(const uint8_t* src, uint8_t* dst, Uint32 pixelCount)
            {
                for (Uint32 i = 0; i < pixelCount; i++)
                {
                    dst[0] = src[2]; // R
                    dst[1] = src[1]; // G
                    dst[2] = src[0]; // B
                    dst[3] = 0xFF;   // A (DirectDraw RGBX carries no alpha)
                    src += 4;
                    dst += 4;
                }
            }

            // 32bpp: converts R8G8B8A8 texture pixels back to DirectDraw
            // RGBX8888 for the CPU shadow. 16bpp copies byte-for-byte.
            static void convertTextureToShadow(const uint8_t* src, uint8_t* dst, const SurfaceEntry& entry)
            {
                if (entry.format != SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM)
                {
                    std::memcpy(dst, src, entry.shadow.size());
                    return;
                }
                const auto pixelCount = static_cast<size_t>(entry.width) * entry.height;
                for (size_t i = 0; i < pixelCount; i++)
                {
                    dst[0] = src[2]; // B
                    dst[1] = src[1]; // G
                    dst[2] = src[0]; // R
                    dst[3] = 0;      // X
                    src += 4;
                    dst += 4;
                }
            }

        private:
            std::unordered_map<void*, SurfaceEntry> mSurfaces;

            SDL_GPUDevice* mDevice = nullptr;
            SDL_Window* mWindow = nullptr;
        };
    }

    GfxBackend* backend_gpu()
    {
        static GfxBackendGPU backend;
        return &backend;
    }
}
