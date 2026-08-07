#include "gfx_backend.h"
#include "gfx_d3d2.h"
#include "logger.h"

#include <cstdio>
#include <fstream>
#include <vector>

namespace openre::gfx
{
    namespace
    {
        // Reference backend: forwards every call to the real DirectDraw/D3D2
        // object through its saved original vtable. Identical behavior to the
        // un-wrapped game, so this is the known-good reference for the GPU
        // backend. Per-frame methods are intentionally silent; only creation
        // events are logged.
        //
        // The persistent [video] disable_d3d_reference config flag (see
        // gfx::reference_enabled) turns the backend into a no-op for the
        // per-frame scene/draw broadcast (create_device, viewport/background,
        // BeginScene/EndScene, render states, Clear, DrawPrimitive*, transforms,
        // GetStats and the present Blt) while the GPU backend is active: the
        // front-end hooks still answer every COM call, and the GPU backend
        // replays the work on its own textures. The surface-layer forwards
        // (Lock/Unlock/GetSurfaceDesc/IsLost/Restore/AddAttachedSurface/
        // SetColorKey/SetPalette/SetClipper) are kept: the game's original code
        // (create_zbuffer 0x00407020, create_device 0x00406D90, surface work
        // 0x0040F580/0x00412BD0/0x00414750, restore_surfaces) depends on the
        // real DirectDraw surface state, and the GPU backend adopts the real
        // surface size/format from the desc those calls fill.
        class GfxBackendD3D final : public GfxBackend
        {
        public:
            bool init() override
            {
                // Debug aid (OPENRE_D3D_DUMP=<N>): every N-th present, read the
                // reference render target back and write d3d_dump_<counter>.bmp.
                // Mirrors the GPU backend's OPENRE_GPU_DUMP so both backends
                // can be captured at the same present for a frame-aligned A/B.
                const char* dumpEnv = std::getenv("OPENRE_D3D_DUMP");
                mDumpInterval = dumpEnv != nullptr ? static_cast<DWORD>(std::atoi(dumpEnv)) : 0;
                if (mDumpInterval != 0)
                    logging::logInfo("[gfx:d3d] scene dump enabled (every {} frames)", mDumpInterval);
                return true;
            }

            void shutdown() override {}

            void present() override
            {
                if (mDumpInterval == 0 || mRenderTarget == nullptr)
                    return;
                if (++mDumpCounter % mDumpInterval != 0)
                    return;

                // Lock the render target through the real DirectDraw surface
                // (the reference renders into it via the original D3D2 device)
                // and write the pixels as a 32bpp top-down BMP.
                DDSURFACEDESC desc = {};
                desc.dwSize = sizeof(desc);
                const auto lockHr = lock(mRenderTarget, nullptr, &desc, DDLOCK_READONLY | DDLOCK_WAIT, nullptr);
                if (FAILED(lockHr) || desc.lpSurface == nullptr || desc.dwWidth == 0 || desc.dwHeight == 0)
                {
                    logging::logError(
                        "[gfx:d3d] dump lock failed: hr={:#010x} surface={} w={} h={}",
                        static_cast<unsigned long>(lockHr),
                        static_cast<void*>(mRenderTarget),
                        desc.dwWidth,
                        desc.dwHeight);
                    return;
                }
                // Diagnose the locked surface: format and a sample pixel so we
                // can tell whether the reference is rendering (a black dump in
                // GPU-active mode previously masked the real content).
                {
                    const auto* sampleRow = static_cast<const uint8_t*>(desc.lpSurface);
                    uint32_t sample = 0;
                    const auto sampleBpp = desc.ddpfPixelFormat.dwRGBBitCount;
                    if (sampleBpp <= 8)
                        sample = sampleRow[0];
                    else if (sampleBpp <= 16)
                        std::memcpy(&sample, sampleRow, 2);
                    else
                        std::memcpy(&sample, sampleRow, 4);
                    logging::logInfo(
                        "[gfx:d3d] dump surface: {}x{} bpp={} pitch={} sample(0,0)={:#010x}",
                        desc.dwWidth,
                        desc.dwHeight,
                        sampleBpp,
                        desc.lPitch,
                        sample);
                }

                const auto* src = static_cast<const uint8_t*>(desc.lpSurface);
                const auto pitch = static_cast<size_t>(desc.lPitch);
                const auto width = static_cast<uint32_t>(desc.dwWidth);
                const auto height = static_cast<uint32_t>(desc.dwHeight);
                const auto bytes = static_cast<size_t>(width) * height * 4;

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
                const uint32_t ppm = 2835;
                std::memcpy(bmp.data() + 38, &ppm, 4);
                std::memcpy(bmp.data() + 42, &ppm, 4);
                const uint32_t zero = 0;
                std::memcpy(bmp.data() + 46, &zero, 4);
                std::memcpy(bmp.data() + 50, &zero, 4);

                // Convert the surface's native format (16/24/32bpp RGB) to 32bpp
                // BGRA for the BMP, honoring the pixel format masks.
                const auto bpp = desc.ddpfPixelFormat.dwRGBBitCount;
                const auto maskR = desc.ddpfPixelFormat.dwRBitMask;
                const auto maskG = desc.ddpfPixelFormat.dwGBitMask;
                const auto maskB = desc.ddpfPixelFormat.dwBBitMask;
                for (uint32_t y = 0; y < height; y++)
                {
                    auto* dst = bmp.data() + pixelOffset + static_cast<size_t>(y) * static_cast<size_t>(width) * 4;
                    const auto* row = src + static_cast<size_t>(y) * pitch;
                    for (uint32_t x = 0; x < width; x++)
                    {
                        uint32_t px = 0;
                        if (bpp <= 8)
                        {
                            px = row[x];
                        }
                        else if (bpp <= 16)
                        {
                            uint16_t v;
                            std::memcpy(&v, row + static_cast<size_t>(x) * 2, 2);
                            px = v;
                        }
                        else if (bpp <= 24)
                        {
                            px = row[x * 3] | (row[x * 3 + 1] << 8) | (row[x * 3 + 2] << 16);
                        }
                        else
                        {
                            std::memcpy(&px, row + static_cast<size_t>(x) * 4, 4);
                        }
                        // Decode the largest channel to a byte by shifting right
                        // until it fits 8 bits (a 5-bit channel -> 8 bits).
                        auto expand = [](uint32_t v, uint32_t mask) -> uint8_t {
                            if (mask == 0)
                                return 0;
                            uint32_t m = mask;
                            uint32_t vv = v & mask;
                            while (m > 0xFF)
                            {
                                vv >>= 1;
                                m >>= 1;
                            }
                            return static_cast<uint8_t>(vv);
                        };
                        dst[x * 4 + 0] = expand(px, maskB);
                        dst[x * 4 + 1] = expand(px, maskG);
                        dst[x * 4 + 2] = expand(px, maskR);
                        dst[x * 4 + 3] = 0xFF;
                    }
                }

                unlock(mRenderTarget, nullptr);

                char path[64] = {};
                std::snprintf(path, sizeof(path), "d3d_dump_%05llu.bmp", static_cast<unsigned long long>(mDumpCounter));
                std::ofstream file(path, std::ios::binary);
                if (file)
                {
                    file.write(reinterpret_cast<const char*>(bmp.data()), static_cast<std::streamsize>(bmp.size()));
                    logging::logInfo("[gfx:d3d] scene dump written: {} ({}x{})", path, width, height);
                }
                else
                {
                    logging::logError("[gfx:d3d] scene dump write failed: {}", path);
                }
            }

            // ---- surface layer ----

            void create_surface(IUnknown* /*surface*/, const DDSURFACEDESC* desc) override
            {
                logging::logInfo(
                    "[gfx:d3d] CreateSurface {}x{} bpp={}", desc->dwWidth, desc->dwHeight, desc->ddpfPixelFormat.dwRGBBitCount);
            }

            void destroy_surface(IUnknown* surface) override
            {
                logging::logDebug("[gfx:d3d] DestroySurface {}", static_cast<void*>(surface));
            }

            HRESULT lock(IUnknown* surface, LPRECT rect, LPDDSURFACEDESC desc, DWORD flags, HANDLE event) override
            {
                if (const auto* e = registry::find(surface); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface*, LPRECT, LPDDSURFACEDESC, DWORD, HANDLE);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::SURF_Lock])(
                        reinterpret_cast<IDirectDrawSurface*>(surface), rect, desc, flags, event);
                }
                return E_UNEXPECTED;
            }

            HRESULT unlock(IUnknown* surface, void* lpRect) override
            {
                if (const auto* e = registry::find(surface); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface*, void*);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::SURF_Unlock])(
                        reinterpret_cast<IDirectDrawSurface*>(surface), lpRect);
                }
                return E_UNEXPECTED;
            }

            HRESULT blt(IUnknown* dst, LPRECT dstRect, IUnknown* src, LPRECT srcRect, DWORD flags, LPDDBLTFX fx) override
            {
                // The game's present (flip_blt) and 2D blits are per-frame
                // surface work; the GPU backend replays them on its own
                // textures, so the reference can skip them when disabled.
                if (!reference_enabled())
                    return S_OK;
                if (const auto* e = registry::find(dst); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(
                        IDirectDrawSurface*, LPRECT, LPDIRECTDRAWSURFACE, LPRECT, DWORD, LPDDBLTFX);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::SURF_Blt])(
                        reinterpret_cast<IDirectDrawSurface*>(dst),
                        dstRect,
                        reinterpret_cast<LPDIRECTDRAWSURFACE>(src),
                        srcRect,
                        flags,
                        fx);
                }
                return E_UNEXPECTED;
            }

            HRESULT get_surface_desc(IUnknown* surface, LPDDSURFACEDESC desc) override
            {
                if (const auto* e = registry::find(surface); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface*, LPDDSURFACEDESC);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::SURF_GetSurfaceDesc])(
                        reinterpret_cast<IDirectDrawSurface*>(surface), desc);
                }
                return E_UNEXPECTED;
            }

            HRESULT is_lost(IUnknown* surface) override
            {
                if (const auto* e = registry::find(surface); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface*);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::SURF_IsLost])(
                        reinterpret_cast<IDirectDrawSurface*>(surface));
                }
                return E_UNEXPECTED;
            }

            HRESULT restore(IUnknown* surface) override
            {
                if (const auto* e = registry::find(surface); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface*);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::SURF_Restore])(
                        reinterpret_cast<IDirectDrawSurface*>(surface));
                }
                return E_UNEXPECTED;
            }

            HRESULT add_attached_surface(IUnknown* surface, IUnknown* attached) override
            {
                if (const auto* e = registry::find(surface); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface*, LPDIRECTDRAWSURFACE);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::SURF_AddAttachedSurface])(
                        reinterpret_cast<IDirectDrawSurface*>(surface), reinterpret_cast<LPDIRECTDRAWSURFACE>(attached));
                }
                return E_UNEXPECTED;
            }

            HRESULT set_color_key(IUnknown* surface, DWORD flags, const DDCOLORKEY* key) override
            {
                if (const auto* e = registry::find(surface); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface*, DWORD, LPDDCOLORKEY);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::SURF_SetColorKey])(
                        reinterpret_cast<IDirectDrawSurface*>(surface), flags, const_cast<LPDDCOLORKEY>(key));
                }
                return E_UNEXPECTED;
            }

            HRESULT set_palette(IUnknown* surface, IUnknown* palette) override
            {
                if (const auto* e = registry::find(surface); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface*, LPDIRECTDRAWPALETTE);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::SURF_SetPalette])(
                        reinterpret_cast<IDirectDrawSurface*>(surface), reinterpret_cast<LPDIRECTDRAWPALETTE>(palette));
                }
                return E_UNEXPECTED;
            }

            HRESULT set_clipper(IUnknown* surface, IUnknown* clipper) override
            {
                if (const auto* e = registry::find(surface); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface*, LPDIRECTDRAWCLIPPER);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::SURF_SetClipper])(
                        reinterpret_cast<IDirectDrawSurface*>(surface), reinterpret_cast<LPDIRECTDRAWCLIPPER>(clipper));
                }
                return E_UNEXPECTED;
            }

            HRESULT get_dc(IUnknown* surface, HDC* hdc) override
            {
                // GDI text bridge (save screen). The GPU backend supplies the
                // HDC (a DIB over its surface shadow) whenever it is the active
                // presenter, so only create the real DirectDraw DC when the
                // reference owns the surface: creating it here while the GPU
                // backend overrides *hdc would leak a real DC that the game's
                // ReleaseDC (passing the DIB DC) can never release, and the
                // real surface would stay DC-locked.
                if (active_backend() == 1)
                    return S_OK;
                if (const auto* e = registry::find(surface); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface*, HDC*);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::SURF_GetDC])(
                        reinterpret_cast<IDirectDrawSurface*>(surface), hdc);
                }
                return E_UNEXPECTED;
            }

            HRESULT release_dc(IUnknown* surface, HDC hdc) override
            {
                // Mirrors get_dc: the game only hands back the DC we created,
                // so skip the real call while the GPU backend owns the HDC.
                if (active_backend() == 1)
                    return S_OK;
                if (const auto* e = registry::find(surface); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface*, HDC);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::SURF_ReleaseDC])(
                        reinterpret_cast<IDirectDrawSurface*>(surface), hdc);
                }
                return E_UNEXPECTED;
            }

            // ---- device / scene ----

            void create_device(IUnknown* /*device*/) override
            {
                if (!reference_enabled())
                    return;
                logging::logInfo("[gfx:d3d] CreateDevice");
            }

            HRESULT set_render_target(IUnknown* device, IUnknown* surface, DWORD flags) override
            {
                // Track the reference's render target so present() can dump it
                // (OPENRE_D3D_DUMP) at the same frame as the GPU scene dump.
                mRenderTarget = surface;
                if (!reference_enabled())
                    return S_OK;
                if (const auto* e = registry::find(device); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice2*, LPDIRECTDRAWSURFACE, DWORD);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::DEV_SetRenderTarget])(
                        reinterpret_cast<IDirect3DDevice2*>(device), reinterpret_cast<LPDIRECTDRAWSURFACE>(surface), flags);
                }
                return E_UNEXPECTED;
            }

            HRESULT set_current_viewport(IUnknown* device, IUnknown* viewport) override
            {
                if (!reference_enabled())
                    return S_OK;
                if (const auto* e = registry::find(device); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice2*, LPDIRECT3DVIEWPORT2);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::DEV_SetCurrentViewport])(
                        reinterpret_cast<IDirect3DDevice2*>(device), reinterpret_cast<LPDIRECT3DVIEWPORT2>(viewport));
                }
                return E_UNEXPECTED;
            }

            HRESULT set_viewport(IUnknown* viewport, const D3DVIEWPORT2* vp) override
            {
                if (!reference_enabled())
                    return S_OK;
                if (const auto* e = registry::find(viewport); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DViewport2*, LPD3DVIEWPORT2);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::VP_SetViewport2])(
                        reinterpret_cast<IDirect3DViewport2*>(viewport), const_cast<LPD3DVIEWPORT2>(vp));
                }
                return E_UNEXPECTED;
            }

            HRESULT set_background(IUnknown* viewport, D3DMATERIALHANDLE materialHandle) override
            {
                if (!reference_enabled())
                    return S_OK;
                if (const auto* e = registry::find(viewport); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DViewport2*, D3DMATERIALHANDLE);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::VP_SetBackground])(
                        reinterpret_cast<IDirect3DViewport2*>(viewport), materialHandle);
                }
                return E_UNEXPECTED;
            }

            HRESULT begin_scene(IUnknown* device) override
            {
                if (!reference_enabled())
                    return S_OK;
                if (const auto* e = registry::find(device); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice2*);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::DEV_BeginScene])(
                        reinterpret_cast<IDirect3DDevice2*>(device));
                }
                return E_UNEXPECTED;
            }

            HRESULT end_scene(IUnknown* device) override
            {
                if (!reference_enabled())
                    return S_OK;
                if (const auto* e = registry::find(device); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice2*);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::DEV_EndScene])(reinterpret_cast<IDirect3DDevice2*>(device));
                }
                return E_UNEXPECTED;
            }

            HRESULT set_render_state(IUnknown* device, D3DRENDERSTATETYPE state, DWORD value) override
            {
                if (!reference_enabled())
                    return S_OK;
                if (const auto* e = registry::find(device); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice2*, D3DRENDERSTATETYPE, DWORD);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::DEV_SetRenderState])(
                        reinterpret_cast<IDirect3DDevice2*>(device), state, value);
                }
                return E_UNEXPECTED;
            }

            HRESULT clear(IUnknown* viewport, DWORD count, const D3DRECT* rects, DWORD flags) override
            {
                if (!reference_enabled())
                    return S_OK;
                if (const auto* e = registry::find(viewport); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DViewport2*, DWORD, LPD3DRECT, DWORD);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::VP_Clear])(
                        reinterpret_cast<IDirect3DViewport2*>(viewport), count, const_cast<LPD3DRECT>(rects), flags);
                }
                return E_UNEXPECTED;
            }

            HRESULT draw_primitive(
                IUnknown* device, D3DPRIMITIVETYPE primType, D3DVERTEXTYPE vertexType, const void* vertices, DWORD vertexCount,
                DWORD flags) override
            {
                if (!reference_enabled())
                    return S_OK;
                if (const auto* e = registry::find(device); e != nullptr)
                {
                    using Fn
                        = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice2*, D3DPRIMITIVETYPE, D3DVERTEXTYPE, LPVOID, DWORD, DWORD);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::DEV_DrawPrimitive])(
                        reinterpret_cast<IDirect3DDevice2*>(device),
                        primType,
                        vertexType,
                        const_cast<LPVOID>(vertices),
                        vertexCount,
                        flags);
                }
                return E_UNEXPECTED;
            }

            HRESULT draw_indexed_primitive(
                IUnknown* device, D3DPRIMITIVETYPE primType, D3DVERTEXTYPE vertexType, const void* vertices, DWORD vertexCount,
                const void* indices, DWORD indexCount, DWORD flags) override
            {
                if (!reference_enabled())
                    return S_OK;
                if (const auto* e = registry::find(device); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(
                        IDirect3DDevice2*, D3DPRIMITIVETYPE, D3DVERTEXTYPE, LPVOID, DWORD, LPWORD, DWORD, DWORD);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::DEV_DrawIndexedPrimitive])(
                        reinterpret_cast<IDirect3DDevice2*>(device),
                        primType,
                        vertexType,
                        const_cast<LPVOID>(vertices),
                        vertexCount,
                        const_cast<LPWORD>(static_cast<const WORD*>(indices)),
                        indexCount,
                        flags);
                }
                return E_UNEXPECTED;
            }

            HRESULT set_transform(IUnknown* device, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix) override
            {
                if (!reference_enabled())
                    return S_OK;
                if (const auto* e = registry::find(device); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice2*, D3DTRANSFORMSTATETYPE, LPD3DMATRIX);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::DEV_SetTransform])(
                        reinterpret_cast<IDirect3DDevice2*>(device), state, const_cast<LPD3DMATRIX>(matrix));
                }
                return E_UNEXPECTED;
            }

            HRESULT multiply_transform(IUnknown* device, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix) override
            {
                if (!reference_enabled())
                    return S_OK;
                if (const auto* e = registry::find(device); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice2*, D3DTRANSFORMSTATETYPE, LPD3DMATRIX);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::DEV_MultiplyTransform])(
                        reinterpret_cast<IDirect3DDevice2*>(device), state, const_cast<LPD3DMATRIX>(matrix));
                }
                return E_UNEXPECTED;
            }

            HRESULT get_stats(IUnknown* device, D3DSTATS* stats) override
            {
                // When the reference is disabled the GPU backend fills the
                // counters itself; nothing in the game reads the device's other
                // stats fields.
                if (!reference_enabled())
                    return S_OK;
                if (const auto* e = registry::find(device); e != nullptr)
                {
                    using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice2*, LPD3DSTATS);
                    return reinterpret_cast<Fn>(e->origVtbl[slots::DEV_GetStats])(
                        reinterpret_cast<IDirect3DDevice2*>(device), stats);
                }
                return E_UNEXPECTED;
            }

        private:
            // Reference render target surface (surface0), tracked from
            // set_render_target so present() can dump it for A/B comparison.
            IUnknown* mRenderTarget = nullptr;
            // OPENRE_D3D_DUMP=<N>: dump the render target every N-th present.
            DWORD mDumpInterval = 0;
            DWORD mDumpCounter = 0;
        };
    }

    GfxBackend* backend_d3d()
    {
        static GfxBackendD3D backend;
        return &backend;
    }
}
