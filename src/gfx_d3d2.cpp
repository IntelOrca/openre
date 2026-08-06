#include "gfx_d3d2.h"
#include "gfx_backend.h"
#include "logger.h"

#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace openre::gfx
{
    namespace
    {
        std::unordered_map<void*, registry::Entry>& registryMap()
        {
            // The game is single-threaded, so no locking is needed.
            static std::unordered_map<void*, registry::Entry> map;
            return map;
        }

        // Maps IDirect3DTexture2 objects to the DirectDraw surface they were
        // obtained from via QueryInterface(IID_IDirect3DTexture2), so a later
        // GetHandle call can resolve the handle back to its owning surface.
        std::unordered_map<void*, void*>& textureToSurface()
        {
            static std::unordered_map<void*, void*> map;
            return map;
        }
    }

    namespace registry
    {
        const Entry* find(void* obj)
        {
            auto& map = registryMap();
            const auto it = map.find(obj);
            return it == map.end() ? nullptr : &it->second;
        }

        void set(void* obj, void** origVtbl, void** newVtbl)
        {
            registryMap()[obj] = Entry{ origVtbl, newVtbl };
        }

        void clear()
        {
            for (auto& entry : registryMap())
            {
                delete[] entry.second.newVtbl;
            }
            registryMap().clear();
        }
    }

    namespace
    {
        // ------------------------------------------------------------------
        // IDirectDraw hooks
        // ------------------------------------------------------------------

        static HRESULT STDMETHODCALLTYPE hook_ddraw_query_interface(IDirectDraw* self, REFIID riid, void** ppv)
        {
            const auto* e = registry::find(self);
            if (e == nullptr)
                return E_UNEXPECTED;
            using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirectDraw*, REFIID, void**);
            const auto hr = reinterpret_cast<Fn>(e->origVtbl[slots::DD_QueryInterface])(self, riid, ppv);
            if (SUCCEEDED(hr) && IsEqualGUID(riid, IID_IDirectDraw2))
                wrap_ddraw2(reinterpret_cast<IDirectDraw2*>(*ppv));
            return hr;
        }

        // The game creates a palette for every paletted surface
        // (DirectDrawSurface::CreateWork); wrap the palette and register it
        // with the GPU backend so its SetEntries can feed the 8bpp -> RGBA
        // expansion. Same slot in the IDirectDraw and IDirectDraw2 vtables.
        template<typename T>
        static HRESULT STDMETHODCALLTYPE
        hook_ddraw_create_palette(T* self, DWORD flags, LPPALETTEENTRY table, LPDIRECTDRAWPALETTE* palette, IUnknown* outer)
        {
            const auto* e = registry::find(self);
            if (e == nullptr)
                return E_UNEXPECTED;
            using Fn = HRESULT(STDMETHODCALLTYPE*)(T*, DWORD, LPPALETTEENTRY, LPDIRECTDRAWPALETTE*, IUnknown*);
            // ddraw.dll validates the object's vtable pointer (see
            // hook_ddraw_create_surface); restore the original for the call.
            auto** vpp = reinterpret_cast<void***>(self);
            auto* orig = *vpp;
            *vpp = e->origVtbl;
            const auto hr = reinterpret_cast<Fn>(e->origVtbl[slots::DD_CreatePalette])(self, flags, table, palette, outer);
            *vpp = orig;
            if (SUCCEEDED(hr) && palette != nullptr && *palette != nullptr)
            {
                backend_d3d()->create_palette(*palette, flags);
                backend_gpu()->create_palette(*palette, flags);
                wrap_palette(*palette);
            }
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE
        hook_ddraw_create_surface(IDirectDraw* self, LPDDSURFACEDESC desc, LPDIRECTDRAWSURFACE* surface, IUnknown* outer)
        {
            const auto* e = registry::find(self);
            if (e == nullptr)
                return E_UNEXPECTED;
            using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirectDraw*, LPDDSURFACEDESC, LPDIRECTDRAWSURFACE*, IUnknown*);
            // ddraw.dll's CreateSurface validates the object's vtable pointer and
            // creates a surface that later fails (e.g. GetSurfaceDesc returns
            // E_INVALIDARG) when a foreign (our wrapped) vtable is present. Swap
            // the original vtable back for the duration of the real call.
            auto** vpp = reinterpret_cast<void***>(self);
            auto* orig = *vpp;
            *vpp = e->origVtbl;
            const auto hr = reinterpret_cast<Fn>(e->origVtbl[slots::DD_CreateSurface])(self, desc, surface, outer);
            *vpp = orig;
            if (SUCCEEDED(hr) && surface != nullptr && *surface != nullptr)
            {
                backend_d3d()->create_surface(*surface, desc);
                backend_gpu()->create_surface(*surface, desc);
                wrap_surface(*surface);
            }
            return hr;
        }

        // ------------------------------------------------------------------
        // IDirectDraw2 hooks
        // ------------------------------------------------------------------

        static HRESULT STDMETHODCALLTYPE hook_ddraw2_query_interface(IDirectDraw2* self, REFIID riid, void** ppv)
        {
            const auto* e = registry::find(self);
            if (e == nullptr)
                return E_UNEXPECTED;
            using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirectDraw2*, REFIID, void**);
            const auto hr = reinterpret_cast<Fn>(e->origVtbl[slots::DD_QueryInterface])(self, riid, ppv);
            if (SUCCEEDED(hr))
            {
                if (IsEqualGUID(riid, IID_IDirect3D2))
                    wrap_d3d2(reinterpret_cast<IDirect3D2*>(*ppv));
                else if (IsEqualGUID(riid, IID_IDirectDraw))
                    wrap_ddraw(reinterpret_cast<IDirectDraw*>(*ppv));
            }
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE
        hook_ddraw2_create_surface(IDirectDraw2* self, LPDDSURFACEDESC desc, LPDIRECTDRAWSURFACE* surface, IUnknown* outer)
        {
            const auto* e = registry::find(self);
            if (e == nullptr)
                return E_UNEXPECTED;
            using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirectDraw2*, LPDDSURFACEDESC, LPDIRECTDRAWSURFACE*, IUnknown*);
            // See hook_ddraw_create_surface: ddraw.dll's CreateSurface validates
            // this->lpVtbl and misbehaves with our wrapped vtable, so restore the
            // original for the duration of the real call.
            auto** vpp = reinterpret_cast<void***>(self);
            auto* orig = *vpp;
            *vpp = e->origVtbl;
            const auto hr = reinterpret_cast<Fn>(e->origVtbl[slots::DD_CreateSurface])(self, desc, surface, outer);
            *vpp = orig;
            if (SUCCEEDED(hr) && surface != nullptr && *surface != nullptr)
            {
                backend_d3d()->create_surface(*surface, desc);
                backend_gpu()->create_surface(*surface, desc);
                wrap_surface(*surface);
            }
            return hr;
        }

        // ------------------------------------------------------------------
        // IDirectDrawSurface hooks
        // ------------------------------------------------------------------

        // NOTE: IDirectDrawSurface::Release is deliberately NOT hooked. ddraw.dll
        // overwrites the AddRef/Release slots (1 and 2) of every surface object's
        // vtable in-place after wrap_surface installs the replacement vtable, so
        // a Release hook installed there never fires (verified at runtime: the
        // other wrapped slots - QI, Lock, Blt, ... - resolve into openre.dll
        // while slot 2 points back into ddraw.dll for every surface). Surface
        // cleanup therefore relies on create_surface / create_device releasing
        // any previous entry for the same key or render-target role.

        static HRESULT STDMETHODCALLTYPE hook_surface_add_attached(IDirectDrawSurface* self, LPDIRECTDRAWSURFACE attached)
        {
            const auto hr = backend_d3d()->add_attached_surface(self, attached);
            backend_gpu()->add_attached_surface(self, attached);
            return hr;
        }

        // The game obtains IDirect3DTexture2 objects by QueryInterface-ing a
        // DirectDraw surface; remember the owning surface and wrap the texture
        // so its GetHandle can be broadcast to the backends.
        static HRESULT STDMETHODCALLTYPE hook_surface_query_interface(IDirectDrawSurface* self, REFIID riid, void** ppv)
        {
            const auto* e = registry::find(self);
            if (e == nullptr)
                return E_UNEXPECTED;
            using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface*, REFIID, void**);
            const auto hr = reinterpret_cast<Fn>(e->origVtbl[slots::SURF_QueryInterface])(self, riid, ppv);
            if (SUCCEEDED(hr) && ppv != nullptr && *ppv != nullptr && IsEqualGUID(riid, IID_IDirect3DTexture2))
            {
                auto* texture = reinterpret_cast<IDirect3DTexture2*>(*ppv);
                textureToSurface()[texture] = self;
                wrap_texture2(texture);
            }
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE hook_surface_blt(
            IDirectDrawSurface* self, LPRECT dstRect, LPDIRECTDRAWSURFACE src, LPRECT srcRect, DWORD flags, LPDDBLTFX fx)
        {
            const auto hr = backend_d3d()->blt(self, dstRect, src, srcRect, flags, fx);
            backend_gpu()->blt(self, dstRect, src, srcRect, flags, fx);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE hook_surface_get_surface_desc(IDirectDrawSurface* self, LPDDSURFACEDESC desc)
        {
            const auto hr = backend_d3d()->get_surface_desc(self, desc);
            backend_gpu()->get_surface_desc(self, desc);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE hook_surface_is_lost(IDirectDrawSurface* self)
        {
            const auto hr = backend_d3d()->is_lost(self);
            backend_gpu()->is_lost(self);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE
        hook_surface_lock(IDirectDrawSurface* self, LPRECT rect, LPDDSURFACEDESC desc, DWORD flags, HANDLE event)
        {
            const auto hr = backend_d3d()->lock(self, rect, desc, flags, event);
            backend_gpu()->lock(self, rect, desc, flags, event);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE hook_surface_restore(IDirectDrawSurface* self)
        {
            const auto hr = backend_d3d()->restore(self);
            backend_gpu()->restore(self);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE hook_surface_set_clipper(IDirectDrawSurface* self, LPDIRECTDRAWCLIPPER clipper)
        {
            const auto hr = backend_d3d()->set_clipper(self, clipper);
            backend_gpu()->set_clipper(self, clipper);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE hook_surface_set_color_key(IDirectDrawSurface* self, DWORD flags, LPDDCOLORKEY key)
        {
            const auto hr = backend_d3d()->set_color_key(self, flags, key);
            backend_gpu()->set_color_key(self, flags, key);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE hook_surface_set_palette(IDirectDrawSurface* self, LPDIRECTDRAWPALETTE palette)
        {
            const auto hr = backend_d3d()->set_palette(self, palette);
            backend_gpu()->set_palette(self, palette);
            return hr;
        }

        // ------------------------------------------------------------------
        // IDirectDrawPalette hooks
        // ------------------------------------------------------------------

        // The game fills a surface's palette through SetEntries (e.g.
        // MarniSurfaceX::vPalUnlock after a palette blit, MarniSurfaceX::vUnlock
        // after a paletted lock); forward the entries so the GPU backend can
        // expand 8bpp surfaces through their palette.
        static HRESULT STDMETHODCALLTYPE
        hook_palette_set_entries(IDirectDrawPalette* self, DWORD flags, DWORD base, DWORD count, LPPALETTEENTRY entries)
        {
            const auto* e = registry::find(self);
            if (e == nullptr)
                return E_UNEXPECTED;
            using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawPalette*, DWORD, DWORD, DWORD, LPPALETTEENTRY);
            const auto hr = reinterpret_cast<Fn>(e->origVtbl[slots::PAL_SetEntries])(self, flags, base, count, entries);
            if (SUCCEEDED(hr))
            {
                backend_gpu()->set_palette_entries(
                    reinterpret_cast<IUnknown*>(self), flags, base, count, static_cast<const PALETTEENTRY*>(entries));
            }
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE hook_surface_unlock(IDirectDrawSurface* self, void* lpRect)
        {
            const auto hr = backend_d3d()->unlock(self, lpRect);
            backend_gpu()->unlock(self, lpRect);
            return hr;
        }

        // ------------------------------------------------------------------
        // IDirect3D2 hooks
        // ------------------------------------------------------------------

        static HRESULT STDMETHODCALLTYPE hook_d3d2_enum_devices(IDirect3D2* self, LPVOID callback, LPVOID context)
        {
            const auto* e = registry::find(self);
            if (e == nullptr)
                return E_UNEXPECTED;
            using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D2*, LPVOID, LPVOID);
            return reinterpret_cast<Fn>(e->origVtbl[slots::D3D2_EnumDevices])(self, callback, context);
        }

        static HRESULT STDMETHODCALLTYPE
        hook_d3d2_create_material(IDirect3D2* self, LPDIRECT3DMATERIAL2* material, IUnknown* outer)
        {
            const auto* e = registry::find(self);
            if (e == nullptr)
                return E_UNEXPECTED;
            using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D2*, LPDIRECT3DMATERIAL2*, IUnknown*);
            const auto hr = reinterpret_cast<Fn>(e->origVtbl[slots::D3D2_CreateMaterial])(self, material, outer);
            if (SUCCEEDED(hr) && material != nullptr && *material != nullptr)
                wrap_material2(*material);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE
        hook_d3d2_create_viewport(IDirect3D2* self, LPDIRECT3DVIEWPORT2* viewport, IUnknown* outer)
        {
            const auto* e = registry::find(self);
            if (e == nullptr)
                return E_UNEXPECTED;
            using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D2*, LPDIRECT3DVIEWPORT2*, IUnknown*);
            const auto hr = reinterpret_cast<Fn>(e->origVtbl[slots::D3D2_CreateViewport])(self, viewport, outer);
            if (SUCCEEDED(hr) && viewport != nullptr && *viewport != nullptr)
                wrap_viewport2(*viewport);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE
        hook_d3d2_create_device(IDirect3D2* self, REFCLSID cls, LPDIRECTDRAWSURFACE surface, LPDIRECT3DDEVICE2* device)
        {
            const auto* e = registry::find(self);
            if (e == nullptr)
                return E_UNEXPECTED;
            using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D2*, REFCLSID, LPDIRECTDRAWSURFACE, LPDIRECT3DDEVICE2*);
            const auto hr = reinterpret_cast<Fn>(e->origVtbl[slots::D3D2_CreateDevice])(self, cls, surface, device);
            if (SUCCEEDED(hr) && device != nullptr && *device != nullptr)
            {
                backend_d3d()->create_device(*device);
                backend_gpu()->create_device(*device);
                // The game creates the device against the render target surface
                // (surface0) and never calls SetRenderTarget afterwards, so
                // broadcast the CreateDevice surface as the render target.
                backend_d3d()->set_render_target(*device, surface, 0);
                backend_gpu()->set_render_target(*device, surface, 0);
                wrap_device2(*device);
            }
            return hr;
        }

        // ------------------------------------------------------------------
        // IDirect3DDevice2 hooks
        // ------------------------------------------------------------------

        static HRESULT STDMETHODCALLTYPE hook_device_get_stats(IDirect3DDevice2* self, LPD3DSTATS stats)
        {
            const auto hr = backend_d3d()->get_stats(self, stats);
            backend_gpu()->get_stats(self, stats);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE hook_device_begin_scene(IDirect3DDevice2* self)
        {
            const auto hr = backend_d3d()->begin_scene(self);
            backend_gpu()->begin_scene(self);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE hook_device_end_scene(IDirect3DDevice2* self)
        {
            const auto hr = backend_d3d()->end_scene(self);
            backend_gpu()->end_scene(self);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE hook_device_set_current_viewport(IDirect3DDevice2* self, LPDIRECT3DVIEWPORT2 viewport)
        {
            const auto hr = backend_d3d()->set_current_viewport(self, viewport);
            backend_gpu()->set_current_viewport(self, viewport);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE
        hook_device_set_render_target(IDirect3DDevice2* self, LPDIRECTDRAWSURFACE surface, DWORD flags)
        {
            const auto hr = backend_d3d()->set_render_target(self, surface, flags);
            backend_gpu()->set_render_target(self, surface, flags);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE
        hook_device_set_render_state(IDirect3DDevice2* self, D3DRENDERSTATETYPE state, DWORD value)
        {
            const auto hr = backend_d3d()->set_render_state(self, state, value);
            backend_gpu()->set_render_state(self, state, value);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE
        hook_device_set_transform(IDirect3DDevice2* self, D3DTRANSFORMSTATETYPE state, LPD3DMATRIX matrix)
        {
            const auto hr = backend_d3d()->set_transform(self, state, matrix);
            backend_gpu()->set_transform(self, state, matrix);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE
        hook_device_multiply_transform(IDirect3DDevice2* self, D3DTRANSFORMSTATETYPE state, LPD3DMATRIX matrix)
        {
            const auto hr = backend_d3d()->multiply_transform(self, state, matrix);
            backend_gpu()->multiply_transform(self, state, matrix);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE hook_device_draw_primitive(
            IDirect3DDevice2* self, D3DPRIMITIVETYPE primType, D3DVERTEXTYPE vertexType, LPVOID vertices, DWORD vertexCount,
            DWORD flags)
        {
            const auto hr = backend_d3d()->draw_primitive(self, primType, vertexType, vertices, vertexCount, flags);
            backend_gpu()->draw_primitive(self, primType, vertexType, vertices, vertexCount, flags);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE hook_device_draw_indexed_primitive(
            IDirect3DDevice2* self, D3DPRIMITIVETYPE primType, D3DVERTEXTYPE vertexType, LPVOID vertices, DWORD vertexCount,
            LPWORD indices, DWORD indexCount, DWORD flags)
        {
            const auto hr = backend_d3d()->draw_indexed_primitive(
                self, primType, vertexType, vertices, vertexCount, indices, indexCount, flags);
            backend_gpu()->draw_indexed_primitive(
                self, primType, vertexType, vertices, vertexCount, indices, indexCount, flags);
            return hr;
        }

        // ------------------------------------------------------------------
        // IDirect3DViewport2 hooks
        // ------------------------------------------------------------------

        static HRESULT STDMETHODCALLTYPE hook_viewport_set_background(IDirect3DViewport2* self, D3DMATERIALHANDLE handle)
        {
            const auto hr = backend_d3d()->set_background(self, handle);
            backend_gpu()->set_background(self, handle);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE
        hook_viewport_clear(IDirect3DViewport2* self, DWORD count, LPD3DRECT rects, DWORD flags)
        {
            const auto hr = backend_d3d()->clear(self, count, rects, flags);
            backend_gpu()->clear(self, count, rects, flags);
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE hook_viewport_set_viewport2(IDirect3DViewport2* self, LPD3DVIEWPORT2 vp)
        {
            const auto hr = backend_d3d()->set_viewport(self, vp);
            backend_gpu()->set_viewport(self, vp);
            return hr;
        }

        // ------------------------------------------------------------------
        // IDirect3DMaterial2 / IDirect3DTexture2 hooks
        // ------------------------------------------------------------------

        // The background material's ambient color is the render target clear
        // color; forward SetMaterial so the GPU backend can track it.
        static HRESULT STDMETHODCALLTYPE hook_material_set_material(IDirect3DMaterial2* self, LPD3DMATERIAL material)
        {
            const auto* e = registry::find(self);
            if (e == nullptr)
                return E_UNEXPECTED;
            using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DMaterial2*, LPD3DMATERIAL);
            const auto hr = reinterpret_cast<Fn>(e->origVtbl[slots::MAT_SetMaterial])(self, material);
            backend_d3d()->set_material(material);
            backend_gpu()->set_material(material);
            return hr;
        }

        // GetHandle is broadcast so the GPU backend can resolve
        // TEXTUREHANDLE render state to a surface. (Release is not hooked for
        // IDirect3DTexture2 either: ddraw.dll repatches the AddRef/Release
        // slots of the underlying surface objects, and the game never releases
        // texture interfaces through a wrapped vtable.)
        static HRESULT STDMETHODCALLTYPE
        hook_texture_get_handle(IDirect3DTexture2* self, LPDIRECT3DDEVICE2 device, LPD3DTEXTUREHANDLE handle)
        {
            const auto* e = registry::find(self);
            if (e == nullptr)
                return E_UNEXPECTED;
            using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DTexture2*, LPDIRECT3DDEVICE2, LPD3DTEXTUREHANDLE);
            const auto hr = reinterpret_cast<Fn>(e->origVtbl[slots::TEX_GetHandle])(self, device, handle);
            if (SUCCEEDED(hr) && handle != nullptr)
            {
                const auto it = textureToSurface().find(self);
                backend_gpu()->create_texture_handle(
                    device, *handle, reinterpret_cast<IUnknown*>(it != textureToSurface().end() ? it->second : nullptr));
            }
            return hr;
        }

        // The game fills an internal surface and then calls
        // IDirect3DTexture2::Load(texture, srcTexture) to copy the pixels into
        // the texture's backing surface; the D3D driver does the copy, so the
        // GPU backend must replay it on its own textures.
        static HRESULT STDMETHODCALLTYPE hook_texture_load(IDirect3DTexture2* self, IDirect3DTexture2* srcTexture)
        {
            const auto* e = registry::find(self);
            if (e == nullptr)
                return E_UNEXPECTED;
            using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DTexture2*, IDirect3DTexture2*);
            const auto hr = reinterpret_cast<Fn>(e->origVtbl[slots::TEX_Load])(self, srcTexture);
            if (SUCCEEDED(hr))
            {
                const auto& map = textureToSurface();
                const auto it = map.find(self);
                const auto sit = map.find(srcTexture);
                if (it != map.end() && sit != map.end())
                {
                    backend_gpu()->texture_load(
                        reinterpret_cast<IUnknown*>(it->second), reinterpret_cast<IUnknown*>(sit->second));
                }
            }
            return hr;
        }
    }

    // ----------------------------------------------------------------------
    // Wrap entry points: allocate a new vtable, copy the original, override
    // the slots we intercept, then swap the object's vtable pointer in place.
    // ----------------------------------------------------------------------

    void wrap_ddraw(IDirectDraw* dd)
    {
        if (dd == nullptr)
            return;
        if (registry::find(dd) != nullptr)
            return; // already wrapped
        auto** orig = *reinterpret_cast<void***>(dd);
        auto* newVtbl = new void*[kDDrawVtblSlots];
        std::memcpy(newVtbl, orig, kDDrawVtblSlots * sizeof(void*));
        newVtbl[slots::DD_QueryInterface] = reinterpret_cast<void*>(&hook_ddraw_query_interface);
        newVtbl[slots::DD_CreatePalette] = reinterpret_cast<void*>(&hook_ddraw_create_palette<IDirectDraw>);
        newVtbl[slots::DD_CreateSurface] = reinterpret_cast<void*>(&hook_ddraw_create_surface);
        registry::set(dd, orig, newVtbl);
        *reinterpret_cast<void***>(dd) = newVtbl;
    }

    void wrap_ddraw2(IDirectDraw2* dd2)
    {
        if (dd2 == nullptr)
            return;
        if (registry::find(dd2) != nullptr)
            return; // already wrapped
        auto** orig = *reinterpret_cast<void***>(dd2);
        auto* newVtbl = new void*[kDDrawVtblSlots];
        std::memcpy(newVtbl, orig, kDDrawVtblSlots * sizeof(void*));
        newVtbl[slots::DD_QueryInterface] = reinterpret_cast<void*>(&hook_ddraw2_query_interface);
        newVtbl[slots::DD_CreatePalette] = reinterpret_cast<void*>(&hook_ddraw_create_palette<IDirectDraw2>);
        newVtbl[slots::DD_CreateSurface] = reinterpret_cast<void*>(&hook_ddraw2_create_surface);
        registry::set(dd2, orig, newVtbl);
        *reinterpret_cast<void***>(dd2) = newVtbl;
    }

    void wrap_surface(IDirectDrawSurface* surface)
    {
        if (surface == nullptr)
            return;
        if (registry::find(surface) != nullptr)
            return; // already wrapped
        auto** orig = *reinterpret_cast<void***>(surface);
        auto* newVtbl = new void*[kSurfaceVtblSlots];
        std::memcpy(newVtbl, orig, kSurfaceVtblSlots * sizeof(void*));
        newVtbl[slots::SURF_QueryInterface] = reinterpret_cast<void*>(&hook_surface_query_interface);
        newVtbl[slots::SURF_Blt] = reinterpret_cast<void*>(&hook_surface_blt);
        newVtbl[slots::SURF_Lock] = reinterpret_cast<void*>(&hook_surface_lock);
        newVtbl[slots::SURF_Unlock] = reinterpret_cast<void*>(&hook_surface_unlock);
        newVtbl[slots::SURF_GetSurfaceDesc] = reinterpret_cast<void*>(&hook_surface_get_surface_desc);
        newVtbl[slots::SURF_IsLost] = reinterpret_cast<void*>(&hook_surface_is_lost);
        newVtbl[slots::SURF_Restore] = reinterpret_cast<void*>(&hook_surface_restore);
        newVtbl[slots::SURF_AddAttachedSurface] = reinterpret_cast<void*>(&hook_surface_add_attached);
        newVtbl[slots::SURF_SetColorKey] = reinterpret_cast<void*>(&hook_surface_set_color_key);
        newVtbl[slots::SURF_SetPalette] = reinterpret_cast<void*>(&hook_surface_set_palette);
        newVtbl[slots::SURF_SetClipper] = reinterpret_cast<void*>(&hook_surface_set_clipper);
        registry::set(surface, orig, newVtbl);
        *reinterpret_cast<void***>(surface) = newVtbl;
    }

    void wrap_palette(IDirectDrawPalette* palette)
    {
        if (palette == nullptr)
            return;
        if (registry::find(palette) != nullptr)
            return; // already wrapped
        auto** orig = *reinterpret_cast<void***>(palette);
        auto* newVtbl = new void*[kPaletteVtblSlots];
        std::memcpy(newVtbl, orig, kPaletteVtblSlots * sizeof(void*));
        newVtbl[slots::PAL_SetEntries] = reinterpret_cast<void*>(&hook_palette_set_entries);
        registry::set(palette, orig, newVtbl);
        *reinterpret_cast<void***>(palette) = newVtbl;
    }

    void wrap_d3d2(IDirect3D2* d3d2)
    {
        if (d3d2 == nullptr)
            return;
        if (registry::find(d3d2) != nullptr)
            return; // already wrapped
        auto** orig = *reinterpret_cast<void***>(d3d2);
        auto* newVtbl = new void*[kD3D2VtblSlots];
        std::memcpy(newVtbl, orig, kD3D2VtblSlots * sizeof(void*));
        newVtbl[slots::D3D2_EnumDevices] = reinterpret_cast<void*>(&hook_d3d2_enum_devices);
        newVtbl[slots::D3D2_CreateMaterial] = reinterpret_cast<void*>(&hook_d3d2_create_material);
        newVtbl[slots::D3D2_CreateViewport] = reinterpret_cast<void*>(&hook_d3d2_create_viewport);
        newVtbl[slots::D3D2_CreateDevice] = reinterpret_cast<void*>(&hook_d3d2_create_device);
        registry::set(d3d2, orig, newVtbl);
        *reinterpret_cast<void***>(d3d2) = newVtbl;
    }

    void wrap_device2(IDirect3DDevice2* device)
    {
        if (device == nullptr)
            return;
        if (registry::find(device) != nullptr)
            return; // already wrapped
        auto** orig = *reinterpret_cast<void***>(device);
        auto* newVtbl = new void*[kDeviceVtblSlots];
        std::memcpy(newVtbl, orig, kDeviceVtblSlots * sizeof(void*));
        newVtbl[slots::DEV_GetStats] = reinterpret_cast<void*>(&hook_device_get_stats);
        newVtbl[slots::DEV_BeginScene] = reinterpret_cast<void*>(&hook_device_begin_scene);
        newVtbl[slots::DEV_EndScene] = reinterpret_cast<void*>(&hook_device_end_scene);
        newVtbl[slots::DEV_SetCurrentViewport] = reinterpret_cast<void*>(&hook_device_set_current_viewport);
        newVtbl[slots::DEV_SetRenderTarget] = reinterpret_cast<void*>(&hook_device_set_render_target);
        newVtbl[slots::DEV_SetRenderState] = reinterpret_cast<void*>(&hook_device_set_render_state);
        newVtbl[slots::DEV_SetTransform] = reinterpret_cast<void*>(&hook_device_set_transform);
        newVtbl[slots::DEV_MultiplyTransform] = reinterpret_cast<void*>(&hook_device_multiply_transform);
        newVtbl[slots::DEV_DrawPrimitive] = reinterpret_cast<void*>(&hook_device_draw_primitive);
        newVtbl[slots::DEV_DrawIndexedPrimitive] = reinterpret_cast<void*>(&hook_device_draw_indexed_primitive);
        registry::set(device, orig, newVtbl);
        *reinterpret_cast<void***>(device) = newVtbl;
    }

    void wrap_viewport2(IDirect3DViewport2* viewport)
    {
        if (viewport == nullptr)
            return;
        if (registry::find(viewport) != nullptr)
            return; // already wrapped
        auto** orig = *reinterpret_cast<void***>(viewport);
        auto* newVtbl = new void*[kViewportVtblSlots];
        std::memcpy(newVtbl, orig, kViewportVtblSlots * sizeof(void*));
        newVtbl[slots::VP_SetBackground] = reinterpret_cast<void*>(&hook_viewport_set_background);
        newVtbl[slots::VP_Clear] = reinterpret_cast<void*>(&hook_viewport_clear);
        newVtbl[slots::VP_SetViewport2] = reinterpret_cast<void*>(&hook_viewport_set_viewport2);
        registry::set(viewport, orig, newVtbl);
        *reinterpret_cast<void***>(viewport) = newVtbl;
    }

    void wrap_material2(IDirect3DMaterial2* material)
    {
        if (material == nullptr)
            return;
        if (registry::find(material) != nullptr)
            return; // already wrapped
        auto** orig = *reinterpret_cast<void***>(material);
        auto* newVtbl = new void*[kMaterialVtblSlots];
        std::memcpy(newVtbl, orig, kMaterialVtblSlots * sizeof(void*));
        newVtbl[slots::MAT_SetMaterial] = reinterpret_cast<void*>(&hook_material_set_material);
        registry::set(material, orig, newVtbl);
        *reinterpret_cast<void***>(material) = newVtbl;
    }

    void wrap_texture2(IDirect3DTexture2* texture)
    {
        if (texture == nullptr)
            return;
        if (registry::find(texture) != nullptr)
            return; // already wrapped
        auto** orig = *reinterpret_cast<void***>(texture);
        auto* newVtbl = new void*[kTextureVtblSlots];
        std::memcpy(newVtbl, orig, kTextureVtblSlots * sizeof(void*));
        newVtbl[slots::TEX_GetHandle] = reinterpret_cast<void*>(&hook_texture_get_handle);
        newVtbl[slots::TEX_Load] = reinterpret_cast<void*>(&hook_texture_load);
        registry::set(texture, orig, newVtbl);
        *reinterpret_cast<void***>(texture) = newVtbl;
    }

    // ----------------------------------------------------------------------
    // Module lifecycle
    // ----------------------------------------------------------------------

    namespace
    {
        int g_activeBackend = 0;
    }

    void set_active_backend(int index)
    {
        g_activeBackend = index;
    }

    int active_backend()
    {
        return g_activeBackend;
    }

    void init()
    {
        backend_d3d()->init();
        backend_gpu()->init();

        // Developer override for automated runs: OPENRE_GFX_BACKEND=1 starts on
        // the GPU backend without pressing F6. Default (unset) stays on the D3D
        // reference backend.
        if (const char* env = std::getenv("OPENRE_GFX_BACKEND"))
        {
            if (env[0] == '1')
                set_active_backend(1);
        }
        logging::logInfo("[gfx] backends initialised (active={})", active_backend());
    }

    void shutdown()
    {
        backend_d3d()->shutdown();
        backend_gpu()->shutdown();
        registry::clear();
    }

    void notify_present()
    {
        backend_d3d()->present();
        backend_gpu()->present();
    }
}
