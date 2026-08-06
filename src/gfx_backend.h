#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d.h>
#include <ddraw.h>
#include <windows.h>

namespace openre::gfx
{
    // Abstract graphics backend. The COM front-end (gfx_d3d2.cpp) broadcasts
    // every render-path call to every registered backend.
    //
    // Every method mirrors a legacy COM method of the same name. Methods that
    // return HRESULT in the original interface return HRESULT here too: the
    // D3D reference backend performs the original call and hands its result
    // back to the front-end, which returns it to the game.
    class GfxBackend
    {
    public:
        virtual ~GfxBackend() = default;

        virtual bool init() = 0;
        virtual void shutdown() = 0;

        // ---- surface layer ----
        virtual void create_surface(IUnknown* surface, const DDSURFACEDESC* desc) = 0;
        virtual void destroy_surface(IUnknown* surface) = 0;
        virtual HRESULT lock(IUnknown* surface, LPRECT rect, LPDDSURFACEDESC desc, DWORD flags, HANDLE event) = 0;
        virtual HRESULT unlock(IUnknown* surface, void* lpRect) = 0;
        virtual HRESULT blt(IUnknown* dst, LPRECT dstRect, IUnknown* src, LPRECT srcRect, DWORD flags, LPDDBLTFX fx) = 0;
        virtual HRESULT get_surface_desc(IUnknown* surface, LPDDSURFACEDESC desc) = 0;
        virtual HRESULT is_lost(IUnknown* surface) = 0;
        virtual HRESULT restore(IUnknown* surface) = 0;
        virtual HRESULT add_attached_surface(IUnknown* surface, IUnknown* attached) = 0;
        virtual HRESULT set_color_key(IUnknown* surface, DWORD flags, const DDCOLORKEY* key) = 0;
        virtual HRESULT set_palette(IUnknown* surface, IUnknown* palette) = 0;
        virtual HRESULT set_clipper(IUnknown* surface, IUnknown* clipper) = 0;

        // ---- device / scene ----
        virtual void create_device(IUnknown* device) = 0;
        virtual HRESULT set_render_target(IUnknown* device, IUnknown* surface, DWORD flags) = 0;
        virtual HRESULT set_current_viewport(IUnknown* device, IUnknown* viewport) = 0;
        virtual HRESULT set_viewport(IUnknown* viewport, const D3DVIEWPORT2* vp) = 0;
        virtual HRESULT set_background(IUnknown* viewport, D3DMATERIALHANDLE materialHandle) = 0;
        virtual HRESULT begin_scene(IUnknown* device) = 0;
        virtual HRESULT end_scene(IUnknown* device) = 0;
        virtual HRESULT set_render_state(IUnknown* device, D3DRENDERSTATETYPE state, DWORD value) = 0;
        virtual HRESULT clear(IUnknown* viewport, DWORD count, const D3DRECT* rects, DWORD flags) = 0;
        virtual HRESULT draw_primitive(
            IUnknown* device, D3DPRIMITIVETYPE primType, D3DVERTEXTYPE vertexType, const void* vertices, DWORD vertexCount,
            DWORD flags) = 0;
        virtual HRESULT draw_indexed_primitive(
            IUnknown* device, D3DPRIMITIVETYPE primType, D3DVERTEXTYPE vertexType, const void* vertices, DWORD vertexCount,
            const void* indices, DWORD indexCount, DWORD flags) = 0;
        virtual HRESULT set_transform(IUnknown* device, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix) = 0;
        virtual HRESULT multiply_transform(IUnknown* device, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* matrix) = 0;
        virtual HRESULT get_stats(IUnknown* device, D3DSTATS* stats) = 0;

        // ---- present ----
        virtual void present() = 0;
    };

    GfxBackend* backend_d3d();
    GfxBackend* backend_gpu();

    // 0 = D3D reference, 1 = GPU. Hotkey wired in M2.
    void set_active_backend(int index);
    int active_backend();

    // Called from marni.cpp.
    void init();
    void shutdown();
    void wrap_ddraw(IDirectDraw* dd);
    void notify_present();

    namespace registry
    {
        struct Entry
        {
            void** origVtbl; // original vtable saved before the swap
            void** newVtbl;  // replacement vtable installed on the object
        };
        // Returns nullptr when obj is not wrapped.
        const Entry* find(void* obj);
        void set(void* obj, void** origVtbl, void** newVtbl);
        void clear();
    }
}
