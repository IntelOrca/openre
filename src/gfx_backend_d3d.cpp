#include "gfx_backend.h"
#include "gfx_d3d2.h"
#include "logger.h"

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
                return true;
            }

            void shutdown() override {}

            void present() override {}

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

            // ---- device / scene ----

            void create_device(IUnknown* /*device*/) override
            {
                if (!reference_enabled())
                    return;
                logging::logInfo("[gfx:d3d] CreateDevice");
            }

            HRESULT set_render_target(IUnknown* device, IUnknown* surface, DWORD flags) override
            {
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
        };
    }

    GfxBackend* backend_d3d()
    {
        static GfxBackendD3D backend;
        return &backend;
    }
}
