#include "gfx_backend.h"
#include "logger.h"
#include "system_window.h"

#include <SDL3/SDL.h>

namespace openre::gfx
{
    namespace
    {
        // GPU backend: creates an SDL_GPU device, claims the game window and
        // presents a cleared swapchain (M2). Surface/texture/draw support
        // arrives in later milestones; the remaining methods stay as stubs.
        // Present only runs while the GPU backend is the active backend, so the
        // DirectDraw primary surface keeps showing when the D3D reference
        // backend (0) is selected.
        class GfxBackendGPU final : public GfxBackend
        {
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
                    if (mWindow != nullptr)
                        SDL_ReleaseWindowFromGPUDevice(mDevice, mWindow);
                    SDL_DestroyGPUDevice(mDevice);
                    mDevice = nullptr;
                }
                mWindow = nullptr;
                logging::logInfo("[gfx:gpu] shutdown (swapchain released, device destroyed)");
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
                logging::logDebug(
                    "[gfx:gpu] CreateSurface {}x{} bpp={} (stub)",
                    desc->dwWidth,
                    desc->dwHeight,
                    desc->ddpfPixelFormat.dwRGBBitCount);
            }

            void destroy_surface(IUnknown* surface) override
            {
                logging::logDebug("[gfx:gpu] DestroySurface {} (stub)", static_cast<void*>(surface));
            }

            HRESULT lock(IUnknown* surface, LPRECT rect, LPDDSURFACEDESC desc, DWORD flags, HANDLE event) override
            {
                logging::logDebug("[gfx:gpu] Lock surface={} flags={:#x} (stub)", static_cast<void*>(surface), flags);
                return S_OK;
            }

            HRESULT unlock(IUnknown* surface, void* lpRect) override
            {
                logging::logDebug("[gfx:gpu] Unlock surface={} (stub)", static_cast<void*>(surface));
                return S_OK;
            }

            HRESULT blt(IUnknown* dst, LPRECT dstRect, IUnknown* src, LPRECT srcRect, DWORD flags, LPDDBLTFX fx) override
            {
                logging::logDebug(
                    "[gfx:gpu] Blt dst={} src={} flags={:#x} (stub)", static_cast<void*>(dst), static_cast<void*>(src), flags);
                return S_OK;
            }

            HRESULT get_surface_desc(IUnknown* /*surface*/, DDSURFACEDESC* /*desc*/) override
            {
                return S_OK;
            }

            HRESULT is_lost(IUnknown* /*surface*/) override
            {
                return DD_OK;
            }

            HRESULT restore(IUnknown* surface) override
            {
                logging::logDebug("[gfx:gpu] Restore surface={} (stub)", static_cast<void*>(surface));
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

            HRESULT set_color_key(IUnknown* surface, DWORD flags, const DDCOLORKEY* key) override
            {
                logging::logDebug("[gfx:gpu] SetColorKey surface={} flags={:#x} (stub)", static_cast<void*>(surface), flags);
                return S_OK;
            }

            HRESULT set_palette(IUnknown* surface, IUnknown* palette) override
            {
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

        private:
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
