#include "gfx_d3d2.h"
#include "gfx_backend.h"
#include "logger.h"

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

        // Maps versioned surface interfaces (the separate IDirectDrawSurface2/3/4
        // wrapper objects ddraw returns from QueryInterface) back to the base
        // IDirectDrawSurface object the game created with CreateSurface. The
        // GPU backends key their registries on the base surface pointer, so a
        // texture obtained through a versioned interface must be attributed to
        // its base surface.
        std::unordered_map<void*, void*>& versionedSurfaceToBase()
        {
            static std::unordered_map<void*, void*> map;
            return map;
        }

        // Follows the versionedSurfaceToBase chain to the canonical base
        // surface pointer; returns `surface` unchanged when it is not a
        // registered versioned wrapper.
        void* baseSurface(void* surface)
        {
            auto& map = versionedSurfaceToBase();
            const auto it = map.find(surface);
            return it == map.end() ? surface : baseSurface(it->second);
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

        void erase(void* obj)
        {
            auto& map = registryMap();
            const auto it = map.find(obj);
            if (it != map.end())
            {
                delete[] it->second.newVtbl;
                map.erase(it);
            }
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
                backend_gpu()->create_surface(*surface, desc);
                wrap_surface(*surface);
            }
            return hr;
        }

        // ------------------------------------------------------------------
        // IDirectDrawSurface hooks
        // ------------------------------------------------------------------

        // NOTE: IDirectDrawSurface::Release IS hooked. ddraw.dll's destruction
        // path validates the surface's vtable pointer while tearing down the
        // primary surface: with our wrapped (foreign) vtable installed it skips
        // releasing the primary/window association, and the next
        // CreateSurface(primary) fails with DDERR_PRIMARYSURFACEALREADYEXISTS.
        // That failure makes change_mode bail out with GPU_9 cleared, so every
        // clear/draw/flip early-returns and rendering freezes (window still
        // resizes) after a mode change (F8) or fullscreen toggle (ALT+ENTER).
        // The earlier comment claimed ddraw.dll overwrites the AddRef/Release
        // slots of the wrapped vtable in place so a Release hook never fires.
        // Verified at runtime that only the AddRef slot (1) is re-patched (it
        // stays ddraw's own AddRef either way); slot 2 remains ours and this
        // hook fires for every wrapped surface.

        static ULONG STDMETHODCALLTYPE hook_surface_release(IDirectDrawSurface* self)
        {
            const auto* e = registry::find(self);
            if (e == nullptr)
                return E_UNEXPECTED;
            using Fn = ULONG(STDMETHODCALLTYPE*)(IDirectDrawSurface*);
            // Restore the original vtable for the duration of the real call so
            // ddraw.dll's destruction path sees its own vtable (same pattern as
            // hook_ddraw_create_surface).
            auto** vpp = reinterpret_cast<void***>(self);
            auto* wrapped = *vpp;
            *vpp = e->origVtbl;
            const auto count = reinterpret_cast<Fn>(e->origVtbl[slots::SURF_Release])(self);
            if (count == 0)
            {
                // The surface was destroyed: drop the backends' registries so a
                // mode change does not leak GPU resources or keep a stale render
                // target. The object is gone, so leave its vtable as ddraw's
                // original and never touch it again.
                backend_gpu()->destroy_surface(self);
                registry::erase(self);

                // Versioned interface wrappers (IDirectDrawSurface2/3/4) die
                // with the surface. Drop their registry entries so a recycled
                // wrapper pointer is not mistaken for an already-wrapped object;
                // keep the newVtbl allocated because the wrapper object's lpVtbl
                // may still point at it if the game holds a live reference.
                auto& vmap = versionedSurfaceToBase();
                for (auto it = vmap.begin(); it != vmap.end();)
                {
                    if (it->second == self)
                    {
                        registryMap().erase(it->first);
                        it = vmap.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
            else
            {
                // The surface survived (still referenced); keep intercepting it.
                *vpp = wrapped;
            }
            return count;
        }

        static HRESULT STDMETHODCALLTYPE hook_surface_add_attached(IDirectDrawSurface* self, LPDIRECTDRAWSURFACE attached)
        {
            return backend_gpu()->add_attached_surface(self, attached);
        }

        // The game obtains IDirect3DTexture2 objects by QueryInterface-ing a
        // DirectDraw surface; remember the owning surface and wrap the texture
        // so its GetHandle can be broadcast to the backends. The game usually
        // holds IDirectDrawSurface2/3/4 interfaces (obtained by QI-ing the
        // wrapped surface) and QIs for the texture through THOSE, so every
        // versioned surface returned here also gets its QueryInterface slot
        // wrapped to keep the chain intercepted.
        static HRESULT STDMETHODCALLTYPE hook_surface_query_interface(IDirectDrawSurface* self, REFIID riid, void** ppv)
        {
            const auto* e = registry::find(self);
            if (e == nullptr)
                return E_UNEXPECTED;
            using Fn = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface*, REFIID, void**);
            const auto hr = reinterpret_cast<Fn>(e->origVtbl[slots::SURF_QueryInterface])(self, riid, ppv);
            if (FAILED(hr) || ppv == nullptr || *ppv == nullptr)
                return hr;
            if (IsEqualGUID(riid, IID_IDirect3DTexture2))
                wrap_texture_from_surface(reinterpret_cast<IDirect3DTexture2*>(*ppv), self);
            else if (
                IsEqualGUID(riid, IID_IDirectDrawSurface) || IsEqualGUID(riid, IID_IDirectDrawSurface2)
                || IsEqualGUID(riid, IID_IDirectDrawSurface3) || IsEqualGUID(riid, IID_IDirectDrawSurface4))
            {
                // ddraw returns a versioned surface interface. When it is a
                // distinct wrapper object (unwrapped vtable), patch only its
                // vtable slot 0 (QueryInterface, the first entry of every COM
                // vtable): the Blt/Lock/etc. slots live at different offsets
                // across the surface versions, so they must be left alone. A
                // later QI(IID_IDirect3DTexture2) through this interface is
                // then intercepted too. If ddraw shares the base surface's
                // pointer/vtable, it is already registered and we skip.
                void* surface = *ppv;
                if (registry::find(surface) == nullptr)
                {
                    auto** orig = *reinterpret_cast<void***>(surface);
                    auto* newVtbl = new void*[kSurfaceVtblSlots];
                    std::memcpy(newVtbl, orig, kSurfaceVtblSlots * sizeof(void*));
                    newVtbl[slots::SURF_QueryInterface] = reinterpret_cast<void*>(&hook_surface_query_interface);
                    registry::set(surface, orig, newVtbl);
                    *reinterpret_cast<void***>(surface) = newVtbl;
                    versionedSurfaceToBase()[surface] = baseSurface(self);
                }
            }
            return hr;
        }

        static HRESULT STDMETHODCALLTYPE hook_surface_blt(
            IDirectDrawSurface* self, LPRECT dstRect, LPDIRECTDRAWSURFACE src, LPRECT srcRect, DWORD flags, LPDDBLTFX fx)
        {
            return backend_gpu()->blt(self, dstRect, src, srcRect, flags, fx);
        }

        static HRESULT STDMETHODCALLTYPE hook_surface_get_surface_desc(IDirectDrawSurface* self, LPDDSURFACEDESC desc)
        {
            return backend_gpu()->get_surface_desc(self, desc);
        }

        static HRESULT STDMETHODCALLTYPE hook_surface_is_lost(IDirectDrawSurface* self)
        {
            return backend_gpu()->is_lost(self);
        }

        static HRESULT STDMETHODCALLTYPE
        hook_surface_lock(IDirectDrawSurface* self, LPRECT rect, LPDDSURFACEDESC desc, DWORD flags, HANDLE event)
        {
            return backend_gpu()->lock(self, rect, desc, flags, event);
        }

        static HRESULT STDMETHODCALLTYPE hook_surface_restore(IDirectDrawSurface* self)
        {
            return backend_gpu()->restore(self);
        }

        static HRESULT STDMETHODCALLTYPE hook_surface_set_clipper(IDirectDrawSurface* self, LPDIRECTDRAWCLIPPER clipper)
        {
            return backend_gpu()->set_clipper(self, clipper);
        }

        static HRESULT STDMETHODCALLTYPE hook_surface_set_color_key(IDirectDrawSurface* self, DWORD flags, LPDDCOLORKEY key)
        {
            return backend_gpu()->set_color_key(self, flags, key);
        }

        static HRESULT STDMETHODCALLTYPE hook_surface_set_palette(IDirectDrawSurface* self, LPDIRECTDRAWPALETTE palette)
        {
            return backend_gpu()->set_palette(self, palette);
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
            return backend_gpu()->unlock(self, lpRect);
        }

        // The game draws the save screen's text via GDI over an HDC obtained
        // from the surface (GetDC/ReleaseDC). The GPU backend supplies its own
        // DIB-backed HDC over the surface shadow (see gfx_backend_gpu.cpp
        // get_dc/release_dc).
        static HRESULT STDMETHODCALLTYPE hook_surface_get_dc(IDirectDrawSurface* self, HDC* hdc)
        {
            // Defensive: if the GPU backend skips (e.g. no device yet), never
            // hand the game back a garbage handle.
            if (hdc != nullptr)
                *hdc = nullptr;
            return backend_gpu()->get_dc(self, hdc);
        }

        static HRESULT STDMETHODCALLTYPE hook_surface_release_dc(IDirectDrawSurface* self, HDC hdc)
        {
            return backend_gpu()->release_dc(self, hdc);
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
            {
                // A previous material destroyed during a mode/resolution change
                // may have had its address reused by the allocator; the stale
                // registry entry would make wrap_material2 silently skip
                // re-hooking the new object. Start from a clean slate.
                registry::erase(*material);
                wrap_material2(*material);
            }
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
            {
                // See hook_d3d2_create_material: a destroyed viewport's address
                // may be reused by a new viewport on a mode/resolution change.
                registry::erase(*viewport);
                wrap_viewport2(*viewport);
            }
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
                // A resolution change destroys the old device and creates a new
                // one, and the heap may hand back the same address. The stale
                // registry entry (and its wrapped vtable) would make
                // wrap_device2 silently skip re-hooking the new device, so all
                // its draw/state calls would stop reaching the GPU backend
                // (permanent black screen). Erase it before re-wrapping.
                registry::erase(*device);
                backend_gpu()->create_device(*device);
                // The game creates the device against the render target surface
                // (surface0) and never calls SetRenderTarget afterwards, so
                // broadcast the CreateDevice surface as the render target.
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
            return backend_gpu()->get_stats(self, stats);
        }

        static HRESULT STDMETHODCALLTYPE hook_device_begin_scene(IDirect3DDevice2* self)
        {
            return backend_gpu()->begin_scene(self);
        }

        static HRESULT STDMETHODCALLTYPE hook_device_end_scene(IDirect3DDevice2* self)
        {
            return backend_gpu()->end_scene(self);
        }

        static HRESULT STDMETHODCALLTYPE hook_device_set_current_viewport(IDirect3DDevice2* self, LPDIRECT3DVIEWPORT2 viewport)
        {
            return backend_gpu()->set_current_viewport(self, viewport);
        }

        static HRESULT STDMETHODCALLTYPE
        hook_device_set_render_target(IDirect3DDevice2* self, LPDIRECTDRAWSURFACE surface, DWORD flags)
        {
            return backend_gpu()->set_render_target(self, surface, flags);
        }

        static HRESULT STDMETHODCALLTYPE
        hook_device_set_render_state(IDirect3DDevice2* self, D3DRENDERSTATETYPE state, DWORD value)
        {
            return backend_gpu()->set_render_state(self, state, value);
        }

        static HRESULT STDMETHODCALLTYPE
        hook_device_set_transform(IDirect3DDevice2* self, D3DTRANSFORMSTATETYPE state, LPD3DMATRIX matrix)
        {
            return backend_gpu()->set_transform(self, state, matrix);
        }

        static HRESULT STDMETHODCALLTYPE
        hook_device_multiply_transform(IDirect3DDevice2* self, D3DTRANSFORMSTATETYPE state, LPD3DMATRIX matrix)
        {
            return backend_gpu()->multiply_transform(self, state, matrix);
        }

        static HRESULT STDMETHODCALLTYPE hook_device_draw_primitive(
            IDirect3DDevice2* self, D3DPRIMITIVETYPE primType, D3DVERTEXTYPE vertexType, LPVOID vertices, DWORD vertexCount,
            DWORD flags)
        {
            return backend_gpu()->draw_primitive(self, primType, vertexType, vertices, vertexCount, flags);
        }

        static HRESULT STDMETHODCALLTYPE hook_device_draw_indexed_primitive(
            IDirect3DDevice2* self, D3DPRIMITIVETYPE primType, D3DVERTEXTYPE vertexType, LPVOID vertices, DWORD vertexCount,
            LPWORD indices, DWORD indexCount, DWORD flags)
        {
            return backend_gpu()->draw_indexed_primitive(
                self, primType, vertexType, vertices, vertexCount, indices, indexCount, flags);
        }

        // ------------------------------------------------------------------
        // IDirect3DViewport2 hooks
        // ------------------------------------------------------------------

        static HRESULT STDMETHODCALLTYPE hook_viewport_set_background(IDirect3DViewport2* self, D3DMATERIALHANDLE handle)
        {
            return backend_gpu()->set_background(self, handle);
        }

        static HRESULT STDMETHODCALLTYPE
        hook_viewport_clear(IDirect3DViewport2* self, DWORD count, LPD3DRECT rects, DWORD flags)
        {
            return backend_gpu()->clear(self, count, rects, flags);
        }

        static HRESULT STDMETHODCALLTYPE hook_viewport_set_viewport2(IDirect3DViewport2* self, LPD3DVIEWPORT2 vp)
        {
            return backend_gpu()->set_viewport(self, vp);
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
        newVtbl[slots::SURF_GetDC] = reinterpret_cast<void*>(&hook_surface_get_dc);
        newVtbl[slots::SURF_ReleaseDC] = reinterpret_cast<void*>(&hook_surface_release_dc);
        newVtbl[slots::SURF_Release] = reinterpret_cast<void*>(&hook_surface_release);
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

    void wrap_texture_from_surface(IDirect3DTexture2* texture, IDirectDrawSurface* surface)
    {
        if (texture == nullptr || surface == nullptr)
            return;
        textureToSurface()[texture] = baseSurface(surface);
        wrap_texture2(texture);
    }

    // ----------------------------------------------------------------------
    // Module lifecycle
    // ----------------------------------------------------------------------

    namespace
    {
        // True when backend_gpu()->init() succeeded, i.e. the GPU backend exists
        // and may present.
        bool g_gpuInitialized = false;
    }

    // The GPU backend is the one and only backend; the active backend is
    // always GPU.
    int active_backend()
    {
        return 1;
    }

    bool gpu_enabled()
    {
        return g_gpuInitialized;
    }

    void init()
    {
        g_gpuInitialized = backend_gpu()->init();
        if (g_gpuInitialized)
            logging::logInfo("[gfx] GPU backend initialised (active backend: gpu)");
        else
            logging::logError("[gfx] GPU backend failed to initialise - the game will not render");
    }

    void shutdown()
    {
        if (g_gpuInitialized)
            backend_gpu()->shutdown();
        registry::clear();
    }

    void notify_present()
    {
        backend_gpu()->present();
    }
}
