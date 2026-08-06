#include "gfx_backend.h"
#include "gfx_shaders.h"
#include "logger.h"
#include "system_window.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openre::gfx
{
    namespace
    {
        // Render-state ids the game passes to SetRenderState. The game uses the
        // DX2-era D3DRENDERSTATE numbering (identical to the Win10 SDK values),
        // see docs/com-coverage-report.md §9.1.
        enum RenderStateId
        {
            RS_TEXTUREHANDLE = 1,
            RS_ANTIALIAS = 2,
            RS_TEXTUREADDRESS = 3,
            RS_TEXTUREPERSPECTIVE = 4,
            RS_ZENABLE = 7,
            RS_SHADEMODE = 9,
            RS_ZWRITEENABLE = 14,
            RS_LASTPIXEL = 16,
            RS_TEXTUREMAG = 17,
            RS_TEXTUREMIN = 18,
            RS_SRCBLEND = 19,
            RS_DESTBLEND = 20,
            RS_TEXTUREMAPBLEND = 21,
            RS_CULLMODE = 22,
            RS_ZFUNC = 23,
            RS_ALPHABLENDENABLE = 27,
            RS_SPECULARENABLE = 29,
            RS_SUBPIXEL = 31,
            RS_EDGEANTIALIAS = 40,
            RS_COLORKEYENABLE = 41,
            RS_ANISOTROPY = 49,
        };

        // Raw D3DBLEND values the game passes (d3dtypes.h: ZERO=1, ONE=2,
        // SRCCOLOR=3, INVSRCCOLOR=4, SRCALPHA=5, INVSRCALPHA=6, ...).
        SDL_GPUBlendFactor mapBlendFactor(DWORD value)
        {
            switch (value)
            {
            case 1: return SDL_GPU_BLENDFACTOR_ZERO;
            case 2: return SDL_GPU_BLENDFACTOR_ONE;
            case 3: return SDL_GPU_BLENDFACTOR_SRC_COLOR;
            case 4: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
            case 5: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            case 6: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            case 7: return SDL_GPU_BLENDFACTOR_DST_COLOR;
            case 8: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
            case 9: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
            case 10: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
            default: return SDL_GPU_BLENDFACTOR_ONE;
            }
        }

        // Raw D3DCMPFUNC values the game passes (NEVER=1 ... ALWAYS=8).
        SDL_GPUCompareOp mapCompareOp(DWORD value)
        {
            switch (value)
            {
            case 1: return SDL_GPU_COMPAREOP_NEVER;
            case 2: return SDL_GPU_COMPAREOP_LESS;
            case 3: return SDL_GPU_COMPAREOP_EQUAL;
            case 4: return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
            case 5: return SDL_GPU_COMPAREOP_GREATER;
            case 6: return SDL_GPU_COMPAREOP_NOT_EQUAL;
            case 7: return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
            case 8: return SDL_GPU_COMPAREOP_ALWAYS;
            default: return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
            }
        }

        // Raw D3DCULL values (NONE=1, CW=2, CCW=3). D3D's default front face is
        // counter-clockwise; SDL_GPU_CULLMODE_BACK/FRONT are relative to the
        // pipeline's front_face setting, so CW->BACK and CCW->FRONT.
        SDL_GPUCullMode mapCullMode(DWORD value)
        {
            switch (value)
            {
            case 2: return SDL_GPU_CULLMODE_BACK;
            case 3: return SDL_GPU_CULLMODE_FRONT;
            default: return SDL_GPU_CULLMODE_NONE;
            }
        }

        // Maps a D3DPRIMITIVETYPE to the SDL_GPU topology. The game draws TL
        // vertices with point/line/triangle lists and strips (the
        // heartbeat/health display animates with line primitives), so every
        // type must map to its true topology instead of collapsing to
        // TRIANGLELIST (which would silently drop line/point draws).
        // TRIANGLEFAN has no SDL_GPU topology; the callers expand fans into a
        // triangle list (expandTriangleFan) before queueing them.
        SDL_GPUPrimitiveType mapPrimitiveType(D3DPRIMITIVETYPE primType)
        {
            switch (primType)
            {
            case D3DPT_POINTLIST: return SDL_GPU_PRIMITIVETYPE_POINTLIST;
            case D3DPT_LINELIST: return SDL_GPU_PRIMITIVETYPE_LINELIST;
            case D3DPT_LINESTRIP: return SDL_GPU_PRIMITIVETYPE_LINESTRIP;
            case D3DPT_TRIANGLELIST: return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            case D3DPT_TRIANGLESTRIP: return SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
            default: return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            }
        }

        // Vertex attribute layout matching D3DTLVERTEX (32 bytes, see d3dtypes.h):
        // float sx, sy, sz, rhw; D3DCOLOR color, specular; float tu, tv. The
        // rhw and specular fields are not consumed.
        constexpr int kTLVertexStride = 32;
        constexpr int kTLVertexPosOffset = 0;
        constexpr int kTLVertexColorOffset = 16;
        constexpr int kTLVertexUvOffset = 24;

        // SDL_GPU has no triangle-fan topology, so a fan of N TL vertices
        // (hub v0 + rim v1..vN-1) is expanded into (N-2) triangles
        // (v0,v1,v2 / v0,v2,v3 / ...) that draw as a triangle list. Returns
        // an empty vector when there are fewer than 3 vertices (nothing to
        // rasterize).
        std::vector<uint8_t> expandTriangleFan(const uint8_t* vertices, DWORD vertexCount)
        {
            std::vector<uint8_t> out;
            if (vertexCount < 3)
                return out;
            out.reserve(static_cast<size_t>(vertexCount - 2) * 3 * kTLVertexStride);
            const uint8_t* hub = vertices;
            for (DWORD i = 1; i + 1 < vertexCount; i++)
            {
                out.insert(out.end(), hub, hub + kTLVertexStride);
                out.insert(
                    out.end(),
                    vertices + static_cast<size_t>(i) * kTLVertexStride,
                    vertices + static_cast<size_t>(i + 1) * kTLVertexStride);
            }
            return out;
        }

        // One frame of deferred draw data (see the class comment for why draws
        // are queued and executed in present()).
        struct QueuedDraw
        {
            SDL_GPUGraphicsPipeline* pipeline = nullptr;
            SDL_GPUTexture* texture = nullptr; // nullptr = untextured draw
            SDL_GPUSampler* sampler = nullptr;
            Uint32 vertexOffset = 0; // byte offset into the frame vertex buffer
            Uint32 vertexCount = 0;
            SDL_GPUPrimitiveType primType = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        };

        // The subset of D3D render/draw state that changes the SDL_GPU
        // pipeline. SDL_GPU pipelines are immutable, so pipelines are cached
        // keyed by the combinations the game actually uses (a handful).
        struct PipelineKey
        {
            bool textured = false;
            bool alphaBlend = false;
            SDL_GPUBlendFactor srcFactor = SDL_GPU_BLENDFACTOR_ONE;
            SDL_GPUBlendFactor dstFactor = SDL_GPU_BLENDFACTOR_ZERO;
            bool zTest = false;
            bool zWrite = false;
            SDL_GPUCompareOp zFunc = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
            SDL_GPUCullMode cull = SDL_GPU_CULLMODE_NONE;
            SDL_GPUPrimitiveType primType = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

            bool operator==(const PipelineKey& o) const
            {
                return textured == o.textured && alphaBlend == o.alphaBlend && srcFactor == o.srcFactor
                    && dstFactor == o.dstFactor && zTest == o.zTest && zWrite == o.zWrite && zFunc == o.zFunc && cull == o.cull
                    && primType == o.primType;
            }
        };

        struct PipelineKeyHash
        {
            size_t operator()(const PipelineKey& k) const
            {
                size_t h = 0;
                h = h * 31 + (k.textured ? 1u : 0u);
                h = h * 31 + (k.alphaBlend ? 1u : 0u);
                h = h * 31 + static_cast<size_t>(k.srcFactor);
                h = h * 31 + static_cast<size_t>(k.dstFactor);
                h = h * 31 + (k.zTest ? 1u : 0u);
                h = h * 31 + (k.zWrite ? 1u : 0u);
                h = h * 31 + static_cast<size_t>(k.zFunc);
                h = h * 31 + static_cast<size_t>(k.cull);
                h = h * 31 + static_cast<size_t>(k.primType);
                return h;
            }
        };

        // Per-device D3D state the GPU backend mirrors (the game has one device,
        // so a single instance is stored on the backend).
        struct DeviceState
        {
            void* renderTarget = nullptr; // IDirectDrawSurface* key into mSurfaces
            void* viewport = nullptr;     // current IDirect3DViewport2*
            D3DVIEWPORT2 viewport2 = {};
            bool haveViewport = false;

            // Mirrored render states (raw D3D values where applicable).
            DWORD texHandle = 0;
            bool alphaBlend = false;
            DWORD srcBlend = 2; // D3DBLEND_ONE
            DWORD dstBlend = 1; // D3DBLEND_ZERO
            bool zEnable = false;
            bool zWrite = true;
            DWORD zFunc = 4;    // D3DCMP_LESSEQUAL
            DWORD cullMode = 1; // D3DCULL_NONE
            DWORD texMag = 2;   // D3DFILTER_LINEAR (matches the game's default)
            DWORD texMin = 6;   // D3DFILTER_LINEARMIPLINEAR (game default when bilinear)

            // Clear request (consumed by the next frame's scene pass).
            bool pendingClearTarget = false;
            bool pendingClearDepth = false;
            SDL_FColor clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };

            // Cumulative draw statistics (get_stats deltas).
            Uint64 totalTriangles = 0;
            Uint64 totalVertices = 0;
        };

        // GPU backend: creates an SDL_GPU device, claims the game window and
        // presents a cleared swapchain (M2). The surface layer (M3) tracks
        // DirectDraw surfaces as SDL_GPU textures with CPU staging buffers and
        // handles CreateSurface/Lock/Unlock/Blt/GetSurfaceDesc. M4 adds the draw
        // pipeline: render states are mirrored, TL vertices are queued per frame
        // and executed in present() as one render pass on the offscreen render
        // target (with a depth texture), followed by a letterboxed blit of that
        // target into the swapchain. Draws are deferred to present() so vertex
        // uploads (copy passes) never have to be interleaved with the render
        // pass, and so the frame's Clear lands on the first pass exactly once.
        // Present only runs while the GPU backend is the active backend, so the
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
                Uint32 bpp = 0;   // 8, 16 or 32
                Uint32 pitch = 0; // DirectDraw row pitch (width * bpp / 8)
                SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
                bool textureCreated = false;
                bool hasContent = false; // texture written at least once
                bool locked = false;
                void* lockedPtr = nullptr; // shadow pointer handed out by the last Lock

                // DirectDraw pixel-format layout, adopted from the
                // DDSURFACEDESC ddpfPixelFormat masks (0 = unknown). Governs
                // the 16bpp RGB565 vs RGB555/ARGB1555 conversion and, via
                // paletted, the 8bpp palette expansion. The shifts/bit counts
                // are derived from the masks by decodeMask.
                Uint32 rMask = 0, gMask = 0, bMask = 0, aMask = 0;
                Uint32 rShift = 0, gShift = 0, bShift = 0, aShift = 0;
                Uint32 rBits = 0, gBits = 0, bBits = 0, aBits = 0;
                bool paletted = false;   // 8bpp indexed surface
                void* palette = nullptr; // IDirectDrawPalette* key into mPalettes

                // True when the shadow was written by the game through its own
                // Lock/Unlock (or a Blt). Paletted surfaces whose content
                // arrived via texture_load must not be re-expanded from their
                // (empty) shadow when their palette changes.
                bool contentFromShadow = false;
            };

            // Per-palette state: 256 RGBA entries (0xAARRGGBB, matching
            // D3DCOLOR). PALETTEENTRYs are converted in set_palette_entries.
            struct PaletteData
            {
                std::array<uint32_t, 256> rgba = {};
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

                const char* dumpEnv = SDL_getenv("OPENRE_GPU_DUMP");
                mDumpInterval = dumpEnv != nullptr ? static_cast<Uint32>(std::atoi(dumpEnv)) : 0;
                if (mDumpInterval != 0)
                    logging::logInfo("[gfx:gpu] scene dump enabled (every {} frames)", mDumpInterval);
                return true;
            }

            void shutdown() override
            {
                if (mDevice != nullptr)
                {
                    // Frames are fenced rather than idle-waited, so an
                    // in-flight frame may still reference the resources; drain
                    // the GPU once before releasing them, then release the last
                    // frame's fence (signaled by the idle wait above).
                    SDL_WaitForGPUIdle(mDevice);
                    if (mFrameFence != nullptr)
                    {
                        SDL_ReleaseGPUFence(mDevice, mFrameFence);
                        mFrameFence = nullptr;
                    }
                    releaseSceneResources();
                    for (auto& pair : mSurfaces)
                        releaseSurface(pair.second);
                    mSurfaces.clear();
                    if (mWindow != nullptr)
                        SDL_ReleaseWindowFromGPUDevice(mDevice, mWindow);
                    SDL_DestroyGPUDevice(mDevice);
                    mDevice = nullptr;
                }
                mWindow = nullptr;
                logging::logInfo("[gfx:gpu] shutdown (resources released, device destroyed)");
            }

            void present() override
            {
                // While the D3D reference backend is active the DirectDraw
                // primary surface (the game's Blt in flip_blt) owns the window;
                // do not acquire/present the swapchain in that case, and drop
                // any queued GPU work so a mid-frame toggle never renders stale
                // draws later.
                if (active_backend() != 1)
                {
                    resetFrameState();
                    return;
                }
                if (mDevice == nullptr || mWindow == nullptr)
                {
                    logging::logDebug("[gfx:gpu] present skipped (device/window not ready)");
                    return;
                }

                // Wait for the previous frame's fence before reusing the shared
                // vertex transfer buffer (and before any vertex-pool growth that
                // releases buffers). This replaces the old full device idle wait
                // with a one-frame-deep sync so CPU and GPU overlap.
                waitForPreviousFrame();

                auto* commandBuffer = SDL_AcquireGPUCommandBuffer(mDevice);
                if (commandBuffer == nullptr)
                {
                    logging::logError("[gfx:gpu] SDL_AcquireGPUCommandBuffer failed: {}", SDL_GetError());
                    resetFrameState();
                    return;
                }

                SDL_GPUTexture* swapchainTexture = nullptr;
                Uint32 winW = 0;
                Uint32 winH = 0;
                if (!SDL_AcquireGPUSwapchainTexture(commandBuffer, mWindow, &swapchainTexture, &winW, &winH))
                {
                    logging::logError("[gfx:gpu] SDL_AcquireGPUSwapchainTexture failed: {}", SDL_GetError());
                    SDL_CancelGPUCommandBuffer(commandBuffer);
                    resetFrameState();
                    return;
                }
                if (swapchainTexture == nullptr)
                {
                    // Window minimized / swapchain being recreated on resize:
                    // nothing to render into this frame, just drop it.
                    logging::logDebug("[gfx:gpu] swapchain texture unavailable (minimized/resized), skipping frame");
                    SDL_CancelGPUCommandBuffer(commandBuffer);
                    resetFrameState();
                    return;
                }

                if (!ensureSceneResources())
                {
                    logging::logDebug("[gfx:gpu] present: scene resources unavailable, showing cleared swapchain");
                    SDL_GPUColorTargetInfo clearTarget = {};
                    clearTarget.texture = swapchainTexture;
                    clearTarget.clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };
                    clearTarget.load_op = SDL_GPU_LOADOP_CLEAR;
                    clearTarget.store_op = SDL_GPU_STOREOP_STORE;
                    auto* pass = SDL_BeginGPURenderPass(commandBuffer, &clearTarget, 1, nullptr);
                    SDL_EndGPURenderPass(pass);
                    submitAndReset(commandBuffer);
                    return;
                }

                // The render target the D3D device draws into (surface0).
                auto* rtEntry = findSurface(mDeviceState.renderTarget);
                SDL_GPUTexture* sceneTexture = rtEntry != nullptr && rtEntry->textureCreated ? rtEntry->texture : nullptr;
                const auto rtW = rtEntry != nullptr ? rtEntry->width : 0u;
                const auto rtH = rtEntry != nullptr ? rtEntry->height : 0u;
                logging::logDebug(
                    "[gfx:gpu] present: rtKey={} rtEntry={} tex={} rt={}x{} vp={}x{} pendingClearT={} clear={}",
                    static_cast<void*>(mDeviceState.renderTarget),
                    static_cast<void*>(rtEntry),
                    static_cast<void*>(sceneTexture),
                    rtW,
                    rtH,
                    mDeviceState.viewport2.dwWidth,
                    mDeviceState.viewport2.dwHeight,
                    mDeviceState.pendingClearTarget,
                    mDeviceState.pendingClearDepth);

                if (sceneTexture != nullptr && rtW != 0 && rtH != 0)
                {
                    // Append the letterboxed blit quad (swapchain pixel space)
                    // to this frame's vertex pool so the single upload below
                    // covers both the scene and the present blit.
                    appendBlitQuad(winW, winH, rtW, rtH);

                    // Grow the vertex pool if this frame needs more room. The
                    // previous frame's fence was waited on at the top of
                    // present(), so releasing/recreating buffers here cannot
                    // race in-flight work.
                    if (mFrameVertices.size() > mVertexBufferCapacity)
                        ensureVertexBuffer(static_cast<Uint32>(mFrameVertices.size()));

                    if (mVertexBuffer != nullptr && !mFrameVertices.empty())
                    {
                        void* mapped = SDL_MapGPUTransferBuffer(mDevice, mVertexTransfer, false);
                        if (mapped == nullptr)
                        {
                            logging::logError("[gfx:gpu] SDL_MapGPUTransferBuffer failed: {}", SDL_GetError());
                        }
                        else
                        {
                            std::memcpy(mapped, mFrameVertices.data(), mFrameVertices.size());
                            SDL_UnmapGPUTransferBuffer(mDevice, mVertexTransfer);

                            auto* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
                            SDL_GPUTransferBufferLocation source = {};
                            source.transfer_buffer = mVertexTransfer;
                            SDL_GPUBufferRegion destination = {};
                            destination.buffer = mVertexBuffer;
                            destination.offset = 0;
                            destination.size = static_cast<Uint32>(mFrameVertices.size());
                            SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
                            SDL_EndGPUCopyPass(copyPass);
                        }
                    }

                    // Scene pass: render into the offscreen target, clearing it
                    // (and/or the depth buffer) once per frame as the game's
                    // viewport Clear requested.
                    SDL_GPUColorTargetInfo colorTarget = {};
                    colorTarget.texture = sceneTexture;
                    colorTarget.mip_level = 0;
                    colorTarget.layer_or_depth_plane = 0;
                    colorTarget.clear_color = mDeviceState.clearColor;
                    colorTarget.load_op = mDeviceState.pendingClearTarget ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
                    colorTarget.store_op = SDL_GPU_STOREOP_STORE;

                    SDL_GPUDepthStencilTargetInfo depthTarget = {};
                    depthTarget.texture = mDepthTexture;
                    depthTarget.clear_depth = 1.0f;
                    depthTarget.load_op = mDeviceState.pendingClearDepth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
                    depthTarget.store_op = SDL_GPU_STOREOP_STORE;
                    depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
                    depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

                    auto* scenePass = SDL_BeginGPURenderPass(commandBuffer, &colorTarget, 1, &depthTarget);
                    SDL_GPUViewport viewport = {};
                    viewport.x = static_cast<float>(mDeviceState.viewport2.dwX);
                    viewport.y = static_cast<float>(mDeviceState.viewport2.dwY);
                    viewport.w = static_cast<float>(mDeviceState.haveViewport ? mDeviceState.viewport2.dwWidth : rtW);
                    viewport.h = static_cast<float>(mDeviceState.haveViewport ? mDeviceState.viewport2.dwHeight : rtH);
                    // The game's D3DVIEWPORT2 dvMinZ/dvMaxZ define the z-range
                    // the viewport maps clip-space z into (the game sets 0/1 at
                    // init, Marni::init_all 0x404071). SDL_GPU's viewport
                    // min/max depth performs the same remap, so honor whatever
                    // the game sets instead of hardcoding the full range.
                    viewport.min_depth = mDeviceState.haveViewport ? mDeviceState.viewport2.dvMinZ : 0.0f;
                    viewport.max_depth = mDeviceState.haveViewport ? mDeviceState.viewport2.dvMaxZ : 1.0f;
                    SDL_SetGPUViewport(scenePass, &viewport);

                    // The TL vertex shader converts screen coords to NDC using
                    // the viewport rect (SDL_PushGPUVertexUniformData is scoped
                    // to the command buffer, so one push serves every draw).
                    //
                    // Legacy D3D2 TL rasterization samples triangles at integer
                    // pixel points; D3D12/SDL_GPU samples at pixel centers
                    // (x + 0.5). Shifting the viewport origin by -0.5 in the
                    // NDC conversion moves scene geometry a half pixel
                    // down-right, which lands the lit pixel set on exactly the
                    // pixels the D3D reference produces (parity sign-off: the
                    // title-screen logo was 1px up-left on the GPU backend).
                    // The present-pass blit quad keeps its unshifted viewport:
                    // it is already pixel-exact at integer coordinates.
                    const float vpSize[4] = { viewport.x - 0.5f, viewport.y - 0.5f, viewport.w, viewport.h };
                    SDL_PushGPUVertexUniformData(commandBuffer, 0, vpSize, sizeof(vpSize));

                    for (const auto& draw : mQueuedDraws)
                    {
                        if (draw.pipeline == nullptr || draw.vertexCount == 0)
                            continue;
                        SDL_BindGPUGraphicsPipeline(scenePass, draw.pipeline);
                        if (draw.texture != nullptr && draw.sampler != nullptr)
                        {
                            SDL_GPUTextureSamplerBinding binding = { draw.texture, draw.sampler };
                            SDL_BindGPUFragmentSamplers(scenePass, 0, &binding, 1);
                        }
                        SDL_GPUBufferBinding vertexBinding = { mVertexBuffer, draw.vertexOffset };
                        SDL_BindGPUVertexBuffers(scenePass, 0, &vertexBinding, 1);
                        SDL_DrawGPUPrimitives(scenePass, draw.vertexCount, 1, 0, 0);
                    }
                    SDL_EndGPURenderPass(scenePass);

                    // Present pass: letterbox the render target into the
                    // swapchain (the blit quad was appended to the vertex pool).
                    ensureBlitPipeline(SDL_GetGPUSwapchainTextureFormat(mDevice, mWindow));
                    if (mBlitPipeline != nullptr)
                    {
                        SDL_GPUColorTargetInfo target = {};
                        target.texture = swapchainTexture;
                        target.mip_level = 0;
                        target.layer_or_depth_plane = 0;
                        target.clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };
                        target.load_op = SDL_GPU_LOADOP_CLEAR;
                        target.store_op = SDL_GPU_STOREOP_STORE;

                        auto* presentPass = SDL_BeginGPURenderPass(commandBuffer, &target, 1, nullptr);
                        SDL_GPUViewport presentVp
                            = { 0.0f, 0.0f, static_cast<float>(winW), static_cast<float>(winH), 0.0f, 1.0f };
                        SDL_SetGPUViewport(presentPass, &presentVp);
                        const float presentVpSize[4] = { 0.0f, 0.0f, static_cast<float>(winW), static_cast<float>(winH) };
                        SDL_PushGPUVertexUniformData(commandBuffer, 0, presentVpSize, sizeof(presentVpSize));
                        SDL_BindGPUGraphicsPipeline(presentPass, mBlitPipeline);
                        SDL_GPUTextureSamplerBinding binding = { sceneTexture, mSamplerLinear };
                        SDL_BindGPUFragmentSamplers(presentPass, 0, &binding, 1);
                        SDL_GPUBufferBinding vertexBinding = { mVertexBuffer, mBlitQuadOffset };
                        SDL_BindGPUVertexBuffers(presentPass, 0, &vertexBinding, 1);
                        SDL_DrawGPUPrimitives(presentPass, 4, 1, 0, 0);
                        SDL_EndGPURenderPass(presentPass);
                    }
                }
                else
                {
                    // No render target yet (pre-init frame): clear the swapchain.
                    SDL_GPUColorTargetInfo clearTarget = {};
                    clearTarget.texture = swapchainTexture;
                    clearTarget.clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };
                    clearTarget.load_op = SDL_GPU_LOADOP_CLEAR;
                    clearTarget.store_op = SDL_GPU_STOREOP_STORE;
                    auto* pass = SDL_BeginGPURenderPass(commandBuffer, &clearTarget, 1, nullptr);
                    SDL_EndGPURenderPass(pass);
                }

                logging::logDebug(
                    "[gfx:gpu] present: {} draws, {} bytes verts, triangles={}",
                    mQueuedDraws.size(),
                    mFrameVertices.size(),
                    mDeviceState.totalTriangles);
                submitAndReset(commandBuffer);
                maybeDumpSceneTexture(sceneTexture, rtW, rtH);
            }

            // ---- surface layer ----

            void create_surface(IUnknown* surface, const DDSURFACEDESC* desc) override
            {
                if (mDevice == nullptr)
                    return;

                // A mode change re-creates surface2/surface0, and ddraw.dll may
                // hand back a recycled pointer for the new object. Release any
                // entry that still exists for this key so its GPU resources are
                // not leaked before the new entry is stored.
                if (mSurfaces.find(surface) != mSurfaces.end())
                {
                    logging::logDebug("[gfx:gpu] CreateSurface {} (replacing previous entry)", static_cast<void*>(surface));
                    releaseSurfaceEntry(surface);
                }

                SurfaceEntry entry;
                entry.width = desc->dwWidth;
                entry.height = desc->dwHeight;
                entry.bpp = desc->ddpfPixelFormat.dwRGBBitCount;
                entry.pitch = entry.width * (entry.bpp / 8);
                adoptPixelFormat(entry, &desc->ddpfPixelFormat);

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
                    "[gfx:gpu] CreateSurface {}x{} bpp={} format={}{}",
                    entry.width,
                    entry.height,
                    entry.bpp,
                    static_cast<int>(entry.format),
                    entry.paletted ? " paletted" : "");
                mSurfaces[surface] = std::move(entry);
            }

            void destroy_surface(IUnknown* surface) override
            {
                if (mSurfaces.find(surface) == mSurfaces.end())
                {
                    logging::logDebug("[gfx:gpu] DestroySurface {} (unknown)", static_cast<void*>(surface));
                    return;
                }
                logging::logDebug("[gfx:gpu] DestroySurface {}", static_cast<void*>(surface));
                if (mDeviceState.renderTarget == surface)
                {
                    // The device's render target is gone (the game destroyed
                    // the surface); drop the key so present() falls back to a
                    // cleared swapchain until a new render target is set.
                    logging::logDebug(
                        "[gfx:gpu] DestroySurface {} (was the current render target)", static_cast<void*>(surface));
                    mDeviceState.renderTarget = nullptr;
                }
                releaseSurfaceEntry(surface);
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
                    entry->lockedPtr = desc->lpSurface;
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
                entry->lockedPtr = desc->lpSurface;
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
                {
                    entry->hasContent = true;
                    entry->contentFromShadow = true;
                }
                entry->locked = false;
                logging::logDebug(
                    "[gfx:gpu] Unlock surface={} {}x{} bpp={} ptrSame={}",
                    static_cast<void*>(surface),
                    entry->width,
                    entry->height,
                    entry->bpp,
                    entry->lockedPtr == entry->shadow.data());
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
                auto* entry = findSurface(surface);
                if (entry != nullptr)
                    entry->palette = palette;
                logging::logDebug(
                    "[gfx:gpu] SetPalette surface={} palette={}", static_cast<void*>(surface), static_cast<void*>(palette));
                return S_OK;
            }

            void create_palette(IUnknown* palette, DWORD flags) override
            {
                // The game creates palettes with a zeroed color table and fills
                // them later via SetEntries; just register the object.
                mPalettes.try_emplace(palette);
                logging::logDebug("[gfx:gpu] CreatePalette palette={} flags={}", static_cast<void*>(palette), flags);
            }

            HRESULT
            set_palette_entries(IUnknown* palette, DWORD flags, DWORD base, DWORD count, const PALETTEENTRY* entries) override
            {
                if (entries == nullptr || count == 0)
                    return S_OK;
                auto& data = mPalettes[palette];
                const auto start = std::min(static_cast<Uint32>(base), 256u);
                const auto n = std::min(static_cast<Uint32>(count), 256u - start);
                // The game always writes peFlags = 0 (MarniSurfaceX::vUnlock /
                // vPalUnlock), which the D3D reference renders as an opaque
                // palette (transparency is handled by colour keying, deferred
                // to a later milestone), so expand every entry to opaque RGBA.
                for (Uint32 i = 0; i < n; i++)
                {
                    data.rgba[start + i] = 0xFF000000u | (static_cast<uint32_t>(entries[i].peRed) << 16)
                        | (static_cast<uint32_t>(entries[i].peGreen) << 8) | static_cast<uint32_t>(entries[i].peBlue);
                }
                logging::logDebug(
                    "[gfx:gpu] SetEntries palette={} base={} count={} flags={}",
                    static_cast<void*>(palette),
                    base,
                    count,
                    flags);

                // A SetEntries may land right after the surface's own Unlock
                // (MarniSurfaceX::vUnlock unlocks first, then pushes the
                // palette), so the texture was expanded with stale entries.
                // Re-expand any surface whose shadow content was written by
                // the game itself (contentFromShadow) so the next draw sees
                // the new colours.
                for (auto& pair : mSurfaces)
                {
                    auto& entry = pair.second;
                    if (entry.paletted && entry.palette == palette && entry.textureCreated && entry.contentFromShadow)
                    {
                        if (uploadFromShadow(entry, nullptr))
                            entry.hasContent = true;
                    }
                }
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
                // The device is re-created on every mode change (Marni::init_all
                // creates a new surface0 and then CreateDevice against it), so
                // the previous render target's entry is dead - its DirectDraw
                // surface was replaced, not Released (ddraw.dll repatches the
                // AddRef/Release vtable slots in-place, so a Release hook can
                // never observe it). Free it here so mode changes do not
                // accumulate one render-target-sized texture per change.
                if (mDeviceState.renderTarget != nullptr)
                {
                    logging::logDebug(
                        "[gfx:gpu] CreateDevice: releasing previous render target entry {}",
                        static_cast<void*>(mDeviceState.renderTarget));
                    releaseSurfaceEntry(mDeviceState.renderTarget);
                }
                mDeviceState = {};
                logging::logInfo("[gfx:gpu] CreateDevice device={}", static_cast<void*>(device));
            }

            HRESULT set_render_target(IUnknown* device, IUnknown* surface, DWORD flags) override
            {
                mDeviceState.renderTarget = surface;
                logging::logDebug(
                    "[gfx:gpu] SetRenderTarget device={} surface={} flags={}",
                    static_cast<void*>(device),
                    static_cast<void*>(surface),
                    flags);
                // The device's render target is the game's offscreen surface0
                // (640x480 32bpp), which the game may never Lock or query via
                // GetSurfaceDesc (e.g. while loading rooms it just draws). If
                // the entry is still deferred (bpp unknown), force-adopt the
                // render target as 32bpp so the scene pass can begin.
                auto* entry = findSurface(surface);
                if (entry != nullptr && entry->width != 0 && entry->height != 0 && entry->bpp == 0)
                {
                    entry->bpp = 32;
                    entry->pitch = entry->width * (entry->bpp / 8);
                    if (ensureTexture(*entry))
                    {
                        logging::logInfo(
                            "[gfx:gpu] render target surface adopted as {}x{} bpp=32", entry->width, entry->height);
                    }
                }
                return S_OK;
            }

            HRESULT set_current_viewport(IUnknown* device, IUnknown* viewport) override
            {
                mDeviceState.viewport = viewport;
                logging::logDebug(
                    "[gfx:gpu] SetCurrentViewport device={} viewport={}",
                    static_cast<void*>(device),
                    static_cast<void*>(viewport));
                return S_OK;
            }

            HRESULT set_viewport(IUnknown* viewport, const D3DVIEWPORT2* vp) override
            {
                if (vp != nullptr)
                {
                    mDeviceState.viewport2 = *vp;
                    mDeviceState.haveViewport = true;
                }
                // Log the full rect + z-range: the game sets dwX=0/dwY=0 and
                // dvMinZ=0/dvMaxZ=1 at init (Marni::init_all), so the offset
                // math in tl_vertex.hlsl and the SDL viewport z-range are
                // no-ops today but must follow whatever the game sets.
                logging::logDebug(
                    "[gfx:gpu] SetViewport2 viewport={} pos={}x{} size={}x{} z=[{},{}] clip=({},{})+{}x{}",
                    static_cast<void*>(viewport),
                    vp != nullptr ? vp->dwX : 0,
                    vp != nullptr ? vp->dwY : 0,
                    vp != nullptr ? vp->dwWidth : 0,
                    vp != nullptr ? vp->dwHeight : 0,
                    vp != nullptr ? vp->dvMinZ : 0.0f,
                    vp != nullptr ? vp->dvMaxZ : 0.0f,
                    vp != nullptr ? vp->dvClipX : 0.0f,
                    vp != nullptr ? vp->dvClipY : 0.0f,
                    vp != nullptr ? vp->dvClipWidth : 0.0f,
                    vp != nullptr ? vp->dvClipHeight : 0.0f);
                return S_OK;
            }

            HRESULT set_background(IUnknown* viewport, D3DMATERIALHANDLE materialHandle) override
            {
                // The ambient clear color arrives via set_material (the game
                // SetMaterials the background material right before this).
                logging::logDebug(
                    "[gfx:gpu] SetBackground viewport={} handle={}", static_cast<void*>(viewport), materialHandle);
                return S_OK;
            }

            void set_material(const D3DMATERIAL* material) override
            {
                if (material != nullptr)
                {
                    mDeviceState.clearColor.r = material->ambient.r;
                    mDeviceState.clearColor.g = material->ambient.g;
                    mDeviceState.clearColor.b = material->ambient.b;
                    mDeviceState.clearColor.a = 1.0f;
                }
                logging::logDebug(
                    "[gfx:gpu] SetMaterial ambient=({}, {}, {})",
                    mDeviceState.clearColor.r,
                    mDeviceState.clearColor.g,
                    mDeviceState.clearColor.b);
            }

            void create_texture_handle(IUnknown* device, DWORD handle, IUnknown* surface) override
            {
                // The game obtains D3D texture handles via
                // IDirect3DTexture2::GetHandle (hooked in the COM front-end);
                // remember the owning DirectDraw surface so a later
                // SetRenderState(TEXTUREHANDLE, h) can resolve to the texture.
                if (handle != 0)
                    mTextureHandles[handle] = surface;
                logging::logDebug(
                    "[gfx:gpu] CreateTextureHandle device={} handle={} surface={}",
                    static_cast<void*>(device),
                    handle,
                    static_cast<void*>(surface));
            }

            // Replays IDirect3DTexture2::Load(dst, src): the game fills an
            // internal surface (src) through the lock/upload path, then the
            // D3D driver copies those pixels into the texture's backing
            // surface (dst) behind our back. Copy the source's GPU texture
            // into the destination's so handle-bound textures get content.
            void texture_load(IUnknown* surface, IUnknown* srcSurface) override
            {
                if (mDevice == nullptr)
                    return;

                auto* dstEntry = findSurface(surface);
                auto* srcEntry = findSurface(srcSurface);
                if (dstEntry == nullptr || srcEntry == nullptr)
                {
                    logging::logDebug(
                        "[gfx:gpu] texture_load surface={} src={} (unknown surface)",
                        static_cast<void*>(surface),
                        static_cast<void*>(srcSurface));
                    return;
                }
                if (!ensureTexture(*dstEntry) || !ensureTexture(*srcEntry))
                {
                    logging::logDebug("[gfx:gpu] texture_load (no GPU texture)");
                    return;
                }
                if (dstEntry->width != srcEntry->width || dstEntry->height != srcEntry->height
                    || dstEntry->format != srcEntry->format)
                {
                    logging::logDebug(
                        "[gfx:gpu] texture_load size/format mismatch {}x{} fmt={} vs {}x{} fmt={}",
                        dstEntry->width,
                        dstEntry->height,
                        static_cast<int>(dstEntry->format),
                        srcEntry->width,
                        srcEntry->height,
                        static_cast<int>(srcEntry->format));
                    return;
                }

                // GPU texture-to-texture copy (fast path; same format/size).
                auto* commandBuffer = SDL_AcquireGPUCommandBuffer(mDevice);
                if (commandBuffer == nullptr)
                {
                    logging::logError("[gfx:gpu] SDL_AcquireGPUCommandBuffer failed: {}", SDL_GetError());
                    return;
                }
                auto* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
                SDL_GPUTextureLocation srcLoc = {};
                srcLoc.texture = srcEntry->texture;
                srcLoc.layer = 0;
                srcLoc.mip_level = 0;
                srcLoc.x = 0;
                srcLoc.y = 0;
                srcLoc.z = 0;
                SDL_GPUTextureLocation dstLoc = {};
                dstLoc.texture = dstEntry->texture;
                dstLoc.layer = 0;
                dstLoc.mip_level = 0;
                dstLoc.x = 0;
                dstLoc.y = 0;
                dstLoc.z = 0;
                SDL_CopyGPUTextureToTexture(copyPass, &srcLoc, &dstLoc, dstEntry->width, dstEntry->height, 1, false);
                SDL_EndGPUCopyPass(copyPass);
                if (!SDL_SubmitGPUCommandBuffer(commandBuffer))
                {
                    logging::logError("[gfx:gpu] SDL_SubmitGPUCommandBuffer failed: {}", SDL_GetError());
                    return;
                }
                SDL_WaitForGPUIdle(mDevice);
                dstEntry->hasContent = true;
                dstEntry->contentFromShadow = false; // content arrived via GPU copy
                logging::logDebug(
                    "[gfx:gpu] texture_load surface={} <- src={} {}x{} fmt={}",
                    static_cast<void*>(surface),
                    static_cast<void*>(srcSurface),
                    dstEntry->width,
                    dstEntry->height,
                    static_cast<int>(dstEntry->format));
            }

            HRESULT begin_scene(IUnknown* device) override
            {
                // Draws are deferred to present(), so a scene has no immediate
                // GPU work; begin_scene just marks the device active.
                logging::logDebug("[gfx:gpu] BeginScene device={}", static_cast<void*>(device));
                return S_OK;
            }

            HRESULT end_scene(IUnknown* device) override
            {
                logging::logDebug("[gfx:gpu] EndScene device={}", static_cast<void*>(device));
                return S_OK;
            }

            HRESULT set_render_state(IUnknown* device, D3DRENDERSTATETYPE state, DWORD value) override
            {
                switch (static_cast<int>(state))
                {
                case RS_TEXTUREHANDLE: mDeviceState.texHandle = value; break;
                case RS_ZENABLE: mDeviceState.zEnable = value != 0; break;
                case RS_ZWRITEENABLE: mDeviceState.zWrite = value != 0; break;
                case RS_ZFUNC: mDeviceState.zFunc = value; break;
                case RS_SHADEMODE: break; // gouraud/flat: per-vertex colors interpolate naturally
                case RS_CULLMODE: mDeviceState.cullMode = value; break;
                case RS_ALPHABLENDENABLE: mDeviceState.alphaBlend = value != 0; break;
                case RS_SRCBLEND: mDeviceState.srcBlend = value; break;
                case RS_DESTBLEND: mDeviceState.dstBlend = value; break;
                case RS_TEXTUREMAG:
                    if (mDeviceState.texMag != value)
                    {
                        logging::logDebug("[gfx:gpu] filter MAG -> {} ({})", value, value == 2 ? "bilinear" : "nearest");
                        mDeviceState.texMag = value;
                    }
                    break;
                case RS_TEXTUREMIN:
                    if (mDeviceState.texMin != value)
                    {
                        logging::logDebug("[gfx:gpu] filter MIN -> {} ({})", value, value == 6 ? "bilinear" : "nearest");
                        mDeviceState.texMin = value;
                    }
                    break;
                case RS_SPECULARENABLE: break;  // specular ignored
                case RS_TEXTUREMAPBLEND: break; // always MODULATE in practice
                case RS_COLORKEYENABLE: break;  // color keying is an M5 concern
                case RS_TEXTUREADDRESS:
                case RS_TEXTUREPERSPECTIVE:
                case RS_ANTIALIAS:
                case RS_LASTPIXEL:
                case RS_SUBPIXEL:
                case RS_EDGEANTIALIAS:
                case RS_ANISOTROPY: break; // ignored: no effect on the SDL_GPU pipeline used here
                default:
                    logging::logDebug("[gfx:gpu] SetRenderState state={} value={} (unhandled)", static_cast<int>(state), value);
                    break;
                }
                return S_OK;
            }

            HRESULT clear(IUnknown* viewport, DWORD count, const D3DRECT* rects, DWORD flags) override
            {
                // Record what the next frame's scene pass must clear. The D3D
                // target clear color is the background material's ambient color
                // (tracked via set_material); the depth buffer clears to 1.0.
                mDeviceState.pendingClearTarget = (flags & D3DCLEAR_TARGET) != 0;
                mDeviceState.pendingClearDepth = (flags & D3DCLEAR_ZBUFFER) != 0;
                logging::logDebug(
                    "[gfx:gpu] Clear viewport={} count={} flags={} (target={}, zbuffer={})",
                    static_cast<void*>(viewport),
                    count,
                    flags,
                    mDeviceState.pendingClearTarget,
                    mDeviceState.pendingClearDepth);
                if (rects != nullptr && count > 0)
                {
                    logging::logDebug(
                        "[gfx:gpu] Clear rect=({}, {}, {}, {})", rects[0].x1, rects[0].y1, rects[0].x2, rects[0].y2);
                }
                return S_OK;
            }

            HRESULT draw_primitive(
                IUnknown* device, D3DPRIMITIVETYPE primType, D3DVERTEXTYPE vertexType, const void* vertices, DWORD vertexCount,
                DWORD flags) override
            {
                if (active_backend() != 1)
                    return S_OK;
                if (vertices == nullptr || vertexCount == 0)
                    return S_OK;
                if (vertexType != D3DVT_TLVERTEX)
                {
                    // Untransformed vertices would need the transform pipeline;
                    // the game only draws TL vertices on this path.
                    logging::logDebug(
                        "[gfx:gpu] DrawPrimitive type={} vertexType={} (unhandled, not TL)",
                        static_cast<int>(primType),
                        static_cast<int>(vertexType));
                    return S_OK;
                }

                if (primType == D3DPT_TRIANGLEFAN)
                {
                    // SDL_GPU has no fan topology; expand to a triangle list.
                    const auto* fanVertices = static_cast<const uint8_t*>(vertices);
                    const auto expanded = expandTriangleFan(fanVertices, vertexCount);
                    if (expanded.empty())
                        return S_OK;
                    queueDraw(
                        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
                        expanded.data(),
                        static_cast<Uint32>(expanded.size() / kTLVertexStride));
                    return S_OK;
                }
                queueDraw(mapPrimitiveType(primType), static_cast<const uint8_t*>(vertices), vertexCount);
                return S_OK;
            }

            HRESULT draw_indexed_primitive(
                IUnknown* device, D3DPRIMITIVETYPE primType, D3DVERTEXTYPE vertexType, const void* vertices, DWORD vertexCount,
                const void* indices, DWORD indexCount, DWORD flags) override
            {
                if (active_backend() != 1)
                    return S_OK;
                if (vertices == nullptr || indices == nullptr || vertexCount == 0 || indexCount == 0)
                    return S_OK;
                if (vertexType != D3DVT_TLVERTEX)
                {
                    logging::logDebug(
                        "[gfx:gpu] DrawIndexedPrimitive type={} vertexType={} (unhandled, not TL)",
                        static_cast<int>(primType),
                        static_cast<int>(vertexType));
                    return S_OK;
                }

                // The game does not use indexed draws on this path, but handle
                // them by reordering the TL vertices through the WORD indices so
                // the draw pipeline stays identical.
                const auto* src = static_cast<const uint8_t*>(vertices);
                const auto* idx = static_cast<const WORD*>(indices);
                std::vector<uint8_t> reordered(static_cast<size_t>(indexCount) * kTLVertexStride);
                for (DWORD i = 0; i < indexCount; i++)
                {
                    if (idx[i] >= vertexCount)
                        continue;
                    std::memcpy(
                        reordered.data() + static_cast<size_t>(i) * kTLVertexStride,
                        src + static_cast<size_t>(idx[i]) * kTLVertexStride,
                        kTLVertexStride);
                }
                if (primType == D3DPT_TRIANGLEFAN)
                {
                    // SDL_GPU has no fan topology; expand to a triangle list.
                    const auto expanded = expandTriangleFan(reordered.data(), indexCount);
                    if (expanded.empty())
                        return S_OK;
                    queueDraw(
                        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
                        expanded.data(),
                        static_cast<Uint32>(expanded.size() / kTLVertexStride));
                    return S_OK;
                }
                queueDraw(mapPrimitiveType(primType), reordered.data(), indexCount);
                return S_OK;
            }

            HRESULT set_transform(IUnknown* device, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix) override
            {
                // TL vertices are already transformed; nothing to apply.
                logging::logDebug("[gfx:gpu] SetTransform state={} (ignored, TL pipeline)", static_cast<int>(state));
                return S_OK;
            }

            HRESULT multiply_transform(IUnknown* device, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix) override
            {
                logging::logDebug("[gfx:gpu] MultiplyTransform state={} (ignored, TL pipeline)", static_cast<int>(state));
                return S_OK;
            }

            HRESULT get_stats(IUnknown* /*device*/, D3DSTATS* stats) override
            {
                // The D3D reference backend fills stats from the real device
                // first; when the GPU backend is active, replace them with the
                // counts of what the GPU actually drew so the game's per-frame
                // deltas keep working. The values are cumulative (like the
                // D3D2 device's counters); Marni::do_render subtracts the
                // previous GetStats to obtain the per-frame deltas.
                if (stats != nullptr && active_backend() == 1)
                {
                    stats->dwTrianglesDrawn = static_cast<DWORD>(mDeviceState.totalTriangles);
                    stats->dwVerticesProcessed = static_cast<DWORD>(mDeviceState.totalVertices);
                    logging::logDebug(
                        "[gfx:gpu] GetStats triangles={} vertices={} (cumulative)",
                        stats->dwTrianglesDrawn,
                        stats->dwVerticesProcessed);
                }
                return S_OK;
            }

            // ---- surface layer helpers ----

            SurfaceEntry* findSurface(void* surface)
            {
                const auto it = mSurfaces.find(surface);
                return it == mSurfaces.end() ? nullptr : &it->second;
            }

            // Frees the GPU resources of the entry stored under `key` and
            // removes it from mSurfaces. Also purges any texture-handle entry
            // that resolves to the surface so a stale handle can never resolve
            // to a freed texture. Used by destroy_surface (backend API for
            // surface destruction), create_surface (recycled pointer) and
            // create_device (previous render target replaced by a mode change).
            void releaseSurfaceEntry(void* key)
            {
                const auto it = mSurfaces.find(key);
                if (it == mSurfaces.end())
                    return;
                for (auto h = mTextureHandles.begin(); h != mTextureHandles.end();)
                {
                    if (h->second == key)
                        h = mTextureHandles.erase(h);
                    else
                        ++h;
                }
                releaseSurface(it->second);
                mSurfaces.erase(it);
            }

            // Derives shift/bit-count for each channel from its mask.
            static void decodeMask(Uint32 mask, Uint32& shift, Uint32& bits)
            {
                shift = 0;
                bits = 0;
                if (mask == 0)
                    return;
                while ((mask & 1) == 0)
                {
                    mask >>= 1;
                    ++shift;
                }
                while ((mask & 1) != 0)
                {
                    mask >>= 1;
                    ++bits;
                }
            }

            static std::string hexWord(Uint32 v)
            {
                std::ostringstream oss;
                oss << "0x" << std::hex << v;
                return oss.str();
            }

            // Adopts the DirectDraw pixel-format layout (channel masks plus the
            // paletted flag) from a DDSURFACEDESC pixel format. Masks are only
            // recorded once; zero masks keep the previous value.
            void adoptPixelFormat(SurfaceEntry& entry, const DDPIXELFORMAT* pf)
            {
                if (pf == nullptr)
                    return;
                if (entry.rMask == 0 && pf->dwRBitMask != 0)
                    entry.rMask = pf->dwRBitMask;
                if (entry.gMask == 0 && pf->dwGBitMask != 0)
                    entry.gMask = pf->dwGBitMask;
                if (entry.bMask == 0 && pf->dwBBitMask != 0)
                    entry.bMask = pf->dwBBitMask;
                if (entry.aMask == 0 && pf->dwRGBAlphaBitMask != 0)
                    entry.aMask = pf->dwRGBAlphaBitMask;
                if ((pf->dwFlags & (DDPF_PALETTEINDEXED8 | DDPF_PALETTEINDEXED4)) != 0)
                    entry.paletted = true;
                if (entry.rBits == 0 || entry.gBits == 0 || entry.bBits == 0 || entry.aBits == 0)
                {
                    decodeMask(entry.rMask, entry.rShift, entry.rBits);
                    decodeMask(entry.gMask, entry.gShift, entry.gBits);
                    decodeMask(entry.bMask, entry.bShift, entry.bBits);
                    decodeMask(entry.aMask, entry.aShift, entry.aBits);
                }
                // Evidence for the 16bpp format decision (M5): the game does not
                // pass channel masks in the CreateSurface desc (they arrive with
                // the first GetSurfaceDesc/Lock), so log them the first time a
                // real pixel format is seen.
                if (entry.bpp == 16 && (pf->dwRBitMask | pf->dwGBitMask | pf->dwBBitMask) != 0)
                {
                    logging::logDebug(
                        "[gfx:gpu] 16bpp masks R={} G={} B={} A={}",
                        hexWord(entry.rMask),
                        hexWord(entry.gMask),
                        hexWord(entry.bMask),
                        hexWord(entry.aMask));
                }
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
                if (entry.bpp != 0 && (entry.rMask == 0 || entry.gMask == 0 || entry.bMask == 0 || entry.aMask == 0))
                {
                    const auto before = entry.rMask | entry.gMask | entry.bMask | entry.aMask;
                    adoptPixelFormat(entry, &desc->ddpfPixelFormat);
                    changed |= (entry.rMask | entry.gMask | entry.bMask | entry.aMask) != before;
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

            // Chooses the SDL_GPU texture format for a surface based on its
            // bit depth and (for 16bpp) the DirectDraw channel masks:
            //  - 32bpp RGBX8888 -> R8G8B8A8 (converted during transfers)
            //  - 16bpp RGB565 (0xF800/0x07E0/0x001F) -> B5G6R5, byte-identical
            //  - 16bpp RGB555/ARGB1555 or any other mask layout -> R8G8B8A8
            //    (expanded per-pixel from the masks)
            //  - 8bpp paletted -> R8G8B8A8 (indices expanded through the
            //    surface's palette during transfers)
            static SDL_GPUTextureFormat pickTextureFormat(const SurfaceEntry& entry)
            {
                switch (entry.bpp)
                {
                case 8: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
                case 16:
                    if (entry.rMask == 0xF800 && entry.gMask == 0x07E0 && entry.bMask == 0x001F)
                        return SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM;
                    return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
                case 32: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
                default: return SDL_GPU_TEXTUREFORMAT_INVALID;
                }
            }

            // Bytes per pixel of a GPU texture format used by the backend
            // (SDL3.4 has no SDL_GPUTextureFormatTexelSize helper).
            static Uint32 textureTexelBytes(SDL_GPUTextureFormat format)
            {
                switch (format)
                {
                case SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM: return 2;
                case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM: return 4;
                default: return 4;
                }
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

                entry.format = pickTextureFormat(entry);
                if (entry.format == SDL_GPU_TEXTUREFORMAT_INVALID)
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

                // The shadow holds the game's pixels (pitch * height); the
                // transfer buffers hold the texture layout (width * texel *
                // height), which differs for paletted 8bpp (1 vs 4 bytes).
                const auto shadowSize = static_cast<Uint32>(entry.pitch) * entry.height;
                const auto textureBytes = static_cast<Uint32>(entry.width) * textureTexelBytes(entry.format) * entry.height;
                SDL_GPUTransferBufferCreateInfo transferInfo = {};
                transferInfo.size = textureBytes;
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

                entry.shadow.assign(shadowSize, 0);
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
                entry.contentFromShadow = false;
                entry.palette = nullptr;
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
                convertTextureToShadow(entry, static_cast<const uint8_t*>(mapped), entry.shadow.data());
                SDL_UnmapGPUTransferBuffer(mDevice, entry.downloadBuffer);
                return true;
            }

            // Uploads the given region of the CPU shadow (nullptr = whole
            // surface) into the GPU texture. Used by Unlock and Blt colorfill.
            bool uploadFromShadow(SurfaceEntry& entry, const RECT* region)
            {
                if (mDevice == nullptr)
                    return false;
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
                const auto srcTexel = entry.bpp / 8;
                // The texture row may be wider than the shadow row: paletted
                // 8bpp surfaces expand 1 byte per pixel into 4 bytes of RGBA.
                const auto dstTexel = textureTexelBytes(entry.format);
                const auto rowPixels = static_cast<size_t>(entry.width);
                for (auto row = top; row < bottom; row++)
                {
                    const auto* srcRow
                        = entry.shadow.data() + static_cast<size_t>(row) * entry.pitch + static_cast<size_t>(left) * srcTexel;
                    auto* dstRow = dst + (static_cast<size_t>(row) * rowPixels + left) * dstTexel;
                    convertShadowToTexture(entry, srcRow, dstRow, w);
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
                dstEntry.contentFromShadow = true;
                logging::logDebug(
                    "[gfx:gpu] Blt colorfill color={} rect=({},{} - {},{})",
                    color,
                    region->left,
                    region->top,
                    region->right,
                    region->bottom);
                return true;
            }

            // Scales an N-bit channel value (0..(1<<bits)-1) to 8 bits,
            // rounding to the nearest 8-bit level.
            static uint8_t scaleTo8(Uint32 value, Uint32 bits)
            {
                if (bits == 0)
                    return 0;
                if (bits >= 8)
                    return static_cast<uint8_t>(value);
                const Uint32 max = (1u << bits) - 1;
                return static_cast<uint8_t>((value * 255u + max / 2u) / max);
            }

            // Quantizes an 8-bit channel value to an N-bit field.
            static Uint32 unscaleToBits(Uint32 value, Uint32 bits)
            {
                if (bits == 0)
                    return 0;
                if (bits >= 8)
                    return value;
                const Uint32 max = (1u << bits) - 1;
                return (value * max + 127u) / 255u;
            }

            // Converts `pixelCount` shadow pixels (the game's native DirectDraw
            // layout) into the texture layout of the entry's GPU format. The
            // per-row source/destination strides are the caller's concern.
            void convertShadowToTexture(const SurfaceEntry& entry, const uint8_t* src, uint8_t* dst, Uint32 pixelCount)
            {
                if (entry.paletted)
                {
                    // 8bpp indexed: expand each index through the surface's
                    // palette to opaque RGBA (no palette yet = black).
                    const auto it = mPalettes.find(entry.palette);
                    const auto* pal = it != mPalettes.end() ? it->second.rgba.data() : nullptr;
                    for (Uint32 i = 0; i < pixelCount; i++)
                    {
                        const auto c = pal != nullptr ? pal[*src] : 0xFF000000u;
                        dst[0] = static_cast<uint8_t>((c >> 16) & 0xFF);
                        dst[1] = static_cast<uint8_t>((c >> 8) & 0xFF);
                        dst[2] = static_cast<uint8_t>(c & 0xFF);
                        dst[3] = static_cast<uint8_t>((c >> 24) & 0xFF);
                        ++src;
                        dst += 4;
                    }
                    return;
                }
                if (entry.format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM)
                {
                    if (entry.bpp == 32)
                    {
                        // DirectDraw RGBX8888 memory order is B,G,R,X; the
                        // standard layout (or unknown masks) byte-swaps to RGBA.
                        for (Uint32 i = 0; i < pixelCount; i++)
                        {
                            dst[0] = src[2]; // R
                            dst[1] = src[1]; // G
                            dst[2] = src[0]; // B
                            dst[3] = 0xFF;   // A (DirectDraw RGBX carries no alpha)
                            src += 4;
                            dst += 4;
                        }
                        return;
                    }
                    // 16bpp RGB555/ARGB1555 (or any other mask layout):
                    // expand each channel from the recorded bit masks.
                    for (Uint32 i = 0; i < pixelCount; i++)
                    {
                        const auto p = static_cast<uint16_t>(src[0]) | (static_cast<uint16_t>(src[1]) << 8);
                        dst[0] = scaleTo8((p >> entry.rShift) & ((1u << entry.rBits) - 1u), entry.rBits);
                        dst[1] = scaleTo8((p >> entry.gShift) & ((1u << entry.gBits) - 1u), entry.gBits);
                        dst[2] = scaleTo8((p >> entry.bShift) & ((1u << entry.bBits) - 1u), entry.bBits);
                        dst[3]
                            = entry.aMask != 0 ? scaleTo8((p >> entry.aShift) & ((1u << entry.aBits) - 1u), entry.aBits) : 0xFF;
                        src += 2;
                        dst += 4;
                    }
                    return;
                }
                // 16bpp RGB565 -> B5G6R5 is byte-identical; unknown layouts
                // also land here so the pixels keep passing through untouched.
                std::memcpy(dst, src, static_cast<size_t>(pixelCount) * 2);
            }

            // Converts a full texture (width*height pixels in the entry's GPU
            // layout) back into the CPU shadow (pitch-stride rows in the game's
            // DirectDraw layout). Used by downloadToShadow.
            void convertTextureToShadow(const SurfaceEntry& entry, const uint8_t* src, uint8_t* dst)
            {
                const auto pixelCount = static_cast<size_t>(entry.width) * entry.height;
                if (entry.paletted)
                {
                    // RGBA -> nearest palette index per pixel.
                    const auto it = mPalettes.find(entry.palette);
                    const auto* pal = it != mPalettes.end() ? it->second.rgba.data() : nullptr;
                    if (pal == nullptr)
                    {
                        std::memset(dst, 0, entry.shadow.size());
                        return;
                    }
                    for (size_t i = 0; i < pixelCount; i++)
                    {
                        const auto r = src[i * 4 + 0];
                        const auto g = src[i * 4 + 1];
                        const auto b = src[i * 4 + 2];
                        Uint32 bestDist = 0xFFFFFFFFu;
                        uint8_t best = 0;
                        for (Uint32 pi = 0; pi < 256; pi++)
                        {
                            const auto c = pal[pi];
                            const int dr = static_cast<int>(r) - static_cast<int>((c >> 16) & 0xFF);
                            const int dg = static_cast<int>(g) - static_cast<int>((c >> 8) & 0xFF);
                            const int db = static_cast<int>(b) - static_cast<int>(c & 0xFF);
                            const auto d = static_cast<Uint32>(dr * dr + dg * dg + db * db);
                            if (d < bestDist)
                            {
                                bestDist = d;
                                best = static_cast<uint8_t>(pi);
                                if (d == 0)
                                    break;
                            }
                        }
                        const auto row = i / entry.width;
                        const auto col = i % entry.width;
                        dst[row * entry.pitch + col] = best;
                    }
                    return;
                }
                if (entry.format == SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM)
                {
                    // 16bpp RGB565 <-> B5G6R5 is byte-identical, row by row.
                    for (Uint32 row = 0; row < entry.height; row++)
                    {
                        std::memcpy(
                            dst + static_cast<size_t>(row) * entry.pitch,
                            src + static_cast<size_t>(row) * entry.width * 2,
                            static_cast<size_t>(entry.width) * 2);
                    }
                    return;
                }
                if (entry.bpp == 32)
                {
                    // R8G8B8A8 -> DirectDraw RGBX8888 (memory order B,G,R,X).
                    for (size_t i = 0; i < pixelCount; i++)
                    {
                        dst[i * 4 + 0] = src[i * 4 + 2]; // B
                        dst[i * 4 + 1] = src[i * 4 + 1]; // G
                        dst[i * 4 + 2] = src[i * 4 + 0]; // R
                        dst[i * 4 + 3] = 0;              // X
                    }
                    return;
                }
                // 16bpp RGB555/ARGB1555: quantize RGBA back into the masks.
                for (size_t i = 0; i < pixelCount; i++)
                {
                    const auto r = src[i * 4 + 0];
                    const auto g = src[i * 4 + 1];
                    const auto b = src[i * 4 + 2];
                    const auto a = src[i * 4 + 3];
                    uint16_t p = 0;
                    p |= static_cast<uint16_t>(unscaleToBits(r, entry.rBits)) << entry.rShift;
                    p |= static_cast<uint16_t>(unscaleToBits(g, entry.gBits)) << entry.gShift;
                    p |= static_cast<uint16_t>(unscaleToBits(b, entry.bBits)) << entry.bShift;
                    if (entry.aMask != 0)
                        p |= static_cast<uint16_t>(unscaleToBits(a, entry.aBits)) << entry.aShift;
                    const auto row = i / entry.width;
                    const auto col = i % entry.width;
                    auto* out = dst + row * entry.pitch + col * 2;
                    out[0] = static_cast<uint8_t>(p & 0xFF);
                    out[1] = static_cast<uint8_t>(p >> 8);
                }
            }

        private:
            std::unordered_map<void*, SurfaceEntry> mSurfaces;
            std::unordered_map<DWORD, void*> mTextureHandles; // D3D texture handle -> surface key
            std::unordered_map<void*, PaletteData> mPalettes; // IDirectDrawPalette* -> RGBA entries
            DWORD mLastUnknownHandle = 0;                     // last unresolved handle logged
            DeviceState mDeviceState;

            SDL_GPUDevice* mDevice = nullptr;
            SDL_Window* mWindow = nullptr;

            // Scene resources (created lazily by ensureSceneResources once the
            // render target surface exists).
            SDL_GPUShader* mVertexShader = nullptr;
            SDL_GPUShader* mTexturedFrag = nullptr;
            SDL_GPUShader* mUntexturedFrag = nullptr;
            SDL_GPUSampler* mSamplerLinear = nullptr;
            SDL_GPUSampler* mSamplerNearest = nullptr;
            SDL_GPUTexture* mDepthTexture = nullptr; // D16_UNORM, render-target sized
            Uint32 mDepthW = 0;
            Uint32 mDepthH = 0;

            // Per-frame vertex pool: CPU staging + GPU vertex buffer + upload
            // transfer buffer. Grows on demand; released at shutdown.
            std::vector<uint8_t> mFrameVertices;
            std::vector<QueuedDraw> mQueuedDraws;
            SDL_GPUBuffer* mVertexBuffer = nullptr;
            SDL_GPUTransferBuffer* mVertexTransfer = nullptr;
            Uint32 mVertexBufferCapacity = 0;
            // Fence of the last submitted frame. The next present waits on it
            // before remapping the shared vertex transfer buffer, allowing one
            // frame of CPU/GPU overlap instead of a full device idle wait.
            SDL_GPUFence* mFrameFence = nullptr;

            // Pipelines: scene pipelines are cached per PipelineKey; the blit
            // pipeline presents the render target into the swapchain.
            std::unordered_map<PipelineKey, SDL_GPUGraphicsPipeline*, PipelineKeyHash> mPipelineCache;
            SDL_GPUGraphicsPipeline* mBlitPipeline = nullptr;
            SDL_GPUTextureFormat mBlitSwapchainFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
            Uint32 mBlitQuadOffset = 0;

            // Debug aid (OPENRE_GPU_DUMP=<N>): every N-th frame, read back the
            // scene render target after the scene pass and write it as a BMP so
            // the GPU-rendered image can be verified without depending on the
            // DirectDraw primary surface that owns the window. Disabled unless
            // the environment variable is set.
            Uint32 mDumpInterval = 0;
            Uint64 mDumpCounter = 0;
            SDL_GPUTransferBuffer* mDumpTransfer = nullptr;

            // ---- deferred draw helpers ----

            // True when the mirrored TEXTUREMAG/TEXTUREMIN render states select
            // bilinear sampling (D3DFILTER_*: 1=NEAREST, 2=LINEAR, 3/4/5/6 are
            // mip variants; this backend has no mipmaps, so any non-NEAREST
            // value maps to linear). The game sets MAG=2/MIN=6 when bilinear
            // (Marni::SetFiltering) and 1/1 when nearest.
            bool wantsLinearFilter() const
            {
                const auto mag = mDeviceState.texMag;
                const auto min = mDeviceState.texMin;
                return (mag != 1 && mag != 0) || (min != 1 && min != 0);
            }

            void queueDraw(SDL_GPUPrimitiveType primType, const uint8_t* vertices, Uint32 vertexCount)
            {
                if (mDevice == nullptr)
                    return;

                SDL_GPUTexture* texture = nullptr;
                const bool textured = resolveTexture(mDeviceState.texHandle, texture);
                SDL_GPUSampler* sampler = nullptr;
                if (textured)
                    sampler = wantsLinearFilter() ? mSamplerLinear : mSamplerNearest;

                PipelineKey key{};
                key.textured = textured;
                key.alphaBlend = mDeviceState.alphaBlend;
                key.srcFactor = mapBlendFactor(mDeviceState.srcBlend);
                key.dstFactor = mapBlendFactor(mDeviceState.dstBlend);
                key.zTest = mDeviceState.zEnable;
                key.zWrite = mDeviceState.zWrite;
                key.zFunc = mapCompareOp(mDeviceState.zFunc);
                key.cull = mapCullMode(mDeviceState.cullMode);
                key.primType = primType;

                auto* pipeline = getOrCreatePipeline(key);
                if (pipeline == nullptr)
                {
                    logging::logDebug("[gfx:gpu] DrawPrimitive dropped (no pipeline)");
                    return;
                }

                const auto vertexBytes = static_cast<size_t>(vertexCount) * kTLVertexStride;
                const auto vertexOffset = static_cast<Uint32>(mFrameVertices.size());
                mFrameVertices.insert(mFrameVertices.end(), vertices, vertices + vertexBytes);
                mQueuedDraws.push_back(QueuedDraw{ pipeline, texture, sampler, vertexOffset, vertexCount, primType });

                mDeviceState.totalVertices += vertexCount;
                switch (primType)
                {
                case SDL_GPU_PRIMITIVETYPE_TRIANGLELIST: mDeviceState.totalTriangles += vertexCount / 3; break;
                case SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP:
                    mDeviceState.totalTriangles += vertexCount >= 3 ? vertexCount - 2 : 0;
                    break;
                default: break; // point/line primitives have no triangles (the stat stays 0)
                }
                logging::logDebug(
                    "[gfx:gpu] queued draw prim={} verts={} textured={} blend={} z={}",
                    static_cast<int>(primType),
                    vertexCount,
                    textured,
                    mDeviceState.alphaBlend,
                    mDeviceState.zEnable);
            }

            // Resolves a D3D texture handle (SetRenderState TEXTUREHANDLE) to
            // the SDL_GPU texture of the surface the game associated with it
            // via IDirect3DTexture2::GetHandle. Returns false when the handle
            // is unknown or not backed by a texture yet (draw untextured).
            bool resolveTexture(DWORD handle, SDL_GPUTexture*& outTexture)
            {
                outTexture = nullptr;
                if (handle == 0)
                    return false;
                const auto it = mTextureHandles.find(handle);
                if (it == mTextureHandles.end())
                {
                    // Log a handle only when it changes so coverage gaps stay
                    // visible without spamming the debug log every draw.
                    if (mLastUnknownHandle != handle)
                    {
                        logging::logDebug("[gfx:gpu] unhandled texture handle {} (draw untextured)", handle);
                        mLastUnknownHandle = handle;
                    }
                    return false;
                }
                auto* entry = findSurface(it->second);
                if (entry == nullptr || !entry->textureCreated)
                {
                    if (mLastUnknownHandle != handle)
                    {
                        logging::logDebug("[gfx:gpu] texture handle {} has no GPU texture yet", handle);
                        mLastUnknownHandle = handle;
                    }
                    return false;
                }
                outTexture = entry->texture;
                return true;
            }

            // ---- pipelines / resources ----

            // Lazily creates shaders, samplers and the depth texture once the
            // render target surface exists. Returns false until then.
            bool ensureSceneResources()
            {
                if (mDevice == nullptr)
                    return false;
                auto* rtEntry = findSurface(mDeviceState.renderTarget);
                if (rtEntry == nullptr || !rtEntry->textureCreated || rtEntry->width == 0 || rtEntry->height == 0)
                    return false;

                if (mVertexShader == nullptr)
                {
                    const auto formats = SDL_GetGPUShaderFormats(mDevice);
                    const bool dxil = (formats & SDL_GPU_SHADERFORMAT_DXIL) != 0;
                    const SDL_GPUShaderFormat format = dxil ? SDL_GPU_SHADERFORMAT_DXIL : SDL_GPU_SHADERFORMAT_SPIRV;
                    logging::logInfo(
                        "[gfx:gpu] creating shaders (format={})",
                        dxil ? static_cast<int>(SDL_GPU_SHADERFORMAT_DXIL) : static_cast<int>(SDL_GPU_SHADERFORMAT_SPIRV));
                    mVertexShader = createShader(
                        dxil ? gTLVertexDxil : gTLVertexSpirv,
                        dxil ? gTLVertexDxilSize : gTLVertexSpirvSize,
                        format,
                        SDL_GPU_SHADERSTAGE_VERTEX,
                        0,
                        1);
                    mTexturedFrag = createShader(
                        dxil ? gTLTexturedFragDxil : gTLTexturedFragSpirv,
                        dxil ? gTLTexturedFragDxilSize : gTLTexturedFragSpirvSize,
                        format,
                        SDL_GPU_SHADERSTAGE_FRAGMENT,
                        1,
                        0);
                    mUntexturedFrag = createShader(
                        dxil ? gTLUntexturedFragDxil : gTLUntexturedFragSpirv,
                        dxil ? gTLUntexturedFragDxilSize : gTLUntexturedFragSpirvSize,
                        format,
                        SDL_GPU_SHADERSTAGE_FRAGMENT,
                        0,
                        0);
                    if (mVertexShader == nullptr || mTexturedFrag == nullptr || mUntexturedFrag == nullptr)
                    {
                        logging::logError("[gfx:gpu] shader creation failed: {}", SDL_GetError());
                        releaseSceneResources();
                        return false;
                    }
                }

                if (mSamplerLinear == nullptr || mSamplerNearest == nullptr)
                {
                    SDL_GPUSamplerCreateInfo samplerInfo = {};
                    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
                    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
                    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
                    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
                    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
                    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
                    if (mSamplerLinear == nullptr)
                        mSamplerLinear = SDL_CreateGPUSampler(mDevice, &samplerInfo);
                    samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
                    samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
                    if (mSamplerNearest == nullptr)
                        mSamplerNearest = SDL_CreateGPUSampler(mDevice, &samplerInfo);
                    if (mSamplerLinear == nullptr || mSamplerNearest == nullptr)
                    {
                        logging::logError("[gfx:gpu] sampler creation failed: {}", SDL_GetError());
                        releaseSceneResources();
                        return false;
                    }
                }

                if (mDepthTexture == nullptr || mDepthW != rtEntry->width || mDepthH != rtEntry->height)
                {
                    if (mDepthTexture != nullptr)
                    {
                        SDL_ReleaseGPUTexture(mDevice, mDepthTexture);
                        mDepthTexture = nullptr;
                    }
                    mDepthW = rtEntry->width;
                    mDepthH = rtEntry->height;
                    if (!SDL_GPUTextureSupportsFormat(
                            mDevice,
                            SDL_GPU_TEXTUREFORMAT_D16_UNORM,
                            SDL_GPU_TEXTURETYPE_2D,
                            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
                    {
                        logging::logError("[gfx:gpu] D16_UNORM depth format unsupported");
                        return false;
                    }
                    SDL_GPUTextureCreateInfo depthInfo = {};
                    depthInfo.type = SDL_GPU_TEXTURETYPE_2D;
                    depthInfo.format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
                    depthInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
                    depthInfo.width = mDepthW;
                    depthInfo.height = mDepthH;
                    depthInfo.layer_count_or_depth = 1;
                    depthInfo.num_levels = 1;
                    depthInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
                    mDepthTexture = SDL_CreateGPUTexture(mDevice, &depthInfo);
                    if (mDepthTexture == nullptr)
                    {
                        logging::logError("[gfx:gpu] depth texture creation failed: {}", SDL_GetError());
                        return false;
                    }
                    logging::logInfo("[gfx:gpu] depth texture created ({}x{} D16)", mDepthW, mDepthH);
                }
                return true;
            }

            void releaseSceneResources()
            {
                if (mDevice == nullptr)
                    return;
                for (auto& pair : mPipelineCache)
                    SDL_ReleaseGPUGraphicsPipeline(mDevice, pair.second);
                mPipelineCache.clear();
                if (mBlitPipeline != nullptr)
                {
                    SDL_ReleaseGPUGraphicsPipeline(mDevice, mBlitPipeline);
                    mBlitPipeline = nullptr;
                }
                mBlitSwapchainFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
                if (mVertexShader != nullptr)
                {
                    SDL_ReleaseGPUShader(mDevice, mVertexShader);
                    mVertexShader = nullptr;
                }
                if (mTexturedFrag != nullptr)
                {
                    SDL_ReleaseGPUShader(mDevice, mTexturedFrag);
                    mTexturedFrag = nullptr;
                }
                if (mUntexturedFrag != nullptr)
                {
                    SDL_ReleaseGPUShader(mDevice, mUntexturedFrag);
                    mUntexturedFrag = nullptr;
                }
                if (mSamplerLinear != nullptr)
                {
                    SDL_ReleaseGPUSampler(mDevice, mSamplerLinear);
                    mSamplerLinear = nullptr;
                }
                if (mSamplerNearest != nullptr)
                {
                    SDL_ReleaseGPUSampler(mDevice, mSamplerNearest);
                    mSamplerNearest = nullptr;
                }
                if (mDepthTexture != nullptr)
                {
                    SDL_ReleaseGPUTexture(mDevice, mDepthTexture);
                    mDepthTexture = nullptr;
                }
                mDepthW = 0;
                mDepthH = 0;
                if (mVertexBuffer != nullptr)
                {
                    SDL_ReleaseGPUBuffer(mDevice, mVertexBuffer);
                    mVertexBuffer = nullptr;
                }
                if (mVertexTransfer != nullptr)
                {
                    SDL_ReleaseGPUTransferBuffer(mDevice, mVertexTransfer);
                    mVertexTransfer = nullptr;
                }
                if (mDumpTransfer != nullptr)
                {
                    SDL_ReleaseGPUTransferBuffer(mDevice, mDumpTransfer);
                    mDumpTransfer = nullptr;
                }
                mVertexBufferCapacity = 0;
                resetFrameState();
            }

            SDL_GPUShader* createShader(
                const uint8_t* code, Uint32 codeSize, SDL_GPUShaderFormat format, SDL_GPUShaderStage stage, Uint32 numSamplers,
                Uint32 numUniformBuffers)
            {
                SDL_GPUShaderCreateInfo info = {};
                info.code_size = codeSize;
                info.code = code;
                info.entrypoint = "main";
                info.format = format;
                info.stage = stage;
                info.num_samplers = numSamplers;
                info.num_uniform_buffers = numUniformBuffers;
                return SDL_CreateGPUShader(mDevice, &info);
            }

            // Fetches (or lazily creates) the pipeline for a state key. Scene
            // pipelines render into the offscreen target (surface0, 32bpp)
            // with a depth target; the blit pipeline targets the swapchain.
            SDL_GPUGraphicsPipeline* getOrCreatePipeline(const PipelineKey& key)
            {
                const auto it = mPipelineCache.find(key);
                if (it != mPipelineCache.end())
                    return it->second;

                auto* pipeline = createPipeline(key, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, true);
                if (pipeline == nullptr)
                    return nullptr;
                mPipelineCache[key] = pipeline;
                logging::logInfo(
                    "[gfx:gpu] created pipeline (textured={} blend={} zTest={} zWrite={} cull={} prim={})",
                    key.textured,
                    key.alphaBlend,
                    key.zTest,
                    key.zWrite,
                    static_cast<int>(key.cull),
                    static_cast<int>(key.primType));
                return pipeline;
            }

            SDL_GPUGraphicsPipeline* createPipeline(const PipelineKey& key, SDL_GPUTextureFormat targetFormat, bool withDepth)
            {
                if (mVertexShader == nullptr || mTexturedFrag == nullptr || mUntexturedFrag == nullptr)
                    return nullptr;

                const SDL_GPUVertexBufferDescription vbDesc[] = {
                    { 0, kTLVertexStride, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0 },
                };
                const SDL_GPUVertexAttribute attrs[] = {
                    { 0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, kTLVertexPosOffset },
                    { 1, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, kTLVertexColorOffset },
                    { 2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, kTLVertexUvOffset },
                };
                SDL_GPUVertexInputState vertexInput = {};
                vertexInput.vertex_buffer_descriptions = vbDesc;
                vertexInput.num_vertex_buffers = 1;
                vertexInput.vertex_attributes = attrs;
                vertexInput.num_vertex_attributes = 3;

                SDL_GPUGraphicsPipelineCreateInfo info = {};
                info.vertex_shader = mVertexShader;
                info.fragment_shader = key.textured ? mTexturedFrag : mUntexturedFrag;
                info.vertex_input_state = vertexInput;
                info.primitive_type = key.primType;

                info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
                info.rasterizer_state.cull_mode = key.cull;
                info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
                info.rasterizer_state.enable_depth_clip = true;

                info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

                info.depth_stencil_state.compare_op = key.zFunc;
                info.depth_stencil_state.enable_depth_test = key.zTest;
                info.depth_stencil_state.enable_depth_write = key.zWrite;

                SDL_GPUColorTargetDescription target = {};
                target.format = targetFormat;
                if (key.alphaBlend)
                {
                    target.blend_state.enable_blend = true;
                    target.blend_state.src_color_blendfactor = key.srcFactor;
                    target.blend_state.dst_color_blendfactor = key.dstFactor;
                    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
                    target.blend_state.src_alpha_blendfactor = key.srcFactor;
                    target.blend_state.dst_alpha_blendfactor = key.dstFactor;
                    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
                }
                const SDL_GPUColorTargetDescription targets[] = { target };
                info.target_info.color_target_descriptions = targets;
                info.target_info.num_color_targets = 1;
                if (withDepth)
                {
                    info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
                    info.target_info.has_depth_stencil_target = true;
                }

                auto* pipeline = SDL_CreateGPUGraphicsPipeline(mDevice, &info);
                if (pipeline == nullptr)
                    logging::logError("[gfx:gpu] SDL_CreateGPUGraphicsPipeline failed: {}", SDL_GetError());
                return pipeline;
            }

            void ensureBlitPipeline(SDL_GPUTextureFormat swapchainFormat)
            {
                if (mBlitPipeline != nullptr && mBlitSwapchainFormat == swapchainFormat)
                    return;
                if (mBlitPipeline != nullptr)
                {
                    // The swapchain format changed (fullscreen / display-mode
                    // change can swap the window's pixel format): a pipeline's
                    // color target format is immutable, so rebuild it.
                    logging::logInfo(
                        "[gfx:gpu] blit pipeline recreated (swapchain format {} -> {})",
                        static_cast<int>(mBlitSwapchainFormat),
                        static_cast<int>(swapchainFormat));
                    SDL_ReleaseGPUGraphicsPipeline(mDevice, mBlitPipeline);
                    mBlitPipeline = nullptr;
                }
                mBlitSwapchainFormat = swapchainFormat;
                PipelineKey key{};
                key.textured = true;
                // The blit quad is 4 vertices appended in appendBlitQuad();
                // with TRIANGLELIST only triangle (0,1,2) would rasterize and
                // the bottom-right half of the screen would stay black. A
                // triangle strip covers the whole quad ((0,1,2) and (1,2,3));
                // cull mode is NONE (PipelineKey default) so winding is
                // irrelevant.
                key.primType = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
                mBlitPipeline = createPipeline(key, swapchainFormat, false);
                if (mBlitPipeline != nullptr)
                    logging::logInfo(
                        "[gfx:gpu] blit pipeline created (swapchain format={})", static_cast<int>(swapchainFormat));
                else
                    logging::logError("[gfx:gpu] blit pipeline creation failed: {}", SDL_GetError());
            }

            // Grows the per-frame vertex pool. Called from present() after the
            // previous frame was submitted and waited on, so releasing and
            // recreating the buffers cannot race in-flight work.
            void ensureVertexBuffer(Uint32 capacity)
            {
                if (mDevice == nullptr)
                    return;
                if (mVertexBuffer != nullptr && mVertexBufferCapacity >= capacity)
                    return;
                if (mVertexBuffer != nullptr)
                {
                    SDL_ReleaseGPUBuffer(mDevice, mVertexBuffer);
                    mVertexBuffer = nullptr;
                }
                if (mVertexTransfer != nullptr)
                {
                    SDL_ReleaseGPUTransferBuffer(mDevice, mVertexTransfer);
                    mVertexTransfer = nullptr;
                }
                mVertexBufferCapacity = std::max(capacity, 4096u);

                SDL_GPUBufferCreateInfo bufferInfo = {};
                bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
                bufferInfo.size = mVertexBufferCapacity;
                mVertexBuffer = SDL_CreateGPUBuffer(mDevice, &bufferInfo);

                SDL_GPUTransferBufferCreateInfo transferInfo = {};
                transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                transferInfo.size = mVertexBufferCapacity;
                mVertexTransfer = SDL_CreateGPUTransferBuffer(mDevice, &transferInfo);

                if (mVertexBuffer == nullptr || mVertexTransfer == nullptr)
                {
                    logging::logError("[gfx:gpu] vertex buffer creation failed: {}", SDL_GetError());
                    if (mVertexBuffer != nullptr)
                    {
                        SDL_ReleaseGPUBuffer(mDevice, mVertexBuffer);
                        mVertexBuffer = nullptr;
                    }
                    if (mVertexTransfer != nullptr)
                    {
                        SDL_ReleaseGPUTransferBuffer(mDevice, mVertexTransfer);
                        mVertexTransfer = nullptr;
                    }
                    mVertexBufferCapacity = 0;
                    return;
                }
                logging::logInfo("[gfx:gpu] vertex pool grown to {} bytes", mVertexBufferCapacity);
            }

            // Appends the letterboxed fullscreen blit quad (in swapchain pixel
            // space) to the frame vertex pool; returns the byte offset where
            // the quad starts so the present pass can bind exactly it.
            void appendBlitQuad(Uint32 winW, Uint32 winH, Uint32 rtW, Uint32 rtH)
            {
                if (winW == 0 || winH == 0 || rtW == 0 || rtH == 0)
                    return;
                const float scale = std::min(static_cast<float>(winW) / rtW, static_cast<float>(winH) / rtH);
                const float outW = rtW * scale;
                const float outH = rtH * scale;
                const float x0 = (static_cast<float>(winW) - outW) * 0.5f;
                const float y0 = (static_cast<float>(winH) - outH) * 0.5f;

                mBlitQuadOffset = static_cast<Uint32>(mFrameVertices.size());
                // D3DTLVERTEX layout (32 bytes): sx, sy, sz, rhw, color, spec, tu, tv.
                // Color is D3DCOLOR white (0xFFFFFFFF, little-endian FF FF FF FF).
                const float pos[4][4] = {
                    { x0, y0, 0.0f, 1.0f },               // top-left
                    { x0, y0 + outH, 0.0f, 1.0f },        // bottom-left
                    { x0 + outW, y0, 0.0f, 1.0f },        // top-right
                    { x0 + outW, y0 + outH, 0.0f, 1.0f }, // bottom-right
                };
                const float uv[4][2] = {
                    { 0.0f, 0.0f },
                    { 0.0f, 1.0f },
                    { 1.0f, 0.0f },
                    { 1.0f, 1.0f },
                };
                for (int i = 0; i < 4; i++)
                {
                    uint8_t vertex[kTLVertexStride] = {};
                    std::memcpy(vertex + 0, pos[i], sizeof(pos[i]));
                    vertex[kTLVertexColorOffset + 0] = 0xFF;
                    vertex[kTLVertexColorOffset + 1] = 0xFF;
                    vertex[kTLVertexColorOffset + 2] = 0xFF;
                    vertex[kTLVertexColorOffset + 3] = 0xFF;
                    std::memcpy(vertex + kTLVertexUvOffset, uv[i], sizeof(uv[i]));
                    mFrameVertices.insert(mFrameVertices.end(), vertex, vertex + kTLVertexStride);
                }
            }

            void submitAndReset(SDL_GPUCommandBuffer* commandBuffer)
            {
                // Acquire a fence instead of blocking on a full device idle
                // wait: the next present waits only on this fence, which lets
                // the CPU run a frame ahead of the GPU while still guaranteeing
                // the shared vertex transfer buffer is free before it is
                // remapped and the vertex pool is regrown.
                mFrameFence = SDL_SubmitGPUCommandBufferAndAcquireFence(commandBuffer);
                if (mFrameFence == nullptr)
                    logging::logError("[gfx:gpu] SDL_SubmitGPUCommandBufferAndAcquireFence failed: {}", SDL_GetError());
                resetFrameState();
            }

            // Waits for the previous frame's fence so the shared vertex
            // transfer buffer can be remapped (and the vertex pool regrown)
            // without racing in-flight GPU work. Skipped on the first frame,
            // where no fence exists yet.
            void waitForPreviousFrame()
            {
                if (mFrameFence == nullptr)
                    return;
                SDL_GPUFence* fences[] = { mFrameFence };
                if (!SDL_WaitForGPUFences(mDevice, true, fences, 1))
                    logging::logError("[gfx:gpu] SDL_WaitForGPUFences failed: {}", SDL_GetError());
                SDL_ReleaseGPUFence(mDevice, mFrameFence);
                mFrameFence = nullptr;
            }

            void resetFrameState()
            {
                mQueuedDraws.clear();
                mFrameVertices.clear();
                mBlitQuadOffset = 0;
                mDeviceState.pendingClearTarget = false;
                mDeviceState.pendingClearDepth = false;
            }

            // Debug aid: dumps the scene render target (after the scene pass
            // executed) to gpu_dump_<counter>.bmp every OPENRE_GPU_DUMP frames.
            void maybeDumpSceneTexture(SDL_GPUTexture* texture, Uint32 width, Uint32 height)
            {
                if (mDevice == nullptr || texture == nullptr || width == 0 || height == 0)
                    return;
                if (mDumpInterval == 0)
                    return;
                if (++mDumpCounter % mDumpInterval != 0)
                    return;

                const auto bytes = static_cast<Uint32>(width) * height * 4;
                if (mDumpTransfer == nullptr)
                {
                    SDL_GPUTransferBufferCreateInfo info = {};
                    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
                    info.size = bytes;
                    mDumpTransfer = SDL_CreateGPUTransferBuffer(mDevice, &info);
                    if (mDumpTransfer == nullptr)
                    {
                        logging::logError("[gfx:gpu] dump transfer buffer creation failed: {}", SDL_GetError());
                        return;
                    }
                }

                auto* commandBuffer = SDL_AcquireGPUCommandBuffer(mDevice);
                if (commandBuffer == nullptr)
                {
                    logging::logError("[gfx:gpu] dump acquire command buffer failed: {}", SDL_GetError());
                    return;
                }
                auto* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
                SDL_GPUTextureRegion source = {};
                source.texture = texture;
                source.w = width;
                source.h = height;
                source.d = 1;
                SDL_GPUTextureTransferInfo destination = {};
                destination.transfer_buffer = mDumpTransfer;
                destination.pixels_per_row = width;
                destination.rows_per_layer = height;
                SDL_DownloadFromGPUTexture(copyPass, &source, &destination);
                SDL_EndGPUCopyPass(copyPass);
                if (!SDL_SubmitGPUCommandBuffer(commandBuffer))
                {
                    logging::logError("[gfx:gpu] dump submit failed: {}", SDL_GetError());
                    return;
                }
                SDL_WaitForGPUIdle(mDevice);

                void* mapped = SDL_MapGPUTransferBuffer(mDevice, mDumpTransfer, false);
                if (mapped == nullptr)
                {
                    logging::logError("[gfx:gpu] dump map failed: {}", SDL_GetError());
                    return;
                }
                const auto* pixels = static_cast<const uint8_t*>(mapped);

                // Write a 32bpp top-down BMP (rows arrive top-first from the
                // texture; a negative biHeight stores them top-down).
                std::vector<uint8_t> bmp(14 + 40 + bytes);
                const auto pixelOffset = static_cast<uint32_t>(14 + 40);
                const uint32_t fileSize = static_cast<uint32_t>(bmp.size());
                bmp[0] = 'B';
                bmp[1] = 'M';
                std::memcpy(bmp.data() + 2, &fileSize, 4);
                std::memcpy(bmp.data() + 10, &pixelOffset, 4);
                const uint32_t headerSize = 40;
                std::memcpy(bmp.data() + 14, &headerSize, 4);
                std::memcpy(bmp.data() + 18, &width, 4);
                const int32_t negHeight = -static_cast<int32_t>(height);
                std::memcpy(bmp.data() + 22, &negHeight, 4);
                const uint16_t planes = 1;
                std::memcpy(bmp.data() + 26, &planes, 2);
                const uint16_t bitCount = 32;
                std::memcpy(bmp.data() + 28, &bitCount, 2);
                const uint32_t compression = 0;
                std::memcpy(bmp.data() + 30, &compression, 4);
                std::memcpy(bmp.data() + 34, &bytes, 4);
                const uint32_t ppm = 2835; // 72 DPI, cosmetic
                std::memcpy(bmp.data() + 38, &ppm, 4);
                std::memcpy(bmp.data() + 42, &ppm, 4);
                const uint32_t zero = 0;
                std::memcpy(bmp.data() + 46, &zero, 4);
                std::memcpy(bmp.data() + 50, &zero, 4);
                const auto rowBytes = static_cast<size_t>(width) * 4;
                for (Uint32 y = 0; y < height; y++)
                    std::memcpy(bmp.data() + pixelOffset + y * rowBytes, pixels + y * rowBytes, rowBytes);

                char path[64] = {};
                std::snprintf(path, sizeof(path), "gpu_dump_%05llu.bmp", static_cast<unsigned long long>(mDumpCounter));
                std::ofstream file(path, std::ios::binary);
                if (file)
                {
                    file.write(reinterpret_cast<const char*>(bmp.data()), static_cast<std::streamsize>(bmp.size()));
                    logging::logInfo("[gfx:gpu] scene dump written: {} ({}x{})", path, width, height);
                }
                else
                {
                    logging::logError("[gfx:gpu] scene dump write failed: {}", path);
                }
                SDL_UnmapGPUTransferBuffer(mDevice, mDumpTransfer);
            }
        };
    }

    GfxBackend* backend_gpu()
    {
        static GfxBackendGPU backend;
        return &backend;
    }
}
