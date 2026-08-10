#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d.h>
#include <ddraw.h>
#include <windows.h>

namespace openre::gfx
{
    // The single graphics backend (SDL_GPU). The COM front-end (marni_ddraw.cpp)
    // intercepts every render-path call and forwards it to this backend.
    //
    // Every method mirrors a legacy COM method of the same name. Methods that
    // return HRESULT in the original interface return HRESULT here too: the
    // backend's result is handed back to the front-end, which returns it to
    // the game.
    class GfxBackend
    {
    public:
        virtual ~GfxBackend() = default;

        // The SDL_GPU device and window are owned by system_gpu (system_gpu.cpp);
        // it hands them to the backend once created (eagerly, from marni::init,
        // so the ddraw surfaces created by init_all register against the device).
        // The backend itself never creates or destroys the device.
        virtual void attach_device(void* device, void* window) = 0;
        // The guest framebuffer (the offscreen render target the scene pass
        // renders into) is owned by system_gpu; it hands it over here whenever
        // it is created or re-created (render-resolution change).
        virtual void set_guest_framebuffer(void* texture, int width, int height) = 0;
        virtual void shutdown() = 0;

        // ---- movie overlay (Phase 7) ----
        // The movie player captures decoded DirectShow frames (top-down RGB24,
        // `pitch` bytes per row) and the backend composites the latest one into
        // the guest framebuffer during present(), after the scene pass and the
        // GDI text overlay - so cutscenes render into the framebuffer with no
        // separate video window. The backend copies the pixels immediately.
        // Passing nullptr (or width/height 0) clears the movie overlay. No-op
        // by default (non-GPU backends have nothing to composite).
        virtual void set_movie_frame(const void* /*pixels*/, int /*width*/, int /*height*/, int /*pitch*/) {}

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
        // The game obtains IDirect3DTexture2 objects by QueryInterface-ing a
        // surface; the front-end (marni_ddraw.cpp) performs the real QI and wraps
        // the returned texture (so its GetHandle/Load reach the backend). The
        // backend itself has no interface to hand out.
        virtual HRESULT query_texture_interface(IUnknown* surface, LPVOID* outTexture) = 0;

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

    GfxBackend* backend_gpu();

    // ---------------------------------------------------------------------
    // Real DirectDraw forwards
    // ---------------------------------------------------------------------
    // The game's original code (create_device 0x00406D90, create_zbuffer
    // 0x00407020, surface work 0x0040F580/0x00412BD0/0x00414750,
    // restore_surfaces) depends on the REAL DirectDraw surface state: the
    // original D3D2 CreateDevice validates the render target surface and its
    // attached z-buffer, so AddAttachedSurface must reach the real ddraw
    // surface. These forwards dispatch the surface-layer operation to the real
    // ddraw surface method (via the wrap registry, using the live vtable when
    // ddraw replaced the one we installed). Per-frame ops (Blt, GetDC,
    // ReleaseDC) are intentionally NOT forwarded: the GPU backend replays them
    // on its own textures and supplies its own DIB-backed HDC, matching the
    // base behaviour while the GPU backend is active.
    HRESULT surface_forward_add_attached_surface(IUnknown* surface, IUnknown* attached);
    HRESULT surface_forward_get_surface_desc(IUnknown* surface, LPDDSURFACEDESC desc);
    HRESULT surface_forward_set_clipper(IUnknown* surface, IUnknown* clipper);
    HRESULT surface_forward_set_palette(IUnknown* surface, IUnknown* palette);
    HRESULT surface_forward_set_color_key(IUnknown* surface, DWORD flags, const DDCOLORKEY* key);
    HRESULT surface_forward_lock(IUnknown* surface, LPRECT rect, LPDDSURFACEDESC desc, DWORD flags, HANDLE event);
    HRESULT surface_forward_unlock(IUnknown* surface, void* lpRect);
    HRESULT surface_forward_is_lost(IUnknown* surface);
    HRESULT surface_forward_restore(IUnknown* surface);
    HRESULT surface_forward_query_texture_interface(IUnknown* surface, LPVOID* outTexture);

    // ---------------------------------------------------------------------
    // Forwarding helpers for the decompiled render path
    // ---------------------------------------------------------------------
    // Decompiled code used to reach the backends through the wrapped COM
    // device vtable (marni_ddraw.cpp). These helpers forward a call to the
    // backend directly, so decompiled code no longer needs the COM front-end
    // for the hot render path.

    inline HRESULT device_set_current_viewport(IUnknown* device, IUnknown* viewport)
    {
        return backend_gpu()->set_current_viewport(device, viewport);
    }

    inline HRESULT device_set_render_state(IUnknown* device, D3DRENDERSTATETYPE state, DWORD value)
    {
        return backend_gpu()->set_render_state(device, state, value);
    }

    inline HRESULT device_draw_primitive(
        IUnknown* device, D3DPRIMITIVETYPE primType, D3DVERTEXTYPE vertexType, const void* vertices, DWORD vertexCount,
        DWORD flags)
    {
        return backend_gpu()->draw_primitive(device, primType, vertexType, vertices, vertexCount, flags);
    }

    inline HRESULT device_begin_scene(IUnknown* device)
    {
        return backend_gpu()->begin_scene(device);
    }

    inline HRESULT device_end_scene(IUnknown* device)
    {
        return backend_gpu()->end_scene(device);
    }

    inline HRESULT device_get_stats(IUnknown* device, D3DSTATS* stats)
    {
        return backend_gpu()->get_stats(device, stats);
    }

    inline HRESULT viewport_set_viewport2(IUnknown* viewport, const D3DVIEWPORT2* vp)
    {
        return backend_gpu()->set_viewport(viewport, vp);
    }

    inline HRESULT viewport_clear(IUnknown* viewport, DWORD count, const D3DRECT* rects, DWORD flags)
    {
        return backend_gpu()->clear(viewport, count, rects, flags);
    }

    inline HRESULT viewport_set_background(IUnknown* viewport, D3DMATERIALHANDLE materialHandle)
    {
        return backend_gpu()->set_background(viewport, materialHandle);
    }

    inline HRESULT surface_is_lost(IUnknown* surface)
    {
        const auto hr = surface_forward_is_lost(surface);
        backend_gpu()->is_lost(surface);
        return hr;
    }

    inline HRESULT surface_restore(IUnknown* surface)
    {
        const auto hr = surface_forward_restore(surface);
        backend_gpu()->restore(surface);
        return hr;
    }

    inline HRESULT surface_blt(IUnknown* dst, LPRECT dstRect, IUnknown* src, LPRECT srcRect, DWORD flags, LPDDBLTFX fx)
    {
        return backend_gpu()->blt(dst, dstRect, src, srcRect, flags, fx);
    }

    inline HRESULT surface_get_surface_desc(IUnknown* surface, LPDDSURFACEDESC desc)
    {
        const auto hr = surface_forward_get_surface_desc(surface, desc);
        backend_gpu()->get_surface_desc(surface, desc);
        return hr;
    }

    inline HRESULT surface_add_attached_surface(IUnknown* surface, IUnknown* attached)
    {
        const auto hr = surface_forward_add_attached_surface(surface, attached);
        backend_gpu()->add_attached_surface(surface, attached);
        return hr;
    }

    inline HRESULT surface_set_color_key(IUnknown* surface, DWORD flags, const DDCOLORKEY* key)
    {
        const auto hr = surface_forward_set_color_key(surface, flags, key);
        backend_gpu()->set_color_key(surface, flags, key);
        return hr;
    }

    inline HRESULT surface_set_palette(IUnknown* surface, IUnknown* palette)
    {
        const auto hr = surface_forward_set_palette(surface, palette);
        backend_gpu()->set_palette(surface, palette);
        return hr;
    }

    inline HRESULT surface_set_clipper(IUnknown* surface, IUnknown* clipper)
    {
        const auto hr = surface_forward_set_clipper(surface, clipper);
        backend_gpu()->set_clipper(surface, clipper);
        return hr;
    }

    inline HRESULT surface_lock(IUnknown* surface, LPRECT rect, LPDDSURFACEDESC desc, DWORD flags, HANDLE event)
    {
        const auto hr = surface_forward_lock(surface, rect, desc, flags, event);
        backend_gpu()->lock(surface, rect, desc, flags, event);
        return hr;
    }

    inline HRESULT surface_unlock(IUnknown* surface, void* lpRect)
    {
        const auto hr = surface_forward_unlock(surface, lpRect);
        backend_gpu()->unlock(surface, lpRect);
        return hr;
    }

    inline HRESULT surface_query_texture_interface(IUnknown* surface, LPVOID* outTexture)
    {
        const auto hr = surface_forward_query_texture_interface(surface, outTexture);
        backend_gpu()->query_texture_interface(surface, outTexture);
        return hr;
    }

    // The GPU backend is the one and only backend; the active backend is
    // always GPU.
    int active_backend();

    // True when the GPU device exists (system_gpu owns it; created lazily on
    // the first begin()/present()).
    bool gpu_enabled();

    // Called from marni.cpp (kill only; init no longer creates the device -
    // that is system_gpu's lazy job).
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
