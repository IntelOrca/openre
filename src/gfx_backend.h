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
        virtual HRESULT get_dc(IUnknown* surface, HDC* hdc) = 0;
        virtual HRESULT release_dc(IUnknown* surface, HDC hdc) = 0;

        // ---- device / scene ----
        virtual void create_device(IUnknown* device) = 0;
        virtual HRESULT set_render_target(IUnknown* device, IUnknown* surface, DWORD flags) = 0;
        virtual HRESULT set_current_viewport(IUnknown* device, IUnknown* viewport) = 0;
        virtual HRESULT set_viewport(IUnknown* viewport, const D3DVIEWPORT2* vp) = 0;
        virtual HRESULT set_background(IUnknown* viewport, D3DMATERIALHANDLE materialHandle) = 0;

        // Optional notifications with a no-op default: the game SetMaterial's
        // the background material (its ambient color drives the target clear
        // color) and asks textures for D3D handles via GetHandle (the handle
        // -> surface mapping lets the GPU backend resolve TEXTUREHANDLE).
        virtual void set_material(const D3DMATERIAL* /*material*/) {}
        virtual void create_texture_handle(IUnknown* /*device*/, DWORD /*handle*/, IUnknown* /*surface*/) {}
        // IDirect3DTexture2::Load(dst, src): the D3D driver copies the pixels
        // between the two surfaces' backing textures; the GPU backend replays
        // that copy on its own textures so handle-bound textures get content.
        virtual void texture_load(IUnknown* /*surface*/, IUnknown* /*srcSurface*/) {}
        // Palette observation hooks: the game creates palettes via
        // IDirectDraw::CreatePalette, attaches them with SetPalette and fills
        // them with SetEntries. The GPU backend expands paletted (8bpp)
        // surfaces through their palette, so it needs these notifications.
        virtual void create_palette(IUnknown* /*palette*/, DWORD /*flags*/) {}
        virtual HRESULT set_palette_entries(
            IUnknown* /*palette*/, DWORD /*flags*/, DWORD /*base*/, DWORD /*count*/, const PALETTEENTRY* /*entries*/)
        {
            return S_OK;
        }
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

    // ---------------------------------------------------------------------
    // Broadcast helpers for the decompiled render path
    // ---------------------------------------------------------------------
    // Decompiled code used to reach the backends through the wrapped COM
    // device vtable (gfx_d3d2.cpp). These helpers forward a call to every
    // registered backend directly, so decompiled code no longer needs the
    // COM front-end for the hot render path.

    inline HRESULT device_set_current_viewport(IUnknown* device, IUnknown* viewport)
    {
        const auto hr = backend_d3d()->set_current_viewport(device, viewport);
        backend_gpu()->set_current_viewport(device, viewport);
        return hr;
    }

    inline HRESULT device_set_render_state(IUnknown* device, D3DRENDERSTATETYPE state, DWORD value)
    {
        const auto hr = backend_d3d()->set_render_state(device, state, value);
        backend_gpu()->set_render_state(device, state, value);
        return hr;
    }

    inline HRESULT device_draw_primitive(
        IUnknown* device, D3DPRIMITIVETYPE primType, D3DVERTEXTYPE vertexType, const void* vertices, DWORD vertexCount,
        DWORD flags)
    {
        const auto hr = backend_d3d()->draw_primitive(device, primType, vertexType, vertices, vertexCount, flags);
        backend_gpu()->draw_primitive(device, primType, vertexType, vertices, vertexCount, flags);
        return hr;
    }

    inline HRESULT device_begin_scene(IUnknown* device)
    {
        const auto hr = backend_d3d()->begin_scene(device);
        backend_gpu()->begin_scene(device);
        return hr;
    }

    inline HRESULT device_end_scene(IUnknown* device)
    {
        const auto hr = backend_d3d()->end_scene(device);
        backend_gpu()->end_scene(device);
        return hr;
    }

    inline HRESULT device_get_stats(IUnknown* device, D3DSTATS* stats)
    {
        const auto hr = backend_d3d()->get_stats(device, stats);
        backend_gpu()->get_stats(device, stats);
        return hr;
    }

    inline HRESULT viewport_set_viewport2(IUnknown* viewport, const D3DVIEWPORT2* vp)
    {
        const auto hr = backend_d3d()->set_viewport(viewport, vp);
        backend_gpu()->set_viewport(viewport, vp);
        return hr;
    }

    inline HRESULT viewport_clear(IUnknown* viewport, DWORD count, const D3DRECT* rects, DWORD flags)
    {
        const auto hr = backend_d3d()->clear(viewport, count, rects, flags);
        backend_gpu()->clear(viewport, count, rects, flags);
        return hr;
    }

    inline HRESULT viewport_set_background(IUnknown* viewport, D3DMATERIALHANDLE materialHandle)
    {
        const auto hr = backend_d3d()->set_background(viewport, materialHandle);
        backend_gpu()->set_background(viewport, materialHandle);
        return hr;
    }

    // 0 = D3D reference, 1 = GPU. Hotkey wired in M2.
    void set_active_backend(int index);
    int active_backend();

    // True while the D3D reference backend must keep forwarding calls to the
    // real DirectDraw/D3D2 objects. The persistent [video] disable_d3d_reference
    // config flag turns it off while the GPU backend is active, so the D3D
    // reference does no per-frame draw work (the front-end hooks still answer
    // every COM call; only the reference's forwarding is skipped).
    bool reference_enabled();

    // True when the GPU backend initialised successfully and can present.
    bool gpu_enabled();

    // True when a live F6 backend toggle is available (mode "both" and the GPU
    // backend is present). In "d3d"/"gpu" single-backend modes F6 is ignored.
    bool backend_toggle_enabled();

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
