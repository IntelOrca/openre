#include "marni.h"
#include "interop.hpp"
#include "logger.h"
#include "marni_draw.h"
#include "marni_movie.h"
#include "marni_renderer.h"
#include "openre.h"
#include "re2.h"
#include "str.h"
#include "system_config.h"
#include "system_filesystem.h"
#include "system_gpu.h"
#include "system_window.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace openre::marni
{
    // Message codes used by the SDL3 -> marni window-message bridge
    // (process_messages). Values match the Win32 WM_* codes so the guest ABI
    // (message is in the Marni vtbl) stays intact. Guarded so the real
    // <windows.h> macros win on _WIN32.
#ifndef WM_MOVE
    constexpr uint32_t WM_MOVE = 0x0003;
#endif
#ifndef WM_SIZE
    constexpr uint32_t WM_SIZE = 0x0005;
#endif
#ifndef WM_DESTROY
    constexpr uint32_t WM_DESTROY = 0x0002;
#endif
#ifndef WM_SYSKEYDOWN
    constexpr uint32_t WM_SYSKEYDOWN = 0x0104;
#endif

    struct DrawInfo;

    static int error_routine(int errorCode);

    static int __stdcall create_device(Marni* self);
    static int __stdcall enum_drivers(Marni* self);
    static int __stdcall create_gpu(Marni* self);
    static int __stdcall surface2_vfill(MarniSurface2* self, LPRECT pSrcRect, uint32_t color, int mode);
    static int adjust_rect(RECT* clip, const RECT* src, RECT* out);
    static int __stdcall surfacex_vfill(MarniSurfaceX* self, LPRECT pRect, uint32_t color, int mode);
    int __stdcall surface2_create_work(MarniSurface2* self, int width, int height, int depth, int palBpp, int palCnt);
    static int __stdcall surface_set_index_color(int x, int y, uint32_t color, int mode);
    int __stdcall surface2_vrelease(MarniSurface2* self);
    static int surface_get_palette_color(MarniSurface2* self, int col_index, int pal_index, uint32_t* color_out);
    static int surface_set_palette_color(MarniSurface2* self, int col_index, int pal_index, uint32_t rgb, int mode);
    static int surface_apply_hue(MarniSurface2* self, int col_index, uint32_t rgb, int mode);
    static int __stdcall surface_operator_eq(MarniSurface* self, MarniSurface* pSrc);
    static int surface_get_index_color(MarniSurface2* self, int x, int y, uint32_t* color_out);
    static int surface_get_color(MarniSurface2* self, int x, int y, uint32_t* color_out);
    static int surface_get_current_color(MarniSurface2* self, int x, int y, uint32_t* color_out);
    static int surface_set_current_color(MarniSurface2* self, int x, int y, uint32_t color, int mode);
    int __stdcall surface2_blt(MarniSurface2* self, RECT* pDstRect, RECT* pSrcRect, MarniSurface2* pSrc, int a5, int a6);
    static void __stdcall destroy(Marni* marni);
    static int __stdcall do_draw_op(Marni* self, int index);
    static void __stdcall do_render(Marni* self, MarniOt* pOt);
    static int __stdcall init_all(Marni* self);
    static int __stdcall clear_buffers(Marni* self);
    static void __stdcall move(Marni* marni);
    static void __stdcall polygon_object_dtor(PolygonObject* self);
    static MarniOt* __stdcall ot_ctor(MarniOt* self, size_t a2, int a3);
    static Prim* __stdcall ot_get_primitive(MarniOt* self);
    static int __stdcall ot_add_primitive_as_z(MarniOt* self, Prim* pPrim, int z);
    static int __stdcall ot_clear(MarniOt* self);
    static int __stdcall ot_alloc(MarniOt* self, int depth, int a3);
    static void __stdcall ot_dtor(MarniOt* self);
    static int __stdcall resize(Marni* marni, uintptr_t wParam);
    static uint16_t __stdcall search_texture_object_0_from_1(Marni* self, int handle, int index);
    static void set_filtering(Marni* self, uint8_t a2);
    static void __stdcall sub_40E800(Marni* self, uint8_t a2);
    static int invalidate_window(HWND hWnd, int width, int height, int fullscreen, LPRECT lpResRect);
    static uint8_t __stdcall sub_416670(MarniOt* pOt);
    static MarniTextureNode* __stdcall search_texture_object_0_from_1_in_condition(Marni* self, int handle, int index);
    static void __stdcall texture_surface_release(Marni* self, int handle);
    static int tex_spr(
        MarniSurface2* dst, MarniSurface2* src, int dstLeft, int dstTop, int dstRight, int dstBottom, int srcU0, int srcV0,
        int srcU1, int srcV1, int clipLeft, int clipTop, int clipRight, int clipBottom, int color, int flags);
    static void __stdcall trans_priority_list(Marni* self, MarniOt* pOt);
    static int __stdcall trans_spr_poly(Marni* self, MarniOt* pOt, PrimSprite* pPrim);
    static std::string __stdcall generate_res_string(const MarniRes* self);
    static int __stdcall change_mode(Marni* self, uint32_t width, uint32_t height, uint32_t depth);
    static int __stdcall reload_texture(Marni* self, int texture);
    static bool __stdcall change_display_mode(Marni* self, int mode);
    static void __stdcall surface3_dtor(MarniSurface3* self);
    static void __stdcall surfacex_dtor(MarniSurfaceX* self);
    static MarniSurfaceX* __stdcall surfacex_ctor(MarniSurfaceX* self);
    static int __stdcall get_z_buffer_caps(Marni* self);
    static int __stdcall surface_pal_blt(MarniSurface2* self, MarniSurface2* pSrc, int paletteSrc, int paletteDst);
    static int __stdcall surfacex_create_texture_object(MarniSurfaceX* self);
    static int __stdcall surfacex_load(MarniSurfaceX* self, MarniSurfaceX* pSrc);
    static void __stdcall surfacex_vrelease(MarniSurfaceX* self);
    static void surfacex_create_surface(MarniSurfaceX* self);
    static int surface_get_alpha_bits(MarniSurfaceX* self);
    char* surface_calc_address(MarniSurface* self, int x, int y);
    static int surface_set_color(MarniSurface2* self, int x, int y, uint32_t color, int alpha);
    static int __stdcall surfacex_vpalunlock(MarniSurfaceX* self);
    static int __stdcall sub_416B90(Marni* self, int a2);

    // 0x0050D905
    void* cstd_malloc(size_t len)
    {
        return malloc(len);
    }

    // 0x0050D89C
    void cstd_free(void* mem)
    {
        free(mem);
    }

    // 0x0050CC9E
    void __stdcall cstd_vector_dtor(void* elements, size_t elementSize, size_t count, void* cb)
    {
        interop::stdcall<void, void*, size_t, size_t, void*>(0x0050CC9E, elements, elementSize, count, cb);
    }

    // 0x0050CD7C
    void __stdcall cstd_vector_ctor(void* elements, size_t elementSize, size_t count, void* cb, void* cb2)
    {
        interop::stdcall<void, void*, size_t, size_t, void*, void*>(0x0050CD7C, elements, elementSize, count, cb, cb2);
    }

    static void surface_release(MarniSurface2* self)
    {
        interop::thiscall<int, MarniSurface2*>((uintptr_t)self->vtbl->release_fn, self);
    }

    static void surface_fill(MarniSurface* self, int r, int g, int b)
    {
        interop::thiscall<int, MarniSurface*, int, int, int>((uintptr_t)self->vtbl->fill, self, r, g, b);
    }

    static int surface_lock(MarniSurface2* self, int a2, int a3)
    {
        return interop::thiscall<int, MarniSurface2*, int, int>((uintptr_t)self->vtbl->lock_fn, self, a2, a3);
    }

    static void surface_unlock(MarniSurface2* self)
    {
        interop::thiscall<int, MarniSurface2*>((uintptr_t)self->vtbl->unlock_fn, self);
    }

    // 0x00401E40
    static int __stdcall prepare_movie(Marni* self)
    {
        if (!self->is_gpu_active)
            return 0;

        if (self->pMovie->flag == 0)
            return 1;

        surface_fill(&self->surface0, 0, 0, 0);
        flip(self);
        if (self->gpu_flag & GpuFlags::GPU_FULLSCREEN)
        {
            surface_fill(&self->surface0, 0, 0, 0);
            flip(self);
            // No GetWindowLongA/SetWindowLongA style toggling: SDL3 borderless
            // fullscreen (set_fullscreen) already removes the window frame.
        }
        return movie_update_window(self->pMovie);
    }

    // 0x00401EF0
    static void __stdcall kill_movie(Marni* self)
    {
        movie_seek(self->pMovie);
    }

    // 0x00401F00
    static void __stdcall sub_401F00(Marni* self)
    {
        sub_414B30(self->pMovie);
    }

    // 0x00401F10
    void __stdcall syskeydown(Marni* self)
    {
        auto movie = self->pMovie;
        if (movie->flag == 0)
            return;

        movie_release(movie);
        if (!(self->gpu_flag & GpuFlags::GPU_FULLSCREEN))
            return;
    }

    // 0x00401F70
    static void __stdcall update_movie(Marni* self)
    {
        auto movie = self->pMovie;
        if (movie->flag == 0)
            return;

        if (!movie_update(movie))
            return;

        if (!(self->gpu_flag & GpuFlags::GPU_FULLSCREEN))
            return;
    }

    // 0x00401FD0
    static int __stdcall set_movie_resolution(Marni* self, const char* path, int mode)
    {
        if (!self->is_gpu_active)
        {
            return 0;
        }

        RECT rc;
        ZeroMemory(&rc, sizeof(RECT));
        if (self->gpu_flag & GpuFlags::GPU_FULLSCREEN)
        {
            // Borderless fullscreen: the movie window covers the whole screen.
            // (The original offset it up by the frame/caption height to
            // compensate for the exclusive-fullscreen window frame.)
            int left = 0, top = 0, right = 0, bottom = 0;
            if (system::window::get_client_rect(left, top, right, bottom))
            {
                rc.left = left;
                rc.top = top;
                rc.right = right;
                rc.bottom = bottom;
            }
        }
        else if (self->resolutions[self->modes].width == 640)
        {
            rc.right = 640;
            rc.bottom = 480;
        }
        else
        {
            rc.right = 320;
            rc.bottom = 240;
        }
        // The SDL3 renderer owns the guest framebuffer; the movie surface
        // handle is not needed.
        return movie_open(self->pMovie, path, (HWND)self->hWnd, &rc, nullptr, nullptr);
    }

    // 0x00402160
    static int __stdcall arrange_object_contents(Marni* self, int a2, int* a3)
    {
        auto v3 = *(int*)(*((int*)self + 0x231DA6) + 4 * a2);
        auto v4 = *(int*)(v3 + 52);
        if ((v4 & 1) == 0)
        {
            out("invalid handle", "Marni::ArrangeObjectContents");
            return 0;
        }
        if ((v4 & 0x10000) != 0)
        {
            out("this object is optimized! (required not be optimized)", "Marni::ArrangeObjectContents");
            return 0;
        }
        *a3 = v3;
        return 1;
    }

    // 0x004021B0

    // 0x004021C0
    int __stdcall add_primitive_front(Marni* self, Prim* pPrim, int z)
    {
        if (!self->is_gpu_active)
            return 0;

        if ((pPrim->type & 8) != 0)
        {
            out();
            return 0;
        }
        else
        {
            ot_add_primitive_as_z(self->otag, pPrim, z);
            return 1;
        }
    }

    // 0x00402210
    int __stdcall add_primitive_scaler(Marni* self, Prim* pPrim, int z)
    {
        if (!self->is_gpu_active)
            return 0;

        ot_add_primitive_as_z(&self->otag[1], pPrim, z);
        return 1;
    }

    // 0x00402240
    int __stdcall add_primitive_back(Marni* self, Prim* pPrim, int z)
    {
        if (!self->is_gpu_active)
            return 0;

        if ((pPrim->type & 8) != 0)
        {
            out("you can't hang this primitive up to the priority list because this is ZCAL.", "Marni::AddPrimitiveBack");
            return 0;
        }

        ot_add_primitive_as_z(&self->otag[3], pPrim, z);
        return 1;
    }

    // 0x00402290
    void __stdcall clear_otags(Marni* self)
    {
        for (auto i = 0; i < 5; i++)
        {
            ot_clear(&self->otag[i]);
        }
        gGameTable.dword_543A14 = &gGameTable.unk_544148;
    }

    // 0x004022E0
    static void __stdcall request_video_memory(Marni* self)
    {
        if (!self->is_gpu_active)
        {
            out("though this class is unable you have tried to call.", "Marni::RequestVideoMemory");
            return;
        }

        auto cnt_2k_buffer = self->polygons_count; // cnt_2K_buffer
        auto count_use_gpu1 = 0;                   // textures with GPU_ENABLED + TEXTURE_PAL8 flags
        self->field_8C8418 = 0;
        self->field_8C8420 = 0;
        self->field_8C841C = 0;
        auto count_use_gpu0 = 0; // textures with GPU_ENABLED + TEXTURE_PAL4 flags
        self->field_8C8414 = 0;

        // Count used entries (index 1..cnt-1) in the 2K buffer.
        if (cnt_2k_buffer > 1)
        {
            for (auto i = 1u; i < cnt_2k_buffer; i++)
            {
                if (self->polygons[i])
                    self->field_8C8414++;
            }
        }

        if (self->gpu_flag & GpuFlags::SOFTWARE_GPU)
        {
            for (auto i = 0; i < 256; i++)
            {
                if (self->textures[i].var_00)
                {
                    self->field_8C8418++;
                    if (self->textures[i].var_00 & GpuFlags::TEXTURE_PAL8)
                        self->field_8C8420++;
                    if (self->textures[i].var_00 & GpuFlags::TEXTURE_PAL4)
                        self->field_8C841C++;
                }
            }
            return;
        }

        auto count_use = 0; // textures with GPU_ENABLED flag
        for (auto i = 0; i < 256; i++)
        {
            auto flags = self->texture_nodes[i].var_14;
            if (flags && (flags & GpuFlags::SOFTWARE_GPU) == 0)
            {
                self->field_8C8418++;
                if (flags & GpuFlags::TEXTURE_PAL8)
                    self->field_8C8420++;
                if (flags & GpuFlags::TEXTURE_PAL4)
                    self->field_8C841C++;
                if (flags & GpuFlags::GPU_ENABLED)
                {
                    count_use++;
                    if (flags & GpuFlags::TEXTURE_PAL8)
                        count_use_gpu1++;
                    if (flags & GpuFlags::TEXTURE_PAL4)
                        count_use_gpu0++;
                }
            }
        }

        if (self->field_8C8418)
            *(float*)&self->field_8C8410 = (float)count_use / (float)self->field_8C8418;
        else
            self->field_8C8410 = 0;
        if (self->field_8C8420)
            self->field_8C8424 = (float)count_use_gpu1 / (float)self->field_8C8420;
        else
            self->field_8C8424 = 0.0f;
        if (self->field_8C841C)
            *(float*)&self->field_8C8428 = (float)count_use_gpu0 / (float)self->field_8C841C;
        else
            self->field_8C8428 = 0;
    }

    // The windowed mode index to restore when leaving fullscreen via ALT+ENTER.
    // Updated in change_display_mode whenever we switch away from a windowed mode.
    static int g_last_windowed_mode = -1;

    // 0x00402500
    bool __stdcall change_resolution(Marni* self)
    {
        // F8 cycles only the windowed render resolutions; the window is
        // decoupled from them, so a "mode" is just an internal render
        // resolution here. The fullscreen mode is exclusive to the ALT+ENTER
        // toggle: F8 neither enters it nor alters the fullscreen state (it is
        // a no-op while fullscreen).
        if (self->gpu_flag & GpuFlags::GPU_FULLSCREEN)
            return false;
        auto mode = self->modes + 1;
        for (auto i = 0; i < self->res_count; i++)
        {
            if (mode >= (uint32_t)self->res_count)
                mode = 0;
            if (self->resolutions[mode].fullscreen == 0)
                return change_display_mode(self, (int)mode);
            mode++;
        }
        return false;
    }

    // ALT+ENTER: toggles between the current windowed mode and the fullscreen
    // mode. Leaving fullscreen restores the last windowed mode, keeping the
    // window position and size the user had before (SDL restores the window
    // geometry when it exits fullscreen).
    bool __stdcall toggle_fullscreen(Marni* self)
    {
        for (auto i = 0; i < self->res_count; i++)
        {
            if (self->resolutions[i].fullscreen > 0)
            {
                if (self->gpu_flag & GpuFlags::GPU_FULLSCREEN)
                {
                    auto mode = g_last_windowed_mode;
                    if (mode < 0 || mode >= self->res_count || self->resolutions[mode].fullscreen > 0)
                        mode = 0;
                    return change_display_mode(self, mode);
                }
                return change_display_mode(self, i);
            }
        }
        return false;
    }

    // 0x00402530
    int __stdcall request_display_mode_count(Marni* self)
    {
        if (self->is_gpu_active)
            return self->res_count;

        out("", "Marni::RequestDisplayModeCount");
        return 0;
    }

    // 0x00402560

    // 0x00402940
    static int __stdcall restore_surfaces(Marni* self)
    {
        return 1;
    }

    static void __stdcall flip_blt(Marni* self, DWORD width, DWORD height) {}

    // 0x00402A80
    void __stdcall flip(Marni* self) {}

    // 0x00402BC0
    void __stdcall draw(Marni* self) {}

    // 0x00403060
    static bool __stdcall change_display_mode(Marni* self, int mode)
    {
        std::string s;
        if (mode < self->res_count)
        {
            auto originalMode = self->modes;
            // Remember the last windowed mode so ALT+ENTER can restore it (with
            // its position and size) when leaving fullscreen.
            if (!(self->gpu_flag & GpuFlags::GPU_FULLSCREEN))
                g_last_windowed_mode = originalMode;
            self->modes = mode;
            const auto& r = self->resolutions[mode];
            if (change_mode(self, r.width, r.height, r.depth))
            {
                out("Marni::ChangeDisplayMode - (%d->%d) w:%d h:%d bpp:%d", "");
                str::string_assign(&gGameTable.marni_config.display_mode, generate_res_string(&r));
                self->var_8C8318 = 0;
                // Keep the config render resolution in sync with the active
                // display mode so system_gpu's guest framebuffer is re-created
                // at the new size by the renderer's begin().
                system::config::set_resolution("video", "render_resolution", { (int32_t)r.width, (int32_t)r.height });
                logging::logInfo(
                    "[marni] Display mode changed: {} -> {} ({}x{} {}bpp fullscreen:{})",
                    originalMode,
                    mode,
                    r.width,
                    r.height,
                    r.depth,
                    r.fullscreen);
                return true;
            }
            self->modes = originalMode;
        }
        else
        {
            out("you were about to set up invalid mode.", "MarniSystem Marni::ChangeMode");
        }
        return false;
    }

    // 0x00403170
    static std::string __stdcall generate_res_string(const MarniRes* self)
    {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "%dx%d %dbpp full:%d", self->width, self->height, self->depth, self->fullscreen);
        return std::string(buffer);
    }

    // 0x00403220
    static int __stdcall change_mode(Marni* self, uint32_t width, uint32_t height, uint32_t depth)
    {
        if ((self->gpu_flag & GpuFlags::GPU_ENABLED) == 0)
            return 0;

        if ((self->gpu_flag & GpuFlags::SOFTWARE_GPU) != 0)
        {
            self->aspect_x = 1.0;
            self->aspect_y = 1.0;
        }
        else
        {
            self->aspect_x = (float)((double)width / self->render_w);
            self->aspect_y = (float)((double)height / self->render_h);
        }
        self->xsize_old = self->xsize;
        self->ysize_old = self->ysize;
        self->bpp_old = self->bpp;
        self->fullscreen_old = (self->gpu_flag & GpuFlags::GPU_FULLSCREEN) != 0;
        self->xsize = width;
        self->ysize = height;
        self->bpp = depth;
        clear_buffers(self);
        if (!init_all(self))
        {
            out("this method failed for some problems to the initialize that for change mode of display for some problems.",
                "Marni::ChangeMode");
            out("this method will change not mode you specified but previous mode.", "Marni::ChangeMode");
            self->xsize = self->xsize_old;
            self->ysize = self->ysize_old;
            self->bpp = self->bpp_old;
            out("previous x=%d y=%d bpp=%d request x=%d y=%d bpp=%d", "Marni::ChangeMode");
            clear_buffers(self);
            if (!init_all(self))
            {
                out("occurred fatal error. this method couldn't come back for somethings.", "Marni::ChangeMode");
                clear_buffers(self);
                return 0;
            }
        }
        if (!restore_surfaces(self))
        {
            out("this method failed for some problems to Reload that for change mode of display for some problems.",
                "Marni::ChangeMode");
            out("this method will change not mode you specified but previous mode.", "Marni::ChangeMode");
            self->xsize = self->xsize_old;
            self->ysize = self->ysize_old;
            self->bpp = self->bpp_old;
            out("previous x=%d y=%d bpp=%d request x=%d y=%d bpp=%d", "Marni::ChangeMode");
            clear_buffers(self);
            if (!init_all(self) || !restore_surfaces(self))
            {
                out("occurred fatal error at Reload. this method couldn't come back for somethings.", "Marni::ChangeMode");
                clear_buffers(self);
                return 0;
            }
        }
        self->gpu_flag |= GpuFlags::GPU_ENABLED;
        return 1;
    }

    // 0x004033F0
    static int __stdcall reload_texture(Marni* self, int texture)
    {
        if (!self->is_gpu_active)
            return 0;

        // No GPU textures to reload: the SDL renderer owns all texture memory
        // (create_texture_handle routes straight to it), so a reload has
        // nothing to do.
        return 1;
    }

    // 0x00403ec0

    // Computes the letterboxed 4:3 rectangle (in screen coordinates) used to
    // present the render in borderless fullscreen. The rect is derived from the
    // actual window rect (SDL window position/size) rather than SDL display
    // bounds: the window client area lives in the process's DPI coordinate
    // space, and the SDL window position/size report the window in exactly that
    // space (SDL_GetDisplayBounds can report a different, physical-pixel DPI
    // scale, which made the letterbox rect cover the whole window and look
    // stretched).
    static void compute_fullscreen_window_rect(Marni* self)
    {
        int left = 0, top = 0, right = 0, bottom = 0;
        if (system::window::get_window_rect(left, top, right, bottom) && (right - left) > 0 && (bottom - top) > 0)
        {
            auto winW = right - left;
            auto winH = bottom - top;
            double scale = std::min((double)winW / self->xsize, (double)winH / self->ysize);
            auto rectW = (int)(self->xsize * scale);
            auto rectH = (int)(self->ysize * scale);
            SetRect(
                (LPRECT)&self->window_rect,
                left + (winW - rectW) / 2,
                top + (winH - rectH) / 2,
                left + (winW + rectW) / 2,
                top + (winH + rectH) / 2);
        }
        else
        {
            SetRect((LPRECT)&self->window_rect, 0, 0, self->xsize, self->ysize);
        }
    }

    // Computes the letterboxed presentation rect (in screen coordinates) for a
    // windowed render: `width`x`height` scaled to fit the window client area
    // and centered. The window no longer tracks the render resolution (it is
    // resizable and sized from config), so the blit target must be derived
    // from the actual client area instead of the render size.
    static void compute_windowed_window_rect(HWND hWnd, int width, int height, LPRECT outRect)
    {
        int left = 0, top = 0, right = 0, bottom = 0;
        if (system::window::get_client_rect(left, top, right, bottom) && (right - left) > 0 && (bottom - top) > 0)
        {
            auto clientW = right - left;
            auto clientH = bottom - top;
            double scale = std::min((double)clientW / width, (double)clientH / height);
            auto rectW = (int)(width * scale);
            auto rectH = (int)(height * scale);
            auto x0 = left + (clientW - rectW) / 2;
            auto y0 = top + (clientH - rectH) / 2;
            SetRect(outRect, x0, y0, x0 + rectW, y0 + rectH);
        }
        else
        {
            SetRect(outRect, 0, 0, width, height);
        }
    }

    // 0x00403F30
    static int __stdcall init_all(Marni* self)
    {
        const auto& r = self->resolutions[self->modes];
        if (r.fullscreen <= 0)
            self->gpu_flag &= ~GpuFlags::GPU_FULLSCREEN;
        else
            self->gpu_flag |= GpuFlags::GPU_FULLSCREEN;
        self->xsize = r.width;
        self->ysize = r.height;
        self->bpp = r.depth;
        self->is_gpu_busy = 1;
        if (self->gpu_flag & GpuFlags::GPU_FULLSCREEN)
        {
            // Borderless fullscreen: let SDL3 cover the display and render at the
            // fullscreen mode's native resolution (a 4:3 mode sized for the
            // display), then letterbox it into the window. Keeping xsize/ysize at
            // the mode resolution is essential - overriding it with the window
            // rect size broke renderer setup, leaving a frozen black screen.
            system::window::set_fullscreen(true);
            compute_fullscreen_window_rect(self);
            gGameTable.dword_54413C = 1;
        }
        else
        {
            system::window::set_fullscreen(false);
            if (self->resolutions[self->modes].fullscreen == 0xFFFFFFFF)
            {
                self->xsize = self->render_w;
                self->ysize = self->render_h;
            }
            invalidate_window((HWND)self->hWnd, self->xsize, self->ysize, 0, (LPRECT)&self->window_rect);
        }

        self->aspect_x = (float)((double)self->xsize / self->render_w);
        self->aspect_y = (float)((double)self->ysize / self->render_h);

        // No GPU surfaces exist in the SDL-only renderer. Synthesize the surface
        // descriptors (16-bit 555, the format the GPU render path used) so the
        // renderer and font code see sane surface0/surface2/surfaceZ fields
        // without allocating any backend surface.
        self->surface0.pPalette = nullptr;
        self->surface0.height = self->ysize;
        self->surface0.width = self->xsize;
        self->surface0.bpp = 16;
        self->surface0.var_25 = 0;
        self->surface0.pitch = 2 * self->xsize;
        self->surface0.var_27 = 1;
        self->surface0.var_28 = 0;
        self->surface0.var_29 = 0;
        self->surface0.bOpen = 1;
        self->surface0.is_vmem = 0;
        self->surface0.desc = {};
        self->surface0.desc.r_shift = 10;
        self->surface0.desc.r_mask = 31;
        self->surface0.desc.r_bitcnt = 5;
        self->surface0.desc.g_shift = 5;
        self->surface0.desc.g_mask = 31;
        self->surface0.desc.g_bitcnt = 5;
        self->surface0.desc.b_shift = 0;
        self->surface0.desc.b_mask = 31;
        self->surface0.desc.b_bitcnt = 5;
        self->surface0.desc.a_shift = 15;
        self->surface0.desc.a_mask = 1;
        self->surface0.desc.a_bitcnt = 1;
        self->gpu_flag |= GpuFlags::RGB555;

        self->surface2.pPalette = nullptr;
        self->surface2.width = (int16_t)self->xsize;
        self->surface2.bpp = 16;
        self->surface2.desc = self->surface0.desc;
        self->surface2.desc.a_bitcnt = 0;
        self->surface2.is_vmem = 0;
        self->surface2.var_25 = 0;
        self->surface2.height = (int16_t)self->ysize;
        self->surface2.pitch = 2 * self->xsize;
        self->surface2.var_27 = 1;
        self->surface2.var_28 = 0;
        self->surface2.var_29 = 0;
        self->surface2.bOpen = 1;

        self->surfaceZ.pDDsurface = nullptr;
        self->surfaceZ.desc.r_bitcnt = 5;
        self->surfaceZ.desc.g_shift = 5;
        self->surfaceZ.desc.g_bitcnt = 5;
        self->surfaceZ.desc.b_bitcnt = 5;
        self->surfaceZ.desc.r_mask = 31;
        self->surfaceZ.desc.g_mask = 31;
        self->surfaceZ.desc.b_mask = 31;
        self->surfaceZ.pPalette = 0;
        self->surfaceZ.desc.r_shift = 0;
        self->surfaceZ.desc.b_shift = 10;
        self->surfaceZ.bpp = 16;
        self->surfaceZ.var_25 = 0;
        self->surfaceZ.width = self->xsize;
        self->surfaceZ.pitch = 2 * self->xsize;
        self->surfaceZ.var_27 = 1;
        self->surfaceZ.var_28 = 0;
        self->surfaceZ.var_29 = 0;
        self->surfaceZ.bOpen = 1;

        // The SDL3 renderer needs a guest framebuffer to draw into; create it
        // at the render resolution (idempotent in system::gpu).
        system::gpu::create_guest_framebuffer(self->xsize, self->ysize);
        if ((self->gpu_flag & GpuFlags::GPU_FULLSCREEN) != 0)
        {
        }
        self->is_gpu_active = 1;
        return 1;
    }

    // 0x00404bb0

    // 0x00404CA0
    static int destroy_object(Marni* self, int index)
    {
        auto* obj = self->polygons[index];
        if (obj)
        {
            polygon_object_dtor(obj);
            operator_delete(obj);
            self->polygons[index] = 0;
        }
        return 1;
    }

    // 0x00404CE0
    void __stdcall unload_texture(Marni* self, int handle)
    {
        if (handle == 0)
            return;

        auto& texture = self->textures[handle];
        if (texture.var_00 != 0)
        {
            // No texture objects exist here; the SDL renderer owns the GPU
            // textures, so just hand the handle back to it.
            logging::logInfo("[marni] unload_texture: handle={} -> g_renderer->unloadTexture", handle);
            if (g_renderer)
                g_renderer->unloadTexture(handle);
            texture.var_00 = 0;
        }
    }

    // 0x00404D20
    int __stdcall clear(Marni* self)
    {
        if (!(self->gpu_flag & GpuFlags::GPU_ENABLED) || !self->is_gpu_active || self->var_8C7EE0
            || !(self->gpu_flag & GpuFlags::SOFTWARE_GPU) && (self->pDevice2 == nullptr || self->pViewport == nullptr))
        {
            return 0;
        }

        if ((self->pMovie->flag & 2) != 0)
            return 1;

        // The SDL3 renderer clears through its own frame loop; nothing to do
        // here (no GPU viewport/device exists).
        return 1;
    }

    // 0x00404E40
    static void __stdcall do_render(Marni* self, MarniOt* pOt)
    {
        // The SDL3 renderer parses the ordering tables itself; no GPU device
        // exists to render through.
        (void)self;
        (void)pOt;
        return;
    }

    // 0x00404FA0
    static int __stdcall clear_buffers(Marni* self)
    {
        // Deactivate the GPU and clear the "device created" flag.
        self->is_gpu_active = 0;
        self->gpu_flag &= ~GpuFlags::GPU_ENABLED;

        // Free every texture node's surface object.
        for (int i = 0; i < 256; i++)
        {
            auto& node = self->texture_nodes[i];
            if (node.var_14 != 0)
            {
                if (node.surface != nullptr)
                {
                    surfacex_dtor(node.surface);
                    cstd_free(node.surface);
                }
                node.surface = nullptr;
            }
        }

        // Release the work-surface wrappers' COM objects.
        for (int i = 0; i < 128; i++)
        {
            auto p = self->var_8C76A0[i];
            if (p != nullptr)
            {
                auto obj = *(void**)p;
                if (obj != nullptr)
                {
                    auto vtbl = *(void***)obj;
                    ((void(__stdcall*)(void*))vtbl[2])(obj); // IUnknown::Release
                    *(void**)p = nullptr;
                }
            }
        }

        surface_release(&self->surface0);
        surface_release(&self->surfaceZ);
        return interop::thiscall<int, MarniSurface2*>((uintptr_t)self->surface2.vtbl->release_fn, &self->surface2);
    }

    // 0x004050C0
    static void __stdcall dtor(Marni* self)
    {
        if (self->pMovie != nullptr)
        {
            movie_dtor(self->pMovie);
            cstd_free(self->pMovie);
            self->pMovie = nullptr;
        }

        clear_buffers(self);

        for (size_t i = 0; i < self->polygons_count; i++)
        {
            if (self->polygons[i] != nullptr)
            {
                polygon_object_dtor(self->polygons[i]);
                cstd_free(self->polygons[i]);
                self->polygons[i] = nullptr;
            }
        }
        cstd_free(self->polygons);
        self->polygons = nullptr;

        for (auto i = 0; i < 256; i++)
            unload_texture(self, i);

        for (auto i = 0; i < 128; i++)
        {
            auto s = self->var_8C76A0[i];
            if (s != nullptr)
            {
                surface3_dtor(s->surface);
                cstd_free(s);
            }
        }

        surfacey_dtor(&self->surface3);
        surfacey_dtor(&self->surface2);
        surfacey_dtor(&self->surfaceZ);
        surfacey_dtor(&self->surface0);

        ot_dtor(&self->otag[4]);
        ot_dtor(&self->otag[3]);
        ot_dtor(&self->otag[2]);
        ot_dtor(&self->otag[1]);
        ot_dtor(&self->otag[0]);

        cstd_vector_dtor(self->textures, sizeof(MarniTextureNode), 256, (void*)0x00405310);
    }

    // 0x00405310

    // 0x00405320
    Marni* __stdcall init(Marni* self, void* hWnd, int width, int height)
    {
        cstd_vector_ctor(self->textures, sizeof(MarniTexture), 256, (void*)0x405DC0, (void*)0x405310);
        auto exception = 0;
        ot_ctor(&self->otag[0], 4096, 1);
        ot_ctor(&self->otag[1], 4096, 1);
        ot_ctor(&self->otag[2], 4096, 1);
        ot_ctor(&self->otag[3], 4096, 1);
        ot_ctor(&self->otag[4], 4096, 1);
        surfacey_ctor((MarniSurfaceY*)&self->surface0);
        surfacey_ctor((MarniSurfaceY*)&self->surfaceZ);
        surfacey_ctor((MarniSurfaceY*)&self->surface2);
        surfacey_ctor((MarniSurfaceY*)&self->surface3);
        self->is_gpu_active = 0;
        auto v5 = (MarniMovie*)cstd_malloc(sizeof(MarniMovie));
        exception = 10;
        self->pMovie = v5 == nullptr ? nullptr : movie_ctor(v5, 0);
        self->field_8C8420 = 0;
        self->field_8C841C = 0;
        self->field_8C8418 = 0;
        self->field_8C8414 = 0;
        self->field_8C8428 = 0;
        self->field_8C8424 = 0.0f;
        self->field_8C8410 = 0;
        exception = 9;
        std::memset(self, 0, 0x1800);
        // Create the SDL_GPU device eagerly. marni::init is the only place we
        // know a window exists before init_all() creates the backend surfaces;
        // the surfaces must be registered against the backend device for
        // GetSurfaceDesc to succeed (which gates is_gpu_active).
        system::gpu::init();
        for (auto i = 0; i < 256; i++)
        {
            self->textures[i].var_00 = 0;
        }
        self->field_8C7E18 = 1.0f;
        self->field_8C7E28 = 1.0f;
        self->field_8C7E38 = 1.0f;
        self->field_8C7E10 = 0;
        self->field_8C7E14 = 0;
        self->field_8C7E1C = 0;
        self->field_8C7E20 = 0;
        self->field_8C7E24 = 0;
        self->field_8C7E2C = 0;
        self->field_8C7E30 = 0;
        self->field_8C7E34 = 0;
        self->field_8C7E3C = 0;
        self->field_8C7E40 = 0;
        self->field_8C7E44 = 0;
        self->field_8C7E48 = 0;
        self->field_8C7E4C = 0;
        self->field_8C7E50 = 255.0f;
        self->field_8C7E54 = 255.0f;
        self->field_8C7E58 = 255.0f;
        self->field_8C7E5C = 0;
        self->field_8C7E60 = 255.0f;
        self->field_8C7E64 = 255.0f;
        self->field_8C7E68 = 255.0f;
        self->field_8C7E6C = 0;
        self->field_8C7E70 = 255.0f;
        self->field_8C7E74 = 255.0f;
        self->field_8C7E78 = 255.0f;
        self->field_8C7E7C = 0;
        self->field_8C7E80 = 0;
        self->field_8C7E84 = 0;
        self->field_8C7E88 = 0;
        self->field_8C7E8C = 0;
        self->field_5000 = 4;
        self->field_5008 = 0;
        self->field_5004 = 0;
        self->gpu_flag = 0;
        self->field_8C7EDC = 260;
        ot_alloc(&self->otag[0], 16, 0);
        ot_alloc(&self->otag[1], 4096, 1);
        ot_alloc(&self->otag[3], 16, 0);
        self->hWnd = hWnd;
        self->render_w = width;

        self->xsize = width;
        self->ysize = height;
        self->dwVidMemFree = 0;
        self->is_gpu_active = 0;
        self->field_8C7EC4 = width / 2;
        self->render_h = height;
        self->aspect_x = (float)(self->xsize / (double)width);
        self->aspect_y = (float)(self->ysize / (double)height);
        self->field_8C7EC8 = height / 2;
        self->var_8C7EE4 = 0;
        self->var_8C7EE0 = 0;
        std::memset(self->resolutions, 0, sizeof(self->resolutions));
        self->res_count = 0;
        self->field_8C8300 = 3;
        self->field_8C7E90 = 0;
        self->field_8C82FC = 0;
        self->desktop_w = 0;
        self->desktop_h = 0;
        system::window::get_desktop_size(self->desktop_w, self->desktop_h);
        // SDL3 has no bits-per-pixel concept; the guest only reads desktop_bpp
        // to seed bpp, and 32bpp matches the SDL window surface.
        self->desktop_bpp = 32;
        self->bpp = self->desktop_bpp;
        self->gpu_flag |= GpuFlags::SURFACE_NO_PALETTE;
        self->is_gpu_busy = 0;
        *((uint32_t*)&self->ambient_b) = 0;
        self->var_8C8318 = 0;
        std::memset(self->var_8C76A0, 0, sizeof(self->var_8C76A0));
        self->pClip = nullptr;
        self->pDraw = 0;
        self->pMaterial = 0;
        self->pViewport = 0;
        self->pDevice2 = nullptr;
        self->p3D = 0;
        self->pDraw2 = 0;
        self->field_8C8430 = 0;
        std::memset(self->field_8C728C, 0, sizeof(self->field_8C728C));
        self->dwVidMemTotal = 0;
        self->res_count = 0;
        self->polygons_count = 512;
        self->polygons = (PolygonObject**)cstd_malloc(512 * sizeof(PolygonObject*));
        std::memset(self->polygons, 0, 512 * sizeof(PolygonObject*));
        self->field_8C701C = -0.5;
        self->field_8C7020 = 0;
        self->is_gpu_active = 0;
        self->pMaterial = 0;
        self->MaterialHandle = 0;
        self->device_cnt = 0;
        std::memset(self->lights, 0, sizeof(self->lights));
        for (auto i = 0; i < 6; i++)
        {
            self->lights->var_14 = 0.5f;
            self->lights->var_18 = 0.5f;
            self->lights->var_1C = 0.5f;
            self->lights->var_20 = 0.5f;
        }

        // Build the display mode list. The original game only offered a single 640x480
        // fullscreen mode (plus an optional 2x windowed mode), so F8 could never cycle
        // between different resolutions and the exclusive fullscreen switch fails on
        // modern systems. Instead we offer a set of windowed resolutions plus
        // one fullscreen mode (rendered borderless via SDL3), all of which go through
        // the same windowed rendering path.
        constexpr uint32_t modeCount = 5;
        struct ModeDef
        {
            uint32_t width;
            uint32_t height;
            uint32_t depth;
            int32_t fullscreen;
        };
        const ModeDef modes[modeCount] = {
            { (uint32_t)(2 * self->render_w),
              (uint32_t)(2 * self->render_h),
              (uint32_t)self->desktop_bpp,
              0 }, // 640x480 windowed
            { (uint32_t)(3 * self->render_w),
              (uint32_t)(3 * self->render_h),
              (uint32_t)self->desktop_bpp,
              0 }, // 960x720 windowed
            { (uint32_t)(4 * self->render_w),
              (uint32_t)(4 * self->render_h),
              (uint32_t)self->desktop_bpp,
              0 }, // 1280x960 windowed
            { (uint32_t)(6 * self->render_w),
              (uint32_t)(6 * self->render_h),
              (uint32_t)self->desktop_bpp,
              0 }, // 1920x1440 windowed
            { (uint32_t)(6 * self->render_w),
              (uint32_t)(6 * self->render_h),
              (uint32_t)self->desktop_bpp,
              1 }, // 1920x1440 fullscreen (borderless, native render res)
        };
        for (const auto& m : modes)
        {
            auto& r = self->resolutions[self->res_count];
            r.width = m.width;
            r.height = m.height;
            r.depth = m.depth;
            r.fullscreen = m.fullscreen;
            self->res_count++;
        }

        self->modes = 0;
        for (auto i = 0; i < self->res_count; i++)
        {
            exception = 13;
            auto isEq = str::string_eq(&gGameTable.marni_config.display_mode, generate_res_string(&self->resolutions[i]));
            exception = 9;
            if (isEq)
            {
                self->modes = i;
                break;
            }
        }

        exception = 14;
        str::string_assign(&gGameTable.marni_config.display_mode, generate_res_string(&self->resolutions[self->modes]));
        exception = 9;
        if (self->modes >= (uint32_t)self->res_count)
        {
            out("you specified invalid mode. correct disp num to 0 automatically.", "MarniSystem Marni");
            self->modes = 0;
        }

        if (!init_all(self))
        {
            out("failed to initialize.", "MarniSystem Marni");
            return self;
        }

        out("you will be able to use the following mode", "MarniSystem Marni");

        for (auto i = 0; i < self->res_count; i++)
        {
            out("%d x %d x %d full=%d", "MarniSystem Marni");
        }

        if (self->gpu_flag & GpuFlags::SOFTWARE_GPU)
        {
            self->is_gpu_active = 1;
            self->gpu_flag |= GpuFlags::GPU_ENABLED;
        }
        else
        {
            // No GPU device exists in the SDL-only build; mark the GPU active
            // the same way the software-renderer branch above does. The SDL3
            // renderer owns the swapchain and presents via SDL itself.
            logging::logInfo("[marni] init: GPU active (no GPU device)");
            self->is_gpu_active = 1;
            self->gpu_flag |= GpuFlags::GPU_ENABLED;
        }
        return self;
    }

    // 0x00405dc0

    // 0x00405DD0
    static int __stdcall get_z_buffer_caps(Marni* self)
    {
        (void)self;
        return 0;
    }

    // Thin wrappers around the MarniSurface2 vtable blit functions.
    static void surface_blt(MarniSurface2* self, LPRECT pDstRect, LPRECT pSrcRect, MarniSurface2* pSrc, int a5, int a6)
    {
        surface2_blt(self, pDstRect, pSrcRect, pSrc, a5, a6);
    }

    // 0x00412580
    // MarniSurface2::Blt - blit pSrc onto self, sampling the source rectangle
    // (pSrcRect, optionally NULL for the whole surface) into the destination
    // rectangle (pDstRect, optionally NULL for the whole surface).
    //
    // a5 is a set of mode flags: 0x10 = flip horizontally, 0x20 = flip
    // vertically, plus the blending flags passed through to Set*Color. When a6
    // is non-NULL it points to a 4-float table used to scale each colour
    // channel (R, G, B, A) before the pixel is written.
    int __stdcall surface2_blt(MarniSurface2* self, RECT* pDstRect, RECT* pSrcRect, MarniSurface2* pSrc, int a5, int a6)
    {
        if (!self->bOpen || !pSrc->bOpen)
        {
            out("tried to call the service although the bitmap is not valid", "MarniBits::Blt");
            return 0;
        }

        // Source rectangle: must lie entirely inside the source surface.
        RECT rc;
        if (pSrcRect)
        {
            if (pSrcRect->top < 0 || pSrcRect->left < 0 || pSrcRect->right >= pSrc->width || pSrcRect->bottom >= pSrc->height)
            {
                out("MarniBits::Blt out of range (src)...%d %d %d %d", "MarniBits::Blt");
                return 0;
            }
            rc.left = pSrcRect->left;
            rc.top = pSrcRect->top;
            rc.right = pSrcRect->right;
            rc.bottom = pSrcRect->bottom;
        }
        else
        {
            rc.left = 0;
            rc.top = 0;
            rc.right = pSrc->width - 1;
            rc.bottom = pSrc->height - 1;
        }

        // Destination rectangle: clipped against the destination surface.
        // a1[0] doubles as the clip rect for Adjust_rect; the original also
        // fills a per-column source-X table into a1[1..] that is never read.
        RECT a1[751];
        LONG left, v13, v15, v16;
        RECT a3;
        if (pDstRect)
        {
            left = pDstRect->left;
            v13 = pDstRect->top;
            v15 = pDstRect->right;
            v16 = pDstRect->bottom;

            a1[0].left = 0;
            a1[0].top = 0;
            a1[0].right = self->width - 1;
            a1[0].bottom = self->height - 1;
            if (!adjust_rect(&a1[0], pDstRect, &a3))
                return 0;
        }
        else
        {
            a3.left = 0;
            a3.top = 0;
            a3.right = self->width - 1;
            a3.bottom = self->height - 1;
            left = a3.left;
            v13 = a3.top;
            v15 = a3.right;
            v16 = a3.bottom;
        }

        // Sizes of the clipped destination and the source rectangles.
        int v17 = v15 - left;
        int v57 = a3.right - a3.left + 1;
        int v59 = a3.bottom - a3.top + 1;
        int v51 = rc.right - rc.left + 1;

        // Source sampling ratio and the offset of the clipped destination's
        // first column/row into the source rectangle. These use __ftol-style
        // float-to-int truncation in the original.
        float ratio = (float)v51 / (float)(v17 + 1);
        int v63 = (rc.right - rc.left) * (a3.left - left) / v17;
        int v62 = (rc.bottom - rc.top) * (a3.top - v13) / (v16 - v13);

        // The original pre-computes the source X for every destination column
        // into a1[] but never reads it back; mirrored here for fidelity.
        for (int v22 = 0; v22 < v57; ++v22)
            (&a1[1].left)[v22] = (int)((float)(v22 + v63) * ratio + (float)rc.left);

        // Pick the blit flavour. When both a5 and a6 are zero the mode is
        // derived from the surface formats: bit 8 = palette-indexed source,
        // bit 12 = palette-indexed destination (var_28). The bpp/var_25 terms
        // are computed but masked away by the 0x1100 test below.
        int v27 = 0;
        if (!a5 && !a6)
        {
            v27 = (pSrc->bpp / 8) | ((self->bpp / 8) << 4);
            if (pSrc->var_28)
                v27 |= 0x100;
            if (self->var_28)
                v27 |= 0x1000;
            v27 |= (((self->var_25 / 8) << 4) | (pSrc->var_25 / 8)) << 16;
        }
        int v28 = v27 & 0x1100;
        if (v28 && v28 != 0x100)
        {
            if (v28 != 0x1100)
                return 1;

            // Palette-indexed blit: copy palette indices, then sync the palette.
            surface_lock(self, 0, 0);
            surface_lock(pSrc, 0, 0);

            self->var_2D = 1;
            self->var_2C = 0;

            int v29 = 0;
            if (v59 > 0)
            {
                do
                {
                    int v31 = 0;
                    if (v57 > 0)
                    {
                        int v32 = v29 + v62;
                        int flipY = a5 & 0x20;
                        do
                        {
                            uint32_t color = 0;
                            if (rc.left + v31 + v63 > rc.right || rc.top + v32 > rc.bottom)
                            {
                                color = 0;
                            }
                            else
                            {
                                surface_get_color(pSrc, rc.left + v31 + v63, rc.top + v32, &color);
                            }

                            int dstX = (a5 & 0x10) ? a3.right - v31 : v31 + a3.left;
                            int dstY = flipY ? a3.bottom - v29 : v29 + a3.top;
                            surface_set_color(self, dstX, dstY, color, a5);
                            ++v31;
                        } while (v31 < v57);
                    }
                    ++v29;
                } while (v29 < v59);
            }

            surface_unlock(pSrc);
            surface_unlock(self);
            interop::thiscall<int, MarniSurface2*, MarniSurface2*, int, int>(
                (uintptr_t)self->vtbl->pal_blt, self, pSrc, -1, -1);
            return 1;
        }

        // General blit: read each source pixel's current colour, optionally
        // scale it with the a6 table, and write it to the destination.
        surface_lock(self, 0, 0);
        surface_lock(pSrc, 0, 0);

        self->var_2D = 1;
        self->var_2C = 0;

        int v36 = 0;
        uint32_t v55 = 0;
        if (v59 > 0)
        {
            do
            {
                int v37 = 0;
                if (v57 > 0)
                {
                    int v64 = v62 + v36;
                    do
                    {
                        uint32_t v38;
                        if (rc.left + v37 + v63 > rc.right || v64 + rc.top > rc.bottom)
                        {
                            v38 = 0;
                        }
                        else
                        {
                            uint32_t a4;
                            if (!surface_get_current_color(pSrc, rc.left + v37 + v63, v64 + rc.top, &a4))
                            {
                                out("failed to acquire the color", "MarniBits::Blt");
                                return 0;
                            }
                            v38 = a4;
                            if ((v38 & 0xFFFFFF) != 0)
                            {
                                if (!pSrc->desc.a_bitcnt)
                                    v38 |= 0xFF000000;
                            }
                            else
                            {
                                v38 = 0;
                            }
                        }

                        if (a6)
                        {
                            const float* pScale = (const float*)a6;
                            int v40 = (int)((float)((v38 >> 16) & 0xFF) * pScale[0]);
                            int v42 = (int)((float)((v38 >> 8) & 0xFF) * pScale[1]);
                            int v45 = (int)((float)(v38 & 0xFF) * pScale[2]);
                            int v46 = (int)((float)((v38 >> 24) & 0xFF) * pScale[3]);
                            if (v40 >= 256)
                                v40 = 255;
                            if (v42 >= 256)
                                v42 = 255;
                            if (v45 >= 256)
                                v45 = 255;
                            if (v46 >= 256)
                                v46 = 255;
                            v38 = (uint32_t)((uint8_t)v45 | ((uint8_t)v42 << 8) | ((uint8_t)v40 << 16) | ((uint8_t)v46 << 24));
                        }

                        if ((v38 & 0xFF000000) != 0xFF000000)
                            self->var_2C = 1;

                        int dstX = (a5 & 0x10) ? a3.right - v37 : v37 + a3.left;
                        int dstY = (a5 & 0x20) ? a3.bottom - (int)v55 : a3.top + (int)v55;
                        surface_set_current_color(self, dstX, dstY, v38, a5);
                        ++v37;
                    } while (v37 < v57);
                    v36 = (int)v55;
                }
                v55 = ++v36;
            } while (v36 < v59);
        }

        surface_unlock(pSrc);
        surface_unlock(self);
        return 1;
    }

    // 0x004123D0
    static int __stdcall surface_pal_blt(MarniSurface2* self, MarniSurface2* pSrc, int paletteSrc, int paletteDst)
    {
        if (!self->bOpen)
        {
            out("invalid class", "MarniBits::PalBlt");
            return 0;
        }

        // A palette number of -1 means "use the surface's current palette".
        int paletteSrc2 = paletteSrc;
        int paletteDst2 = paletteDst;
        if (paletteSrc == -1)
            paletteSrc2 = pSrc->var_22;
        if (paletteDst == -1)
            paletteDst2 = self->var_22;

        // Both surfaces must be palette indexed.
        if (!pSrc->var_28 || !self->var_28)
        {
            out("", "this is not palette indexed bits. MarniBits::PalBlt");
            return 0;
        }

        // Both palette numbers must lie within the destination and source palettes.
        if (self->pal_cnt <= paletteDst2 || pSrc->pal_cnt <= paletteSrc2)
        {
            out("", "specified invalid palette number. MarniBits::PalBlt");
            return 0;
        }

        uint8_t bpp = self->bpp;
        if (pSrc->bpp < bpp)
            bpp = pSrc->bpp;
        int rowSize = 1 << bpp;

        if (!interop::thiscall<int, MarniSurface2*, int>((uintptr_t)self->vtbl->pal_lock, self, 0)
            || !interop::thiscall<int, MarniSurface2*, int>((uintptr_t)pSrc->vtbl->pal_lock, pSrc, 0))
        {
            out("", "failed to lock. MarniBits::PalBlt");
            return 0;
        }

        if ((paletteSrc == -1 || paletteDst == -1) && self->pal_cnt <= pSrc->pal_cnt)
        {
            // Copy every palette of the source surface onto the matching palette of the destination.
            for (int pal = 0; pal < self->pal_cnt; pal++)
            {
                for (int i = 0; i < rowSize; i++)
                {
                    uint32_t rgb;
                    surface_get_palette_color(pSrc, i, pal, &rgb);
                    surface_set_palette_color(self, i, pal, rgb, 0);
                }
            }
        }
        else
        {
            // Copy a single palette range from the source palette to the destination palette.
            for (int i = 0; i < rowSize; i++)
            {
                uint32_t rgb;
                surface_get_palette_color(pSrc, i, paletteSrc2, &rgb);
                surface_set_palette_color(self, i, paletteDst2, rgb, 0);
            }
        }

        interop::thiscall<void, MarniSurface2*>((uintptr_t)self->vtbl->pal_unlock, self);
        interop::thiscall<void, MarniSurface2*>((uintptr_t)pSrc->vtbl->pal_unlock, pSrc);
        return 1;
    }

    // 0x00416B50
    static int __stdcall texture_node_alloc(Marni* self)
    {
        int index = 1;
        for (auto i = &self->texture_nodes[1];; ++i)
        {
            // A node is free when both its `next` link and `var_14` are zero.
            if (i->var_14 == 0 && i->next == 0)
                break;
            if (++index >= 256)
                return 0;
        }
        memset(&self->texture_nodes[index], 0, sizeof(MarniTextureNode));
        return index;
    }

    // 0x00416C40
    static int __stdcall search_texture_object_1(Marni* self, int count)
    {
        // Find a free texture slot (indices 1..255). The loop stops as soon as
        // a slot whose `var_00` flag is clear is found, otherwise 0 is returned.
        int v3 = 1;
        for (auto i = &self->textures[1]; i->var_00; ++i)
        {
            if (++v3 >= 256)
                return 0;
        }

        auto& texture = self->textures[v3];
        // Reset the slot's surface so it can be re-created from scratch.
        surface_release(&texture.surface);

        // Temporary texture object (count == 0): no texture nodes are allocated.
        if (!count)
        {
            texture.head = 0;
            return v3;
        }

        // Allocate the head texture node for this texture object.
        auto v7 = texture_node_alloc(self);
        texture.head = (uint16_t)v7;
        if (!v7)
        {
            out("there is no available work of texture object level0. Marni::SearchTextureObject1",
                "Marni::SearchTextureObject1");
            return 0;
        }

        // Build a linked list of texture nodes starting from `v7`; the last
        // node terminates the list with `next == 0`.
        int v10;
        int v8 = 0;
        int v9 = v7;
        if (count > 0)
        {
            while (1)
            {
                v10 = texture_node_alloc(self);
                if (!v10)
                {
                    out("there is no available work of texture object level0. Marni::SearchTextureObject1",
                        "Marni::SearchTextureObject1");
                    return 0;
                }
                self->texture_nodes[v9].next = (uint16_t)v10;
                v9 = v10;
                ++v8;
                self->texture_nodes[v10].next = 0xFFFF;
                if (v8 >= count)
                    break;
            }
        }
        else
        {
            v10 = count;
        }
        self->texture_nodes[v10].next = 0;
        return v3;
    }

    // 0x00405EC0
    int __stdcall create_texture_handle(Marni* self, MarniSurface2* pSrcSurface, uint32_t mode)
    {
        if (!self->is_gpu_active)
            return 0;

        if (!pSrcSurface->bOpen)
        {
            out("invalid bits specified. Marni::CreateTextureHandle", "Marni::CreateTextureHandle");
            return 0;
        }

        // Route the upload straight to the SDL renderer: build a marni::Image
        // from the surface (pixels = pitch*height bytes, palette copied as-is,
        // PSX 555 layout) and let the renderer own the GPU texture.
        marni::Image image;
        image.width = pSrcSurface->width;
        image.height = pSrcSurface->height;
        image.depth = pSrcSurface->bpp;
        image.palBpp = pSrcSurface->var_25;
        image.palCnt = pSrcSurface->var_28 != 0 ? pSrcSurface->pal_cnt : 0;
        image.psxFormat = true;
        if (pSrcSurface->pBitmap != nullptr && pSrcSurface->width > 0 && pSrcSurface->height > 0)
        {
            auto pitch = pSrcSurface->pitch > 0 ? pSrcSurface->pitch : pSrcSurface->width;
            auto* src = (const uint8_t*)pSrcSurface->pBitmap;
            image.pixels.assign(src, src + (size_t)pitch * pSrcSurface->height);
        }
        if (pSrcSurface->pPalette != nullptr && image.palCnt > 0)
        {
            auto palBytes = (size_t)image.palCnt * (1 << image.depth) * (image.palBpp == 32 ? 4 : 2);
            auto* src = (const uint8_t*)pSrcSurface->pPalette;
            image.palette.assign(src, src + palBytes);
        }
        logging::logInfo(
            "[marni] create_texture_handle: {}x{} depth={} palCnt={} mode={:#x} -> g_renderer->loadTexture",
            image.width,
            image.height,
            image.depth,
            image.palCnt,
            mode);
        if (!g_renderer)
            return 0;
        auto handle = g_renderer->loadTexture(image, mode);
        if (handle <= 0)
            out("failed to generate the texture Marni::CreateTextureHandle", "Marni::CreateTextureHandle");
        return handle;

        if (!pSrcSurface->bOpen)
        {
            out("invalid bits specified. Marni::CreateTextureHandle", "Marni::CreateTextureHandle");
            return 0;
        }

        auto gpu_flg = self->gpu_flag;
        if ((gpu_flg & GpuFlags::SOFTWARE_GPU) != 0 || (mode & 0x4000) != 0)
        {
            // Temporary texture object: allocate the slot and copy the source
            // surface into it without creating a GPU texture (no reload).
            auto texture_id = search_texture_object_1(self, 0);
            if (!texture_id)
            {
                out("failed to allocate on not enough memory. Marni::CreateTextureHandle", "Marni::CreateTextureHandle");
                return 0;
            }

            auto& texture = self->textures[texture_id];
            if (pSrcSurface->var_28) // Is_paletted
            {
                surface2_create_work(
                    &texture.surface, pSrcSurface->width, pSrcSurface->height, 8, self->bpp, pSrcSurface->pal_cnt);
                texture.surface.desc = self->surface0.desc;
                surface_blt(&texture.surface, nullptr, nullptr, pSrcSurface, 0, 0);
                surface_pal_blt(&texture.surface, pSrcSurface, -1, -1);
            }
            else
            {
                surface2_create_work(&texture.surface, pSrcSurface->width, pSrcSurface->height, self->bpp, 0, -1);
                texture.surface.desc = self->surface0.desc;
                surface_blt(&texture.surface, nullptr, nullptr, pSrcSurface, 0, 0);
            }
            texture.var_00 = mode;
            return texture_id;
        }

        if (pSrcSurface->width > 256 || pSrcSurface->height > 256)
        {
            out("size greater than 256x256 is not supported. Marni::CreateTextureHandle", "Marni::CreateTextureHandle");
            return 0;
        }

        // Number of palette entries the texture object is allocated for.
        int pal_count = 1;
        if ((mode & 0x20) != 0)
        {
            if (pSrcSurface->var_28) // Is_paletted
                pal_count = pSrcSurface->pal_cnt;
            else
                mode &= ~0x20;
        }

        // Adjust the mode for paletted surfaces / GPUs without a 4bpp or 8bpp
        // hardware palette: convert to an RGB texture (clear bit 0x20, set bit
        // 0x40) so the texture can be generated without hardware palette support.
        auto bpp = pSrcSurface->bpp;
        if ((gpu_flg & (GpuFlags::TEXTURE_PAL4 | GpuFlags::TEXTURE_PAL8)) == 0 && (mode & 0x20) != 0)
        {
            mode = (mode & ~0x20) | 0x40;
        }
        else if (
            pSrcSurface->var_28
            && ((
                (bpp == 4 && ((gpu_flg & GpuFlags::TEXTURE_PAL8) != 0 || (gpu_flg & GpuFlags::TEXTURE_PAL4) != 0))
                || (bpp == 8 && (gpu_flg & GpuFlags::TEXTURE_PAL8) != 0))))
        {
            mode |= 0x80;
            if ((gpu_flg & 0x100) == 0)
                mode = (mode & ~0x20) | 0x40;
        }

        int texture_id = 0;
        switch (mode & 0xFFFFDFEB)
        {
        case 1:
        case 2:
            texture_id = search_texture_object_1(self, 1);
            if (!texture_id)
                goto alloc_failed;
            {
                auto& texture = self->textures[texture_id];
                surface2_create_work(
                    &texture.surface, pSrcSurface->width, pSrcSurface->height, pSrcSurface->bpp, pSrcSurface->var_25, -1);
                surface_blt(&texture.surface, nullptr, nullptr, pSrcSurface, 0, 0);
                texture.var_36 = 1;
            }
            break;
        case 0x22:
        case 0x41:
        case 0x42:
        case 0xC1:
        case 0xC2:
            texture_id = search_texture_object_1(self, pal_count);
            if (!texture_id)
                goto alloc_failed;
            {
                auto& texture = self->textures[texture_id];
                surface2_create_work(
                    &texture.surface,
                    pSrcSurface->width,
                    pSrcSurface->height,
                    pSrcSurface->bpp,
                    pSrcSurface->var_25,
                    pal_count);
                surface_blt(&texture.surface, nullptr, nullptr, pSrcSurface, 0, 0);
                surface_pal_blt(&texture.surface, pSrcSurface, -1, -1);
                texture.var_36 = (uint16_t)pal_count;
            }
            break;
        case 0x81:
        case 0x82:
        case 0xA1:
        case 0xA2:
            texture_id = search_texture_object_1(self, 1);
            if (!texture_id)
                goto alloc_failed;
            {
                auto& texture = self->textures[texture_id];
                surface2_create_work(
                    &texture.surface,
                    pSrcSurface->width,
                    pSrcSurface->height,
                    pSrcSurface->bpp,
                    pSrcSurface->var_25,
                    pal_count);
                surface_blt(&texture.surface, nullptr, nullptr, pSrcSurface, 0, 0);
                surface_pal_blt(&texture.surface, pSrcSurface, -1, -1);
                texture.var_36 = 1;
            }
            break;
        default: out("not supported type...0x%08x Marni::CreateTextureHandle", "Marni::CreateTextureHandle"); return 0;
        }

        self->textures[texture_id].var_00 = mode;
        if ((mode & 0x2000) != 0)
            return texture_id;

        if (!reload_texture(self, texture_id))
        {
            out("failed to generate the texutre Marni::CreateTextureHandle", "Marni::CreateTextureHandle");
            texture_surface_release(self, texture_id);
            return 0;
        }

        request_video_memory(self);
        return texture_id;

    alloc_failed:
        out("failed to allocate on not enough memory. Marni::CreateTextureHandle", "Marni::CreateTextureHandle");
        return 0;
    }

    // 0x004063D0
    long __stdcall message(Marni* self, void* hWnd, uint32_t msg, void* wParam, void* lParam)
    {
        switch (msg)
        {
        case WM_MOVE: move(self); break;
        case WM_SIZE: resize(self, (uintptr_t)wParam); break;
        case WM_DESTROY: destroy(self); break;
        case WM_SYSKEYDOWN:
            if ((self->gpu_flag & GpuFlags::GPU_FULLSCREEN) != 0)
            {
                syskeydown(self);
            }
            break;
        }
        (void)hWnd;
        (void)lParam;
        return 1;
    }

    // 0x00406450
    static void __stdcall move(Marni* marni)
    {
        if (marni->gpu_flag & GpuFlags::GPU_FULLSCREEN)
        {
            // Borderless fullscreen moves the window to the display origin; keep
            // the letterboxed presentation rect instead of the full client area.
            compute_fullscreen_window_rect(marni);
            return;
        }
        // Windowed: recompute the letterboxed presentation rect for the new
        // window position (the rect is in screen coordinates, so it shifts with
        // the client origin).
        compute_windowed_window_rect((HWND)marni->hWnd, marni->xsize, marni->ysize, (LPRECT)&marni->window_rect);
    }

    // 0x004064D0
    static void __stdcall destroy(Marni* marni)
    {
        marni->gpu_flag &= ~GpuFlags::GPU_ENABLED;

        clear_buffers(marni);

        for (auto i = 0; i < 256; i++)
            unload_texture(marni, i);

        marni->hWnd = nullptr;

        surface_release(&marni->surface0);
        surface_release(&marni->surface2);

        movie_release(marni->pMovie);
    }

    // 0x004065C0
    static int __stdcall resize(Marni* marni, uintptr_t wParam)
    {
        if (!marni->is_gpu_active)
            return 1;

        if (wParam == 1)
        {
            marni->var_8C7EE0 = 1;
            sub_401F00(marni);
            return 1;
        }

        auto gpu_flg = marni->gpu_flag;
        if (gpu_flg & GpuFlags::GPU_FULLSCREEN)
        {
            // Borderless fullscreen: the SDL3 window already covers the display,
            // so just recompute the letterboxed presentation rect.
            marni->is_gpu_busy = 1;
            compute_fullscreen_window_rect(marni);
            marni->is_gpu_busy = 0;

            restore_surfaces(marni);
            marni->var_8C7EE0 = 0;
            surface_fill(&marni->surface2, 0, 0, 0);
            marni->var_8C8318 = 0;
            return 1;
        }

        if (marni->var_8C7EE0)
        {
            marni->var_8C7EE0 = 0;
            prepare_movie(marni);
            return 1;
        }

        marni->is_gpu_busy = 1;
        if (!(gpu_flg & GpuFlags::GPU_ENABLED))
            return 0;

        clear_buffers(marni);
        auto result = init_all(marni);
        if (result)
        {
            restore_surfaces(marni);
            marni->is_gpu_busy = 0;
            marni->gpu_flag |= GpuFlags::GPU_ENABLED;
            return 1;
        }
        return result;
    }

    // 0x00406860

    // 0x00406A10
    static int error_routine(int errorCode)
    {
        // No GPU error handler in the SDL-only build; neutralize original calls.
        (void)errorCode;
        return 0;
    }

    // 0x00406D90
    static int __stdcall create_device(Marni* self)
    {
        (void)self;
        return 1;
    }

    // 0x00407020

    // 0x00407290
    static int __stdcall enum_drivers(Marni* self)
    {
        (void)self;
        return 1;
    }

    // 0x00407440
    static int __stdcall create_gpu(Marni* self)
    {
        (void)self;
        return 0;
    }

    // 0x00407480
    static void __stdcall sub_407480(Marni* self, Prim* pOt)
    {
        memcpy(&self->field_8C7E10, (uint8_t*)pOt + 8, 0x40);
        memcpy(&self->field_8C7E50, (uint8_t*)pOt + 72, 0x40);
    }

    // 0x004074C0
    static int __stdcall trans_matrix(Marni* self, Prim* pPrim)
    {
        auto pScaler = (PrimScaler*)pPrim;

        // 0x2000: copy the scale factors into the aspect ratio
        if ((pScaler->type & 0x2000) != 0)
        {
            self->aspect_x = pScaler->rate_x;
            self->aspect_y = pScaler->rate_y;
        }

        // 0x4000: scale the primitive size by the aspect ratio
        if ((pScaler->type & 0x4000) != 0)
        {
            self->xsize = (int)((float)(int32_t)pScaler->var_2C * self->aspect_x);
            self->ysize = (int)((float)(int32_t)pScaler->var_30 * self->aspect_y);
        }

        // 0x800: projection
        if ((pScaler->type & 0x800) != 0)
            self->field_8C7EDC = pScaler->prj;

        // 0x400: centre point, offset from the middle of the render target
        if ((pScaler->type & 0x400) != 0)
        {
            self->field_8C7EC4 = pScaler->c_x;
            self->field_8C7EC8 = pScaler->c_y;
            *(int32_t*)&self->pad_8C7ECC[0] = pScaler->c_x - self->render_w / 2;
            *(int32_t*)&self->pad_8C7ECC[4] = pScaler->c_y - self->render_h / 2;
        }

        // 0x200: colour
        if ((pScaler->type & 0x200) != 0)
            self->field_8C7E90 = pScaler->rgb0;

        // 0x1000: ambient colour - write the RGBA dword across the four channel
        // fields (memory order is B, G, R, A) and refresh the viewport background
        // material unless the GPU is in software mode (SOFTWARE_GPU).
        if ((pScaler->type & 0x1000) != 0)
        {
            *(uint32_t*)&self->ambient_b = pScaler->rgb1;
        }

        return 1;
    }
    // 0x00411630
    static void apply_matrix_float(float* vec, const float* mat)
    {
        const float v0 = vec[0];
        const float v1 = vec[1];
        const float v2 = vec[2];
        vec[0] = v0 * mat[0] + v1 * mat[1] + v2 * mat[2];
        vec[1] = v0 * mat[4] + v1 * mat[5] + v2 * mat[6];
        vec[2] = v0 * mat[8] + v1 * mat[9] + v2 * mat[10];
    }

    // MARNI_POLY_OBJECT: the polygon object used by both PolygonObject
    // (the Marni::polygons array) and the local 0x58-byte TMD loader buffers.
    // Dword fields match MarniPolygonObject::ctor/CreateWork layout.
    struct MarniPolyObject
    {
        void* vTbl;           // +0x00
        uint8_t* vertices;    // +0x04
        uint8_t* normals;     // +0x08
        uint8_t* primitives;  // +0x0C
        uint32_t magic;       // +0x10
        uint32_t pad_14;      // +0x14
        uint32_t vertexCount; // +0x18
        uint32_t pad_1C;      // +0x1C
        uint32_t normalCount; // +0x20
        uint32_t pad_24;      // +0x24
        uint32_t primCount;   // +0x28
        uint32_t pad_2C;      // +0x2C
        uint32_t type;        // +0x30
        uint32_t flags;       // +0x34
        uint8_t pad_38[0x20]; // +0x38
    };
    static_assert(sizeof(MarniPolyObject) == 0x58);

    // 0x00416490
    static MarniPolyObject* polygon_object_ctor(MarniPolyObject* self)
    {
        self->vTbl = (void*)0x5173F8;
        memset((uint8_t*)self + 0x10, 0, 0x24);
        self->primitives = nullptr;
        self->normals = nullptr;
        self->vertices = nullptr;
        self->flags = 0;
        return self;
    }

    // 0x004161F0
    static int polygon_object_dtor_0(MarniPolyObject* self)
    {
        operator_delete(self->vertices);
        self->primitives = nullptr;
        self->normals = nullptr;
        self->vertices = nullptr;
        self->flags = 0;
        memset((uint8_t*)self + 0x10, 0, 0x24);
        return 1;
    }

    // 0x00416280
    static int create_work(MarniPolyObject* self, int vertexCount, int normalCount, int primCount, int type)
    {
        polygon_object_dtor_0(self);

        self->vertexCount = (uint32_t)vertexCount;
        self->normalCount = (uint32_t)normalCount;
        self->primCount = (uint32_t)primCount;
        self->type = (uint32_t)type;

        uint32_t maskType = (uint32_t)type & 0xFF801FFF;
        uint8_t* buffer;

        if (maskType > 0x10014C0)
        {
            if (maskType > 0x1800400)
            {
                if (maskType != 0x1800401)
                {
                    out("", "");
                    return 0;
                }
                buffer = (uint8_t*)operator_new(12 * (primCount + vertexCount));
                self->normalCount = (uint32_t)vertexCount;
                self->vertices = buffer;
                self->normals = nullptr;
                self->primitives = buffer + 12 * vertexCount;
            }
            else if (maskType == 0x1800400)
            {
                buffer = (uint8_t*)operator_new(12 * vertexCount + 16 * primCount);
                self->vertices = buffer;
                self->normals = nullptr;
                self->normalCount = (uint32_t)vertexCount;
                self->primitives = buffer + 12 * vertexCount;
            }
            else
            {
                uint32_t v13 = maskType - 0x1800080;
                if (v13 == 0)
                {
                    buffer = (uint8_t*)operator_new(6 * (normalCount + vertexCount + 4 * primCount));
                    self->vertices = buffer;
                    self->normals = buffer + 6 * vertexCount;
                    self->primitives = buffer + 6 * (vertexCount + normalCount);
                }
                else if (v13 == 1)
                {
                    buffer = (uint8_t*)operator_new(6 * (vertexCount + normalCount + 3 * primCount));
                    self->vertices = buffer;
                    self->normals = buffer + 6 * vertexCount;
                    self->primitives = buffer + 6 * (vertexCount + normalCount);
                }
                else
                {
                    out("", "");
                    return 0;
                }
            }
        }
        else if (maskType == 0x10014C0)
        {
            buffer = (uint8_t*)operator_new(8 * (primCount + 4 * vertexCount));
            self->normalCount = (uint32_t)vertexCount;
            self->vertices = buffer;
            self->normals = nullptr;
            self->primitives = buffer + 32 * vertexCount;
        }
        else if (maskType > 0x1442)
        {
            if (maskType != 0x800400)
            {
                out("", "");
                return 0;
            }
            buffer = (uint8_t*)operator_new(12 * vertexCount + 40 * primCount);
            self->vertices = buffer;
            self->normals = nullptr;
            self->primitives = buffer + 12 * vertexCount;
        }
        else
        {
            if (maskType == 0x1442)
            {
                buffer = (uint8_t*)operator_new(14 * primCount + 12 * (vertexCount + normalCount));
                self->vertices = buffer;
                self->normals = buffer + 12 * vertexCount;
                self->primitives = buffer + 12 * (vertexCount + normalCount);
            }
            else
            {
                uint32_t v7 = maskType - 0x402;
                if (v7 == 0)
                    buffer = (uint8_t*)operator_new(12 * (primCount + vertexCount));
                else if (v7 == 2)
                    buffer = (uint8_t*)operator_new(12 * (vertexCount + 2 * primCount));
                else
                {
                    out("", "");
                    return 0;
                }
                self->vertices = buffer;
                self->normals = nullptr;
                self->primitives = buffer + 12 * vertexCount;
            }
        }

        self->magic = 1129270349;
        self->flags = 1;
        return 1;
    }

    // 0x00416220
    static int create_work_0(
        MarniPolyObject* self, int vertexCount, int normalCount, int primCount, int a5, int a6, int a7, int a8, int a9, int a10,
        int a11, int a12)
    {
        return create_work(
            self,
            vertexCount,
            normalCount,
            primCount,
            a5 | (2 * (a6 | (32 * (a8 | (8 * (a9 | (4 * (a11 | (4 * (a10 | (32 * (a12 | (32 * a7))))))))))))));
    }

    // 0x004153E0
    static int polygon_object_operator_eq(MarniPolyObject* self, const MarniPolyObject* other)
    {
        polygon_object_dtor_0(self);
        create_work(self, (int)other->vertexCount, (int)other->normalCount, (int)other->primCount, (int)other->type);

        uint32_t type = self->type & 0xFF801FFF;
        uint32_t size;
        switch (type)
        {
        case 0x1800400:
            memcpy(self->vertices, other->vertices, 12 * self->vertexCount);
            size = 16 * self->primCount;
            break;
        case 0x1800401:
            memcpy(self->vertices, other->vertices, 12 * self->vertexCount);
            size = 12 * self->primCount;
            break;
        case 0x1800080:
            memcpy(self->vertices, other->vertices, 6 * self->vertexCount);
            memcpy(self->normals, other->normals, 6 * self->normalCount);
            size = 24 * self->primCount;
            break;
        case 0x1800081:
            memcpy(self->vertices, other->vertices, 6 * self->vertexCount);
            memcpy(self->normals, other->normals, 6 * self->normalCount);
            size = 18 * self->primCount;
            break;
        case 0x10014C0:
            memcpy(self->vertices, other->vertices, 32 * self->vertexCount);
            size = 8 * self->primCount;
            break;
        case 0x800400:
            memcpy(self->vertices, other->vertices, 12 * self->vertexCount);
            self->normals = nullptr;
            size = 40 * self->primCount;
            break;
        case 0x1442:
            memcpy(self->vertices, other->vertices, 12 * self->vertexCount);
            memcpy(self->normals, other->normals, 12 * self->normalCount);
            size = 14 * self->primCount;
            break;
        case 0x402:
            memcpy(self->vertices, other->vertices, 12 * self->vertexCount);
            self->normals = nullptr;
            size = 12 * self->primCount;
            break;
        case 0x404:
            memcpy(self->vertices, other->vertices, 12 * self->vertexCount);
            self->normals = nullptr;
            size = 24 * self->primCount;
            break;
        default: out("", ""); return 0;
        }

        memcpy(self->primitives, other->primitives, size);
        return 1;
    }

    // 0x00416050
    static int modify_vertex(MarniPolyObject* self, int index, float x, float y, float z)
    {
        if ((self->flags & 1) == 0 || index >= (int)self->vertexCount)
        {
            out("", "");
            return 0;
        }

        uint32_t type = self->type & 0xFF801FFF;
        switch (type)
        {
        case 0x800400:
        case 0x402:
        case 0x404:
        case 0x1442:
        case 0x1800400:
        case 0x1800401:
            *(float*)(self->vertices + 12 * index) = x;
            *(float*)(self->vertices + 12 * index + 4) = y;
            *(float*)(self->vertices + 12 * index + 8) = z;
            return 1;
        case 0x10014C0:
            *(float*)(self->vertices + 32 * index) = x;
            *(float*)(self->vertices + 32 * index + 4) = y;
            *(float*)(self->vertices + 32 * index + 8) = z;
            return 1;
        case 0x1800080:
        case 0x1800081:
        {
            int scale = 1 << ((self->type >> 13) & 0x1F);
            *(int16_t*)(self->vertices + 6 * index) = (int16_t)(scale * x);
            *(int16_t*)(self->vertices + 6 * index + 2) = (int16_t)(scale * y);
            *(int16_t*)(self->vertices + 6 * index + 4) = (int16_t)(scale * z);
            return 1;
        }
        default: out("", ""); return 0;
        }
    }

    // 0x00415CC0
    static int modify_normal(MarniPolyObject* self, int index, float x, float y, float z)
    {
        if ((self->flags & 1) != 0 && index < (int)self->normalCount)
        {
            uint32_t type = self->type & 0xFF801FFF;
            switch (type)
            {
            case 0x402:
            case 0x404:
            case 0x800400:
            case 0x1800400:
            case 0x1800401: out("", ""); return 1;
            case 0x1442:
                *(float*)(self->normals + 12 * index) = x;
                *(float*)(self->normals + 12 * index + 4) = y;
                *(float*)(self->normals + 12 * index + 8) = z;
                return 1;
            case 0x10014C0:
                *(float*)(self->vertices + 32 * index + 12) = x;
                *(float*)(self->vertices + 32 * index + 16) = y;
                *(float*)(self->vertices + 32 * index + 20) = z;
                return 1;
            case 0x1800080:
            case 0x1800081:
            {
                int scale = 1 << ((self->type >> 18) & 0x1F);
                *(int16_t*)(self->normals + 6 * index) = (int16_t)(scale * x);
                *(int16_t*)(self->normals + 6 * index + 2) = (int16_t)(scale * y);
                *(int16_t*)(self->normals + 6 * index + 4) = (int16_t)(scale * z);
                return 1;
            }
            default: out("", ""); return 0;
            }
        }

        out("", "");
        return 0;
    }

    // 0x00416030
    static int modify_vertex_0(MarniPolyObject* self, int index, float* vec)
    {
        return modify_vertex(self, index, vec[0], vec[1], vec[2]);
    }

    // 0x00415CA0
    static int modify_normal_0(MarniPolyObject* self, int index, float* vec)
    {
        return modify_normal(self, index, vec[0], vec[1], vec[2]);
    }

    // 0x00415E80
    static int __stdcall refer_vertex(PolygonObject* self, int index, float* dst)
    {
        auto* s = (MarniPolyObject*)self;
        if ((s->flags & 1) == 0 || index >= (int)s->vertexCount)
        {
            out("", "");
            return 0;
        }

        uint32_t type = s->type & 0xFF801FFF;
        switch (type)
        {
        case 0x800400:
        case 0x402:
        case 0x404:
        case 0x1442:
        case 0x1800400:
        case 0x1800401:
            dst[0] = *(float*)(s->vertices + 12 * index);
            dst[1] = *(float*)(s->vertices + 12 * index + 4);
            dst[2] = *(float*)(s->vertices + 12 * index + 8);
            return 1;
        case 0x10014C0:
            dst[0] = *(float*)(s->vertices + 32 * index);
            dst[1] = *(float*)(s->vertices + 32 * index + 4);
            dst[2] = *(float*)(s->vertices + 32 * index + 8);
            return 1;
        case 0x1800080:
        case 0x1800081:
        {
            double scale = (double)(1 << ((s->type >> 13) & 0x1F));
            dst[0] = (float)((double)*(int16_t*)(s->vertices + 6 * index) / scale);
            dst[1] = (float)((double)*(int16_t*)(s->vertices + 6 * index + 2) / scale);
            dst[2] = (float)((double)*(int16_t*)(s->vertices + 6 * index + 4) / scale);
            return 1;
        }
        default: out("", ""); return 0;
        }
    }

    // 0x00415AE0
    static int __stdcall refer_normal(PolygonObject* self, int index, float* dst)
    {
        auto* s = (MarniPolyObject*)self;
        if ((s->flags & 1) != 0 && index < (int)s->normalCount)
        {
            uint32_t type = s->type & 0xFF801FFF;
            switch (type)
            {
            case 0x402:
            case 0x404:
            case 0x800400:
            case 0x1800400:
            case 0x1800401: out("", ""); return 1;
            case 0x1442:
                dst[0] = *(float*)(s->normals + 12 * index);
                dst[1] = *(float*)(s->normals + 12 * index + 4);
                dst[2] = *(float*)(s->normals + 12 * index + 8);
                return 1;
            case 0x10014C0:
                dst[0] = *(float*)(s->vertices + 32 * index + 12);
                dst[1] = *(float*)(s->vertices + 32 * index + 16);
                dst[2] = *(float*)(s->vertices + 32 * index + 20);
                return 1;
            case 0x1800080:
            case 0x1800081:
            {
                double scale = (double)(1 << ((s->type >> 18) & 0x1F));
                dst[0] = (float)((double)*(int16_t*)(s->normals + 6 * index) / scale);
                dst[1] = (float)((double)*(int16_t*)(s->normals + 6 * index + 2) / scale);
                dst[2] = (float)((double)*(int16_t*)(s->normals + 6 * index + 4) / scale);
                return 1;
            }
            default: out("", ""); return 0;
            }
        }

        out("", "");
        return 0;
    }

    // 0x004156E0
    static int __stdcall modify_primitive(PolygonObject* self, int index, void* dst)
    {
        auto* s = (MarniPolyObject*)self;
        if ((s->flags & 1) == 0 || index >= (int)s->primCount)
        {
            out("", "");
            return 0;
        }

        uint32_t type = s->type & 0xFF801FFF;
        switch (type)
        {
        case 0x402:
        case 0x1800401:
            ((uint32_t*)dst)[0] = *(uint32_t*)(s->primitives + 12 * index);
            ((uint32_t*)dst)[1] = *(uint32_t*)(s->primitives + 12 * index + 4);
            ((uint32_t*)dst)[2] = *(uint32_t*)(s->primitives + 12 * index + 8);
            return 1;
        case 0x404: memcpy(dst, s->primitives + 24 * index, 0x18); return 1;
        case 0x1442:
            ((uint32_t*)dst)[0] = *(uint32_t*)(s->primitives + 14 * index);
            ((uint32_t*)dst)[1] = *(uint32_t*)(s->primitives + 14 * index + 4);
            ((uint32_t*)dst)[2] = *(uint32_t*)(s->primitives + 14 * index + 8);
            *(uint16_t*)((uint8_t*)dst + 12) = *(uint16_t*)(s->primitives + 14 * index + 12);
            return 1;
        case 0x10014C0:
            ((uint32_t*)dst)[0] = *(uint32_t*)(s->primitives + 8 * index);
            ((uint32_t*)dst)[1] = *(uint32_t*)(s->primitives + 8 * index + 4);
            return 1;
        case 0x800400: memcpy(dst, s->primitives + 40 * index, 0x28); return 1;
        case 0x1800400:
            ((uint32_t*)dst)[0] = *(uint32_t*)(s->primitives + 16 * index);
            ((uint32_t*)dst)[1] = *(uint32_t*)(s->primitives + 16 * index + 4);
            ((uint32_t*)dst)[2] = *(uint32_t*)(s->primitives + 16 * index + 8);
            ((uint32_t*)dst)[3] = *(uint32_t*)(s->primitives + 16 * index + 12);
            return 1;
        case 0x1800080: memcpy(dst, s->primitives + 24 * index, 0x18); return 1;
        case 0x1800081:
            ((uint32_t*)dst)[0] = *(uint32_t*)(s->primitives + 18 * index);
            ((uint32_t*)dst)[1] = *(uint32_t*)(s->primitives + 18 * index + 4);
            ((uint32_t*)dst)[2] = *(uint32_t*)(s->primitives + 18 * index + 8);
            ((uint32_t*)dst)[3] = *(uint32_t*)(s->primitives + 18 * index + 12);
            *(uint16_t*)((uint8_t*)dst + 16) = *(uint16_t*)(s->primitives + 18 * index + 16);
            return 1;
        default: out("", ""); return 0;
        }
    }

    // 0x004158E0
    static int refer_primitive(MarniPolyObject* self, int index, void* dst)
    {
        if ((self->flags & 1) == 0 || index >= (int)self->primCount)
        {
            out("", "");
            return 0;
        }

        uint32_t type = self->type & 0xFF801FFF;
        switch (type)
        {
        case 0x402:
        case 0x1800401:
            *(uint32_t*)(self->primitives + 12 * index) = ((uint32_t*)dst)[0];
            *(uint32_t*)(self->primitives + 12 * index + 4) = ((uint32_t*)dst)[1];
            *(uint32_t*)(self->primitives + 12 * index + 8) = ((uint32_t*)dst)[2];
            return 1;
        case 0x404:
        case 0x1800080: memcpy(self->primitives + 24 * index, dst, 0x18); return 1;
        case 0x1442:
            *(uint32_t*)(self->primitives + 14 * index) = ((uint32_t*)dst)[0];
            *(uint32_t*)(self->primitives + 14 * index + 4) = ((uint32_t*)dst)[1];
            *(uint32_t*)(self->primitives + 14 * index + 8) = ((uint32_t*)dst)[2];
            *(uint16_t*)(self->primitives + 14 * index + 12) = *(uint16_t*)((uint8_t*)dst + 12);
            return 1;
        case 0x10014C0:
            *(uint32_t*)(self->primitives + 8 * index) = ((uint32_t*)dst)[0];
            *(uint32_t*)(self->primitives + 8 * index + 4) = ((uint32_t*)dst)[1];
            return 1;
        case 0x800400: memcpy(self->primitives + 40 * index, dst, 0x28); return 1;
        case 0x1800400:
            *(uint32_t*)(self->primitives + 16 * index) = ((uint32_t*)dst)[0];
            *(uint32_t*)(self->primitives + 16 * index + 4) = ((uint32_t*)dst)[1];
            *(uint32_t*)(self->primitives + 16 * index + 8) = ((uint32_t*)dst)[2];
            *(uint32_t*)(self->primitives + 16 * index + 12) = ((uint32_t*)dst)[3];
            return 1;
        case 0x1800081:
            *(uint32_t*)(self->primitives + 18 * index) = ((uint32_t*)dst)[0];
            *(uint32_t*)(self->primitives + 18 * index + 4) = ((uint32_t*)dst)[1];
            *(uint32_t*)(self->primitives + 18 * index + 8) = ((uint32_t*)dst)[2];
            *(uint32_t*)(self->primitives + 18 * index + 12) = ((uint32_t*)dst)[3];
            *(uint16_t*)(self->primitives + 18 * index + 16) = *(uint16_t*)((uint8_t*)dst + 16);
            return 1;
        default: out("", ""); return 0;
        }
    }

    // 0x00430F20
    static int marni_poly_object_reset(MarniPolyObject* self)
    {
        *(uint32_t*)((uint8_t*)self + 80) = 0;
        memset((uint8_t*)self + 56, 0, 0x18);
        return polygon_object_dtor_0(self);
    }

    // TMDFile: header/state for a TMD data source. Layout matches
    // MarniPolygonObject::Open's stack TMDFile (constructed via tmd_file_ctor).
    struct TmdFile
    {
        uint32_t flag;  // +0x00 byte, 1 = valid
        char* filename; // +0x04
        uint8_t* data;  // +0x08
    };
    static_assert(sizeof(TmdFile) == 0x0C);

    // 0x00430880
    static int tmd_file_dtor(TmdFile* self)
    {
        operator_delete(self->filename);
        self->filename = nullptr;
        operator_delete(self->data);
        self->data = nullptr;
        self->flag = 1;
        return 1;
    }

    // 0x004308B0
    // Loads a TMD file into the object. Returns 1 whether or not the file could
    // be opened (mirroring the original). The data buffer is allocated with the
    // original 32-bit-aligned + 4 pad sizing (4 * (size / 4) + 4).
    static int tmd_file_create(TmdFile* self, const char* filename)
    {
        tmd_file_dtor(self);

        auto data = system::fs::readAllBytes((std::string("data://") + filename).c_str());
        if (data.empty())
        {
            out("failed to open the file. TMDFile::Create", "");
            return 1;
        }

        self->data = (uint8_t*)operator_new((size_t)(4 * ((int)data.size() / 4) + 4));
        std::memcpy(self->data, data.data(), data.size());

        operator_delete(self->filename);
        self->filename = (char*)operator_new(std::strlen(filename) + 1);
        std::memcpy(self->filename, filename, std::strlen(filename) + 1);

        self->flag = 1;
        return 1;
    }

    // 0x00430970
    static TmdFile* tmd_file_ctor(TmdFile* self, const char* filename)
    {
        self->flag = 1;
        self->filename = nullptr;
        self->data = nullptr;
        if (filename)
            tmd_file_create(self, filename);
        return self;
    }

    // 0x004309A0
    static int sub_4309A0(void* self)
    {
        return tmd_file_dtor((TmdFile*)self);
    }

    // Per-primitive record layout for refer_primitive/modify_primitive. The
    // 18-byte record must be contiguous in memory (the original keeps the
    // equivalent locals in one stack block and refer_primitive copies the whole
    // record).
    struct PrimRecord
    {
        uint16_t v0; // +0x00
        uint16_t v1; // +0x02
        uint16_t v2; // +0x04
        uint16_t n0; // +0x06
        uint16_t n1; // +0x08
        uint16_t n2; // +0x0A
        uint8_t u0;  // +0x0C
        uint8_t u1;  // +0x0D
        uint8_t u2;  // +0x0E
        uint8_t u3;  // +0x0F
        uint8_t u4;  // +0x10
        uint8_t u5;  // +0x11
    };
    static_assert(sizeof(PrimRecord) == 0x12);

    // 0x00430750
    static uint32_t tmd_counts_objects(TmdFile* self, uint32_t* data)
    {
        if (!data)
        {
            if (!self->flag)
            {
                out("you can't specify NULL pointer in this condition of the class. TMDFile::CountsObjects", "");
                return 0;
            }
            data = (uint32_t*)self->data;
        }
        if (*data != 65)
        {
            out("invalid header for TMD. TMDFile::CountsObjects", "");
            return 0;
        }
        return data[2];
    }

    // 0x00430590
    static int tmd_counts_variation(TmdFile* self, int a2, int a3, int a4, int a5)
    {
        uint8_t* data;
        uint8_t* prim;
        uint8_t* outPtr;
        int variationCount;
        int index;
        uint32_t* record;
        int counter;
        uint8_t* object;

        data = (uint8_t*)a5;
        if (!a5)
        {
            if (!(uint8_t)self->flag)
            {
                out("you can't specify NULL pointer in this condition of the class.", "TMDFile::CountsVariation");
                return 0;
            }
            data = self->data;
        }
        if (*(uint32_t*)data != 65)
        {
            out("invalid header for TMD.", "TMDFile::CountsVariation");
            return 0;
        }
        if ((int32_t)*(uint32_t*)(data + 8) <= a2)
        {
            out("the object you specified is invalid. max...%d ap...%d", "TMDFile::CountsVariation");
            return 0;
        }
        object = data + 28 * a2 + 12;
        if ((*(uint8_t*)(data + 4) & 1) != 0)
            prim = *(uint8_t**)(data + 28 * a2 + 28);
        else
            prim = data + 4 * ((uint32_t)(*(uint32_t*)(data + 28 * a2 + 28) + 12) >> 2);
        variationCount = 0;
        counter = 0;
        memset((void*)a3, 0, 4 * ((uint32_t)(12 * a4) >> 2));
        if (*(int32_t*)(object + 20) <= 0)
            return variationCount;
        outPtr = (uint8_t*)a3 + 4;
        while (1)
        {
            index = 0;
            if (variationCount > 0)
            {
                record = (uint32_t*)((uint8_t*)a3 + 4);
                while (index < variationCount)
                {
                    if (record[1] == *(uint32_t*)prim)
                    {
                        if ((*(uint32_t*)prim & 0x4000000) == 0)
                            break;
                        if ((*(uint32_t*)(prim + 8) & 0x1800000) != 0x800000)
                        {
                            out("we now don't suppose to support the data except 8bit and 4bit palettized",
                                "TMDFile::CountsVariation");
                            return 0;
                        }
                        if (*record == ((*(uint32_t*)(prim + 4) & 0xFFFF0000) | (*(uint8_t*)(prim + 10) & 0x1F)))
                            break;
                    }
                    ++index;
                    record += 3;
                }
                if (index < variationCount)
                    ++*(uint16_t*)((uint8_t*)a3 + 12 * index);
            }
            if (index == variationCount)
            {
                *(uint16_t*)(outPtr - 4) = 1;
                *(uint32_t*)(outPtr + 4) = *(uint32_t*)prim;
                ++variationCount;
                *(uint32_t*)outPtr = ((*(uint32_t*)(prim + 4) & 0xFFFF0000) | (*(uint8_t*)(prim + 10) & 0x1F));
                outPtr += 12;
                *(uint16_t*)(outPtr - 14) = (uint16_t)(4 * *(uint8_t*)(prim + 1) + 4);
            }
            if (variationCount > a4)
            {
                out("not enough work to continue a process.", "MarniSystem TMDFile::CountsVariation");
                return 0;
            }
            ++counter;
            prim += 4 * *(uint8_t*)(prim + 1) + 4;
            if (counter >= *(int32_t*)(object + 20))
                return variationCount;
        }
    }

    // 0x004307E0
    static uint32_t tmd_counts_vertex(TmdFile* self, int objectIndex, uint32_t* data)
    {
        uint32_t* p = data;
        if (!p)
        {
            if (!self->flag)
            {
                out("you can't specify NULL pointer in this condition of the class. TMDFile::CountsVertex", "");
                return 0;
            }
            p = (uint32_t*)self->data;
        }
        if (*p != 65)
        {
            out("invalid header for TMD. TMDFile::CountsVertex", "");
            return 0;
        }
        return p[7 * objectIndex + 4];
    }

    // 0x00430830
    static int tmd_counts_vertex_0(TmdFile* self, int a2, uint32_t* a3)
    {
        uint32_t* v3 = a3;
        if (!a3)
        {
            if (!(self->flag & 0xFF))
            {
                out("you can't specify NULL pointer in this condition of the class.", "TMDFile::CountsVertex");
                return 0;
            }
            v3 = (uint32_t*)self->data;
        }
        if (*v3 != 65)
        {
            out("invalid header for TMD.", "TMDFile::CountsVertex");
            return 0;
        }
        return (int)v3[7 * a2 + 6];
    }

    // 0x00430790
    static uint32_t tmd_counts_primitive(TmdFile* self, int index, uint32_t* data)
    {
        uint32_t* v3 = data;
        if (!data)
        {
            if (!self->flag)
            {
                out("you can't specify NULL pointer in this condition of the class. TMDFile::CountsPrimitive", "");
                return 0;
            }
            v3 = (uint32_t*)self->data;
        }
        if (*v3 != 65)
        {
            out("invalid header for TMD. TMDFile::CountsPrimitive", "");
            return 0;
        }
        return v3[7 * index + 8];
    }

    // 0x004303C0
    static int tmd_read_tmd(TmdFile* self, int a2, int a3, uint32_t* a4, uint32_t* a5, uint32_t* a6, int a7)
    {
        uint32_t v7;
        uint32_t v9;

        v7 = (uint32_t)a7;
        if (!a7)
        {
            if (!(self->flag & 0xFF))
            {
                out("you can't specify NULL pointer in this condition of the class. TMDFile::GetVertex", "");
                return 0;
            }
            v7 = (uint32_t)self->data;
        }
        if (*(uint32_t*)v7 != 0x41)
        {
            out("invalid header for TMD. TMDFile::GetVertex", "");
            return 0;
        }
        if (*(int32_t*)(v7 + 8) <= a2)
        {
            out("the object you specified is invalid. max...%d ap...%d TMDFile::GetVertex", "");
            return 0;
        }
        if ((*(uint8_t*)(v7 + 4) & 1) != 0)
            v9 = *(uint32_t*)(v7 + 28 * a2 + 12);
        else
            v9 = *(uint32_t*)(v7 + 28 * a2 + 12) + v7 + 12;
        *a4 = *(int16_t*)(v9 + 8 * a3);
        *a5 = *(int16_t*)(v9 + 8 * a3 + 2);
        *a6 = *(int16_t*)(v9 + 8 * a3 + 4);
        return 1;
    }

    // 0x00430310
    static uint32_t tmd_get_vertex(TmdFile* self, int a1, int a2, uint32_t* a3, uint32_t* a4, uint32_t* a5, int a6)
    {
        uint8_t* v7;

        v7 = (uint8_t*)a6;
        if (!a6)
        {
            if (!*(uint8_t*)self)
            {
                out("you can't specify NULL pointer in this condition of the class. TMDFile::GetVertex", "");
                return 0;
            }
            v7 = self->data;
        }

        if (*(uint32_t*)v7 != 65)
        {
            out("invalid header for TMD. TMDFile::GetVertex", "");
            return 0;
        }
        if (*(int32_t*)(v7 + 8) <= a1)
        {
            out("the object you specified is invalid. max...%d ap...%d TMDFile::GetVertex", "");
            return 0;
        }

        uint8_t* v9;
        if ((v7[4] & 1) != 0)
            v9 = (uint8_t*)(uintptr_t)*(uint32_t*)(v7 + 28 * a1 + 20);
        else
            v9 = (uint8_t*)((uintptr_t)*(uint32_t*)(v7 + 28 * a1 + 20) + (uintptr_t)v7 + 12);

        *a3 = (uint32_t)*(int16_t*)(v9 + 8 * a2);
        *a4 = (uint32_t)*(int16_t*)(v9 + 8 * a2 + 2);
        *a5 = (uint32_t)*(int16_t*)(v9 + 8 * a2 + 4);
        return 1;
    }

    // 0x00430470
    static uint32_t tmd_rerieves_primitives(TmdFile* self, uint32_t a1, uint32_t* data, uint32_t a3, uint32_t a4)
    {
        uint8_t* v5 = (uint8_t*)a4;
        if (!a4)
        {
            if (!(self->flag & 0xFF))
            {
                out("you can't specify NULL pointer in this condition of the class. TMDFile::RerievesPrimitives", "");
                return 0;
            }
            v5 = self->data;
        }

        if (*(uint32_t*)v5 != 0x41)
        {
            out("invalid header for TMD. TMDFile::RerievesPrimitives", "");
            return 0;
        }

        if (*(uint32_t*)(v5 + 8) <= a1)
        {
            out("the object you specified is invalid. max...%d ap...%d TMDFile::RerievesPrimitives", "");
            return 0;
        }

        uint8_t* v7 = v5 + 28 * a1 + 12;
        uint32_t* v8;
        if ((*(uint8_t*)(v5 + 4) & 1) != 0)
            v8 = *(uint32_t**)(v5 + 28 * a1 + 28);
        else
            v8 = (uint32_t*)(v5 + 4 * ((uint32_t)(*(uint32_t*)(v5 + 28 * a1 + 28) + 12) >> 2));

        uint8_t* v12 = v7;
        for (uint32_t i = 0; i < *(uint32_t*)(v7 + 20); ++i)
        {
            if (*v8 == *(uint32_t*)(a3 + 8)
                && ((*v8 & 0x4000000) == 0 || (v8[1] & 0xFFFF0000 | *((uint8_t*)v8 + 10) & 0x1F) == *(uint32_t*)(a3 + 4)))
            {
                memcpy(data, v8, *(int16_t*)(a3 + 2));
                v7 = v12;
                uint32_t v10 = 4 * ((uint32_t)*(int16_t*)(a3 + 2) >> 2);
                data = (uint32_t*)((uint8_t*)data + v10);
                v8 = (uint32_t*)((uint8_t*)v8 + v10);
            }
            else
            {
                v8 += *((uint8_t*)v8 + 1) + 1;
            }
        }
        return 1;
    }

    // 0x00430A60
    // MarniPolygonObject::Open - main TMD parser. Parses the raw TMD buffer at arg0
    // (object index a2) into the polygon object's vertex/normal/primitive buffers.
    // Returns 0 on early validation failure, 1 otherwise (even on the later
    // "not supported format" / "could not create work" / retrieve failures, which
    // mirror the original).
    // Must be __stdcall: the original is a thiscall with a `retn 0Ch` epilogue, and
    // both the hookThisCall trampoline at 0x00430A60 and interop::thiscall rely on
    // the callee cleaning up its stack arguments.
    static int __stdcall marni_poly_object_open(MarniPolyObject* self, uint8_t* arg0, int a2, uint32_t a3)
    {
        marni_poly_object_reset(self);

        TmdFile tmd;
        tmd_file_ctor(&tmd, nullptr);

        // TMDFile::CountsVariation output table (12 bytes per variant record).
        // CountsVariation zeroes the whole 240-byte table, so reads past the first
        // record (the min/max loop steps over every record's second dword) see zeroes.
        uint32_t variationBuf[64];
        memset(variationBuf, 0, sizeof(variationBuf));

        // Per-primitive record passed to refer_primitive. The 18-byte record must
        // be a single contiguous object (the original keeps v48..v58 in one stack
        // block); passing separate locals lets the compiler scatter them and
        // refer_primitive copies garbage. Declared directly before variationBuf so
        // the original's stack over-read in refer_primitive (up to 24/40 bytes for
        // some primitive types) reads the variation table.
        PrimRecord v48;

        uint32_t v44, v42, v45;

        if (a2 >= (int)tmd_counts_objects(&tmd, (uint32_t*)arg0))
        {
            out("object number you specified is invalid...%d", "TMDObject::Create");
            sub_4309A0(&tmd);
            return 0;
        }

        int v6 = tmd_counts_variation(&tmd, a2, (int)&variationBuf[0], 20, (int)arg0);
        if (!v6)
        {
            out("failed at analysis process.", "TMDObject::Create");
            sub_4309A0(&tmd);
            return 0;
        }
        if (v6 > 4)
        {
            out("not enough work. found %d object.", "TMDObject::In");
            sub_4309A0(&tmd);
            return 0;
        }

        int v7 = v6;
        int v43 = v6;
        int v8 = (uint16_t)variationBuf[1];
        int v9 = (uint16_t)variationBuf[1];
        if (v6 > 0)
        {
            int* v10 = (int*)&variationBuf[1];
            int v11 = v6;
            do
            {
                int v12 = (uint16_t)*v10;
                if (v9 > v12)
                    v9 = (uint16_t)*v10;
                if (v8 > v12)
                    v8 = (uint16_t)*v10;
                v10 += 3;
                --v11;
            } while (v11);
        }
        if (v8 - v9 >= 2)
        {
            out("this object is consisting of more than or equal 2 objects.", "TMDObject::In");
            sub_4309A0(&tmd);
            return 0;
        }

        uint32_t v47 = (a3 == 0xFFFFFFFF) ? (uint32_t)(v9 & 0xFFFFFFFE) : a3;

        if (v7 > 0)
        {
            int16_t* v14 = (int16_t*)&variationBuf[0];
            int16_t* v15 = (int16_t*)((uint8_t*)self + 0x38);
            int v16 = v7;
            do
            {
                int16_t v17 = *v14;
                v14 += 6;
                v15[2] = v17;
                *v15 = *(v14 - 3);
                v15[1] = *(v14 - 4);
                v15 += 3;
                --v16;
            } while (v16);
        }

        *(uint32_t*)((uint8_t*)self + 0x50) = (uint32_t)v7;

        if (v7 > 0)
        {
            int v18 = 0;
            int* v19 = (int*)&variationBuf[2];
            bool mismatch = false;
            do
            {
                if (variationBuf[2] != (uint32_t)*v19)
                {
                    mismatch = true;
                    break;
                }
                ++v18;
                v19 += 3;
            } while (v18 < v7);
            if (mismatch)
            {
                out("not supported for the object including header having more than 1.", "TMDObject::In");
                sub_4309A0(&tmd);
                return 0;
            }
        }

        uint32_t v20 = tmd_counts_vertex(&tmd, a2, (uint32_t*)arg0);
        int v21 = tmd_counts_vertex_0(&tmd, a2, (uint32_t*)arg0);
        uint32_t v22 = tmd_counts_primitive(&tmd, a2, (uint32_t*)arg0);

        int v23 = 0;
        if (v43 > 0)
        {
            int v24 = v43;
            int16_t* v25 = (int16_t*)&variationBuf[0] + 1;
            do
            {
                if (v23 < *v25)
                    v23 = *v25;
                v25 += 6;
                --v24;
            } while (v24);
        }

        uint8_t* lpMem = (uint8_t*)operator_new(4 * ((uint32_t)(v22 * (uint32_t)v23) >> 2) + 4);

        int v40 = 0;
        if ((variationBuf[2] & 0x3DFFFFFF) == 0x34000609)
        {
            if (create_work_0(self, (int)v20, v21, (int)v22, 1, 0, 3, 2, 0, 0, 0, 12))
            {
                for (int i = 0; i < (int)v20; ++i)
                {
                    tmd_read_tmd(&tmd, a2, i, &v44, &v42, &v45, (int)arg0);
                    modify_vertex(self, i, (float)(int32_t)v44, (float)(-(int32_t)v42), (float)(int32_t)v45);
                }
                for (int j = 0; j < v21; ++j)
                {
                    tmd_get_vertex(&tmd, a2, j, &v44, &v42, &v45, (int)arg0);
                    const float scale = 0.000244140625f; // flt_51742C (1/4096)
                    modify_normal(
                        self, j, (float)(int32_t)v44 * scale, (float)(-(int32_t)v42) * scale, (float)(int32_t)v45 * scale);
                }

                int v38 = 0;
                if (v43 > 0)
                {
                    int16_t* v29 = (int16_t*)&variationBuf[0];
                    bool done = false;
                    while (tmd_rerieves_primitives(&tmd, (uint32_t)a2, (uint32_t*)lpMem, (uint32_t)v29, (uint32_t)arg0))
                    {
                        int v30 = 0;
                        if (*v29 > 0)
                        {
                            int8_t v31 = (int8_t)(((uint8_t)((uint16_t)*(uint32_t*)((uint8_t*)v29 + 4) - (uint8_t)v47)) << 7);
                            uint8_t* v26 = lpMem;
                            do
                            {
                                v48.v0 = *(uint16_t*)(v26 + 0x12);
                                v48.v1 = *(uint16_t*)(v26 + 0x16);
                                v48.v2 = *(uint16_t*)(v26 + 0x1A);
                                v48.n0 = *(uint16_t*)(v26 + 0x10);
                                v48.n1 = *(uint16_t*)(v26 + 0x14);
                                v48.n2 = *(uint16_t*)(v26 + 0x18);
                                v48.u0 = (uint8_t)((uint8_t)v31 + v26[4]);
                                v48.u1 = (uint8_t)v26[5];
                                v48.u2 = (uint8_t)((uint8_t)v31 + v26[8]);
                                v48.u3 = (uint8_t)v26[9];
                                v48.u4 = (uint8_t)((uint8_t)v31 + v26[0xC]);
                                v48.u5 = (uint8_t)v26[0xD];
                                refer_primitive(self, v40, &v48);
                                ++v30;
                                ++v40;
                                v26 += 4 * ((uint32_t)v29[1] >> 2);
                            } while (v30 < *v29);
                        }
                        v29 += 6;
                        if (++v38 >= v43)
                        {
                            done = true;
                            break;
                        }
                    }
                    if (!done)
                        out("failed to retrieve primitives specified.", "TMDObject::In");
                }
            }
            else
            {
                out("could not create work.", "TMDObject::In");
            }
        }
        else
        {
            out("sorry for not supported format...%x", "TMDObject::In");
        }

        operator_delete(lpMem);
        sub_4309A0(&tmd);
        return 1;
    }

    // 0x00430F40
    static MarniPolyObject* marni_poly_object_ctor_base(MarniPolyObject* self, char* filename, int a3)
    {
        polygon_object_ctor(self);
        self->vTbl = (void*)0x517430;
        *(uint32_t*)((uint8_t*)self + 0x50) = 0;
        memset((uint8_t*)self + 0x38, 0, 0x18);
        if (filename)
            interop::thiscall<int, void*, char*, int, int>(0x004309B0, self, filename, a3, -1);
        return self;
    }

    // 0x00430260
    static MarniPolyObject* tm2_object_ctor(MarniPolyObject* self, char* filename, int a3)
    {
        marni_poly_object_ctor_base(self, 0, 0);
        self->vTbl = (void*)0x517424;
        if (filename)
            interop::thiscall<int, void*, char*, int, int>(0x004309B0, self, filename, a3, -1);
        return self;
    }

    // 0x004302C0
    static int tm2_object_dtor(MarniPolyObject* self)
    {
        self->vTbl = (void*)0x517424;
        marni_poly_object_reset(self);
        self->vTbl = (void*)0x517430;
        marni_poly_object_reset(self);
        polygon_object_dtor((PolygonObject*)self);
        return 1;
    }

    // 0x0042FF00
    static int tm2_object_adjust_texture_coordinates(MarniPolyObject* self, int a2, int a3)
    {
        if ((self->flags & 1) != 0)
        {
            // Scratch buffer for the primitive record. modify_primitive can copy
            // up to 40 bytes (0x800400); the 6 texture offset bytes at +12..+17
            // (v7..v12 in the original) must be contiguous with the copied record.
            uint32_t v6[10];
            auto* v7 = (uint8_t*)v6 + 12;
            for (uint32_t i = 0; i < self->primCount; ++i)
            {
                modify_primitive((PolygonObject*)self, (int)i, v6);
                v7[1] += (uint8_t)a3;
                v7[0] += (uint8_t)a2;
                v7[3] += (uint8_t)a3;
                v7[2] += (uint8_t)a2;
                v7[5] += (uint8_t)a3;
                v7[4] += (uint8_t)a2;
                refer_primitive(self, (int)i, v6);
            }
            return 1;
        }
        else
        {
            out("", "");
            return 0;
        }
    }

    // 0x0042FFB0
    static int tm2_object_in(MarniPolyObject* self, uint8_t* lpMem, int a3, int a4)
    {
        marni_poly_object_reset(self);

        uint8_t* v5;
        int v20;
        int v22;
        int v23;
        int v6;
        if (a3 < (int)(*((uint32_t*)lpMem + 2) / 2)
            && (v5 = &lpMem[56 * a3 + 12],
                v20 = *(uint32_t*)&lpMem[56 * a3 + 60],
                v22 = *(uint32_t*)&lpMem[56 * a3 + 32],
                *(uint32_t*)v5 == *(uint32_t*)&lpMem[56 * a3 + 40])
            && *(uint32_t*)&lpMem[56 * a3 + 20] == *(uint32_t*)&lpMem[56 * a3 + 48]
            && (v6 = *(uint32_t*)&lpMem[56 * a3 + 32] + 2 * v20,
                v23 = v6,
                create_work_0(
                    self, *(uint32_t*)&lpMem[56 * a3 + 16], *(uint32_t*)&lpMem[56 * a3 + 24], v6, 1, 0, 3, 2, 0, 0, 0, 12)))
        {
            int v7 = 0;
            if (a3 > 0)
            {
                uint8_t* v8 = lpMem + 32;
                int lpMema = a3;
                do
                {
                    int v9 = 3 * *(uint32_t*)v8;
                    int v10 = v7 + 16 * *((uint32_t*)v8 + 7);
                    v8 += 56;
                    v7 = v10 + 4 * v9;
                    --lpMema;
                } while (lpMema);
            }

            uint8_t* v11 = &lpMem[v7 + *(uint32_t*)lpMem];
            uint32_t* v12 = (uint32_t*)operator_new(28 * v6);
            uint8_t* v13 = (uint8_t*)*((uint32_t*)v5 + 4);
            uint8_t* lpMemb = (uint8_t*)v12;
            if ((lpMem[4] & 1) == 0)
                v13 = lpMem + 4 * ((uint32_t)((uintptr_t)v13 + 12) >> 2);

            int v14 = v22;
            if (v22 > 0)
            {
                do
                {
                    v12[0] = 872416777;
                    v12[1] = *(uint32_t*)v11;
                    v12[2] = *((uint32_t*)v11 + 1);
                    v12[3] = *((uint32_t*)v11 + 2);
                    v12[4] = *(uint32_t*)v13;
                    v12[5] = *((uint32_t*)v13 + 1);
                    v12[6] = *((uint32_t*)v13 + 2);
                    v12 += 7;
                    v11 += 12;
                    v13 += 12;
                    --v14;
                } while (v14);
            }

            uint8_t* v15 = (uint8_t*)*((uint32_t*)v5 + 11);
            if ((lpMem[4] & 1) == 0)
                v15 = lpMem + 4 * ((uint32_t)((uintptr_t)v15 + 12) >> 2);

            if (v20 > 0)
            {
                do
                {
                    v12[0] = 872416777;
                    v12[1] = *(uint32_t*)v11;
                    v12[2] = *((uint32_t*)v11 + 1);
                    v12[3] = *((uint32_t*)v11 + 2);
                    v12[4] = *(uint32_t*)v15;
                    v12[5] = *((uint32_t*)v15 + 1);
                    v12[6] = *((uint32_t*)v15 + 2);
                    uint32_t* v16 = v12 + 7;
                    v11 += 16;
                    v15 += 16;
                    v16[0] = 872416777;
                    v12 = v16 + 7;
                    v12[-6] = *((uint32_t*)v11 - 4) ^ (uint16_t)(*((uint32_t*)v11 - 4) ^ *((uint32_t*)v11 - 3));
                    v12[-5] = *((uint32_t*)v11 - 3) ^ (uint16_t)(*((uint32_t*)v11 - 3) ^ *((uint32_t*)v11 - 1));
                    v12[-4] = *((uint32_t*)v11 - 2);
                    v12[-3] = *((uint32_t*)v15 - 3);
                    v12[-2] = *((uint32_t*)v15 - 1);
                    v12[-1] = *((uint32_t*)v15 - 2);
                    --v20;
                } while (v20);
            }

            uint8_t v24[28];
            memcpy(v24, v5, sizeof(v24));
            int v17 = *(uint32_t*)lpMem;
            int v18 = *((uint32_t*)lpMem + 1);
            *(uint32_t*)lpMem = 65;
            *((uint32_t*)v5 + 5) = (uint32_t)v23;
            if ((lpMem[4] & 1) != 0)
                *((uint32_t*)v5 + 4) = (uint32_t)lpMemb;
            else
                *((uint32_t*)v5 + 4) = (uint32_t)(lpMemb - lpMem - 12);
            if (!marni_poly_object_open(self, lpMem, 2 * a3, (uint32_t)a4))
                out("", "");
            *(uint32_t*)lpMem = v17;
            *((uint32_t*)lpMem + 1) = v18;
            memcpy(v5, v24, 0x1C);
            operator_delete(lpMemb);
            return 1;
        }
        else
        {
            out("", "");
            return 0;
        }
    }

    // 0x00404BB0
    static uint32_t create_object_handle(Marni* self, void* a2, int a3)
    {
        if (!self->is_gpu_active)
            return 0;

        if (((*(uint8_t*)((char*)a2 + 52)) & 1) == 0 || self->polygons_count <= 1)
        {
            out("", "");
            return 0;
        }

        int v5 = 1;
        while (self->polygons[v5])
        {
            ++v5;
            if (v5 >= (int)self->polygons_count)
            {
                out("", "");
                return 0;
            }
        }

        auto* v9 = (MarniPolyObject*)operator_new(0x38);
        v9 = v9 ? polygon_object_ctor(v9) : nullptr;
        self->polygons[v5] = (PolygonObject*)v9;
        polygon_object_operator_eq(v9, (const MarniPolyObject*)a2);
        v9->flags |= (uint32_t)a3;
        return (uint32_t)v5;
    }

    // 0x00443F20
    static void stream_elem_ctor(void* self)
    {
        ((uint32_t*)self)[15] = 0;
        ((uint32_t*)self)[1] = 0;
        *(uint32_t*)self = 0;
        memset((uint8_t*)self + 0x0A, 0, 0x30);
    }

    // 0x00443370
    static void stream_elem_dtor(void* self)
    {
        if (*(uint32_t*)self)
        {
            destroy_object(gGameTable.pMarni, *(uint32_t*)self);
            *(uint32_t*)self = 0;
        }
        if (((uint32_t*)self)[1])
        {
            destroy_object(gGameTable.pMarni, ((uint32_t*)self)[1]);
            ((uint32_t*)self)[1] = 0;
        }
        ((uint32_t*)self)[15] = 0;
        ((uint32_t*)self)[1] = 0;
        *(uint32_t*)self = 0;
        memset((uint8_t*)self + 0x0A, 0, 0x30);
    }

    // 0x00443E80
    static int stream_alloc(uint32_t* stream, int count)
    {
        uint8_t* v3 = (uint8_t*)stream[7];
        if (v3)
        {
            cstd_vector_dtor(v3, 0x40, *((uint32_t*)v3 - 1), (void*)0x00443370);
            operator_delete(v3 - 4);
        }

        void* v5 = operator_new((count << 6) + 4);
        if (v5)
        {
            uint32_t* v6 = (uint32_t*)v5 + 1;
            *(uint32_t*)v5 = (uint32_t)count;
            cstd_vector_ctor(v6, 0x40, count, (void*)0x00443F20, (void*)0x00443370);
            stream[7] = (uint32_t)v6;
        }
        else
        {
            stream[7] = 0;
        }
        stream[1] = (uint32_t)count;
        return 0;
    }

    // 0x00503480
    static int zapping_check(unsigned int a1, int a2)
    {
        int v3 = 0;
        int v5 = a2;
        if (a1 >= 2)
            return 0;
        if ((a2 & 1) != a1)
            return 0;
        if (a2 < 11)
        {
            v3 = 1;
            if ((a2 & 1) != 0)
            {
                if (check_flag(FlagGroup::Zapping, FG_ZAPPING_6))
                {
                    v5 = 9;
                    goto label_9;
                }
            }
            else
            {
                if (check_flag(FlagGroup::Zapping, FG_ZAPPING_5))
                    v5 = 8;
                if (check_flag(FlagGroup::Zapping, FG_ZAPPING_15))
                {
                    v5 = 10;
                    goto label_9;
                }
            }
        }
        if (v5 < 80 || v5 > 90)
        {
            if (!v3)
                return 0;
        }
        else if ((v5 & 1) != 0)
        {
            if (check_flag(FlagGroup::Zapping, FG_ZAPPING_6))
                v5 = 89;
        }
        else
        {
            if (check_flag(FlagGroup::Zapping, FG_ZAPPING_5))
                v5 = 88;
            if (check_flag(FlagGroup::Zapping, FG_ZAPPING_15))
                v5 = 90;
        }
    label_9:
        if (a1 == 0)
            return (v5 == 10) || (v5 == 90);
        return (v5 == 9) || (v5 == 89);
    }

    // 0x00445B30
    static int tmd_object_kind(const uint8_t* pTmd, int index)
    {
        const uint8_t* entry = pTmd + 12 + 56 * index;
        const uint32_t baseData = ((pTmd[4] & 1) == 0) ? (uint32_t)((uintptr_t)pTmd + 12) : 0;

        uint32_t v4 = 0;
        const int triCount = *(uint32_t*)(entry + 20);
        if (triCount)
        {
            const uint8_t* v6 = (const uint8_t*)(baseData + *(uint32_t*)(entry + 24) + 6);
            for (int i = 0; i < triCount; i++)
            {
                v4 |= 1u << (*v6 & 3);
                v6 += 12;
            }
        }

        const int quadCount = *(uint32_t*)(entry + 48);
        if (quadCount)
        {
            const uint8_t* v11 = (const uint8_t*)(baseData + *(uint32_t*)(entry + 52) + 6);
            for (int i = 0; i < quadCount; i++)
            {
                v4 |= 1u << (*v11 & 3);
                v11 += 16;
            }
        }

        switch (v4)
        {
        case 4:
        case 8:
        case 12: return 2;
        case 6: return 1;
        default: return 0;
        }
    }

    // 0x0052517C
    static const uint8_t sTmdObjectListData[92] = {
        0x08, 0x03, 0x05, 0x06, 0xFF, 0x10, 0x00, 0x00, 0x02, 0x03, 0x04, 0x07, 0x08, 0x09, 0x0A, 0xFF, 0x20, 0x00, 0xFF,
        0x21, 0x07, 0xFF, 0x23, 0x00, 0x00, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0xFF, 0x25, 0x00, 0x00, 0x03, 0x05, 0x07, 0x0D, 0x14, 0xFF, 0x2B, 0x06, 0x07, 0x08, 0x09, 0x0C,
        0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0xFF, 0x2E, 0x00, 0x00, 0x00,
        0x00, 0x01, 0x02, 0xFF, 0x2F, 0x00, 0x00, 0x00, 0x02, 0xFF, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    // 0x005251D8
    static const int sTmdObjectListOffsets[48] = {
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  8,  18, -1, 20, -1, 24, -1, -1,
        -1, -1, -1, 44, -1, -1, 52, 76, -1, -1, 52, 84, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    };

    // 0x00445AE0
    static int tmd_object_list_check(int listId, int objectId)
    {
        if (listId == 74)
            return 1;
        if (listId >= 16 && listId - 16 < 48)
        {
            const int offset = sTmdObjectListOffsets[listId - 16];
            if (offset >= 0)
            {
                const int8_t* v3 = (const int8_t*)(sTmdObjectListData + offset);
                int v4 = *v3;
                if (v4 == -1)
                    return 1;
                if (v4 >= 0)
                {
                    while (v4 != objectId)
                    {
                        v4 = *++v3;
                        if (v4 < 0)
                            return 0;
                    }
                    return 1;
                }
            }
        }
        return 0;
    }

    // 0x004468E0
    static int tmd_write_uv_table(const uint8_t* pTmd, int a2, int a3, uint8_t* a4)
    {
        const uint8_t* entry = pTmd + 12 + 56 * a2;
        const uint32_t baseData = ((pTmd[4] & 1) == 0) ? (uint32_t)((uintptr_t)pTmd + 12) : 0;

        memset(a4, 0xFF, 0x804);
        *(uint32_t*)a4 = (uint32_t)a2;

        uint16_t* triUv = (uint16_t*)(a4 + 4);
        uint16_t* quadUv = (uint16_t*)(a4 + 0x404);
        int v13 = 0;
        uint8_t* result = nullptr;
        for (int v6 = 0; v6 < 4; v6++)
        {
            uint8_t* v7 = (uint8_t*)(baseData + *(uint32_t*)(entry + 24));
            uint32_t v8 = 0;
            if (*(uint32_t*)(entry + 20) != 0)
            {
                for (; v8 < *(uint32_t*)(entry + 20); ++v8)
                {
                    if (v6 == ((*(uint32_t*)v7 >> 22) & 3) && (((1 << (*(uint8_t*)(v7 + 6) & 3))) & a3))
                    {
                        if (v8 < 0x200)
                            triUv[v8] = (uint16_t)v13;
                        ++v13;
                    }
                    v7 += 12;
                }
            }

            result = (uint8_t*)(baseData + *(uint32_t*)(entry + 52));
            uint32_t v11 = 0;
            if (*(uint32_t*)(entry + 48) != 0)
            {
                for (; v11 < *(uint32_t*)(entry + 48); ++v11)
                {
                    if (v6 == ((*(uint32_t*)result >> 22) & 3) && (((1 << (result[6] & 3))) & a3))
                    {
                        if (v11 < 0x200)
                            quadUv[v11] = (uint16_t)v13;
                        v13 += 2;
                    }
                    result += 16;
                }
            }
        }
        return (int)(intptr_t)result;
    }

    // 0x00445BF0
    static uint32_t tmd_create_poly_object(const uint8_t* pTmd, int entryIndex, int a3, uint16_t* a4, int a5)
    {
        MarniPolyObject obj;
        tm2_object_ctor(&obj, nullptr, 0);

        if (a3 != 1 && a3 != 2 && a3 != 4 && a3 != 8 && a3 != 3 && a3 != 6 && a3 != 12)
        {
            tm2_object_dtor(&obj);
            return 0;
        }

        const uint8_t* entry = pTmd + 12 + 56 * entryIndex;
        const uint32_t baseData = ((pTmd[4] & 1) == 0) ? (uint32_t)((uintptr_t)pTmd + 12) : 0;

        int v5 = 0;
        const int triCount = *(uint32_t*)(entry + 20);
        if (triCount)
        {
            const uint8_t* v11 = (const uint8_t*)(baseData + *(uint32_t*)(entry + 24) + 6);
            for (int i = 0; i < triCount; i++)
            {
                if ((1 << (*v11 & 3)) & a3)
                    ++v5;
                v11 += 12;
            }
        }
        const int quadCount = *(uint32_t*)(entry + 48);
        if (quadCount)
        {
            const uint8_t* v13 = (const uint8_t*)(baseData + *(uint32_t*)(entry + 52) + 6);
            for (int i = 0; i < quadCount; i++)
            {
                if ((1 << (*v13 & 3)) & a3)
                    v5 += 2;
                v13 += 16;
            }
        }

        if (v5 == 0)
        {
            tm2_object_dtor(&obj);
            return 0;
        }

        const int vertexCount = *(uint32_t*)(entry + 4);
        const int normalCount = *(uint32_t*)(entry + 12);
        create_work(&obj, vertexCount, normalCount, v5, 0x1B00081);

        const int16_t* v16 = *(const int16_t**)(entry + 0);
        for (int i = 0; i < vertexCount; i++)
        {
            modify_vertex(&obj, i, (float)v16[0], (float)-v16[1], (float)v16[2]);
            v16 += 4;
        }
        const int16_t* v18 = *(const int16_t**)(entry + 8);
        for (int j = 0; j < normalCount; j++)
        {
            modify_normal(
                &obj,
                j,
                (float)((double)v18[0] * 0.000244140625),
                (float)((double)-v18[1] * 0.000244140625),
                (float)((double)v18[2] * 0.000244140625));
            v18 += 4;
        }

        int v20 = 0;
        int v67[4] = { 0, 0, 0, 0 };
        for (int v51 = 0; v51 < 4; v51++)
        {
            v67[v51] = 0;
            const int triCount2 = *(uint32_t*)(entry + 20);
            const int16_t* v22 = (const int16_t*)(baseData + *(uint32_t*)(entry + 16));
            const uint8_t* v23 = (const uint8_t*)(baseData + *(uint32_t*)(entry + 24));
            for (int v64 = 0; v64 < triCount2; ++v64)
            {
                const uint8_t v25 = v23[6] & 3;
                if (v51 == ((*(const uint32_t*)v23 >> 22) & 3) && ((1 << v25) & a3))
                {
                    PrimRecord rec;
                    rec.v0 = v22[1];
                    rec.v1 = v22[3];
                    rec.v2 = v22[5];
                    rec.n0 = v22[0];
                    rec.n1 = v22[2];
                    rec.n2 = v22[4];
                    const int v26 = v25 << 7;
                    rec.u0 = (uint8_t)(v26 + v23[0]);
                    rec.u1 = (uint8_t)((*(const uint32_t*)v23 >> 8) & 0xFF);
                    rec.u2 = (uint8_t)(v26 + v23[4]);
                    rec.u3 = (uint8_t)((*((const uint32_t*)v23 + 1) >> 8) & 0xFF);
                    rec.u4 = (uint8_t)(v26 + v23[8]);
                    rec.u5 = (uint8_t)((*((const uint32_t*)v23 + 2) >> 8) & 0xFF);
                    refer_primitive(&obj, v20++, &rec);
                    ++v67[v51];
                }
                v22 += 6;
                v23 += 12;
            }

            const int quadCount2 = *(uint32_t*)(entry + 48);
            const int16_t* v28 = (const int16_t*)(baseData + *(uint32_t*)(entry + 44));
            const uint8_t* v29 = (const uint8_t*)(baseData + *(uint32_t*)(entry + 52));
            for (int v64 = 0; v64 < quadCount2; ++v64)
            {
                if (v51 == ((*(const uint32_t*)v29 >> 22) & 3) && ((1 << (v29[6] & 3)) & a3))
                {
                    const int v32 = (v29[6] & 1) << 7;
                    PrimRecord rec;
                    rec.v0 = v28[1];
                    rec.v1 = v28[3];
                    rec.v2 = v28[5];
                    rec.n0 = v28[0];
                    rec.n1 = v28[2];
                    rec.n2 = v28[4];
                    rec.u0 = (uint8_t)(v32 + v29[0]);
                    rec.u1 = (uint8_t)((*(const uint32_t*)v29 >> 8) & 0xFF);
                    rec.u2 = (uint8_t)(v32 + v29[4]);
                    rec.u3 = (uint8_t)((*((const uint32_t*)v29 + 1) >> 8) & 0xFF);
                    rec.u4 = (uint8_t)(v32 + v29[8]);
                    rec.u5 = (uint8_t)((*((const uint32_t*)v29 + 2) >> 8) & 0xFF);
                    refer_primitive(&obj, v20, &rec);

                    rec.v0 = v28[3];
                    rec.v1 = v28[7];
                    rec.v2 = v28[5];
                    rec.n0 = v28[2];
                    rec.n1 = v28[6];
                    rec.n2 = v28[4];
                    rec.u0 = (uint8_t)(v32 + v29[4]);
                    rec.u1 = (uint8_t)((*((const uint32_t*)v29 + 1) >> 8) & 0xFF);
                    rec.u2 = (uint8_t)(v32 + v29[12]);
                    rec.u3 = (uint8_t)((*((const uint32_t*)v29 + 3) >> 8) & 0xFF);
                    rec.u4 = (uint8_t)(v32 + v29[8]);
                    rec.u5 = (uint8_t)((*((const uint32_t*)v29 + 2) >> 8) & 0xFF);
                    refer_primitive(&obj, v20 + 1, &rec);
                    v20 += 2;
                    v67[v51] += 2;
                }
                v28 += 8;
                v29 += 16;
            }
        }

        uint32_t v35 = 0;
        uint16_t* v36 = a4;
        for (uint32_t v34 = 0; v34 < 4; v34++)
        {
            const int v37 = v67[v34];
            if (v37)
            {
                v36[0] = (uint16_t)v34;
                v36[2] = (uint16_t)v37;
                ++v35;
                v36 += 3;
            }
        }
        a4[3 * v35 - 1] = 0;

        if (a5)
            tm2_object_adjust_texture_coordinates(&obj, a5, 0);
        const uint32_t objectHandle = create_object_handle(gGameTable.pMarni, &obj, 0);
        tm2_object_dtor(&obj);
        return objectHandle;
    }

    // 0x0040E9D0
    static int __stdcall trans_object_ngtin3_vinsnins(Marni* self, MarniOt* pOt, Prim* pPrim)
    {
        return 0;
    }

    // 0x00408140
    static int __stdcall trans_object(Marni* self, MarniOt* pOt, Prim* pPrim)
    {
        if (self->is_gpu_active == 0)
            return 0;

        // The 2K object primitive stores a polygon object index at offset 0x4C;
        // look the object up in the polygon object table ("2K buffer").
        const auto* prim = (const uint8_t*)pPrim;
        const uint32_t objectIndex = *(const uint32_t*)(prim + 0x4C);
        auto* pObject = self->polygons[objectIndex];
        if (pObject == nullptr || (*((const uint8_t*)pObject + 0x34) & 1) == 0)
        {
            if (pObject == nullptr)
                out("object didn't exist on the handle you specified.", "MarniSystem Marni::TransObject");
            else
                out("object you specified is invalid.", "MarniSystem Marni::TransObject");
            self->is_gpu_active = 0;
            return 0;
        }

        // Copy the polygon object header (obj+0x10, 9 dwords). Fields of interest:
        // [2]=vertices count [4]=normals count [6]=primitives count [8]=primitive type.
        uint32_t header[9];
        memcpy(header, (uint8_t*)pObject + 0x10, sizeof(header));
        if (header[4] > 0x400u)
        {
            out("object you specified has too normals then can't proceed. %d", "Marni::TransObject");
            return 0;
        }
        if (header[2] > 0x800u)
        {
            out("object you specified has too vertices then can't proceed. %d", "Marni::TransObject");
            return 0;
        }
        if (header[6] > 0x800u)
        {
            out("object you specified has too primitives then can't proceed. %d", "Marni::TransObject");
            return 0;
        }

        // 0x100 type bit: copy the object matrices (2x 0x40 bytes) into the system.
        if ((pPrim->type & 0x100) != 0)
        {
            memcpy(&self->field_8C7E10, prim + 0x60, 0x40);
            memcpy(&self->field_8C7E50, prim + 0xA0, 0x40);
        }

        // Dispatch on the masked polygon object primitive type.
        const uint32_t type = header[8] & 0xFF801FFF;
        if (type > 0x10014C0)
        {
            if (type > 0x1800400)
            {
                if (type != 0x1800401)
                    goto unsupported;
                return 1;
            }
            if (type == 0x1800400)
                return 1;
            const uint32_t v9 = type - 0x1800080;
            if (v9 == 0)
                return 1;
            if (v9 == 1)
            {
                // 0x1800081: NG/TIN3-style polygon object
                trans_object_ngtin3_vinsnins(self, pOt, pPrim);
                return 1;
            }
        }
        else
        {
            if (type == 0x10014C0)
                return 1;
            if (type <= 0x1442)
            {
                if (type == 0x1442)
                    return 1;
                const uint32_t v7 = type - 0x402;
                if (v7 == 0)
                    return 1;
                if (v7 == 2)
                    return 1;
            }
            else if (type == 0x800400)
                return 1;
        }

    unsupported:
        out("this type is not supported.", "MarniSystem Marni::TransObject");
        return 0;
    }

    // 0x004082c0

    // 0x00408e80

    // 0x00409000

    // 0x00409540

    // 0x00409740

    // 0x00409830

    // 0x00409a10

    // 0x00409c00

    // 0x00409ee0

    // 0x0040a1d0

    // Gradient triangle primitive: 3 texture-mapped vertices sharing one flat
    // colour. The R/G/B channels are doubled (values >255 overflow into the
    // specular field, and any overflow also sets specularEnable). The type's
    // 0x100000..0x400000 mode bits pick the alpha byte exactly like the sprite
    // builder, and the positions are perspective-projected by z.
    struct PrimGradTri : Prim
    {
        uint32_t texture; // 0x0008
        uint32_t var_0C;  // 0x000C
        int16_t x0;       // 0x0010
        int16_t y0;       // 0x0012
        int16_t x1;       // 0x0014
        int16_t y1;       // 0x0016
        int16_t x2;       // 0x0018
        int16_t y2;       // 0x001A
        int16_t z;        // 0x001C
        uint8_t u0;       // 0x001E
        uint8_t v0;       // 0x001F
        uint8_t u1;       // 0x0020
        uint8_t v1;       // 0x0021
        uint8_t u2;       // 0x0022
        uint8_t v2;       // 0x0023
        uint8_t b;        // 0x0024
        uint8_t g;        // 0x0025
        uint8_t r;        // 0x0026
        uint8_t a;        // 0x0027
    };
    static_assert(sizeof(PrimGradTri) == 0x28);

    // 0x0040a4b0
    static int __stdcall sub_40A4B0(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return 0;
    }

    // Prim type 0x45 (69) gouraud quad layout: 4 int16 coordinates, 4 (u,v)
    // texcoords and a per-quad BGRA colour in bytes 0x28..0x2B.
    struct PrimGouraudQuad : Prim
    {
        uint32_t texture; // 0x0008
        uint32_t var_0C;  // 0x000C
        int16_t x0;       // 0x0010
        int16_t y0;       // 0x0012
        int16_t x1;       // 0x0014
        int16_t y1;       // 0x0016
        int16_t x2;       // 0x0018
        int16_t y2;       // 0x001A
        int16_t x3;       // 0x001C
        int16_t y3;       // 0x001E
        uint8_t u0;       // 0x0020
        uint8_t v0;       // 0x0021
        uint8_t u1;       // 0x0022
        uint8_t v1;       // 0x0023
        uint8_t u2;       // 0x0024
        uint8_t v2;       // 0x0025
        uint8_t u3;       // 0x0026
        uint8_t v3;       // 0x0027
        uint8_t b;        // 0x0028
        uint8_t g;        // 0x0029
        uint8_t r;        // 0x002A
        uint8_t a;        // 0x002B
    };
    static_assert(sizeof(PrimGouraudQuad) == 0x2C);

    // 0x0040A830
    static int __stdcall sub_40A830(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return 0;
    }

    // Prim type 0x46 (70) gouraud textured quad: four int16 corners, four
    // (u,v) texcoord bytes and four per-vertex B,G,R,A colour sets at
    // 0x28..0x37. Each colour channel is doubled; channels that overflow 255
    // wrap into the per-vertex specular field and set specularEnable (the
    // shade mode is gouraud, 2). The alpha byte is picked by the type's
    // 0x100000..0x400000 mode bits exactly like the other quad builders;
    // 0x400000 uses the primitive's own alpha byte.
    struct PrimPolyG4 : Prim
    {
        uint32_t texture; // 0x0008
        uint32_t var_0C;  // 0x000C
        int16_t x0;       // 0x0010
        int16_t y0;       // 0x0012
        int16_t x1;       // 0x0014
        int16_t y1;       // 0x0016
        int16_t x2;       // 0x0018
        int16_t y2;       // 0x001A
        int16_t x3;       // 0x001C
        int16_t y3;       // 0x001E
        uint8_t u0;       // 0x0020
        uint8_t v0;       // 0x0021
        uint8_t u1;       // 0x0022
        uint8_t v1;       // 0x0023
        uint8_t u2;       // 0x0024
        uint8_t v2;       // 0x0025
        uint8_t u3;       // 0x0026
        uint8_t v3;       // 0x0027
        uint8_t b0;       // 0x0028
        uint8_t g0;       // 0x0029
        uint8_t r0;       // 0x002A
        uint8_t a0;       // 0x002B
        uint8_t b1;       // 0x002C
        uint8_t g1;       // 0x002D
        uint8_t r1;       // 0x002E
        uint8_t a1;       // 0x002F
        uint8_t b2;       // 0x0030
        uint8_t g2;       // 0x0031
        uint8_t r2;       // 0x0032
        uint8_t a2;       // 0x0033
        uint8_t b3;       // 0x0034
        uint8_t g3;       // 0x0035
        uint8_t r3;       // 0x0036
        uint8_t a3;       // 0x0037
    };
    static_assert(sizeof(PrimPolyG4) == 0x38);

    // 0x0040ab60
    static int __stdcall sub_40AB60(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return 0;
    }

    // 0x0040B260
    static int __stdcall sub_40B260(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return 0;
    }

    // Prim type 0x1004C quad layout (4 int16 coords, int16 z, 4 texcoord pairs)
    struct PrimPolyFT4 : Prim
    {
        uint32_t texture; // 0x0008
        uint32_t var_0C;  // 0x000C
        int16_t x0;       // 0x0010
        int16_t y0;       // 0x0012
        int16_t x1;       // 0x0014
        int16_t y1;       // 0x0016
        int16_t x2;       // 0x0018
        int16_t y2;       // 0x001A
        int16_t x3;       // 0x001C
        int16_t y3;       // 0x001E
        int16_t z;        // 0x0020
        uint8_t u0;       // 0x0022
        uint8_t v0;       // 0x0023
        uint8_t u1;       // 0x0024
        uint8_t v1;       // 0x0025
        uint8_t u2;       // 0x0026
        uint8_t v2;       // 0x0027
        uint8_t u3;       // 0x0028
        uint8_t v3;       // 0x0029
    };
    static_assert(sizeof(PrimPolyFT4) == 0x2C); // 0x2A of fields + 2 bytes tail padding

    // 0x0040b560
    static int __stdcall sub_40B560(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return 0;
    }

    // 0x0040b8d0
    static int __stdcall sub_40B8D0(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return 0;
    }

    // 0x0040BCF0
    static int __stdcall sub_40BCF0(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return 0;
    }

    // 0x0040c100
    static int __stdcall sub_40C100(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return 0;
    }

    // 0x0040c470
    static int __stdcall sub_40C470(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return 0;
    }

    static inline int draw_line_clamp(int value)
    {
        return value > 255 ? 255 : value;
    }

    // Pack 8-bit R/G/B channels into a 0x00RRGGBB color (matches the original
    // byte packing: low byte = blue, byte 1 = green, byte 2 = red).
    static inline uint32_t draw_line_pack_rgb(int r, int g, int b)
    {
        return ((uint32_t)(uint8_t)r << 16) | ((uint32_t)(uint8_t)g << 8) | (uint32_t)(uint8_t)b;
    }

    // 0x004C2C30 WHY DO WE JUMP HERE IN THIS FILE?!?!?
    static void draw_line(
        MarniSurface* surface, int x0, int y0, int x1, int y1, int a5, int a6, int width, int height, int color0, int color1,
        int flg)
    {
        if (!surface->bOpen)
        {
            out("the Bits was invalid. triangle_texture", "");
            return;
        }

        // Channel bases from the start color (0x00RRGGBB) and per-channel deltas
        // towards the end color.
        const int rBase = (color0 >> 16) & 0xFF;
        const int gBase = (color0 >> 8) & 0xFF;
        const int bBase = color0 & 0xFF;
        const int rDiff = ((color1 >> 16) & 0xFF) - rBase;
        const int gDiff = ((color1 >> 8) & 0xFF) - gBase;
        const int bDiff = (uint8_t)color1 - (uint8_t)bBase;

        // Axis-aligned step directions and per-axis lengths.
        const int dx = x1 >= x0 ? x1 - x0 : x0 - x1;
        const int dy = y1 >= y0 ? y1 - y0 : y0 - y1;
        const int stepX = dx != 0 ? 2 * (x1 > x0) - 1 : 0;
        const int stepY = dy != 0 ? 2 * (y1 > y0) - 1 : 0;

        // Running line position (the original swaps the values into y1/x1).
        int lineX = x0;
        int lineY = y0;

        if ((flg & 2) != 0)
        {
            // ---- Additive path: accumulate the gradient color into the surface.
            if (dx == 0 && dy == 0)
            {
                // Single point: paint the doubled 2x2 block or a lone pixel.
                if ((flg & 1) != 0)
                {
                    const int px = 2 * x0;
                    const int py = 2 * y0;
                    uint32_t pixel;

                    surface_get_current_color(surface, px, py, &pixel);
                    int r = draw_line_clamp(rBase + ((pixel >> 16) & 0xFF));
                    int g = draw_line_clamp(gBase + ((pixel >> 8) & 0xFF));
                    int b = draw_line_clamp(bBase + (uint8_t)pixel);
                    surface_set_current_color(surface, px, py, draw_line_pack_rgb(r, g, b), 0);

                    surface_get_current_color(surface, px + 1, py, &pixel);
                    r = draw_line_clamp(rBase + ((pixel >> 16) & 0xFF));
                    g = draw_line_clamp(gBase + ((pixel >> 8) & 0xFF));
                    b = draw_line_clamp(bBase + (uint8_t)pixel);
                    surface_set_current_color(surface, px + 1, py, draw_line_pack_rgb(r, g, b), 0);

                    surface_get_current_color(surface, px, py + 1, &pixel);
                    r = draw_line_clamp(rBase + ((pixel >> 16) & 0xFF));
                    g = draw_line_clamp(gBase + ((pixel >> 8) & 0xFF));
                    b = draw_line_clamp(bBase + (uint8_t)pixel);
                    surface_set_current_color(surface, px, py + 1, draw_line_pack_rgb(r, g, b), 0);

                    surface_get_current_color(surface, px + 1, py + 1, &pixel);
                    r = draw_line_clamp(rBase + ((pixel >> 16) & 0xFF));
                    g = draw_line_clamp(gBase + ((pixel >> 8) & 0xFF));
                    b = draw_line_clamp(bBase + (uint8_t)pixel);
                    surface_set_current_color(surface, px + 1, py + 1, draw_line_pack_rgb(r, g, b), 0);
                }
                else
                {
                    uint32_t pixel;
                    surface_get_current_color(surface, x0, y0, &pixel);
                    int r = draw_line_clamp(rBase + ((pixel >> 16) & 0xFF));
                    int g = draw_line_clamp(gBase + ((pixel >> 8) & 0xFF));
                    int b = draw_line_clamp(bBase + (uint8_t)pixel);
                    surface_set_current_color(surface, x0, y0, draw_line_pack_rgb(r, g, b), 0);
                }
                return;
            }

            if (dx >= dy)
            {
                // ---- X-major.
                int err = -2 * dy;
                if (dx > 0)
                {
                    const int doubled = flg & 1;
                    const int step = 2 * dy;
                    int rAcc = 0;
                    int gAcc = 0;
                    int bAcc = 0;
                    int xx = 2 * x0;
                    int yy = 2 * y0;
                    int count = dx;

                    do
                    {
                        uint32_t pixel;
                        if (doubled)
                        {
                            // 2x2 block, each pixel accumulating on the previous.
                            surface_get_current_color(surface, xx, yy, &pixel);
                            int r = draw_line_clamp(rAcc / dx + rBase + ((pixel >> 16) & 0xFF));
                            int g = draw_line_clamp(gAcc / dx + gBase + ((pixel >> 8) & 0xFF));
                            int b = draw_line_clamp(bAcc / dx + bBase + (uint8_t)pixel);
                            surface_set_current_color(surface, xx, yy, draw_line_pack_rgb(r, g, b), 0);

                            surface_get_current_color(surface, xx + 1, yy, &pixel);
                            r = draw_line_clamp(r + ((pixel >> 16) & 0xFF));
                            g = draw_line_clamp(g + ((pixel >> 8) & 0xFF));
                            b = draw_line_clamp(b + (uint8_t)pixel);
                            surface_set_current_color(surface, xx + 1, yy, draw_line_pack_rgb(r, g, b), 0);

                            surface_get_current_color(surface, xx, yy + 1, &pixel);
                            r = draw_line_clamp(r + ((pixel >> 16) & 0xFF));
                            g = draw_line_clamp(g + ((pixel >> 8) & 0xFF));
                            b = draw_line_clamp(b + (uint8_t)pixel);
                            surface_set_current_color(surface, xx, yy + 1, draw_line_pack_rgb(r, g, b), 0);

                            surface_get_current_color(surface, xx + 1, yy + 1, &pixel);
                            r = draw_line_clamp(r + ((pixel >> 16) & 0xFF));
                            g = draw_line_clamp(g + ((pixel >> 8) & 0xFF));
                            b = draw_line_clamp(b + (uint8_t)pixel);
                            surface_set_current_color(surface, xx + 1, yy + 1, draw_line_pack_rgb(r, g, b), 0);
                        }
                        else
                        {
                            surface_get_current_color(surface, lineX, lineY, &pixel);
                            int r = draw_line_clamp(rAcc / dx + rBase + ((pixel >> 16) & 0xFF));
                            int g = draw_line_clamp(gAcc / dx + gBase + ((pixel >> 8) & 0xFF));
                            int b = draw_line_clamp(bAcc / dx + bBase + (uint8_t)pixel);
                            surface_set_current_color(surface, lineX, lineY, draw_line_pack_rgb(r, g, b), 0);
                        }

                        const bool neg = (step + err) < 0;
                        lineX += stepX;
                        xx += 2 * stepX;
                        err += step;
                        if (!neg)
                        {
                            lineY += stepY;
                            yy += 2 * stepY;
                            err -= 2 * dx;
                        }
                        gAcc += gDiff;
                        rAcc += rDiff;
                        bAcc += bDiff;
                        --count;
                    } while (count != 0);
                }
                return;
            }

            // ---- Y-major.
            int err = -2 * dx;
            if (dy > 0)
            {
                const int savedDoubled = flg & 1;
                int doubled = savedDoubled;
                const int step = 2 * dx;
                int rAcc = 0;
                int gAcc = 0;
                int bAcc = 0;
                int xx = 2 * x0;
                int yy = 2 * y0;
                int count = dy;

                while (true)
                {
                    uint32_t pixel;
                    if (doubled)
                    {
                        surface_get_current_color(surface, xx, yy, &pixel);
                        int r = draw_line_clamp(rAcc / dy + rBase + ((pixel >> 16) & 0xFF));
                        int g = draw_line_clamp(gAcc / dy + gBase + ((pixel >> 8) & 0xFF));
                        int b = draw_line_clamp(bAcc / dy + bBase + (uint8_t)pixel);
                        surface_set_current_color(surface, xx, yy, draw_line_pack_rgb(r, g, b), 0);

                        surface_get_current_color(surface, xx + 1, yy, &pixel);
                        r = draw_line_clamp(r + ((pixel >> 16) & 0xFF));
                        g = draw_line_clamp(g + ((pixel >> 8) & 0xFF));
                        b = draw_line_clamp(b + (uint8_t)pixel);
                        surface_set_current_color(surface, xx + 1, yy, draw_line_pack_rgb(r, g, b), 0);

                        surface_get_current_color(surface, xx, yy + 1, &pixel);
                        r = draw_line_clamp(r + ((pixel >> 16) & 0xFF));
                        g = draw_line_clamp(g + ((pixel >> 8) & 0xFF));
                        b = draw_line_clamp(b + (uint8_t)pixel);
                        surface_set_current_color(surface, xx, yy + 1, draw_line_pack_rgb(r, g, b), 0);

                        surface_get_current_color(surface, xx + 1, yy + 1, &pixel);
                        r = draw_line_clamp(r + ((pixel >> 16) & 0xFF));
                        g = draw_line_clamp(g + ((pixel >> 8) & 0xFF));
                        b = draw_line_clamp(b + (uint8_t)pixel);
                        surface_set_current_color(surface, xx + 1, yy + 1, draw_line_pack_rgb(r, g, b), 0);
                    }
                    else
                    {
                        surface_get_current_color(surface, lineX, lineY, &pixel);
                        int r = draw_line_clamp(rAcc / dy + rBase + ((pixel >> 16) & 0xFF));
                        int g = draw_line_clamp(gAcc / dy + gBase + ((pixel >> 8) & 0xFF));
                        int b = draw_line_clamp(bAcc / dy + bBase + (uint8_t)pixel);
                        surface_set_current_color(surface, lineX, lineY, draw_line_pack_rgb(r, g, b), 0);
                    }

                    yy += 2 * stepY;
                    const bool neg = (step + err) < 0;
                    lineY += stepY;
                    err += step;
                    if (!neg)
                    {
                        xx += 2 * stepX;
                        lineX += stepX;
                        err -= 2 * dy;
                    }
                    gAcc += gDiff;
                    rAcc += rDiff;
                    bAcc += bDiff;
                    --count;
                    if (count == 0)
                        break;
                    doubled = savedDoubled;
                }
            }
            return;
        }

        // ---- Non-additive path: solid gradient color written directly.
        if (dx == 0 && dy == 0)
        {
            // Single point: strip the alpha nibble of the start color.
            const uint32_t color = color0 & 0xFFFFFF;
            if ((flg & 1) != 0)
            {
                const int px = 2 * x0;
                const int py = 2 * y0;
                surface_set_current_color(surface, px, py, color, 0);
                surface_set_current_color(surface, px + 1, py, color, 0);
                surface_set_current_color(surface, px, py + 1, color, 0);
                surface_set_current_color(surface, px + 1, py + 1, color, 0);
            }
            else
            {
                surface_set_current_color(surface, x0, y0, color, 0);
            }
            return;
        }

        if (dx < dy)
        {
            // ---- Y-major (non-additive).
            int err = -2 * dx;
            if (dy > 0)
            {
                const int doubled = flg & 1;
                const int step = 2 * dx;
                int rAcc = 0;
                int gAcc = 0;
                int bAcc = 0;
                int xx = 2 * x0;
                int ypos = 2 * y0;
                int count = dy;

                while (true)
                {
                    // Blue is truncated to a byte, green is summed in 16 bits
                    // before shifting, red is shifted and masked to 0xFF0000.
                    const uint32_t color = (uint32_t)(uint8_t)(bBase + bAcc / dy)
                        | (uint32_t)(uint16_t)(((uint16_t)gBase + (uint16_t)(gAcc / dy)) << 8)
                        | ((uint32_t)((rBase + rAcc / dy) << 16) & 0xFF0000);

                    if (doubled)
                    {
                        surface_set_current_color(surface, xx, ypos, color, 0);
                        surface_set_current_color(surface, xx + 1, ypos, color, 0);
                        surface_set_current_color(surface, xx, ypos + 1, color, 0);
                        surface_set_current_color(surface, xx + 1, ypos + 1, color, 0);
                    }
                    else
                    {
                        surface_set_current_color(surface, lineX, lineY, color, 0);
                    }

                    const bool neg = (step + err) < 0;
                    const int saved = step + err;
                    lineY += stepY;
                    ypos += 2 * stepY;
                    err += step;
                    if (!neg)
                    {
                        lineX += stepX;
                        xx += 2 * stepX;
                        err = saved - 2 * dy;
                    }
                    rAcc += rDiff;
                    gAcc += gDiff;
                    bAcc += bDiff;
                    --count;
                    if (count == 0)
                        break;
                }
            }
            return;
        }

        // ---- X-major (non-additive).
        int err = -2 * dy;
        if (dx > 0)
        {
            const int doubled = flg & 1;
            const int step = 2 * dy;
            int rAcc = 0;
            int gAcc = 0;
            int bAcc = 0;
            int xx = 2 * x0;
            int ypos = 2 * y0;
            int count = dx;

            while (true)
            {
                const uint32_t color = (uint32_t)(uint8_t)(bBase + bAcc / dx)
                    | (uint32_t)(uint16_t)(((uint16_t)gBase + (uint16_t)(gAcc / dx)) << 8)
                    | ((uint32_t)((rBase + rAcc / dx) << 16) & 0xFF0000);

                if (doubled)
                {
                    surface_set_current_color(surface, xx, ypos, color, 0);
                    surface_set_current_color(surface, xx + 1, ypos, color, 0);
                    surface_set_current_color(surface, xx, ypos + 1, color, 0);
                    surface_set_current_color(surface, xx + 1, ypos + 1, color, 0);
                }
                else
                {
                    surface_set_current_color(surface, lineX, lineY, color, 0);
                }

                const bool neg = (step + err) < 0;
                const int saved = step + err;
                lineX += stepX;
                xx += 2 * stepX;
                err += step;
                if (!neg)
                {
                    lineY += stepY;
                    ypos += 2 * stepY;
                    err = saved - 2 * dx;
                }
                rAcc += rDiff;
                gAcc += gDiff;
                bAcc += bDiff;
                --count;
                if (count == 0)
                    break;
            }
        }
    }

    // GPU-backend path for MARNI line primitives (type 17/18: status-screen
    // ECG, item box, weapon frame). When the SDL_GPU backend drives the frame
    // the software rasterizer path is both wrong (its surface0 pixels are
    // wiped by the deferred target clear at present) and slow (each segment
    // does a full 640x480 lock/unlock pair = GPU readback + upload + two
    // SDL_WaitForGPUIdle calls). Instead, emit a solid untextured quad through
    // the wrapped GPU device: it lands in the GPU scene pass after the clear,
    // exactly like every other primitive.
    //
    // The software rasterizer (DrawLine with flg&1) paints a 2x2 block at the
    // doubled coordinates for every sample, so the quad spans 2 pixels:
    // [2*x0, 2*x0+2) x [2*y0, 2*y1+2) in the 640x480 render target. Lines
    // without flg&1 are 1 pixel wide at raw coordinates. flg&2 lines are
    // additive (accumulate into the target) -> ONE/ONE blend. The color is
    // 0x00RRGGBB -> 0xFFRRGGBB (the TL vertex shader swizzles the
    // B,G,R,A memory bytes to RGBA). All in-game line users are axis-aligned,
    // so the bounding-box quad is exact; a diagonal line would render as its
    // bounding rectangle (no such line exists today).
    static void draw_line_gpu(Marni* self, int x0, int y0, int x1, int y1, uint32_t color0, uint32_t color1, int type) {}

    // 0x0040C6E0
    static void __stdcall draw_line_flat(Marni* self, PrimLine2* line)
    {
        auto type = 1;
        if (self->xsize != 640)
        {
            type = (int)line->pNext;
        }
        if (line->type & 0x200000)
        {
            type |= 2;
        }
        if ((self->gpu_flag & GpuFlags::SOFTWARE_GPU) == 0)
        {
            // GPU backend: emit the line as a GPU primitive instead of
            // software-rasterizing it into surface0 (see draw_line_gpu).
            draw_line_gpu(self, line->x0, line->y0, line->x1, line->y1, line->color0, line->color0, type);
            return;
        }
        if ((self->gpu_flag & GpuFlags::SOFTWARE_GPU) == 0)
        {
            surface_lock(&self->surface0, 0, 0);
        }
        draw_line(
            &self->surface0,
            line->x0,
            line->y0,
            line->x1,
            line->y1,
            0,
            0,
            self->xsize,
            self->ysize,
            line->color0,
            line->color0,
            type);
        if ((self->gpu_flag & GpuFlags::SOFTWARE_GPU) == 0)
        {
            surface_unlock(&self->surface0);
        }
    }

    // 0x0040C790
    static void __stdcall draw_line_gourad(Marni* self, PrimLine2* line)
    {
        auto type = 1;
        if (self->xsize != 640)
        {
            type = (int)line->pNext;
        }
        if (line->type & 0x200000)
        {
            type |= 2;
        }
        if ((self->gpu_flag & GpuFlags::SOFTWARE_GPU) == 0)
        {
            // GPU backend: emit the line as a GPU primitive instead of
            // software-rasterizing it into surface0 (see draw_line_gpu).
            draw_line_gpu(self, line->x0, line->y0, line->x1, line->y1, line->color0, line->color1, type);
            return;
        }
        if ((self->gpu_flag & GpuFlags::SOFTWARE_GPU) == 0)
        {
            surface_lock(&self->surface0, 0, 0);
        }
        draw_line(
            &self->surface0,
            line->x0,
            line->y0,
            line->x1,
            line->y1,
            0,
            0,
            self->xsize,
            self->ysize,
            line->color0,
            line->color1,
            type);
        if ((self->gpu_flag & GpuFlags::SOFTWARE_GPU) == 0)
        {
            surface_unlock(&self->surface0);
        }
    }

    // 0x0040C840
    static void __stdcall trans_priority_list(Marni* self, MarniOt* pOt)
    {
        if (pOt == nullptr)
            return;

        if (self->gpu_flag & GpuFlags::SOFTWARE_GPU)
        {
            // Not implemented (software rendering?)
        }
        else
        {
            Prim* v8;
            while ((v8 = ot_get_primitive(pOt)) != nullptr)
            {
                if ((v8->type & 0xFE00) != 0)
                {
                    trans_matrix(self, v8);
                }
                else
                {
                    switch (v8->type & 0xFFFFF)
                    {
                    case 0: break;
                    case 17: draw_line_flat(self, (PrimLine2*)v8); break;
                    case 18: draw_line_gourad(self, (PrimLine2*)v8); break;
                    case 33:
                    case 36:
                    case 37:
                    case 38:
                    case 44:
                    case 45:
                    case 46:
                    case 61:
                    case 69:
                    case 70:
                    case 73:
                    case 76:
                    case 77: trans_spr_poly(self, pOt, (PrimSprite*)v8); break;
                    case 88:
                    case 0x100 | 88: trans_object(self, pOt, v8); break;
                    case 256: sub_407480(self, v8); break;
                    case 0x10000 | 44:
                    case 0x10000 | 45:
                    case 0x10000 | 73:
                    case 0x10000 | 76:
                    case 0x10000 | 77: trans_spr_poly(self, pOt, (PrimSprite*)v8); break;
                    default:
                        out("passed invalid primitive header...", "Marni::TransPriorityList");
                        self->is_gpu_active = 0;
                        return;
                    }
                }
            }
        }
    }

    // 0x0040cbd0

    // 0x0040ccd0

    // 0x0040ce50

    // 0x0040CFD0
    static int __stdcall sub_40CFD0(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return 0;
    }

    // Prim type 0x1002C quad layout (float z projection, 4 int16 coords, 4 texcoords)
    struct PrimSprQuad : Prim
    {
        uint32_t texture; // 0x0008
        uint32_t var_0C;  // 0x000C
        float z;          // 0x0010
        int16_t x0;       // 0x0014
        int16_t y0;       // 0x0016
        int16_t x1;       // 0x0018
        int16_t y1;       // 0x001A
        uint8_t u0;       // 0x001C
        uint8_t v0;       // 0x001D
        uint8_t u1;       // 0x001E
        uint8_t v1;       // 0x001F
    };
    static_assert(sizeof(PrimSprQuad) == 0x20);

    // 0x0040D300
    static int __stdcall sub_40D300(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return 0;
    }

    // 0x0040D560
    // Scaled sprite primitive (type 0x2D) -> 4 TL vertices. Unlike the
    // int16 quad builders, the primitive carries a float centre (x, y) and a
    // projection depth z (floats at offsets 0x10..0x18); the quad half-size is
    // half the texture-coordinate span ((u1-u0)/2, (v1-v0)/2) projected by
    // 1/z. Colour bytes B,G,R,A are read from the tail of the primitive; the
    // B/G/R bytes are doubled, with values >= 256 clamping to 255 and the
    // excess spilled into the vertex specular field. The alpha channel depends
    // on the type's 0x100000..0x400000 mode bits and gpu_flag bit 0x4000.
    static int __stdcall sub_40D560(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return 0;
    }

    // 0x40D8D0
    static int __stdcall sub_40D8D0(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return 0;
    }

    // 0x0040E6E0

    // 0x0040DBA0
    static int __stdcall MarniDrawPolyFT4(Marni* self, PrimSprite* pPrim, DrawInfo* drawInfo)
    {
        return 0;
    }

    // 0x0040DD90
    static int __stdcall sub_40DD90(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return 0;
    }

    // 0x0040DF60
    static int __stdcall sub_40DF60(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return 0;
    }

    // 0x0040DF70
    static int __stdcall trans_spr_poly(Marni* self, MarniOt* pOt, PrimSprite* pPrim)
    {
        return 0;
    }

    // 0x0040e6e0

    // 0x0040E770
    static void set_filtering(Marni* self, uint8_t a2) {}

    // 0x0040E800
    static void __stdcall sub_40E800(Marni* self, uint8_t a2)
    {
        self->field_700C = a2 == 0 ? -1 : 0;
        self->num_draw_ops = a2 == 0 ? -1 : 0;
    }

    // 0x0040EAF0
    static int __stdcall do_draw_op(Marni* self, int index)
    {
        return 0;
    }

    // 0x0040ec90

    // 0x0040ECA0
    static int __stdcall surfacex_create_texture_object(MarniSurfaceX* self)
    {
        return 0;
    }

    // 0x0040ED20
    static MarniSurfaceX* __stdcall surfacex_ctor(MarniSurfaceX* self)
    {
        // MarniSurfaceX::Ctor — MarniSurfaceY::Ctor then override the vtbl with
        // MarniSurfaceX::vTbl (0x517358) and clear the texture fields.
        surfacey_ctor((MarniSurfaceY*)self);
        self->vtbl = (MarniSurfaceVTBL*)0x00517358;
        self->texture_handle = 0;
        self->pTexture = nullptr;
        return self;
    }

    // 0x0040EDB0
    static void __stdcall surfacex_dtor(MarniSurfaceX* self) {}

    // 0x0040EE00

    // 0x0040EE30
    static int __stdcall surfacex_load(MarniSurfaceX* self, MarniSurfaceX* pSrc)
    {
        return 0;
    }

    // 0x0040EE60
    static int invalidate_window(HWND hWnd, int width, int height, int /*fullscreen*/, LPRECT lpResRect)
    {
        // The window is resizable and decoupled from the render resolution, so
        // a display-mode change never resizes it here. Only the letterboxed
        // presentation rect is recomputed from the actual client area.
        // The original binary dropped the window to HWND_BOTTOM here to keep it
        // behind the desktop, but on a modern OS that just hides the game behind
        // other windows on every F8 press, so we skip the Z-order change.

        if (lpResRect)
            compute_windowed_window_rect(hWnd, width, height, lpResRect);

        // No InvalidateRect: SDL repaints continuously, so nothing needs an
        // explicit invalidation after a display-mode change.
        return 1;
    }

    // 0x0040EF50

    // 0x0040F090

    // 0x0040F0F0

    // 0x0040F250

    // 0x0040F2F0

    // 0x0040f370

    // 0x0040f380
    static int __stdcall surfacex_vfill(MarniSurfaceX* self, LPRECT pRect, uint32_t color, int mode)
    {
        return 0;
    }

    // 0x0040f520

    // 0x0040F580
    static int __stdcall surfacey_vrelease(MarniSurface2* self)
    {
        return 0;
    }

    // 0x0040F790
    static int __stdcall surfacex_vlock(MarniSurfaceX* self, int* a2, int* a3)
    {
        return 0;
    }

    // 0x0040f600
    static int __stdcall surfacex_vpallock(MarniSurfaceX* self, int* a2)
    {
        return 0;
    }

    // 0x0040f9c0
    static int __stdcall surfacex_vpalunlock(MarniSurfaceX* self)
    {
        return 0;
    }

    // 0x0040fad0
    static int __stdcall surfacex_vunlock(MarniSurfaceX* self)
    {
        return 0;
    }

    // MarniSurfaceX::vRelease (0x40EE00) — the release_fn slot of MarniSurfaceX::vTbl
    // (0x517358). Releases the texture if one is attached, then delegates to
    // MarniSurfaceY::vRelease (0x40F580). Used by surfacex_create_work to drop any
    // existing surface when the object is (re-)created in place.
    static void __stdcall surfacex_vrelease(MarniSurfaceX* self) {}

    // 0x0040fbe0
    static int surface_get_alpha_bits(MarniSurfaceX* self)
    {
        return 0;
    }

    // 0x004134C0
    char* surface_calc_address(MarniSurface* self, int x, int y)
    {
        if (!self->bOpen)
        {
            out("this Bits is invalid but you are trying to use the service.", "MarniBits::CalcAddress");
            return nullptr;
        }

        if (!self->bLocked)
        {
            out("this Bits doesn't be the Lock. You need to lock before doing this operation. MarniBits::CalcAddress", "");
            return nullptr;
        }

        if (self->width <= x || self->height <= y || x < 0 || y < 0)
        {
            out("the coordinate you specified is wrong...x=%d y=%d MarniBits::CalcAddress", "");
            return nullptr;
        }

        switch (self->bpp)
        {
        case 4:
            if (!self->var_2B)
                return (char*)self->pBitmap + x / 2 + y * self->pitch;
            [[fallthrough]];
        case 8: return (char*)self->pBitmap + y * self->pitch + x;
        case 0x10: return (char*)self->pBitmap + 2 * x + y * self->pitch;
        case 0x18: return (char*)self->pBitmap + 3 * x + y * self->pitch;
        case 0x20: return (char*)self->pBitmap + 4 * x + y * self->pitch;
        default: out("this BitPixel isn't supported...%d MarniBits::CalcAddress", ""); return nullptr;
        }
    }

    // 0x0040FFD0
    // MarniSurfaceX::CreateSurface — called by surfacex_create_work right after the
    // surface has been created. First validates the surface's pixel format
    // (surface_get_alpha_bits returns the bit depth of dwRGBAlphaBitMask), then locks
    // the surface and scans every pixel's alpha bits. If any alpha bit is clear the
    // surface is not fully opaque, and self->var_2C is set so the texture is drawn
    // through the alpha-blended draw-op path (see trans_spr_poly / tex_spr).
    static void surfacex_create_surface(MarniSurfaceX* self) {}

    // 0x0040FEF0
    MarniSurfaceY* __stdcall surfacey_ctor(MarniSurfaceY* self)
    {
        // Mirror MarniSurface2::Ctor call: zero the base surface (0x30 bytes) and set its
        // vtbl, then override the vtbl with MarniSurfaceY::vTbl and zero the texture fields.
        // NOTE: must NOT memset sizeof(MarniSurfaceY) — the embedded Marni::Surface0/Z/2/3
        // members are MarniSurface (0x3C) and surface3 sits at the exact tail of Marni.
        surface2_ctor(self);
        self->vtbl = (MarniSurfaceVTBL*)0x0051737C;
        self->pDDsurface = nullptr;
        self->pDDpalette = nullptr;
        self->var_29 = 0;
        self->bOpen = 0;
        self->var_27 = 0;
        return self;
    }

    // 0x0040FF20
    void __stdcall surfacey_dtor(MarniSurface2* self)
    {
        self->vtbl = (MarniSurfaceVTBL*)0x0051737C;
        surfacey_vrelease(self);
        surface2_release(self);
    }

    // I guess we are just jumping to 0x00412000+ then?

    // 0x00413730
    static int surface_get_palette_color(MarniSurface2* self, int col_index, int pal_index, uint32_t* color_out)
    {
        if (!self->bOpen)
        {
            out("you tried to use this class regardless of invalid class", "MarniBits::GetPaletteColor");
            return 0;
        }
        if (!self->bLocked && !self->bPalLocked)
        {
            out("don't forget to lock before using this", "MarniBits::GetPaletteColor");
            return 0;
        }
        if (!self->var_28)
        {
            out("this is not palette indexed class", "MarniBits::GetPaletteColor");
            return 0;
        }

        int colorsPerPal = 1 << self->bpp;
        if (colorsPerPal <= col_index)
        {
            out("you specified number which is out of range", "MarniBits::GetPaletteColor");
            return 0;
        }
        if (pal_index >= self->pal_cnt)
        {
            out("you specified number which is out of range of pal count", "MarniBits::GetPaletteColor");
            return 0;
        }

        uint32_t v8;
        if (self->var_25 == 16)
        {
            v8 = ((uint16_t*)self->pPalette)[col_index + pal_index * colorsPerPal];
        }
        else if (self->var_25 == 32)
        {
            v8 = ((uint32_t*)self->pPalette)[col_index + pal_index * colorsPerPal];
        }
        else
        {
            out("not supported type", "MarniBits::GetPaletteColor");
            return 0;
        }

        uint8_t a_bitcnt = self->desc.a_bitcnt;
        int v10;
        if (a_bitcnt == 0)
        {
            v10 = 255;
        }
        else
        {
            v10 = (self->desc.a_mask & (v8 >> self->desc.a_shift)) << (8 - a_bitcnt);
            if (v10)
                v10 |= 255 >> a_bitcnt;
        }

        int v12 = (self->desc.r_mask & (v8 >> self->desc.r_shift)) << (8 - self->desc.r_bitcnt);
        if (v12)
            v12 |= 255 >> self->desc.r_bitcnt;

        int v14 = (self->desc.g_mask & (v8 >> self->desc.g_shift)) << (8 - self->desc.g_bitcnt);
        if (v14)
            v14 |= 255 >> self->desc.g_bitcnt;

        int v16 = (self->desc.b_mask & (v8 >> self->desc.b_shift)) << (8 - self->desc.b_bitcnt);
        if (v16)
            v16 |= 255 >> self->desc.b_bitcnt;

        if (a_bitcnt != 0)
        {
            if (v12 || v14 || v16)
            {
                if (v10 == 0)
                {
                    // LOBYTE(v16) = LOBYTE(v14) = LOBYTE(v12) = 0
                    v16 = 0;
                    v14 = 0;
                    v12 = 0;
                }
            }
            else
            {
                v10 = 0;
            }
        }
        else
        {
            v10 = (v12 || v14 || v16) ? 0xFF : 0;
        }

        // pack 0xAARRGGBB
        *color_out = (uint8_t)v16 | (((uint8_t)v14 | ((((uint32_t)v10 << 8) | (uint8_t)v12) << 8)) << 8);
        return 1;
    }

    // 0x00411590
    static int adjust_rect(RECT* clip, const RECT* src, RECT* out)
    {
        // Returns 1 if src and clip overlap with positive area; writes the intersection to out. Returns 0 otherwise.
        LONG left = src->left;
        LONG clipRight = clip->right;
        if (left > clipRight)
            return 0;

        LONG top = src->top;
        LONG clipBottom = clip->bottom;
        if (top > clipBottom)
            return 0;

        LONG right = src->right;
        LONG clipLeft = clip->left;
        if (right < clipLeft)
            return 0;

        LONG bottom = src->bottom;
        LONG clipTop = clip->top;
        if (bottom < clipTop || right - left <= 0 || bottom - top <= 0 || clipRight - clipLeft <= 0
            || clipBottom - clipTop <= 0)
            return 0;

        if (left >= clipLeft)
            clipLeft = left;
        if (top < clipTop)
            top = clipTop;
        if (right > clipRight)
            right = clipRight;
        if (bottom <= clipBottom)
            clipBottom = bottom;

        out->right = right;
        out->left = clipLeft;
        out->top = top;
        out->bottom = clipBottom;
        return 1;
    }

    // 0x00412BD0
    static int __stdcall surface2_vfill(MarniSurface2* self, LPRECT pSrcRect, uint32_t color, int mode)
    {
        return 0;
    }

    // Skipping a lot until we talk about what the heck is going on.
    // 0x00412d20

    // 0x00414750
    int __stdcall surface2_create_work(MarniSurface2* self, int width, int height, int depth, int palBpp, int palCnt)
    {
        // The object can be re-created in place, so release anything it currently owns
        // (vtbl->release_fn at offset 0x20).
        surface_release(self);

        self->width = (int16_t)width;
        self->height = (int16_t)height;
        self->bpp = (uint8_t)depth;

        // depth is the pixel bit depth (-4 = 4-bit paletted, 4 = 4bpp, 8, 16 or 32).
        switch (depth)
        {
        case -4:
            self->pBitmap = operator_new((size_t)(height * width));
            self->pitch = (int16_t)width;
            self->bpp = 4;
            self->var_2B = 1;
            break;
        case 4:
            self->pBitmap = operator_new((size_t)(height * width / 2));
            self->pitch = (int16_t)(width / 2);
            break;
        case 8:
            self->pBitmap = operator_new((size_t)(height * width));
            self->pitch = (int16_t)width;
            break;
        case 16:
            self->pBitmap = operator_new((size_t)(2 * height * width));
            self->pitch = (int16_t)(2 * width);
            break;
        case 32:
            self->pBitmap = operator_new((size_t)(4 * height * width));
            self->pitch = (int16_t)(4 * width);
            break;
        default: goto unsupported_format;
        }

        if (!self->pBitmap)
        {
            out("", "MarniBits::CreateWork");
            return 0;
        }

        self->var_25 = (uint8_t)palBpp;
        if (palBpp)
        {
            self->pal_cnt = (palCnt > 0) ? (int16_t)palCnt : 1;
            self->var_28 = 1; // Is_paletted

            size_t allocSize;
            if (self->bpp == 4)
            {
                allocSize = (size_t)(16 * self->pal_cnt * (palBpp >> 3));
            }
            else if (self->bpp == 8)
            {
                allocSize = (size_t)((self->pal_cnt * (palBpp >> 3)) << 8);
            }
            else
            {
                goto unsupported_format;
            }

            self->pPalette = operator_new(allocSize);
            if (!self->pPalette)
            {
                out("", "failed to create the work for palette MarniBits::CreateWork");
                operator_delete(self->pBitmap);
                self->pBitmap = nullptr;
                return 0;
            }
        }

        // Fill in the pixel format descriptor for the surface's bit depth.
        switch (self->bpp)
        {
        case 4:
        case 8:
        case 0x10:
            self->desc.a_shift = 15;
            self->desc.a_bitcnt = 1;
            self->desc.a_mask = 1;
            self->desc.r_shift = 10;
            self->desc.r_bitcnt = 5;
            self->desc.r_mask = 31;
            self->desc.g_shift = 5;
            self->desc.g_bitcnt = 5;
            self->desc.g_mask = 31;
            self->desc.b_bitcnt = 5;
            self->desc.b_mask = 31;
            self->desc.b_shift = 0;
            break;
        case 0x20:
            self->desc.a_shift = 24;
            self->desc.a_bitcnt = 8;
            self->desc.a_mask = 0xFF;
            self->desc.r_shift = 16;
            self->desc.r_bitcnt = 8;
            self->desc.r_mask = 0xFF;
            self->desc.g_shift = 8;
            self->desc.g_bitcnt = 8;
            self->desc.g_mask = 0xFF;
            self->desc.b_bitcnt = 8;
            self->desc.b_mask = 0xFF;
            self->desc.b_shift = 0;
            break;
        default: break;
        }

        self->bOpen = 1;
        self->var_27 = 1;
        self->var_29 = 1;
        return 1;

    unsupported_format:
        out("unsupported format...%d MarniBits::CreateWork", "");
        return 0;
    }

    // 0x00413950
    static int surface_set_palette_color(MarniSurface2* self, int col_index, int pal_index, uint32_t rgb, int mode)
    {
        if (!self->bOpen || (!self->bLocked && !self->bPalLocked) || !self->var_28 || (1 << self->bpp) <= col_index
            || pal_index >= self->pal_cnt)
        {
            out("you tried to use this class regardless of invalid class.", "MarniBits::SetPaletteColor");
            return 0;
        }

        // Split the incoming color into its ARGB components.
        uint32_t v7 = (rgb >> 16) & 0xFF;  // red
        uint32_t rgba = (rgb >> 8) & 0xFF; // green
        uint32_t v9 = (rgb >> 24) & 0xFF;  // alpha
        uint32_t v10 = rgb & 0xFF;         // blue

        if (mode == 128)
        {
            // Additive blending: add the existing palette entry to the new color.
            uint32_t oldColor;
            surface_get_palette_color(self, col_index, pal_index, &oldColor);
            v7 += (oldColor >> 16) & 0xFF;
            rgba += (oldColor >> 8) & 0xFF;
            v9 = (oldColor >> 24) & 0xFF;
            v10 = (oldColor & 0xFF) + v10;
            if (v7 > 0xFF)
                v7 = 0xFF;
            if (rgba > 0xFF)
                rgba = 0xFF;
            if (v10 > 0xFF)
                v10 = 0xFF;
        }
        else if (mode == 512)
        {
            // Alpha blending: mix the new color with the existing palette entry.
            uint32_t v16 = v9 * rgba / 0xFF;
            uint32_t v17 = v9 * v10 / 0xFF;
            uint32_t oldColor;
            surface_get_palette_color(self, col_index, pal_index, &oldColor);
            uint32_t v11 = oldColor;
            uint32_t rgbb = (0xFF - v9) * ((oldColor >> 16) & 0xFF) / 0xFF;
            uint32_t oldGreen = (0xFF - v9) * ((oldColor >> 8) & 0xFF) / 0xFF;
            v7 = rgbb + v9 * v7 / 0xFF;
            uint32_t v12 = (0xFF - v9) * (oldColor & 0xFF) / 0xFF;
            v9 = (oldColor >> 24) & 0xFF;
            rgba = oldGreen + v16;
            v10 = v17 + v12;
        }

        uint8_t a_bitcnt = self->desc.a_bitcnt;
        if (a_bitcnt)
            goto label_26;
        if (v7 || rgba || v10)
        {
            if (!v9)
            {
                v10 = 0;
                v7 = 0;
                rgba = 0;
                goto label_27;
            }
        label_26:
            if (v9 == 0xFF)
                goto label_28;
            goto label_27;
        }
        v9 = 0;
    label_27:
        self->var_2C = 1;
    label_28:
        uint8_t palbpp = self->var_25;
        int v15 = ((self->desc.a_mask & (v9 >> (8 - a_bitcnt))) << self->desc.a_shift)
            | ((self->desc.r_mask & (v7 >> (8 - self->desc.r_bitcnt))) << self->desc.r_shift)
            | ((self->desc.g_mask & (rgba >> (8 - self->desc.g_bitcnt))) << self->desc.g_shift)
            | ((self->desc.b_mask & (v10 >> (8 - self->desc.b_bitcnt))) << self->desc.b_shift);
        if (palbpp == 16)
        {
            *(uint16_t*)((uint8_t*)self->pPalette + 2 * col_index + 2 * pal_index * (1 << self->bpp)) = (uint16_t)v15;
            return 1;
        }
        if (palbpp == 32)
        {
            *(uint32_t*)((uint8_t*)self->pPalette + 4 * col_index + 4 * pal_index * (1 << self->bpp)) = (uint32_t)v15;
            return 1;
        }
        out("not supported type.", "MarniBits::SetPaletteColor");
        return 1;
    }

    // 0x004149D0
    void __stdcall surface2_ctor(MarniSurface2* self)
    {
        std::memset(self, 0, sizeof(*self));
        self->vtbl = (MarniSurfaceVTBL*)0x005173B0;
    }

    // 0x00414A30
    void __stdcall surface2_release(MarniSurface2* self)
    {
        self->vtbl = (MarniSurfaceVTBL*)0x005173B0;
        surface2_vrelease(self);
    }

    // 0x00414A40
    int __stdcall surface2_vrelease(MarniSurface2* self)
    {
        if (self->bLocked)
        {
            surface_unlock(self);
        }
        if (self->var_27 && self->var_29)
        {
            operator_delete(self->pBitmap);
            operator_delete(self->pPalette);
        }

        self->pPalette = nullptr;
        self->pBitmap = nullptr;
        self->bPalLocked = 0;
        self->bLocked = 0;
        std::memset(&self->desc, 0, sizeof(self->desc));
        self->var_25 = 0;
        self->bpp = 0;
        self->pitch = 0;
        self->height = 0;
        self->width = 0;
        self->var_2B = 0;
        self->var_2A = 0;
        self->var_28 = 0;
        self->var_27 = 0;
        self->bOpen = 0;
        self->var_29 = 0;
        self->var_22 = 0;
        return 1;
    }

    // 0x004141A0
    static int surface_get_color(MarniSurface2* self, int x, int y, uint32_t* color_out)
    {
        auto* addr = surface_calc_address((MarniSurface*)self, x, y);
        if (!addr)
        {
            out("initialization failed", "MarniBits::GetColor");
            return 0;
        }

        uint32_t color; // value read from the pixel (raw surface pixel, may be a palette index)
        switch (self->bpp)
        {
        case 4:
            color = *(uint8_t*)addr;
            if (self->var_2B)
            {
                // Byte-per-pixel layout; the whole byte is the value.
                break;
            }
            if (self->var_2A)
            {
                if ((x & 1) == 0)
                {
                    color &= 0xF;
                    break;
                }
            }
            else if ((x & 1) != 0)
            {
                color &= 0xF;
                break;
            }
            color >>= 4;
            color &= 0xF;
            break;
        case 8: color = *(uint8_t*)addr; break;
        case 16: color = *(uint16_t*)addr; break;
        case 32: color = *(uint32_t*)addr; break;
        default: out("unsupported bit pixel", "MarniBits::GetColor"); return 0;
        }

        *color_out = color;
        return 1;
    }

    // 0x00413C60
    static int surface_apply_hue(MarniSurface2* self, int col_index, uint32_t rgb, int mode)
    {
        return surface_set_palette_color(self, col_index, self->var_22, rgb, mode);
    }

    // 0x004130D0
    static int __stdcall surface_operator_eq(MarniSurface* self, MarniSurface* pSrc)
    {
        if (!pSrc->bOpen)
        {
            out("this Bits is invalid but you are trying to use the service.", "MarniBits::operator =");
            return 0;
        }

        if (!self->var_27)
        {
            // Shallow copy: the destination shares the source's buffers.
            if (!pSrc->var_29)
            {
                out("this Bits is not in memory, so the no-copy copy is not possible.", "MarniBits::operator =");
                return 0;
            }

            self->pBitmap = pSrc->pBitmap;
            self->pPalette = pSrc->pPalette;
            *(uint32_t*)&self->desc = *(uint32_t*)&pSrc->desc;
            pSrc->bLocked = 0;
            *(uint32_t*)&self->desc.g_mask = *(uint32_t*)&pSrc->desc.g_mask;
            *(uint32_t*)&self->desc.b_bitcnt = *(uint32_t*)&pSrc->desc.b_bitcnt;
            self->bpp = pSrc->bpp;
            self->var_25 = pSrc->var_25;
            self->width = pSrc->width;
            self->height = pSrc->height;
            self->pitch = pSrc->pitch;
            self->var_28 = pSrc->var_28;
            self->var_2A = pSrc->var_2A;
            self->pal_cnt = pSrc->pal_cnt;
            self->var_2B = pSrc->var_2B;
            self->var_2D = pSrc->var_2D;
            self->var_2C = pSrc->var_2C;
            self->var_27 = 0;
            self->bOpen = 1;
            self->var_29 = 1;
            self->bLocked = 0;
            return 1;
        }

        if (!self->bOpen)
            return 0;

        if (self->var_28)
        {
            if (pSrc->var_28)
            {
                if (self->bpp != pSrc->bpp)
                {
                    out("palette conversion was specified (8-4)", "MarniBits::operator =");
                    goto fail;
                }
                surface_pal_blt(self, pSrc, -1, -1);
            }
            else
            {
                if (!surface_lock(self, 0, 0))
                {
                    out("failed to lock.", "MarniBits::operator =");
                    goto fail;
                }
                for (uint32_t i = 0; i < (uint32_t)(1u << self->bpp); ++i)
                {
                    surface_apply_hue(self, i, (i & 3 | (32 * (i & 0x1C | (32 * (i & 0xE0))))) << 6, 0);
                }
                surface_unlock(self);
            }
        }

        if (memcmp(&pSrc->desc, &self->desc, 0xC) == 0 && self->width == pSrc->width && self->height == pSrc->height
            && !self->var_28 && !pSrc->var_28 && self->bpp == pSrc->bpp)
        {
            // Descriptor matches: do a direct per-row copy.
            surface_lock(self, 0, 0);
            surface_lock(pSrc, 0, 0);

            for (int row = 0; row < (int)self->height; ++row)
            {
                char* dst = surface_calc_address(self, 0, row);
                char* src = surface_calc_address(pSrc, 0, row);
                switch (self->bpp)
                {
                case 8:
                    for (int i = 0; i < (int)self->width / 4; ++i)
                    {
                        *(uint32_t*)dst = *(uint32_t*)src;
                        dst += 4;
                        src += 4;
                    }
                    memcpy(dst, src, (size_t)(self->width % 4));
                    break;
                case 16:
                    for (int i = 0; i < (int)self->width / 2; ++i)
                    {
                        *(uint32_t*)dst = *(uint32_t*)src;
                        dst += 4;
                        src += 4;
                    }
                    memcpy(dst, src, (size_t)(2 * (self->width % 2)));
                    break;
                case 32:
                    for (int i = 0; i < (int)self->width; ++i)
                    {
                        *(uint32_t*)dst = *(uint32_t*)src;
                        dst += 4;
                        src += 4;
                    }
                    break;
                default: out("it is direct-loaded, not supported.", "MarniBits::operator ="); return 0;
                }
            }

            surface_unlock(self);
            surface_unlock(pSrc);
            return 1;
        }

        // Fall back to a hardware blit of the full rectangles.
        RECT rc;
        RECT v18;
        SetRect(&rc, 0, 0, self->width - 1, self->height - 1);
        SetRect(&v18, 0, 0, pSrc->width - 1, pSrc->height - 1);
        if (surface2_blt(self, &rc, &v18, pSrc, 0, 0))
        {
            return 1;
        }

        out("copy failed.", "MarniBits::operator =");

    fail:
        surface_unlock(self);
        surface_unlock(pSrc);
        self->bOpen = 0;
        return 0;
    }

    // 0x00414AC0
    static int __stdcall surface3_vrelease(MarniSurface3* self)
    {
        surface2_vrelease(self);
        self->pDDsurface = nullptr;
        return 1;
    }

    // 0x00413C80
    static int surface_set_color(MarniSurface2* self, int x, int y, uint32_t color, int alpha)
    {
        // SetColor does not support alpha; the caller must pass 0.
        if (alpha != 0)
        {
            out("alpha is not supported", "MarniBits::SetColor");
            return 0;
        }

        char* addr = surface_calc_address((MarniSurface*)self, x, y);
        if (!addr)
        {
            out("initialization failed", "MarniBits::SetColor");
            return 0;
        }

        switch (self->bpp)
        {
        case 4:
        {
            // 4bpp: the color is the palette index; nibble-packed unless var_2B is set.
            uint8_t old = *(uint8_t*)addr;
            if (self->var_2B)
            {
                // Unpacked layout (one byte per pixel). NOTE: mirroring the original
                // binary, the byte is written back unchanged — the color is never
                // applied for this layout (the value is read and stored as-is).
                *(uint8_t*)addr = old;
            }
            else if (self->var_2A)
            {
                if ((x & 1) != 0)
                    *(uint8_t*)addr = (uint8_t)((old & 0xF) | ((color & 0xF) << 4));
                else
                    *(uint8_t*)addr = (uint8_t)((old & 0xF0) | (color & 0xF));
            }
            else
            {
                if ((x & 1) != 0)
                    *(uint8_t*)addr = (uint8_t)((old & 0xF0) | (color & 0xF));
                else
                    *(uint8_t*)addr = (uint8_t)((old & 0xF) | ((color & 0xF) << 4));
            }
            return 1;
        }
        case 8: *(uint8_t*)addr = (uint8_t)color; return 1;
        case 16: *(uint16_t*)addr = (uint16_t)color; return 1;
        case 32: *(uint32_t*)addr = color; return 1;
        default:
            // 24bpp (and anything else) is not handled by SetColor.
            out("not supported", "MarniBits::SetColor");
            return 1;
        }
    }

    // 0x004144E0
    static int surface_get_index_color(MarniSurface2* self, int x, int y, uint32_t* color_out)
    {
        auto* addr = surface_calc_address((MarniSurface*)self, x, y);
        if (!addr)
        {
            out("initialization failed", "MarniBits::GetIndexColor");
            return 0;
        }

        if (!self->var_28)
        {
            out("not a palette-index surface", "MarniBits::GetIndexColor");
            return 0;
        }

        int bpp = self->bpp;
        uint32_t palette_index; // value read from the pixel (used as the palette entry index)
        switch (bpp)
        {
        case 4:
            palette_index = *(uint8_t*)addr;
            if (self->var_2B)
            {
                // Byte-per-pixel layout; the whole byte is the palette index.
                break;
            }
            if (self->var_2A)
            {
                if ((x & 1) == 0)
                {
                    palette_index &= 0xF;
                    break;
                }
            }
            else if ((x & 1) != 0)
            {
                palette_index &= 0xF;
                break;
            }
            palette_index >>= 4;
            palette_index &= 0xF;
            break;
        case 8: palette_index = *(uint8_t*)addr; break;
        case 16: palette_index = *(uint16_t*)addr; break;
        case 32: palette_index = *(uint32_t*)addr; break;
        default: out("unsupported bit pixel", "MarniBits::GetIndexColor"); return 0;
        }

        // The palette is laid out as [var_22][1 << bpp] (row-major), so the flat
        // entry index is palette_index + var_22 * (1 << bpp). The shift count is
        // masked to 5 bits, matching the original `shl` instruction semantics.
        int palette_stride = 1 << (bpp & 31);
        int pal_offset = palette_index + palette_stride * self->var_22;
        uint32_t pal_entry;
        switch (self->var_25) // palette bpp
        {
        case 8: pal_entry = *(uint8_t*)((uint8_t*)self->pPalette + pal_offset); break;
        case 16: pal_entry = *(uint16_t*)((uint8_t*)self->pPalette + 2 * pal_offset); break;
        case 32: pal_entry = *(uint32_t*)((uint8_t*)self->pPalette + 4 * pal_offset); break;
        default: out("unsupported bit pixel (pal)", "MarniBits::GetIndexColor"); return 0;
        }

        // Expand the palette entry's bit fields into 8-bit A/R/G/B channels.
        int a_bitcnt = self->desc.a_bitcnt;
        int alpha = 255;
        if (a_bitcnt != 0)
        {
            alpha = (int)((self->desc.a_mask & (pal_entry >> self->desc.a_shift)) << (8 - a_bitcnt));
            if (alpha != 0)
                alpha |= 255 >> a_bitcnt;
        }
        int red = (int)((self->desc.r_mask & (pal_entry >> self->desc.r_shift)) << (8 - self->desc.r_bitcnt));
        if (red != 0)
            red |= 255 >> self->desc.r_bitcnt;
        int green = (int)((self->desc.g_mask & (pal_entry >> self->desc.g_shift)) << (8 - self->desc.g_bitcnt));
        if (green != 0)
            green |= 255 >> self->desc.g_bitcnt;
        int blue = (int)((self->desc.b_mask & (pal_entry >> self->desc.b_shift)) << (8 - self->desc.b_bitcnt));
        if (blue != 0)
            blue |= 255 >> self->desc.b_bitcnt;

        // 0xAARRGGBB
        *color_out = ((uint32_t)(uint8_t)alpha << 24) | ((uint32_t)(uint8_t)red << 16) | ((uint32_t)(uint8_t)green << 8)
            | (uint32_t)(uint8_t)blue;
        return 1;
    }

    // 0x00414AE0
    static void __stdcall surface3_dtor(MarniSurface3* self)
    {
        self->vtbl = (MarniSurfaceVTBL*)0x005173D4;
        surface3_vrelease(self);
        surface2_release(self);
    }

    // 0x004142D0
    static int surface_get_current_color(MarniSurface2* self, int x, int y, uint32_t* color_out)
    {
        // Palette-index surfaces look the pixel up in the palette instead.
        if (self->var_28)
            return surface_get_index_color(self, x, y, color_out);

        auto* addr = surface_calc_address((MarniSurface*)self, x, y);
        if (!addr)
        {
            out("initialization failed", "MarniBits::GetCurrentColor");
            return 0;
        }

        uint32_t pixel; // raw pixel value read from the surface
        switch (self->bpp)
        {
        case 4:
            pixel = *(uint8_t*)addr;
            // Two pixels per byte; var_2A selects which nibble is the low pixel.
            if (self->var_2A)
            {
                if ((x & 1) == 0)
                    pixel &= 0xF;
                else
                    pixel >>= 4;
            }
            else
            {
                if ((x & 1) == 0)
                    pixel >>= 4;
                else
                    pixel &= 0xF;
            }
            break;
        case 8: pixel = *(uint8_t*)addr; break;
        case 16: pixel = *(uint16_t*)addr; break;
        case 32: pixel = *(uint32_t*)addr; break;
        default: out("unsupported bit pixel", "MarniBits::GetColor"); return 0;
        }

        // Expand the pixel's bit fields into 8-bit A/R/G/B channels.
        int a_bitcnt = self->desc.a_bitcnt;
        int alpha = 255;
        if (a_bitcnt != 0)
        {
            alpha = (int)((self->desc.a_mask & (pixel >> self->desc.a_shift)) << (8 - a_bitcnt));
            if (alpha != 0)
                alpha |= 255 >> a_bitcnt;
        }
        int red = (int)((self->desc.r_mask & (pixel >> self->desc.r_shift)) << (8 - self->desc.r_bitcnt));
        if (red != 0)
            red |= 255 >> self->desc.r_bitcnt;
        int green = (int)((self->desc.g_mask & (pixel >> self->desc.g_shift)) << (8 - self->desc.g_bitcnt));
        if (green != 0)
            green |= 255 >> self->desc.g_bitcnt;
        int blue = (int)((self->desc.b_mask & (pixel >> self->desc.b_shift)) << (8 - self->desc.b_bitcnt));
        if (blue != 0)
            blue |= 255 >> self->desc.b_bitcnt;

        // A fully transparent pixel (alpha == 0) collapses to black, and a pixel
        // with no color channels forces alpha to zero.
        if (red || green || blue)
        {
            if (!alpha)
            {
                blue = 0;
                green = 0;
                red = 0;
            }
        }
        else
        {
            alpha = 0;
        }

        // 0xAARRGGBB
        *color_out = ((uint32_t)(uint8_t)alpha << 24) | ((uint32_t)(uint8_t)red << 16) | ((uint32_t)(uint8_t)green << 8)
            | (uint32_t)(uint8_t)blue;
        return 1;
    }

    // 0x00413710
    static int __stdcall surface_set_index_color(int x, int y, uint32_t color, int mode)
    {
        // Original is a no-op stub: it only logs via MarniOut() and returns 1.
        // All parameters are unused. SetCurrentColor calls it when the surface
        // is palette-based; this type is "not supported" by the original.
        out("not supported this type. MarniBits::SetIndexColor", "");
        return 1;
    }

    // 0x00413DD0
    static int surface_set_current_color(MarniSurface2* self, int x, int y, uint32_t color, int mode)
    {
        // Paletted surfaces are handled by the palette-index path.
        if (self->var_28)
            return surface_set_index_color(x, y, color, mode);

        auto* addr = surface_calc_address((MarniSurface*)self, x, y);
        if (!addr)
        {
            out("initialization failed", "MarniBits::SetCurrentColor");
            return 0;
        }

        // Split the 0xAARRGGBB color into 8-bit channels.
        uint32_t red = (color >> 16) & 0xFF;
        uint32_t green = (color >> 8) & 0xFF;
        uint32_t blue = color & 0xFF;
        uint32_t alpha = (color >> 24) & 0xFF;

        uint32_t current;
        if (mode == 128) // additive blend
        {
            // Add the source color to the pixel already in the surface, clamping
            // each channel at 255. The alpha channel is taken from the surface.
            surface_get_current_color(self, x, y, &current);
            red += (current >> 16) & 0xFF;
            green += (current >> 8) & 0xFF;
            blue += current & 0xFF;
            alpha = (current >> 24) & 0xFF;
            if (red > 0xFF)
                red = 0xFF;
            if (green > 0xFF)
                green = 0xFF;
            if (blue > 0xFF)
                blue = 0xFF;
        }
        else if (mode == 512) // alpha blend
        {
            // src * alpha / 255 + dst * (255 - alpha) / 255; alpha from the surface.
            uint32_t srcRed = alpha * red / 0xFF;
            uint32_t srcGreen = alpha * green / 0xFF;
            uint32_t srcBlue = alpha * blue / 0xFF;
            surface_get_current_color(self, x, y, &current);
            red = srcRed + (255 - alpha) * ((current >> 16) & 0xFF) / 0xFF;
            green = srcGreen + (255 - alpha) * ((current >> 8) & 0xFF) / 0xFF;
            blue = srcBlue + (255 - alpha) * (current & 0xFF) / 0xFF;
            alpha = (current >> 24) & 0xFF;
        }

        // Pack the channels into the surface pixel format via the field masks.
        uint32_t packed = ((uint32_t)(self->desc.r_mask & (red >> (8 - self->desc.r_bitcnt))) << self->desc.r_shift)
            | ((uint32_t)(self->desc.g_mask & (green >> (8 - self->desc.g_bitcnt))) << self->desc.g_shift)
            | ((uint32_t)(self->desc.b_mask & (blue >> (8 - self->desc.b_bitcnt))) << self->desc.b_shift)
            | ((uint32_t)(self->desc.a_mask & (alpha >> (8 - self->desc.a_bitcnt))) << self->desc.a_shift);

        switch (self->bpp)
        {
        case 8:
            if (mode & 2)
                *(uint8_t*)addr = (uint8_t)(*(uint8_t*)addr & packed);
            else if (mode & 4)
                *(uint8_t*)addr = (uint8_t)(*(uint8_t*)addr | packed);
            else
                *(uint8_t*)addr = (uint8_t)packed;
            break;
        case 16:
            if (mode & 2)
                *(uint16_t*)addr = (uint16_t)(*(uint16_t*)addr & packed);
            else if (mode & 4)
                *(uint16_t*)addr = (uint16_t)(*(uint16_t*)addr | packed);
            else
                *(uint16_t*)addr = (uint16_t)packed;
            break;
        case 24:
            if (mode & 2)
            {
                *(uint16_t*)addr = (uint16_t)(*(uint16_t*)addr & packed);
                addr[2] = (uint8_t)(addr[2] & (packed >> 16));
            }
            else if (mode & 4)
            {
                *(uint16_t*)addr = (uint16_t)(*(uint16_t*)addr | packed);
                addr[2] = (uint8_t)(addr[2] | (packed >> 16));
            }
            else
            {
                *(uint16_t*)addr = (uint16_t)packed;
                addr[2] = (uint8_t)(packed >> 16);
            }
            break;
        case 32:
            if (mode & 2)
                *(uint32_t*)addr &= packed;
            else if (mode & 4)
                *(uint32_t*)addr |= packed;
            else
                *(uint32_t*)addr = packed;
            break;
        default: out("this bitpixel isn't supported", "MarniBits::SetCurrentColor"); break;
        }
        return 1;
    }

    // 0x00412ED0
    static void __stdcall MarniBits_SetAddress(MarniSurface2* self, void* address, int flags)
    {
        // Returns 1 on success, 0 on failure in the original; no caller uses the result.
        if (self->bOpen)
        {
            out("this class is active and the value could not be set", "MarniBits::SetAddress");
            return;
        }

        if (self->var_27 == 1)
        {
            out("tried to set on this class (possible leak)", "MarniBits::SetAddress");
            return;
        }

        self->var_27 = 0;
        self->pBitmap = address;
        self->pPalette = (void*)flags;
    }

    // 0x00412D20
    static int __stdcall MarniBits_FileOut(MarniSurface* self, const char* lpFileName, int /*a3*/)
    {
        if (!self->bOpen)
        {
            out("surface is not open", "MarniBits::FileOut");
            return 0;
        }

        auto dataSize = static_cast<int>(3 * self->width * self->height + 58);
        auto* buffer = (uint8_t*)std::malloc(dataSize);
        if (!buffer)
        {
            out("failed to allocate memory", "MarniBits::FileOut");
            return 0;
        }

        // BMP file header (14 bytes)
        *(uint16_t*)(buffer + 0) = 0x4D42;   // 'BM'
        *(uint32_t*)(buffer + 2) = dataSize; // file size
        *(uint16_t*)(buffer + 6) = 0;        // reserved1
        *(uint16_t*)(buffer + 8) = 0;        // reserved2
        *(uint32_t*)(buffer + 10) = 58;      // offset to pixel data

        // DIB header (BITMAPINFOHEADER, 40 bytes)
        *(uint32_t*)(buffer + 14) = 40;           // header size
        *(uint32_t*)(buffer + 18) = self->width;  // width
        *(uint32_t*)(buffer + 22) = self->height; // height
        *(uint16_t*)(buffer + 26) = 1;            // planes
        *(uint16_t*)(buffer + 28) = 24;           // bit count
        *(uint32_t*)(buffer + 30) = 0;            // compression
        *(uint32_t*)(buffer + 34) = 0;            // image size
        *(uint32_t*)(buffer + 38) = 0;            // x pixels per meter
        *(uint32_t*)(buffer + 42) = 0;            // y pixels per meter
        *(uint32_t*)(buffer + 46) = 0;            // colors used
        *(uint32_t*)(buffer + 50) = 0;            // important colors

        MarniSurface2 ecx0a;
        surface2_ctor(&ecx0a);

        ecx0a.desc.r_bitcnt = 8;
        ecx0a.desc.g_shift = 8;
        ecx0a.desc.g_bitcnt = 8;
        ecx0a.desc.b_bitcnt = 8;
        ecx0a.desc.r_mask = 0xFF;
        ecx0a.desc.g_mask = 0xFF;
        ecx0a.desc.b_mask = 0xFF;
        ecx0a.width = self->width;
        ecx0a.height = self->height;
        ecx0a.desc.r_shift = 16;
        ecx0a.desc.b_shift = 0;
        ecx0a.bpp = 24;
        ecx0a.var_25 = 0;
        ecx0a.pitch = 3 * self->width;

        MarniBits_SetAddress(&ecx0a, buffer + 58, 0);
        ecx0a.var_29 = 1;
        ecx0a.bOpen = 1;
        ecx0a.var_27 = 1;

        surface2_blt(&ecx0a, nullptr, nullptr, self, 32, 0);

        ecx0a.var_27 = 0;

        if (FILE* file = std::fopen(lpFileName, "wb"))
        {
            std::fwrite(buffer, 1, dataSize, file);
            std::fclose(file);
        }
        std::free(buffer);

        surface2_release(&ecx0a);
        return 1;
    }

    // 0x00401EF0
    void __stdcall movie_kill(Marni* self)
    {
        kill_movie(self);
    }

    // 0x004164C0
    static void __stdcall polygon_object_dtor(PolygonObject* self)
    {
        interop::thiscall<int, void*>(0x004164C0, self);
    }

    // 0x00416630
    static MarniOt* __stdcall ot_ctor(MarniOt* self, size_t a2, int a3)
    {
        return interop::thiscall<MarniOt*, MarniOt*, size_t, int>(0x00416630, self, a2, a3);
    }

    // 0x004164D0
    static Prim* __stdcall ot_get_primitive(MarniOt* self)
    {
        return interop::thiscall<Prim*, MarniOt*>(0x004164D0, self);
    }

    // 0x00416500
    static int __stdcall ot_add_primitive_as_z(MarniOt* self, Prim* pPrim, int z)
    {
        if (self->is_valid)
        {
            auto n = std::clamp(z, 0, self->zdepth - 1);
            auto last = self->pHead + (self->zdepth - n) - 1;
            pPrim->pNext = last->pNext;
            last->pNext = pPrim;
            return 1;
        }
        else
        {
            out("not valid class", "cPriorityList2::AddPrimitiveAsZ");
            return 0;
        }
    }

    // 0x00416550
    static int __stdcall ot_clear(MarniOt* self)
    {
        if (!self->is_valid)
            return 0;

        for (auto i = 0; i < self->zdepth - 1; i++)
        {
            auto& prim = self->pHead[i];
            prim.pNext = &prim + 1;
        }

        auto& lastPrim = self->pHead[self->zdepth - 1];
        lastPrim.pNext = nullptr;
        lastPrim.type = 0;
        self->pCurrent = self->pHead;
        return 1;
    }

    // 0x004165B0
    static int __stdcall ot_alloc(MarniOt* self, int depth, int a3)
    {
        return interop::thiscall<int, MarniOt*, int, int>(0x004165B0, self, depth, a3);
    }

    // 0x00416610
    static void __stdcall ot_dtor(MarniOt* self)
    {
        cstd_free(self->pHead);
        self->pHead = nullptr;
        self->zdepth = 0;
        self->is_valid = 0;
    }

    // 0x00416670
    static uint8_t __stdcall sub_416670(MarniOt* pOt)
    {
        return pOt->var_10;
    }

    // 0x004168F0
    static MarniTextureNode* __stdcall search_texture_object_0_from_1_in_condition(Marni* self, int handle, int index)
    {
        auto texture = self->textures[handle];
        if ((texture.var_00 & 0x2000) != 0)
            return nullptr;

        switch (texture.var_00 & ~0x14)
        {
        case 1:
        case 2:
        case 0x81:
        case 0x82:
        {
            auto n = search_texture_object_0_from_1(self, handle, 0);
            return n == 0 ? nullptr : &self->texture_nodes[n];
        }
        case 0x22:
        case 0x41:
        case 0x42:
        case 0xC1:
        case 0xC2:
        {
            auto n = search_texture_object_0_from_1(self, handle, index);
            return n == 0 ? nullptr : &self->texture_nodes[n];
        }
        case 0xA1:
        case 0xA2:
        {
            auto n = search_texture_object_0_from_1(self, handle, 0);
            if (n == 0)
                return 0;

            auto result = &self->texture_nodes[n];
            if (index < 0 || index >= texture.surface.pal_cnt)
                return 0;

            return result;
        }
        default: return nullptr;
        }
    }

    // 0x00416AF0
    static uint16_t __stdcall search_texture_object_0_from_1(Marni* self, int handle, int index)
    {
        if (handle > 256)
            return 0;

        auto surface = &self->textures[handle];
        if (surface->var_00 == 0)
            return 0;

        auto result = surface->head;
        for (auto i = 0; i < index; i++)
        {
            result = self->texture_nodes[result].next;
        }
        return result;
    }

    // 0x00416B90
    static int __stdcall sub_416B90(Marni* self, int a2)
    {
        auto& node = self->texture_nodes[a2];
        if (node.var_14 != 0)
        {
            if (node.surface)
            {
                surfacex_dtor(node.surface);
                operator_delete(node.surface);
            }
            node.surface = nullptr;
            node.next = 0;
            node.var_14 = 0;
        }
        return 1;
    }

    // 0x00416BE0
    static void __stdcall texture_surface_release(Marni* self, int handle)
    {
        auto& texture = self->textures[handle];
        if (texture.var_00 != 0)
        {
            auto current = texture.head;
            do
            {
                auto next = self->texture_nodes[current].next;
                sub_416B90(self, current);
                current = next;
            } while (current != 0);
            surface_release(&texture.surface);
            texture.var_00 = 0;
        }
    }

    // 0x00416730
    static int __stdcall suspend_texture_use(Marni* self, int handle)
    {
        if ((self->gpu_flag & GpuFlags::SOFTWARE_GPU) != 0)
            return 1;

        if (handle >= 256)
        {
            out("invalid handle. Marni::SuspendTextureUse", "Marni::SuspendTextureUse");
            return 0;
        }

        auto& texture = self->textures[handle];
        if (texture.var_00 == 0)
        {
            out("this texture is invalid. Marni::SuspendTextureUse", "Marni::SuspendTextureUse");
            return 0;
        }
        if ((texture.var_00 & 0x2000) != 0)
        {
            out("this texture is already suspended. Marni::SuspendTextureUse", "Marni::SuspendTextureUse");
            return 0;
        }

        // The Marni texture slots are never populated by openre
        // (create_texture_handle routes uploads straight to the SDL renderer
        // and texture_pages[].handle stores the renderer handle), so this
        // slot's node chain is uninitialized debug-CRT fill (0xCD). Walking it
        // would free garbage and crash in _CrtIsValidHeapPointer. There are no
        // MarniSurfaceX objects to release here; keep the renderer texture
        // alive (ResumeTextureUse only marks the slot active again) and mark
        // the slot suspended the same way the original does.
        logging::logInfo("[marni] SuspendTextureUse no-op (handle={})", handle);
        texture.var_00 |= 0x2000;
        return 1;
    }

    // tex_spr rasterizer state (shared globals written by TexSpr, consumed by the texspr_* routines)
    static int& s_uRange = *(int*)0x662A08; // srcU1 - srcU0
    static int& s_vRange = *(int*)0x662A0C; // srcV1 - srcV0
    static uint8_t*& s_dstBase = *(uint8_t**)0x662A10;
    static uint8_t*& s_srcBase = *(uint8_t**)0x662A14;
    static MarniSurface2*& s_dstSurface = *(MarniSurface2**)0x662A18;
    static int& s_color = *(int*)0x662A1C; // 0xAARRGGBB blend color
    static int& s_dstWidth = *(int*)0x662E20;
    static int& s_dstHeight = *(int*)0x662E24;
    static uint16_t*& s_palette = *(uint16_t**)0x662E28;
    static MarniSurface2*& s_srcSurface = *(MarniSurface2**)0x662E2C;
    static int& s_rowCount = *(int*)0x662E30; // clipped height; adjusted by the rasterizers
    static int& s_clipWidth = *(int*)0x662E34;
    static int& s_srcU0 = *(int*)0x662E38;
    static int& s_srcV0 = *(int*)0x662E3C;
    static int& s_pitchShift = *(int*)0x662E40;
    static int& s_clipLeft = *(int*)0x662E44;
    static int& s_y = *(int*)0x662E48; // current destination y
    static int& s_clipLeftOff = *(int*)0x662E4C;
    static int& s_clipTopOff = *(int*)0x662E50;

    static int& s_lutInit = *(int*)0x660A00;
    static auto* s_xTable = (int*)0x660A08;         // destination x -> source x lookup
    static auto* s_gammaTable = (uint8_t*)0x650A00; // 256x256 gamma ramp
    static auto* s_lutR = (uint16_t*)0x64CA00;      // 565/555 red scaling LUT (256x32 words)
    static auto* s_lutG = (uint16_t*)0x644A00;      // green
    static auto* s_lutB = (uint16_t*)0x648A00;      // blue
    static auto* s_paletteScratch = (uint16_t*)0x662A20;

    // 0x00413630
    static uint8_t* surface_calc_pal_address(MarniSurface2* self, int x, int y)
    {
        if (!self->var_28)
            return 0;
        if (!self->bOpen || !self->bLocked)
        {
            out("this Bits is invalid but you are trying to use the service.", "MarniBits::CalcPalAddress");
            return 0;
        }
        int palCount = 1 << self->bpp;
        if (x >= palCount || y >= self->pal_cnt || x < 0 || y < 0)
        {
            out("the coordinate you specified is wrong...x=%d y=%d MarniBits::CalcPalAddress", "");
            return 0;
        }
        if (self->var_25 == 16)
            return (uint8_t*)self->pPalette + 2 * (x + y * palCount);
        if (self->var_25 != 32)
        {
            out("this BitPixel isn't supported...%d MarniBits::CalcPalAddress", "");
            return 0;
        }
        return (uint8_t*)self->pPalette + 4 * (x + y * palCount);
    }

    // Source row byte offset for a given interpolated row.
    static int texspr_src_row(int row)
    {
        if (s_pitchShift)
            return (s_srcV0 + row) << s_pitchShift;
        return s_srcSurface->pitch * (s_srcV0 + row);
    }

    // Destination y for a given row index.
    static int texspr_src_y(int row)
    {
        return s_srcV0 + s_vRange * (row + s_clipTopOff) / s_dstHeight;
    }

    // 0x0042D4D0
    static int texspr_copy()
    {
        for (int row = 0; row < s_rowCount; ++row)
        {
            int srcY = texspr_src_y(row);
            int xOff = s_clipLeftOff;
            int dstX = s_clipLeft;
            int endX = s_clipWidth + xOff;
            while (xOff < endX)
            {
                uint32_t pixel;
                surface_get_current_color(s_srcSurface, s_xTable[xOff], srcY, &pixel);
                surface_set_current_color(s_dstSurface, dstX, s_y, pixel, 0);
                ++xOff;
                ++dstX;
            }
            ++s_y;
        }
        return s_rowCount;
    }

    // 0x0042DA80
    static int texspr_copy_alpha()
    {
        for (int row = 0; row < s_rowCount; ++row)
        {
            int srcY = texspr_src_y(row);
            int xOff = s_clipLeftOff;
            int dstX = s_clipLeft;
            int endX = s_clipWidth + xOff;
            while (xOff < endX)
            {
                uint32_t pixel;
                surface_get_current_color(s_srcSurface, s_xTable[xOff], srcY, &pixel);
                if (pixel)
                    surface_set_current_color(s_dstSurface, dstX, s_y, pixel, 0);
                ++xOff;
                ++dstX;
            }
            ++s_y;
        }
        return s_rowCount;
    }

    // 0x0042D910
    static int texspr_copy_modulate_alpha()
    {
        for (int row = 0; row < s_rowCount; ++row)
        {
            int srcY = texspr_src_y(row);
            int xOff = s_clipLeftOff;
            int dstX = s_clipLeft;
            int endX = s_clipWidth + xOff;
            while (xOff < endX)
            {
                uint32_t pixel;
                surface_get_current_color(s_srcSurface, s_xTable[xOff], srcY, &pixel);
                if (pixel)
                {
                    int r = ((pixel >> 16) & 0xFF) * ((s_color >> 16) & 0xFF) >> 7;
                    int g = ((s_color >> 8) & 0xFF) * ((pixel >> 8) & 0xFF) >> 7;
                    int b = (pixel & 0xFF) * (s_color & 0xFF) >> 7;
                    if (r > 255)
                        r = 255;
                    if (g > 255)
                        g = 255;
                    if (b > 255)
                        b = 255;
                    uint32_t outColor = (uint32_t)b | ((uint32_t)(g | (r << 8)) << 8);
                    surface_set_current_color(s_dstSurface, dstX, s_y, outColor, 0);
                }
                ++xOff;
                ++dstX;
            }
            ++s_y;
        }
        return s_rowCount;
    }

    // 0x0042D590
    static int texspr_copy_modulate_avg()
    {
        for (int row = 0; row < s_rowCount; ++row)
        {
            int srcY = texspr_src_y(row);
            int xOff = s_clipLeftOff;
            int dstX = s_clipLeft;
            int endX = s_clipWidth + xOff;
            while (xOff < endX)
            {
                uint32_t pixel;
                uint32_t dstColor;
                surface_get_current_color(s_srcSurface, s_xTable[xOff], srcY, &pixel);
                surface_get_current_color(s_dstSurface, dstX, s_y, &dstColor);
                if (pixel)
                {
                    int r = ((pixel >> 16) & 0xFF) * ((s_color >> 16) & 0xFF) >> 7;
                    int g = ((s_color >> 8) & 0xFF) * ((pixel >> 8) & 0xFF) >> 7;
                    int b = (pixel & 0xFF) * (s_color & 0xFF) >> 7;
                    if (r > 255)
                        r = 255;
                    if (g > 255)
                        g = 255;
                    if (b > 255)
                        b = 255;
                    uint32_t outColor = (uint32_t)(uint8_t)(b / 2 + ((dstColor >> 1) & 0x7F))
                        | ((uint32_t)(uint16_t)(g / 2 + ((dstColor >> 9) & 0x7F)) << 8)
                        | (((r / 2 + ((dstColor >> 17) & 0x7F)) << 16) & 0xFF0000);
                    surface_set_current_color(s_dstSurface, dstX, s_y, outColor, 0);
                }
                ++xOff;
                ++dstX;
            }
            ++s_y;
        }
        return s_rowCount;
    }

    // 0x0042D760
    static int texspr_copy_additive()
    {
        for (int row = 0; row < s_rowCount; ++row)
        {
            int srcY = texspr_src_y(row);
            int xOff = s_clipLeftOff;
            int dstX = s_clipLeft;
            int endX = s_clipWidth + xOff;
            while (xOff < endX)
            {
                uint32_t pixel;
                uint32_t dstColor;
                surface_get_current_color(s_srcSurface, s_xTable[xOff], srcY, &pixel);
                surface_get_current_color(s_dstSurface, dstX, s_y, &dstColor);
                if (pixel)
                {
                    int r = ((dstColor >> 16) & 0xFF) + (((pixel >> 16) & 0xFF) * ((s_color >> 16) & 0xFF) >> 7);
                    int g = ((dstColor >> 8) & 0xFF) + (((s_color >> 8) & 0xFF) * ((pixel >> 8) & 0xFF) >> 7);
                    int b = (dstColor & 0xFF) + ((pixel & 0xFF) * (s_color & 0xFF) >> 7);
                    if (r > 255)
                        r = 255;
                    if (g > 255)
                        g = 255;
                    if (b > 255)
                        b = 255;
                    uint32_t outColor = (uint32_t)b | ((uint32_t)(g | (r << 8)) << 8);
                    surface_set_current_color(s_dstSurface, dstX, s_y, outColor, 0);
                }
                ++xOff;
                ++dstX;
            }
            ++s_y;
        }
        return s_rowCount;
    }

    // 0x0042DB40
    static int texspr_pal_copy()
    {
        int* xTable = &s_xTable[s_clipLeftOff];
        uint16_t* palette = s_palette;
        uint16_t* dst = (uint16_t*)(s_dstBase + s_y * s_dstSurface->pitch + 2 * s_clipLeft);
        int interp = ((s_vRange << 16) / s_dstHeight) * s_clipTopOff;
        int step = (s_vRange << 16) / s_dstHeight;
        s_rowCount += s_clipTopOff;
        while (s_clipTopOff < s_rowCount)
        {
            uint8_t* srcPtr = s_srcBase + texspr_src_row(interp >> 16);
            if (s_clipLeft < s_clipWidth + s_clipLeft)
            {
                int count = s_clipWidth;
                int* p = xTable;
                do
                {
                    int xOff = *p++;
                    int idx = *(uint8_t*)(srcPtr + xOff);
                    if (idx)
                        *dst = palette[idx];
                    ++dst;
                } while (--count);
            }
            dst = (uint16_t*)((uint8_t*)dst + s_dstSurface->pitch);
            interp += step;
            ++s_clipTopOff;
        }
        return interp;
    }

    // 0x0042EE30
    static int texspr_pal_copy_565()
    {
        int* xTable = &s_xTable[s_clipLeftOff];
        uint16_t* palette = s_palette;
        uint16_t* dst = (uint16_t*)(s_dstBase + s_y * s_dstSurface->pitch + 2 * s_clipLeft);
        int interp = ((s_vRange << 16) / s_dstHeight) * s_clipTopOff;
        int step = (s_vRange << 16) / s_dstHeight;
        s_rowCount += s_clipTopOff;
        while (s_clipTopOff < s_rowCount)
        {
            uint8_t* srcPtr = s_srcBase + texspr_src_row(interp >> 16);
            if (s_clipLeft < s_clipWidth + s_clipLeft)
            {
                int count = s_clipWidth;
                int* p = xTable;
                do
                {
                    int xOff = *p++;
                    *dst++ = palette[*(uint8_t*)(srcPtr + xOff)];
                } while (--count);
            }
            dst = (uint16_t*)((uint8_t*)dst + s_dstSurface->pitch);
            interp += step;
            ++s_clipTopOff;
        }
        return interp;
    }

    // 0x0042DC80
    static int texspr_pal_halfadd_565_1px()
    {
        int* xTable = &s_xTable[s_clipLeftOff];
        uint16_t* palette = s_palette;
        uint16_t* dst = (uint16_t*)(s_dstBase + s_y * s_dstSurface->pitch + 2 * s_clipLeft);
        int interp = ((s_vRange << 16) / s_dstHeight) * s_clipTopOff;
        int step = (s_vRange << 16) / s_dstHeight;
        s_rowCount += s_clipTopOff;
        while (s_clipTopOff < s_rowCount)
        {
            uint8_t* srcPtr = s_srcBase + texspr_src_row(interp >> 16);
            if (s_clipLeft < s_clipWidth + s_clipLeft)
            {
                int count = s_clipWidth;
                int* p = xTable;
                do
                {
                    int xOff = *p++;
                    int idx = *(uint8_t*)(srcPtr + xOff);
                    if (idx)
                        *dst = (uint16_t)(((*dst >> 1) & 0x7BEF) + ((palette[idx] >> 1) & 0x7BEF));
                    ++dst;
                } while (--count);
            }
            dst = (uint16_t*)((uint8_t*)dst + s_dstSurface->pitch);
            interp += step;
            ++s_clipTopOff;
        }
        return interp;
    }

    // 0x0042E090
    static int texspr_pal_halfadd_555_1px()
    {
        int* xTable = &s_xTable[s_clipLeftOff];
        uint16_t* palette = s_palette;
        uint16_t* dst = (uint16_t*)(s_dstBase + s_y * s_dstSurface->pitch + 2 * s_clipLeft);
        int interp = ((s_vRange << 16) / s_dstHeight) * s_clipTopOff;
        int step = (s_vRange << 16) / s_dstHeight;
        s_rowCount += s_clipTopOff;
        while (s_clipTopOff < s_rowCount)
        {
            uint8_t* srcPtr = s_srcBase + texspr_src_row(interp >> 16);
            if (s_clipLeft < s_clipWidth + s_clipLeft)
            {
                int count = s_clipWidth;
                int* p = xTable;
                do
                {
                    int xOff = *p++;
                    int idx = *(uint8_t*)(srcPtr + xOff);
                    if (idx)
                        *dst = (uint16_t)(((*dst >> 1) & 0x3DEF) + ((palette[idx] >> 1) & 0x3DEF));
                    ++dst;
                } while (--count);
            }
            dst = (uint16_t*)((uint8_t*)dst + s_dstSurface->pitch);
            interp += step;
            ++s_clipTopOff;
        }
        return interp;
    }

    // 0x0042E9B0
    static int texspr_pal_add_565()
    {
        int* xTable = &s_xTable[s_clipLeftOff];
        uint16_t* palette = s_palette;
        uint16_t* dst = (uint16_t*)(s_dstBase + s_y * s_dstSurface->pitch + 2 * s_clipLeft);
        int interp = ((s_vRange << 16) / s_dstHeight) * s_clipTopOff;
        int step = (s_vRange << 16) / s_dstHeight;
        s_rowCount += s_clipTopOff;
        while (s_clipTopOff < s_rowCount)
        {
            uint8_t* srcPtr = s_srcBase + texspr_src_row(interp >> 16);
            if (s_clipLeft < s_clipWidth + s_clipLeft)
            {
                int count = s_clipWidth;
                int* p = xTable;
                do
                {
                    int xOff = *p++;
                    int idx = *(uint8_t*)(srcPtr + xOff);
                    if (idx)
                    {
                        int pal = palette[idx];
                        int dstv = *dst;
                        int r = (pal & 0xF800) + (dstv & 0xF800);
                        if (r > 0xF800)
                            r = 0xF800;
                        int g = (pal & 0x7E0) + (dstv & 0x7E0);
                        if (g > 0x7E0)
                            g = 0x7E0;
                        int b = (pal & 0x1F) + (dstv & 0x1F);
                        if (b > 0x1F)
                            b = 0x1F;
                        *dst = (uint16_t)(r | g | b);
                    }
                    ++dst;
                } while (--count);
            }
            dst = (uint16_t*)((uint8_t*)dst + s_dstSurface->pitch);
            interp += step;
            ++s_clipTopOff;
        }
        return interp;
    }

    // 0x0042EB60
    static int texspr_pal_add_555()
    {
        int* xTable = &s_xTable[s_clipLeftOff];
        uint16_t* palette = s_palette;
        uint16_t* dst = (uint16_t*)(s_dstBase + s_y * s_dstSurface->pitch + 2 * s_clipLeft);
        int interp = ((s_vRange << 16) / s_dstHeight) * s_clipTopOff;
        int step = (s_vRange << 16) / s_dstHeight;
        s_rowCount += s_clipTopOff;
        while (s_clipTopOff < s_rowCount)
        {
            uint8_t* srcPtr = s_srcBase + texspr_src_row(interp >> 16);
            if (s_clipLeft < s_clipWidth + s_clipLeft)
            {
                int count = s_clipWidth;
                int* p = xTable;
                do
                {
                    int xOff = *p++;
                    int idx = *(uint8_t*)(srcPtr + xOff);
                    if (idx)
                    {
                        int pal = palette[idx];
                        int dstv = *dst;
                        int r = (pal & 0x7C00) + (dstv & 0x7C00);
                        if (r > 0x7C00)
                            r = 0x7C00;
                        int g = (pal & 0x3E0) + (dstv & 0x3E0);
                        if (g > 0x3E0)
                            g = 0x3E0;
                        int b = (pal & 0x1F) + (dstv & 0x1F);
                        if (b > 0x1F)
                            b = 0x1F;
                        *dst = (uint16_t)(r | g | b);
                    }
                    ++dst;
                } while (--count);
            }
            dst = (uint16_t*)((uint8_t*)dst + s_dstSurface->pitch);
            interp += step;
            ++s_clipTopOff;
        }
        return interp;
    }

    // 0x0042E1D0
    static int texspr_pal_halfadd_565()
    {
        int* xTable = &s_xTable[s_clipLeftOff];
        uint16_t* palette = s_palette;
        uint16_t* dst = (uint16_t*)(s_dstBase + s_y * s_dstSurface->pitch + 2 * s_clipLeft);
        int interp = ((s_vRange << 16) / s_dstHeight) * s_clipTopOff;
        int step = (s_vRange << 16) / s_dstHeight;
        s_rowCount += s_clipTopOff;
        while (s_clipTopOff < s_rowCount)
        {
            uint8_t* srcPtr = s_srcBase + texspr_src_row(interp >> 16);
            if (s_clipLeft < s_clipWidth + s_clipLeft)
            {
                int count = s_clipWidth;
                int* p = xTable;
                do
                {
                    int xOff = *p++;
                    int idx = *(uint8_t*)(srcPtr + xOff);
                    if (idx)
                    {
                        int dstv = *dst;
                        int q = palette[idx] >> 2;
                        int r = (q & 0x3800) + (dstv & 0xF800);
                        if (r > 0xF800)
                            r = 0xF800;
                        int g = (dstv & 0x7E0) + (q & 0x1E0);
                        if (g > 0x7E0)
                            g = 0x7E0;
                        int b = (dstv & 0x1F) + (q & 7);
                        if (b > 0x1F)
                            b = 0x1F;
                        *dst = (uint16_t)(r | g | b);
                    }
                    ++dst;
                } while (--count);
            }
            dst = (uint16_t*)((uint8_t*)dst + s_dstSurface->pitch);
            interp += step;
            ++s_clipTopOff;
        }
        return interp;
    }

    // 0x0042E380
    static int texspr_pal_halfadd_555()
    {
        int* xTable = &s_xTable[s_clipLeftOff];
        uint16_t* palette = s_palette;
        uint16_t* dst = (uint16_t*)(s_dstBase + s_y * s_dstSurface->pitch + 2 * s_clipLeft);
        int interp = ((s_vRange << 16) / s_dstHeight) * s_clipTopOff;
        int step = (s_vRange << 16) / s_dstHeight;
        s_rowCount += s_clipTopOff;
        while (s_clipTopOff < s_rowCount)
        {
            uint8_t* srcPtr = s_srcBase + texspr_src_row(interp >> 16);
            if (s_clipLeft < s_clipWidth + s_clipLeft)
            {
                int count = s_clipWidth;
                int* p = xTable;
                do
                {
                    int xOff = *p++;
                    int idx = *(uint8_t*)(srcPtr + xOff);
                    if (idx)
                    {
                        int dstv = *dst;
                        int q = palette[idx] >> 2;
                        int r = (q & 0x1C00) + (dstv & 0x7C00);
                        if (r > 0x7C00)
                            r = 0x7C00;
                        int g = (dstv & 0x3E0) + (q & 0xE0);
                        if (g > 0x3E0)
                            g = 0x3E0;
                        int b = (dstv & 0x1F) + (q & 7);
                        if (b > 0x1F)
                            b = 0x1F;
                        *dst = (uint16_t)(r | g | b);
                    }
                    ++dst;
                } while (--count);
            }
            dst = (uint16_t*)((uint8_t*)dst + s_dstSurface->pitch);
            interp += step;
            ++s_clipTopOff;
        }
        return interp;
    }

    // 0x0042E530
    static int texspr_pal_alphablend_565()
    {
        int* xTable = &s_xTable[s_clipLeftOff];
        uint16_t* palette = s_palette;
        uint16_t* dst = (uint16_t*)(s_dstBase + s_y * s_dstSurface->pitch + 2 * s_clipLeft);
        int alpha = (s_color >> 24) & 0xFF;
        int inv = 255 - alpha;
        int interp = ((s_vRange << 16) / s_dstHeight) * s_clipTopOff;
        int step = (s_vRange << 16) / s_dstHeight;
        s_rowCount += s_clipTopOff;
        while (s_clipTopOff < s_rowCount)
        {
            uint8_t* srcPtr = s_srcBase + texspr_src_row(interp >> 16);
            if (s_clipLeft < s_clipWidth + s_clipLeft)
            {
                int count = s_clipWidth;
                int* p = xTable;
                do
                {
                    int xOff = *p++;
                    int idx = *(uint8_t*)(srcPtr + xOff);
                    if (idx)
                    {
                        int pal = palette[idx];
                        int dstv = *dst;
                        uint16_t r = (uint16_t)(((alpha * (pal & 0xF800) / 0xFF) & 0xF800)
                                                + ((inv * (dstv & 0xF800) / 0xFF) & 0xF800));
                        uint16_t g
                            = (uint16_t)(((alpha * (pal & 0x7E0) / 0xFF) & 0x7E0) + ((inv * (dstv & 0x7E0) / 0xFF) & 0x7E0));
                        uint16_t b = (uint16_t)(((alpha * (pal & 0x1F) / 0xFF) & 0x1F) + ((inv * (dstv & 0x1F) / 0xFF) & 0x1F));
                        *dst = (uint16_t)(r | g | b);
                    }
                    ++dst;
                } while (--count);
            }
            dst = (uint16_t*)((uint8_t*)dst + s_dstSurface->pitch);
            interp += step;
            ++s_clipTopOff;
        }
        return interp;
    }

    // 0x0042E770
    static int texspr_pal_alphablend_555_1px()
    {
        int* xTable = &s_xTable[s_clipLeftOff];
        uint16_t* palette = s_palette;
        uint16_t* dst = (uint16_t*)(s_dstBase + s_y * s_dstSurface->pitch + 2 * s_clipLeft);
        int alpha = (s_color >> 24) & 0xFF;
        int inv = 255 - alpha;
        int interp = ((s_vRange << 16) / s_dstHeight) * s_clipTopOff;
        int step = (s_vRange << 16) / s_dstHeight;
        s_rowCount += s_clipTopOff;
        while (s_clipTopOff < s_rowCount)
        {
            uint8_t* srcPtr = s_srcBase + texspr_src_row(interp >> 16);
            if (s_clipLeft < s_clipWidth + s_clipLeft)
            {
                int count = s_clipWidth;
                int* p = xTable;
                do
                {
                    int xOff = *p++;
                    int idx = *(uint8_t*)(srcPtr + xOff);
                    if (idx)
                    {
                        int pal = palette[idx];
                        int dstv = *dst;
                        uint16_t r = (uint16_t)(((alpha * (pal & 0x7C00) / 0xFF) & 0x7C00)
                                                + ((inv * (dstv & 0x7C00) / 0xFF) & 0x7C00));
                        uint16_t g
                            = (uint16_t)(((alpha * (pal & 0x3E0) / 0xFF) & 0x3E0) + ((inv * (dstv & 0x3E0) / 0xFF) & 0x3E0));
                        uint16_t b = (uint16_t)(((alpha * (pal & 0x1F) / 0xFF) & 0x1F) + ((inv * (dstv & 0x1F) / 0xFF) & 0x1F));
                        *dst = (uint16_t)(r | g | b);
                    }
                    ++dst;
                } while (--count);
            }
            dst = (uint16_t*)((uint8_t*)dst + s_dstSurface->pitch);
            interp += step;
            ++s_clipTopOff;
        }
        return interp;
    }

    // 0x0042DDC0
    static int texspr_pal_copy_2px_parity()
    {
        int row = s_clipTopOff;
        int* xTable = &s_xTable[s_clipLeftOff];
        uint16_t* palette = s_palette;
        uint16_t* dst = (uint16_t*)(s_dstBase + s_y * s_dstSurface->pitch + 2 * s_clipLeft);
        int interp = ((s_vRange << 16) / s_dstHeight) * s_clipTopOff;
        int step = (s_vRange << 16) / s_dstHeight;
        s_rowCount += s_clipTopOff;
        while (s_clipTopOff < s_rowCount)
        {
            uint8_t* srcPtr = s_srcBase + texspr_src_row(interp >> 16);
            int* p = xTable;
            int endX = s_clipWidth + s_clipLeft;
            uint16_t* dstRow = dst;
            uint16_t* nextRow = (uint16_t*)((uint8_t*)dst + s_dstSurface->pitch);
            // Skip the first pixel on odd destination rows (keeps 2-pixel alignment).
            if ((((uint8_t)s_clipLeft ^ (uint8_t)row) & 1) != 0)
            {
                ++dstRow;
                ++p;
                --endX;
            }
            if (s_clipLeft < endX)
            {
                int n = ((uint32_t)(endX - s_clipLeft - 1) >> 1) + 1;
                do
                {
                    int idx = *(uint8_t*)(srcPtr + *p);
                    if (idx)
                        *dstRow = palette[idx];
                    p += 2;
                    dstRow += 2;
                    --n;
                } while (n);
            }
            interp += step;
            ++row;
            s_clipTopOff = row;
            if (row >= s_rowCount)
                break;
            dst = nextRow;
        }
        return interp;
    }

    // 0x0042DF10
    static int texspr_pal_copy_2px_yparity()
    {
        int row = s_clipTopOff;
        int* xTable = &s_xTable[s_clipLeftOff];
        uint16_t* palette = s_palette;
        uint16_t* dst = (uint16_t*)(s_dstBase + s_y * s_dstSurface->pitch + 2 * s_clipLeft);
        int interp = ((s_vRange << 16) / s_dstHeight) * s_clipTopOff;
        int step = (s_vRange << 16) / s_dstHeight;
        s_rowCount += s_clipTopOff;
        while (s_clipTopOff < s_rowCount)
        {
            uint8_t* srcPtr = s_srcBase + texspr_src_row(interp >> 16);
            int* p = xTable;
            int endX = s_clipWidth + s_clipLeft;
            uint16_t* dstRow = dst;
            uint16_t* nextRow = (uint16_t*)((uint8_t*)dst + s_dstSurface->pitch);
            interp += step;
            if ((((uint8_t)row + (uint8_t)s_y) & 1) == 0)
            {
                if ((s_clipLeft & 1) != 0)
                {
                    ++dstRow;
                    ++p;
                    --endX;
                }
                if (s_clipLeft < endX)
                {
                    int n = ((uint32_t)(endX - s_clipLeft - 1) >> 1) + 1;
                    do
                    {
                        int idx = *(uint8_t*)(srcPtr + *p);
                        if (idx)
                            *dstRow = palette[idx];
                        p += 2;
                        dstRow += 2;
                        --n;
                    } while (n);
                }
            }
            s_clipTopOff = ++row;
            if (row >= s_rowCount)
                break;
            dst = nextRow;
        }
        return interp;
    }

    // 0x0042ED10
    static int texspr_direct_copy_16()
    {
        int* xTable = &s_xTable[s_clipLeftOff];
        uint16_t* dst = (uint16_t*)(s_dstBase + s_y * s_dstSurface->pitch + 2 * s_clipLeft);
        int interp = ((s_vRange << 16) / s_dstHeight) * s_clipTopOff;
        int step = (s_vRange << 16) / s_dstHeight;
        s_rowCount += s_clipTopOff;
        while (s_clipTopOff < s_rowCount)
        {
            uint8_t* srcPtr = s_srcBase + texspr_src_row(interp >> 16);
            if (s_clipLeft < s_clipWidth + s_clipLeft)
            {
                int count = s_clipWidth;
                int* p = xTable;
                do
                {
                    int xOff = *p++;
                    *dst++ = *(uint16_t*)(srcPtr + 2 * xOff);
                } while (--count);
            }
            dst = (uint16_t*)((uint8_t*)dst + s_dstSurface->pitch);
            interp += step;
            ++s_clipTopOff;
        }
        return interp;
    }

    // 0x0042EF70
    static int texspr_direct_copy_16_nz()
    {
        int* xTable = &s_xTable[s_clipLeftOff];
        uint16_t* dst = (uint16_t*)(s_dstBase + s_y * s_dstSurface->pitch + 2 * s_clipLeft);
        int interp = ((s_vRange << 16) / s_dstHeight) * s_clipTopOff;
        int step = (s_vRange << 16) / s_dstHeight;
        s_rowCount += s_clipTopOff;
        while (s_clipTopOff < s_rowCount)
        {
            uint8_t* srcPtr = s_srcBase + texspr_src_row(interp >> 16);
            if (s_clipLeft < s_clipWidth + s_clipLeft)
            {
                int count = s_clipWidth;
                int* p = xTable;
                do
                {
                    int xOff = *p++;
                    uint16_t val = *(uint16_t*)(srcPtr + 2 * xOff);
                    if (val)
                        *dst = val;
                    ++dst;
                } while (--count);
            }
            dst = (uint16_t*)((uint8_t*)dst + s_dstSurface->pitch);
            interp += step;
            ++s_clipTopOff;
        }
        return interp;
    }

    // 0x0042F090
    static int* texspr_direct_copy_16_1to1()
    {
        int width = s_clipWidth;
        uint16_t* dst = (uint16_t*)(s_dstBase + s_y * s_dstSurface->pitch + 2 * s_clipLeft);
        int row = s_clipTopOff;
        int srcPitchWords = (uint32_t)s_srcSurface->pitch >> 1;
        s_rowCount += s_clipTopOff;
        int rowSkipWords = srcPitchWords - s_clipWidth;
        uint16_t* src;
        if (s_pitchShift)
            src = (uint16_t*)(s_srcBase + ((s_clipTopOff + s_srcV0) << s_pitchShift) + 2 * (s_clipLeftOff + s_srcU0));
        else
            src = (uint16_t*)(s_srcBase + s_srcSurface->pitch * (s_clipTopOff + s_srcV0) + 2 * (s_clipLeftOff + s_srcU0));
        while (s_clipTopOff < s_rowCount)
        {
            uint32_t* dstDwords = (uint32_t*)dst;
            dst = (uint16_t*)((uint8_t*)dst + s_dstSurface->pitch);
            int copied = 0;
            if (width >= 2)
            {
                do
                {
                    *dstDwords++ = *(uint32_t*)src;
                    src += 2; // advance 4 bytes per dword, matching the OG int* source
                    ++copied;
                } while (copied < (s_clipWidth >> 1));
            }
            if ((width & 1) != 0)
            {
                *(uint16_t*)dstDwords = *src;
                src = (uint16_t*)((uint8_t*)src + 2);
            }
            src = (uint16_t*)((uint8_t*)src + 2 * rowSkipWords);
            s_clipTopOff = ++row;
        }
        return (int*)src;
    }

    // 0x0042F1A0
    static int texspr_pal_alpha_threshold()
    {
        int alpha = (s_color >> 24) & 0xFF;
        if (alpha <= 0x33)
            return alpha;
        if (alpha < 0x66)
            return texspr_pal_copy_2px_yparity();
        if (alpha < 0xCC)
            return texspr_pal_copy_2px_parity();
        return texspr_pal_copy();
    }

    // 0x0042F1D0
    static int tex_spr(
        MarniSurface2* dst, MarniSurface2* src, int dstLeft, int dstTop, int dstRight, int dstBottom, int srcU0, int srcV0,
        int srcU1, int srcV1, int clipLeft, int clipTop, int clipRight, int clipBottom, int color, int flags)
    {
        if (!dst->bOpen || !src->bOpen)
        {
            out("the Bits was invalid. TexSpr", "");
            return 0;
        }

        // Lazily build the gamma ramp and the 555/565 colour-scaling lookup tables.
        if (!s_lutInit)
        {
            for (int i = 0; i < 256; ++i)
            {
                int value = 0;
                uint8_t* rowDest = &s_gammaTable[i * 256];
                for (int j = 0; j < 256; ++j)
                {
                    int out = value / 128;
                    if (out > 255)
                        out = 255;
                    rowDest[j] = (uint8_t)out;
                    value += i;
                }
            }
            for (int i = 0; i < 256; ++i)
            {
                for (int k = 0; k < 32; ++k)
                {
                    int c = (k * i) / 128;
                    if (flags & 0x8000) // 555 tables
                    {
                        s_lutR[i * 32 + k] = (uint16_t)((c < 32) ? (c << 10) : 0x7C00);
                        s_lutG[i * 32 + k] = (uint16_t)((c < 32) ? (32 * c) : 0x3E0);
                        s_lutB[i * 32 + k] = (uint16_t)((c < 32) ? c : 31);
                    }
                    else // 565 tables
                    {
                        s_lutR[i * 32 + k] = (uint16_t)((c < 32) ? (c << 11) : 0xF800);
                        s_lutG[i * 32 + k] = (uint16_t)((c < 32) ? (32 * ((2 * k * i) / 128)) : 0x7E0);
                        s_lutB[i * 32 + k] = (uint16_t)((c < 32) ? c : 31);
                    }
                }
            }
            s_lutInit = 1;
        }

        if (dst->bpp != 16 && dst->bpp != 32)
        {
            out("this pixel size doesn't support till now. TexSpr", "");
            return 0;
        }

        // Reject rectangles that fall completely outside the clip window.
        if (dstLeft > clipRight || dstRight < clipLeft || dstTop > clipBottom || dstBottom < clipTop)
            return 1;

        s_srcSurface = src;
        s_color = color;
        int row = dstBottom - dstTop;
        s_dstSurface = dst;
        s_srcU0 = srcU0;
        s_uRange = srcU1 - srcU0;
        int col = dstRight - dstLeft;
        s_srcV0 = srcV0;
        s_dstWidth = dstRight - dstLeft;
        s_dstHeight = dstBottom - dstTop;
        s_vRange = srcV1 - srcV0;

        if (dstRight - dstLeft <= 0 || row <= 0)
            return 1;

        if (dstRight > clipRight)
            col = clipRight - dstLeft;
        s_clipWidth = col;
        if (dstBottom > clipBottom)
            row = clipBottom - dstTop;
        s_rowCount = row;

        if (dstLeft >= clipLeft)
        {
            s_clipLeft = dstLeft;
            s_clipLeftOff = 0;
        }
        else
        {
            s_clipLeft = clipLeft;
            s_clipLeftOff = clipLeft - dstLeft;
            s_clipWidth -= clipLeft - dstLeft;
        }

        if (dstTop >= clipTop)
        {
            s_y = dstTop;
            s_clipTopOff = 0;
        }
        else
        {
            s_y = clipTop;
            s_clipTopOff = clipTop - dstTop;
            s_rowCount = row - (clipTop - dstTop);
        }

        s_dstBase = (uint8_t*)surface_calc_address((MarniSurface*)dst, 0, 0);
        s_srcBase = (uint8_t*)surface_calc_address((MarniSurface*)src, 0, 0);
        uint8_t* palBase = surface_calc_pal_address(src, 0, 0);
        s_palette = (uint16_t*)(palBase + (1 << src->bpp) * src->var_22 * (src->var_25 >> 3));

        // "Optimized bits" path: mark the source format so the rasterizer can pick its palette layout.
        if ((flags & 0x4000) != 0)
        {
            if (src->bpp == 8 && src->var_28)
            {
                if (flags & 0x800000)
                    flags |= 0x200000; // 8-bit paletted 555
                else
                    flags |= 0x100000; // 8-bit paletted 565
            }
            else if (src->bpp == 16)
            {
                if (flags & 0x800000)
                    flags |= 0x400000; // 16-bit 555
                else
                    flags |= 0x300000; // 16-bit 565
                if (s_dstWidth == s_uRange && s_dstHeight == s_vRange)
                    flags |= 0x10000; // 1:1 copy
            }
        }

        // Source pitch -> shift amount.
        int pitch = src->pitch;
        if (pitch <= 128)
        {
            switch (pitch)
            {
            case 128: s_pitchShift = 7; break;
            case 64: s_pitchShift = 6; break;
            case 32: s_pitchShift = 5; break;
            case 16: s_pitchShift = 4; break;
            default: s_pitchShift = 0; break;
            }
        }
        else if (pitch == 256)
        {
            s_pitchShift = 8;
        }
        else if (pitch == 512)
        {
            s_pitchShift = 9;
        }
        else
        {
            s_pitchShift = 0;
        }

        // Precompute the destination x -> source x lookup table.
        if ((flags & 0x10000) == 0)
        {
            int acc = 0;
            for (int i = 0; i < s_dstWidth; ++i)
            {
                s_xTable[i] = srcU0 + acc / s_dstWidth;
                acc += s_uRange;
            }
        }

        // 8-bit paletted sources are converted into the destination's 565/555 layout
        // through the scaling LUTs, tinted by the blend colour.
        if ((flags & 0x2000) != 0)
        {
            if ((flags & 0x4000) != 0)
            {
                uint16_t* scratch = s_paletteScratch;
                int redMul = 32 * ((color >> 16) & 0xFF);
                int blueMul = 32 * (color & 0xFF);
                int greenMul = 32 * ((color >> 8) & 0xFF);
                int palCount = 1 << src->bpp;
                if ((flags & 0x300000) == 0x100000)
                {
                    for (int k = 0; k < palCount; ++k)
                    {
                        uint16_t v = s_palette[k];
                        scratch[k] = (uint16_t)(s_lutB[blueMul | (v & 0x1F)] | s_lutR[redMul | (v >> 11)]
                                                | s_lutG[greenMul | ((v >> 6) & 0x1F)]);
                    }
                }
                else if ((flags & 0x300000) == 0x200000)
                {
                    for (int k = 0; k < palCount; ++k)
                    {
                        uint16_t v = s_palette[k];
                        scratch[k] = (uint16_t)(s_lutB[blueMul | (v & 0x1F)] | s_lutR[redMul | (v >> 10)]
                                                | s_lutG[greenMul | ((v >> 5) & 0x1F)]);
                    }
                }
                s_palette = scratch;
            }
        }

        // Dispatch on the draw mode. Bits 14/15 (0x4000/0x8000) are ignored here.
        switch (flags & 0xFFFF3FFF)
        {
        case 0x1000: texspr_copy(); return 1;
        case 0x1001: texspr_copy_alpha(); return 1;
        case 0x2001: texspr_copy_modulate_alpha(); return 1;
        case 0x2011: texspr_copy_modulate_avg(); return 1;
        case 0x2021: texspr_copy_additive(); return 1;
        case 0x101000: texspr_pal_copy_565(); return 1;
        case 0x101001: texspr_pal_copy(); return 1;
        case 0x101011: texspr_pal_halfadd_565_1px(); return 1;
        case 0x101021: texspr_pal_add_565(); return 1;
        case 0x101031: texspr_pal_halfadd_565(); return 1;
        case 0x101041: texspr_pal_alphablend_565(); return 1;
        case 0x101111: texspr_pal_copy_2px_parity(); return 1;
        case 0x101121: texspr_pal_copy_2px_parity(); return 1;
        case 0x101131: texspr_pal_copy_2px_yparity(); return 1;
        case 0x101141: texspr_pal_alpha_threshold(); return 1;
        case 0x102001: texspr_pal_copy(); return 1;
        case 0x102011: texspr_pal_halfadd_565_1px(); return 1;
        case 0x102021: texspr_pal_add_565(); return 1;
        case 0x102031: texspr_pal_halfadd_565(); return 1;
        case 0x102041: texspr_pal_alphablend_565(); return 1;
        case 0x102111: texspr_pal_copy_2px_parity(); return 1;
        case 0x102121: texspr_pal_copy_2px_parity(); return 1;
        case 0x102131: texspr_pal_copy_2px_yparity(); return 1;
        case 0x102141: texspr_pal_alpha_threshold(); return 1;
        case 0x111000: texspr_pal_copy_565(); return 1;
        case 0x201000: texspr_pal_copy_565(); return 1;
        case 0x201001: texspr_pal_copy(); return 1;
        case 0x201011: texspr_pal_halfadd_555_1px(); return 1;
        case 0x201021: texspr_pal_add_555(); return 1;
        case 0x201031: texspr_pal_halfadd_555(); return 1;
        case 0x201041: texspr_pal_alphablend_555_1px(); return 1;
        case 0x201111: texspr_pal_copy_2px_parity(); return 1;
        case 0x201121: texspr_pal_copy_2px_parity(); return 1;
        case 0x201131: texspr_pal_copy_2px_yparity(); return 1;
        case 0x201141: texspr_pal_alpha_threshold(); return 1;
        case 0x202001: texspr_pal_copy(); return 1;
        case 0x202011: texspr_pal_halfadd_555_1px(); return 1;
        case 0x202021: texspr_pal_add_555(); return 1;
        case 0x202031: texspr_pal_halfadd_555(); return 1;
        case 0x202041: texspr_pal_alphablend_555_1px(); return 1;
        case 0x202111: texspr_pal_copy_2px_parity(); return 1;
        case 0x202121: texspr_pal_copy_2px_parity(); return 1;
        case 0x202131: texspr_pal_copy_2px_yparity(); return 1;
        case 0x202141: texspr_pal_alpha_threshold(); return 1;
        case 0x211000: texspr_pal_copy_565(); return 1;
        case 0x301000: texspr_direct_copy_16(); return 1;
        case 0x301001: texspr_direct_copy_16_nz(); return 1;
        case 0x311000: texspr_direct_copy_16_1to1(); return 1;
        case 0x311001: texspr_direct_copy_16_nz(); return 1;
        case 0x401000: texspr_direct_copy_16(); return 1;
        case 0x401001: texspr_direct_copy_16_nz(); return 1;
        case 0x411000: texspr_direct_copy_16_1to1(); return 1;
        case 0x411001: texspr_direct_copy_16_nz(); return 1;
        default: out("unexpected situation...%x TexSpr", ""); return 1;
        }
    }

    // 0x0044462E0 (matrix fill helper for the door scaler prims)
    static int set_door_prim(PrimScaler* scaler)
    {
        struct DoorMatrix
        {
            int16_t m[3][3]; // 0x0000
            int16_t pad;     // 0x0012
            int32_t t[3];    // 0x0014
        };
        static auto* s_rcMatrix = (DoorMatrix*)0x99CE80;
        static auto* s_llMatrix = (DoorMatrix*)0x99CE40;
        static auto* s_lcMatrix = (DoorMatrix*)0x99CE60;

        // Scale factor 1/4096 applied to the 16-bit matrix elements.
        constexpr double kScale = 0.000244140625;

        // rcMatrix row 0
        *(float*)&scaler[0].rgb0 = (float)(s_rcMatrix->m[0][0] * kScale);
        *(float*)&scaler[0].rgb1 = (float)(s_rcMatrix->m[0][1] * kScale);
        scaler[0].c_y = 0;
        *(float*)&scaler[0].c_x = (float)(s_rcMatrix->m[0][2] * kScale);
        // rcMatrix row 1
        scaler[0].rate_x = (float)(s_rcMatrix->m[1][0] * kScale);
        scaler[0].rate_y = (float)(s_rcMatrix->m[1][1] * kScale);
        scaler[0].var_28 = 0;
        *(float*)&scaler[0].var_24 = (float)(s_rcMatrix->m[1][2] * kScale);
        // rcMatrix row 2
        *(float*)&scaler[0].var_2C = (float)(s_rcMatrix->m[2][0] * kScale);
        *(float*)&scaler[0].var_30 = (float)(s_rcMatrix->m[2][1] * kScale);
        scaler[1].type = 0;
        scaler[1].prj = 0;
        scaler[1].rgb0 = 0;
        scaler[1].rgb1 = 0;
        *(float*)&scaler[1].pNext = (float)(s_rcMatrix->m[2][2] * kScale);
        // rcMatrix translation
        *(float*)&scaler[0].c_y = (float)s_rcMatrix->t[0];
        *(float*)&scaler[0].var_28 = (float)s_rcMatrix->t[1];
        scaler[1].c_x = 1065353216; // 1.0f
        *(float*)&scaler[1].type = (float)s_rcMatrix->t[2];

        // llMatrix row 0
        *(float*)&scaler[1].var_2C = (float)(s_llMatrix->m[0][0] * kScale);
        *(float*)&scaler[1].var_30 = (float)(-(s_llMatrix->m[0][1] * kScale));
        scaler[2].type = 0;
        *(float*)&scaler[2].pNext = (float)(s_llMatrix->m[0][2] * kScale);
        // llMatrix row 1
        *(float*)&scaler[2].prj = (float)(s_llMatrix->m[1][0] * kScale);
        *(float*)&scaler[2].rgb0 = (float)(-(s_llMatrix->m[1][1] * kScale));
        scaler[2].c_x = 0;
        *(float*)&scaler[2].rgb1 = (float)(s_llMatrix->m[1][2] * kScale);
        // llMatrix row 2
        *(float*)&scaler[2].c_y = (float)(s_llMatrix->m[2][0] * kScale);
        scaler[2].rate_x = (float)(-(s_llMatrix->m[2][1] * kScale));
        scaler[2].var_28 = 0;
        scaler[2].var_2C = 0;
        scaler[2].var_30 = 0;
        scaler[2].type = 0;
        scaler[2].c_x = 0;
        scaler[2].var_24 = 0;
        scaler[3].pNext = 0;
        scaler[2].rate_y = (float)(s_llMatrix->m[2][2] * kScale);

        // lcMatrix (16-bit, values shifted right by 4)
        *(float*)&scaler[3].type = (float)(s_lcMatrix->m[0][0] >> 4);
        *(float*)&scaler[3].prj = (float)(s_lcMatrix->m[0][1] >> 4);
        scaler[3].rgb1 = 0;
        *(float*)&scaler[3].rgb0 = (float)(s_lcMatrix->m[0][2] >> 4);
        *(float*)&scaler[3].c_x = (float)(s_lcMatrix->m[1][0] >> 4);
        *(float*)&scaler[3].c_y = (float)(s_lcMatrix->m[1][1] >> 4);
        scaler[3].rate_y = 0.0f;
        scaler[3].rate_x = (float)(s_lcMatrix->m[1][2] >> 4);
        *(float*)&scaler[3].var_24 = (float)(s_lcMatrix->m[2][0] >> 4);
        *(float*)&scaler[3].var_28 = (float)(s_lcMatrix->m[2][1] >> 4);
        scaler[4].pNext = 0;
        scaler[4].type = 0;
        scaler[4].prj = 0;
        scaler[3].rgb1 = 0;
        scaler[3].rate_y = 0.0f;
        scaler[3].var_30 = 0;
        *(float*)&scaler[3].var_2C = (float)(s_lcMatrix->m[2][2] >> 4);
        scaler[4].rgb0 = 0;

        const int result = s_rcMatrix->t[2] >> 4;
        if (result < 0)
            return 0;
        if (result > 4096)
            return 4096;
        return result;
    }

    // 0x00432BB0
    void unload_door_texture()
    {
        static auto* pDoorWork = (uint32_t*)0x669B28;        // door work pointer array
        static auto* pDoorWorkEnd = (uint32_t*)0x669B58;     // door work object handles [12]
        static auto* pDoorScalerBlock = (uint32_t*)0x669B88; // door scaler block pointer
        static auto* pDoorMdlh = (uint32_t*)0x669B8C;        // Door_mdlh[12]
        static auto* pDoorVar94 = (uint32_t*)0x669BBC;
        static auto* pDoorVarC4 = (uint32_t*)0x669BEC; // door texture handle

        if (*pDoorVarC4)
        {
            marni::unloadTexture(*pDoorVarC4);
            *pDoorVarC4 = 0;
        }

        for (int i = 0; i < 12; i++)
        {
            if (pDoorMdlh[i])
            {
                destroy_object(gGameTable.pMarni, pDoorMdlh[i]);
                pDoorMdlh[i] = 0;
            }
            if (pDoorWorkEnd[i])
            {
                destroy_object(gGameTable.pMarni, pDoorWorkEnd[i]);
                pDoorWorkEnd[i] = 0;
            }
        }

        if (*pDoorScalerBlock)
        {
            operator_delete((void*)*pDoorScalerBlock);
            *pDoorScalerBlock = 0;
        }

        auto* v1 = pDoorWork;
        while (v1 < pDoorWorkEnd)
        {
            if (*v1)
            {
                operator_delete((void*)*v1);
                *v1 = 0;
            }
            ++v1;
        }

        memset(pDoorVar94, 0, 0x30);
    }

    // 0x00432C60
    void door_disp0(int doorId, int a1, int a2, int a3)
    {
        static auto* pDoorScalerBlock = (uint32_t*)0x669B88;

        if (doorId >= 12)
            return;

        auto* scaler = (PrimScaler*)((char*)*pDoorScalerBlock + 224 * doorId);
        const int32_t type = scaler->type;
        *(uint32_t*)&scaler[1].rate_x = 0x00808080;
        scaler->type = type & 0xFF8FFFFF;

        int v7 = set_door_prim(scaler);
        if (a1)
        {
            scaler->type |= 0x200000;
            v7 = 2;
        }

        // The door scaler block is a persistent 224-byte record per door (0x669B88 + 224 * doorId);
        // TransObject reads the object handle at +0x4C, so the full block must reach the OT.
        // marni::add_scaler would truncate it to sizeof(PrimScaler), corrupting the object index.
        add_primitive_scaler(gGameTable.pMarni, (Prim*)scaler, a3 + (v7 >> 7));
    }

    // 0x004335A0
    static int calc_prj(const float* a1, uint16_t* a2, uint16_t* a3, uint16_t* a4)
    {
        struct DoorMatrix
        {
            int16_t m[3][3]; // 0x0000
            int16_t pad;     // 0x0012
            int32_t t[3];    // 0x0014
        };
        static auto* s_rcMatrix = (DoorMatrix*)0x99CE80;

        constexpr double k = 0.000244140625;
        const double v10 = s_rcMatrix->m[2][0] * k * a1[0] - s_rcMatrix->m[2][1] * k * a1[1] + s_rcMatrix->m[2][2] * k * a1[2];
        const double v8 = s_rcMatrix->t[0] + s_rcMatrix->m[0][0] * k * a1[0] - s_rcMatrix->m[0][1] * k * a1[1]
            + s_rcMatrix->m[0][2] * k * a1[2];
        double v4 = s_rcMatrix->t[2] + v10;
        if (v4 == 0.0)
            v4 = 1.0;
        const double v5 = gGameTable.global_prj;
        const double v9 = s_rcMatrix->t[1] + s_rcMatrix->m[1][0] * k * a1[0] - s_rcMatrix->m[1][1] * k * a1[1]
            + s_rcMatrix->m[1][2] * k * a1[2];
        const double v11 = 1.0 / v4 * v5 * v9 + 120.0;
        *a2 = (uint16_t)(int64_t)(1.0 / v4 * v5 * v8 + 160.0);
        *a3 = (uint16_t)(int64_t)v11;
        const int64_t v6 = (int64_t)v11;
        *a4 = (uint16_t)v6;
        return (int)((int32_t)v6 >> 2);
    }

    // 0x00432CD0
    void door_disp1(int doorId)
    {
        static auto* s_pDoorScalerBlock = (uint32_t*)0x669B88;
        static auto* s_pDoorPrim = (uint32_t*)0x669B28;
        static auto* s_pDoorPrimCount = (uint32_t*)0x669BBC;

        if (doorId >= 12)
            return;

        auto* v29 = (char*)*s_pDoorScalerBlock + 224 * doorId;
        uint32_t v13 = 0;
        arrange_object_contents(gGameTable.pMarni, *((uint32_t*)v29 + 19), (int*)&v13);

        uint32_t v34[9] = {};
        if ((*(uint8_t*)(v13 + 52) & 1) != 0)
            memcpy(v34, (const void*)(v13 + 16), sizeof(v34));
        const uint32_t v1 = v34[6];

        if (s_pDoorPrim[doorId] == 0)
        {
            const uint32_t v2 = 48 * v34[6];
            auto* v3 = (uint32_t*)operator_new(48 * v34[6]);
            s_pDoorPrim[doorId] = (uint32_t)v3;
            s_pDoorPrimCount[doorId] = v1;
            memset(v3, 0, v2);
        }

        auto* v4 = (uint8_t*)s_pDoorPrim[doorId];
        int v5 = 0;
        uint32_t v14 = 0;
        if (v1 != 0)
        {
            while (v5 < (int)s_pDoorPrimCount[doorId])
            {
                PrimRecord rec;
                modify_primitive((PolygonObject*)v13, v5, &rec);
                float v31[3];
                float v32[3];
                float v33[3];
                float v35[3];
                float v36[3];
                float v37[3];
                refer_vertex((PolygonObject*)v13, rec.v0, v31);
                refer_vertex((PolygonObject*)v13, rec.v1, v32);
                refer_vertex((PolygonObject*)v13, rec.v2, v33);
                refer_normal((PolygonObject*)v13, rec.n0, v35);
                refer_normal((PolygonObject*)v13, rec.n1, v36);
                refer_normal((PolygonObject*)v13, rec.n2, v37);

                int16_t v27;
                int16_t v28;
                int16_t v30;
                const int v6 = calc_prj(v32, (uint16_t*)v4 + 10, (uint16_t*)v4 + 11, (uint16_t*)&v30);
                const int v7 = calc_prj(v31, (uint16_t*)v4 + 8, (uint16_t*)v4 + 9, (uint16_t*)&v27) + v6;
                const int v8 = calc_prj(v33, (uint16_t*)v4 + 12, (uint16_t*)v4 + 13, (uint16_t*)&v28) + v7;

                const int16_t v9 = *(int16_t*)(v4 + 24);
                *(int16_t*)(v4 + 32) = 30000;
                const int16_t v10 = *(int16_t*)(v4 + 26);
                *(int16_t*)(v4 + 28) = v9;
                *(int16_t*)(v4 + 30) = v10;
                *(uint32_t*)(v4 + 4) = 65613;
                const uint32_t v12 = *((uint32_t*)v29 + 2);
                *(uint32_t*)(v4 + 12) = 0;
                *(uint32_t*)(v4 + 8) = v12;
                *(uint8_t*)(v4 + 34) = rec.u0;
                *(uint8_t*)(v4 + 35) = rec.u1;
                *(uint8_t*)(v4 + 36) = rec.u2;
                *(uint8_t*)(v4 + 37) = rec.u3;
                *(uint8_t*)(v4 + 38) = rec.u4;
                *(uint8_t*)(v4 + 39) = rec.u5;
                *(uint8_t*)(v4 + 40) = rec.u0;
                *(uint8_t*)(v4 + 41) = rec.u1;
                *(uint32_t*)(v4 + 44) = 8421504;

                if (v8 / 3 > 400)
                    marni::add_scaler((const PrimScaler*)v4, v8 / 3);
                v4 += 48;
                if (++v14 >= v34[6])
                    break;
                v5 = (int)v14;
            }
        }
    }

    // 0x0043F550
    void unload_texture_page(int page)
    {
        auto& tp = gGameTable.texture_pages[page];
        if (tp.handle != 0)
        {
            marni::unloadTexture(tp.handle);
        }
        tp.handle = 0;
        tp.clutCount = 0;
        tp.suspended = 0;
        update_timer();
    }

    // 0x00441520
    void result_unload_textures()
    {
        static constexpr uint32_t pages[26]
            = { 0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33 };
        for (const auto page : pages)
        {
            unload_texture_page(page);
        }
    }

    // 0x00416D40
    static int __stdcall flush_surfaces_marni(Marni* self);

    // 0x00441710
    void flush_surfaces()
    {
        flush_surfaces_marni(gGameTable.pMarni);
    }

    // 0x00416D40
    static int __stdcall flush_surfaces_marni(Marni* self)
    {
        // No GPU textures to migrate; the SDL renderer owns the GPU textures.
        (void)self;
        return 1;
    }

    // 0x004450C0
    // Releases the registered "work" surface streams. a1 selects the set:
    //   a1 != 0  -> set 0 (main model/movie streams)
    //   a1 == 0  -> set 2 (room streams)
    int unload_register_surfaces(int a1)
    {
        static auto* pWorkRegs = (uint32_t*)0x687F44;      // 12-byte entries {count, ptr, work}
        static auto* pWorkRegsEnd = (uint32_t*)0x6880DC;   // end of the 12-byte table
        static auto* pStreamRegs = (uint32_t*)0x6808AC;    // 40-byte entries (texture/object streams)
        static auto* pStreamRegsEnd = (uint32_t*)0x680DFC; // end of the 40-byte table

        const int v1 = a1 != 0 ? 0 : 2;

        for (auto* v2 = pWorkRegs + 3 * v1; v2 < pWorkRegsEnd; v2 += 3)
        {
            if (v2[-1] > 0)
            {
                if (v2[0])
                {
                    auto* v3 = (char*)v2[0];
                    cstd_vector_dtor(v3, 0x124, *((uint32_t*)v3 - 1), (void*)0x00444430);
                    operator_delete(v3 - 4);
                }
                v2[1] = 0;
                v2[0] = 0;
                v2[-1] = 0;
            }
        }

        for (auto* v5 = pStreamRegs + 10 * v1; v5 < pStreamRegsEnd; v5 += 10)
        {
            if (v5[-1])
            {
                marni::unloadTexture(v5[-1]);
                v5[-1] = 0;
            }
            if (v5[0])
            {
                marni::unloadTexture(v5[0]);
                v5[0] = 0;
            }
            if (v5[1])
            {
                marni::unloadTexture(v5[1]);
                v5[1] = 0;
            }
            if (v5[2])
            {
                auto* v6 = (char*)v5[2];
                cstd_vector_dtor(v6, 0x40, *((uint32_t*)v6 - 1), (void*)0x00443370);
                operator_delete(v6 - 4);
            }
            if (v5[3])
            {
                interop::thiscall<void, void*>(0x004302C0, (void*)v5[3]); // stdiobuf dtor
                operator_delete((void*)v5[3]);
                v5[3] = 0;
            }
            v5[2] = 0;
            v5[-4] = 0;
            v5[-3] = 0;
            v5[-2] = 0;
            v5[4] = 0;
            v5[-5] = -1;
        }

        memset((void*)0x680DE8, 0xFF, 0x180C);
        return -1;
    }

    // 0x0043EC00
    // Destroys the dynamic object array dword_671424[0..10).
    static int destroy_dynamic_objects()
    {
        int result;
        auto* v0 = (uint32_t*)0x671424;
        do
        {
            result = *v0;
            if (*v0)
            {
                result = destroy_object(gGameTable.pMarni, *v0);
                *v0 = 0;
            }
            ++v0;
        } while ((uint32_t)v0 < 0x67144C);
        return result;
    }

    // 0x0043DF40
    // Unloads all object textures and destroys the room/object models.
    int release_object_textures()
    {
        static auto* pObjTex = (uint32_t*)0x671620; // obj_tex_handle (32 entries of 85 dwords)

        // Iterate from obj_tex_handle.Tex_handle2 (dword 4) to &stru_6740A0.field_10.
        auto* p = pObjTex + 4;
        do
        {
            if (p[-1])
            {
                marni::unloadTexture(p[-1]);
                p[-1] = 0;
            }
            if (p[0])
            {
                marni::unloadTexture(p[0]);
                p[0] = 0;
            }
            const int v1 = p[1];
            p[74] = 0;
            p[3] = (uint32_t)(p - 4);
            if (v1)
            {
                destroy_object(gGameTable.pMarni, v1);
                p[1] = 0;
            }
            if (p[2])
            {
                destroy_object(gGameTable.pMarni, p[2]);
                p[2] = 0;
            }
            p[3] = (uint32_t)(p - 4);
            p[4] = (uint32_t)(p - 4);
            p[71] = 0;
            p[72] = 0;
            p[73] = 0;
            p[74] = 0;
            p[70] = 0;
            memset(p + 75, 0, 0x18);
            p += 85;
        } while ((uint32_t)p < 0x6740B0);

        destroy_dynamic_objects();

        // Unload the shared world texture handle.
        int result = *(uint32_t*)0x674DF0;
        if (*(uint32_t*)0x674DF0)
        {
            marni::unloadTexture(*(uint32_t*)0x674DF0);
            *(uint32_t*)0x674DF0 = 0;
            // (the UnloadTexture result is discarded: the C++ unload_texture wrapper is void)
        }
        *(uint32_t*)0x674DF4 = 0;
        return result;
    }

    // 0x004419A0
    void kill()
    {
        static auto* pKillFlag = (uint32_t*)0x6805CC;

        if (gGameTable.movie_r0 >= 2)
        {
            openre::movie_kill();
            gGameTable.movie_r0 = 5;
        }

        if (!*pKillFlag)
        {
            *pKillFlag = 1;
            unload_register_surfaces(1);
            release_object_textures();

            if (gGameTable.pMarni)
            {
                dtor(gGameTable.pMarni);
                operator_delete(gGameTable.pMarni);
                gGameTable.pMarni = nullptr;
            }
        }
    }

    // 0x00441270
    void add_tile(void* primPtr, int z, int is_back)
    {
        marni::add_tile((marni::Tile*)primPtr, z, is_back);
    }

    // 0x00442E40
    bool sub_442E40()
    {
        using sig = bool (*)();
        auto p = (sig)0x00442E40;
        return p();
    }

    // 0x00443620
    void mapping_tmd(int workNo, Md1* pTmd, int id)
    {
        static auto* s_pStreamBase = (uint32_t*)0x680898;     // 40-byte stream entries
        static auto* s_pUvTableDoor = (uint8_t*)0x680DE8;     // byte_680DE8
        static auto* s_pUvTableElevator = (uint8_t*)0x6815EC; // byte_6815EC
        static auto* s_pUvTableOther = (uint8_t*)0x681DF0;    // byte_681DF0

        uint32_t* stream = s_pStreamBase + 10 * workNo;

        const int v33 = *((uint32_t*)pTmd + 2) / 2;
        const int count = v33 + (workNo == 0 ? 20 : 0);
        stream_alloc(stream, count);

        int v24 = 0;
        if (v33 > 0)
        {
            for (;;)
            {
                uint8_t* base = (uint8_t*)stream[7];
                const int v6 = v24 << 6;
                int* v7 = (int*)(base + v6);

                if (*v7)
                {
                    destroy_object(gGameTable.pMarni, *v7);
                    *v7 = 0;
                }
                if (v7[1])
                {
                    destroy_object(gGameTable.pMarni, v7[1]);
                    v7[1] = 0;
                }
                v7[15] = 0;
                v7[1] = 0;
                *v7 = 0;
                memset((char*)v7 + 10, 0, 0x30);

                MarniPolyObject obj;
                tm2_object_ctor(&obj, nullptr, 0);
                const int v8 = v24;
                tm2_object_in(&obj, (uint8_t*)pTmd, v24, -1);

                *(uint16_t*)(base + v6 + 8) = (uint16_t)tmd_object_kind((const uint8_t*)pTmd, v24);
                const uint32_t v38 = *(uint32_t*)((char*)&obj + 0x50);
                *(uint32_t*)(base + v6 + 60) = v38;

                const uint8_t* srcRec = (const uint8_t*)&obj + 0x38;
                if (v38 <= 1)
                {
                    uint8_t* v13 = base + v6 + 10;
                    *(uint32_t*)v13 = *(uint32_t*)srcRec;
                    *(uint16_t*)(v13 + 4) = *(uint16_t*)(srcRec + 4);
                    *(uint16_t*)(base + v6 + 10) = (uint16_t)((*(uint16_t*)srcRec >> 6) - 480);
                    *(uint16_t*)(base + v6 + 14) = 0;
                }
                else
                {
                    for (int v22 = 0; v22 < (int)v38; v22++)
                    {
                        uint8_t* v12 = base + v6 + 10 + 6 * v22;
                        const uint8_t* v11 = srcRec + 6 * v22;
                        *(uint32_t*)v12 = *(uint32_t*)v11;
                        *(uint16_t*)(v12 + 4) = *(uint16_t*)(v11 + 4);
                        *(uint16_t*)(base + v6 + 10 + 6 * v22) = (uint16_t)((*(uint16_t*)v11 >> 6) - 480);
                        *(uint16_t*)(base + v6 + 14 + 6 * v22) = *(uint16_t*)(v11 + 4);
                    }
                }

                if (tmd_object_list_check(id, v8))
                {
                    auto* v15 = (MarniPolyObject*)operator_new(0x54);
                    v15 = v15 ? tm2_object_ctor(v15, nullptr, 0) : nullptr;
                    if (stream[8])
                    {
                        tm2_object_dtor((MarniPolyObject*)stream[8]);
                        operator_delete((void*)stream[8]);
                    }
                    const bool copyHeader = (obj.flags & 1) != 0;
                    stream[8] = (uint32_t)v15;
                    uint32_t objData[9] = {};
                    if (copyHeader)
                        memcpy(objData, (char*)&obj + 0x10, sizeof(objData));

                    create_work(v15, 3 * (int)objData[6], 3 * (int)objData[6], (int)objData[6], (int)(intptr_t)objData[8]);
                    if ((int)objData[6] > 0)
                    {
                        int v23 = 2;
                        for (int v18 = 0; v18 < (int)objData[6]; v18++)
                        {
                            PrimRecord rec;
                            modify_primitive((PolygonObject*)&obj, v18, &rec);
                            float v25[3];
                            refer_vertex((PolygonObject*)&obj, rec.v0, v25);
                            modify_vertex_0(v15, v23 - 2, v25);
                            refer_vertex((PolygonObject*)&obj, rec.v1, v25);
                            modify_vertex_0(v15, v23 - 1, v25);
                            refer_vertex((PolygonObject*)&obj, rec.v2, v25);
                            modify_vertex_0(v15, v23, v25);

                            rec.v0 = (uint16_t)(3 * v18);
                            rec.v1 = (uint16_t)(3 * v18 + 1);
                            rec.v2 = (uint16_t)(3 * v18 + 2);

                            refer_normal((PolygonObject*)&obj, rec.n0, v25);
                            modify_normal_0(v15, v23 - 2, v25);
                            refer_normal((PolygonObject*)&obj, rec.n1, v25);
                            modify_normal_0(v15, v23 - 1, v25);
                            refer_normal((PolygonObject*)&obj, rec.n2, v25);
                            modify_normal_0(v15, v23, v25);

                            rec.n0 = (uint16_t)(3 * v18);
                            rec.n1 = (uint16_t)(3 * v18 + 1);
                            rec.n2 = (uint16_t)(3 * v18 + 2);

                            refer_primitive(v15, v18, &rec);
                            v23 += 3;
                        }
                    }
                }

                enum
                {
                    DISPATCH_NONE,
                    DISPATCH_30,
                    DISPATCH_33,
                    DISPATCH_39,
                };
                int dispatch = DISPATCH_NONE;

                if (!v8 && (zapping_check(1u, id) || zapping_check(0u, id)))
                {
                    dispatch = DISPATCH_30;
                }
                else if (id != 48)
                {
                    if (id == 49)
                    {
                        if (v8 == 4)
                            dispatch = DISPATCH_33;
                        else if (v8)
                            dispatch = DISPATCH_39;
                        else
                            dispatch = DISPATCH_30;
                    }
                    else
                    {
                        switch (id)
                        {
                        case 51:
                            if (v8 == 11)
                            {
                                *(uint32_t*)(base + 704)
                                    = tmd_create_poly_object((const uint8_t*)pTmd, 11, 3, (uint16_t*)(base + 714), 0);
                                *(uint32_t*)(base + 708)
                                    = tmd_create_poly_object((const uint8_t*)pTmd, 11, 4, (uint16_t*)(base + 738), 0);
                                tmd_write_uv_table((const uint8_t*)pTmd, 11, 4, s_pUvTableDoor);
                            }
                            else
                                dispatch = DISPATCH_39;
                            break;
                        case 52:
                            switch (v8)
                            {
                            case 16:
                                *(uint32_t*)(base + 1024) = create_object_handle(gGameTable.pMarni, &obj, 0);
                                *(uint32_t*)(base + 1028)
                                    = tmd_create_poly_object((const uint8_t*)pTmd, 16, 6, (uint16_t*)(base + 1034), -128);
                                tmd_write_uv_table((const uint8_t*)pTmd, 16, 6, s_pUvTableDoor);
                                break;
                            case 5:
                                *(uint32_t*)(base + 320)
                                    = tmd_create_poly_object((const uint8_t*)pTmd, 5, 4, (uint16_t*)(base + 330), 0);
                                *(uint32_t*)(base + 324) = 0;
                                tmd_write_uv_table((const uint8_t*)pTmd, 5, 4, s_pUvTableElevator);
                                break;
                            case 10:
                                *(uint32_t*)(base + 640)
                                    = tmd_create_poly_object((const uint8_t*)pTmd, 10, 3, (uint16_t*)(base + 650), 0);
                                *(uint32_t*)(base + 644)
                                    = tmd_create_poly_object((const uint8_t*)pTmd, 10, 4, (uint16_t*)(base + 674), 0);
                                tmd_write_uv_table((const uint8_t*)pTmd, 10, 4, s_pUvTableOther);
                                break;
                            default: dispatch = DISPATCH_39; break;
                            }
                            break;
                        case 54:
                            if (v8 == 2)
                            {
                                *(uint32_t*)(base + 128)
                                    = tmd_create_poly_object((const uint8_t*)pTmd, 2, 3, (uint16_t*)(base + 138), 0);
                                *(uint32_t*)(base + 132)
                                    = tmd_create_poly_object((const uint8_t*)pTmd, 2, 8, (uint16_t*)(base + 162), 0);
                                tmd_write_uv_table((const uint8_t*)pTmd, 2, 8, s_pUvTableDoor);
                            }
                            else
                                dispatch = DISPATCH_39;
                            break;
                        default:
                            if (id == 40 && v8 == 1)
                            {
                                *(uint32_t*)(base + 64) = create_object_handle(gGameTable.pMarni, &obj, 0);
                                *(uint32_t*)(base + 68)
                                    = tmd_create_poly_object((const uint8_t*)pTmd, 1, 6, (uint16_t*)(base + 74), -128);
                                tmd_write_uv_table((const uint8_t*)pTmd, 1, 6, s_pUvTableDoor);
                            }
                            else
                                dispatch = DISPATCH_39;
                            break;
                        }
                    }
                }
                else
                {
                    if (v8 == 4)
                        dispatch = DISPATCH_33;
                    else if (v8 != 3)
                        dispatch = DISPATCH_39;
                    else
                    {
                        *(uint32_t*)(base + 192)
                            = tmd_create_poly_object((const uint8_t*)pTmd, 3, 3, (uint16_t*)(base + 202), 0);
                        *(uint32_t*)(base + 196)
                            = tmd_create_poly_object((const uint8_t*)pTmd, 3, 4, (uint16_t*)(base + 226), 0);
                        tmd_write_uv_table((const uint8_t*)pTmd, 3, 3, s_pUvTableElevator);
                    }
                }

                switch (dispatch)
                {
                case DISPATCH_30:
                    *(uint32_t*)(base + 0) = tmd_create_poly_object((const uint8_t*)pTmd, 0, 3, (uint16_t*)(base + 10), 0);
                    *(uint32_t*)(base + 4) = tmd_create_poly_object((const uint8_t*)pTmd, 0, 4, (uint16_t*)(base + 34), 0);
                    break;
                case DISPATCH_33:
                    *(uint32_t*)(base + 256) = tmd_create_poly_object((const uint8_t*)pTmd, 4, 3, (uint16_t*)(base + 266), 0);
                    *(uint32_t*)(base + 260) = tmd_create_poly_object((const uint8_t*)pTmd, 4, 4, (uint16_t*)(base + 290), 0);
                    tmd_write_uv_table((const uint8_t*)pTmd, 4, 4, s_pUvTableDoor);
                    break;
                case DISPATCH_39:
                    *(uint32_t*)(base + v6 + 0) = create_object_handle(gGameTable.pMarni, &obj, 0);
                    tm2_object_adjust_texture_coordinates(&obj, -128, 0);
                    *(uint32_t*)(base + v6 + 4) = create_object_handle(gGameTable.pMarni, &obj, 0);
                    break;
                }

                if ((int)v38 > 0)
                {
                    for (int v19 = 0; v19 < (int)v38; v19++)
                    {
                        if (*(uint16_t*)(base + v6 + 8) == 1 || (id == 54 && v24 != 2))
                        {
                            --*(uint16_t*)(base + v6 + 10 + 6 * v19);
                            --*(uint16_t*)(base + v6 + 34 + 6 * v19);
                        }
                        *(uint16_t*)(base + v6 + 10 + 6 * v19) &= 1;
                        *(uint16_t*)(base + v6 + 34 + 6 * v19) &= 1;
                    }
                }
                tm2_object_dtor(&obj);

                v24 = v8 + 1;
                if (v24 >= v33)
                {
                    stream[0] = (uint32_t)id;
                    stream[9] = 0x680898 + 40 * workNo;
                    return;
                }
            }
        }

        stream[0] = (uint32_t)id;
        stream[9] = 0x680898 + 40 * workNo;
    }

    static void out_internal(const char* message, const char* location)
    {
        // std::printf("[marni] %s: %s\n", location, message);
    }

    // 0x004DBFD0
    void out(const char* message, const char* location)
    {
        out_internal(message, location);
    }

    // 0x004DBFD0
    void out()
    {
        out_internal("", "");
    }

    // 0x0050B220
    void config_flip_filter(MarniConfig* self)
    {
        self->bilinear ^= 1;
    }

    // 0x0050ACB0
    void config_read_all(MarniConfig* self)
    {
        system::config::load();
        interop::thiscall<void, MarniConfig*>(0x0050ACB0, self);
        // DisplayMode is derived from [video] render_resolution and DriverMode
        // is no longer persisted. Drop any legacy values now so save() stops
        // emitting them (the in-memory values were already read by ReadAll).
        system::config::remove_key(self->path.data, "DisplayMode");
        system::config::remove_key(self->path.data, "DriverMode");
    }

    // 0x0050B020
    void config_flush_all(MarniConfig* self)
    {
        interop::thiscall<void, MarniConfig*>(0x0050B020, self);
        system::config::save();
    }

    // 0x0050C690
    // Copies a Shift-JIS string (a2) into a1 honouring the maximum character
    // count a3 (no limit when a3 == -1). Returns the last byte read.
    static uint8_t string_copy_sjis(uint8_t* dst, const uint8_t* src, int maxLen)
    {
        const uint8_t* v3 = src;
        uint8_t result = *src;
        for (; result; ++v3)
        {
            if (maxLen != -1)
            {
                const int v7 = maxLen--;
                if (v7 <= 0)
                    break;
            }
            if ((result >= 0x81u && result <= 0x9Fu) || (result >= 0xE0u && result <= 0xFCu))
            {
                *dst = result;
                result = v3[1];
                ++dst;
                ++v3;
            }
            *dst = result;
            result = v3[1];
            ++dst;
        }
        *dst = 0;
        return result;
    }

    // 0x0050BC30
    // Resets an OldStdString to the empty string.
    static uint8_t string_reset_empty(OldStdString* self)
    {
        interop::thiscall<void, void*>(0x50BC10, self); // OldStdString dtor (0x0050BC10)
        self->length = 1;
        auto* v2 = (char*)operator_new(1);
        self->data = v2;
        return string_copy_sjis((uint8_t*)v2, (const uint8_t*)0x669F4C, -1);
    }

    // 0x00509C70
    // Clears the config path strings.
    static uint8_t config_reset_strings()
    {
        string_reset_empty(&gGameTable.ss_file_string);
        string_reset_empty((OldStdString*)0x689F34);
        return string_reset_empty((OldStdString*)0x689F44); // ss_save_path_string
    }

    // 0x0050B900
    void config_shutdown()
    {
        config_reset_strings();
        interop::thiscall<void, MarniConfig*>(0x0050ACA0, &gGameTable.marni_config);
    }

    // 0x00442CB0
    void set_gpu_flag()
    {
        switch (gGameTable.byte_680592)
        {
        case 0:
        {
            marni::set_gpu_flag(GpuFlags::FILTER_BIT_0, false);
            marni::set_gpu_flag(GpuFlags::FILTER_BIT_1, false);
            break;
        }
        case 1:
        {
            marni::set_gpu_flag(GpuFlags::FILTER_BIT_0, true);
            marni::set_gpu_flag(GpuFlags::FILTER_BIT_1, false);
            break;
        }
        case 2:
        {
            marni::set_gpu_flag(GpuFlags::FILTER_BIT_0, false);
            marni::set_gpu_flag(GpuFlags::FILTER_BIT_1, true);
            break;
        }
        case 3:
        {
            marni::set_gpu_flag(GpuFlags::FILTER_BIT_0, true);
            marni::set_gpu_flag(GpuFlags::FILTER_BIT_1, true);
            break;
        }
        }
    }

    // Jumping around more...
    // 0x00401F70
    int __stdcall marni_movie_update(Marni* self)
    {
        if (self->pMovie->flag && !movie_update(self->pMovie) && self->gpu_flag & GpuFlags::GPU_FULLSCREEN)
        {
            // No GetWindowLongA/SetWindowLongA style restore: SDL3 borderless
            // fullscreen keeps the frame off the movie window.
        }

        return 1;
    }

    // 0x00411360
    void font_trans(MarniFont* self, MarniSurface* surface)
    {
        if (!self->bitmap)
            return;

        // The font bitmap is an atlas of self->width * self->height characters,
        // each rendered as an 8x8 pixel block on the surface.
        if (surface->width < 8 * (int)self->width || surface->height < 8 * (int)self->height)
        {
            out("bits you specified is too small to trans this screen font. recommended to %dx%d cScreenFont::Trans", "");
            return;
        }

        if (!surface_lock(surface, 0, 0))
            return;

        const uint8_t* bitmap = (const uint8_t*)self->bitmap;
        for (int i = 0; i < (int)self->height; i++)
        {
            for (int j = 0; j < (int)self->width; j++)
            {
                uint8_t ch = bitmap[i * (int)self->width + j];
                if (ch == 0 || ch == 0x20) // 0 = empty cell, ' ' = space
                    continue;

                // Look up the 8x8 (2bpp) glyph for this character in the fixed font table.
                uint8_t code = (uint8_t)(ch - 32);
                const uint16_t* glyph = (const uint16_t*)(0x51F2A0 + 2 * (((code >> 5) << 8) + (code & 0x1F)));
                for (int row = 0; row < 8; row++)
                {
                    uint16_t bits = glyph[row * 32];
                    for (int px = 0; px < 8; px++)
                    {
                        int pixel = (bits >> (14 - px * 2)) & 3;
                        if (pixel == 1 || pixel == 2) // skip transparent (0) and shadow (3)
                        {
                            surface_set_current_color(surface, px + j * 8, row + i * 8, 0xFFFFFF, 0);
                        }
                    }
                }
            }
        }

        surface_unlock(surface);
    }

    void init_hooks()
    {
        interop::hookThisCall(0x00401E40, &prepare_movie);
        interop::hookThisCall(0x00401EF0, &kill_movie);
        interop::hookThisCall(0x00401F00, &sub_401F00);
        interop::hookThisCall(0x00401F10, &syskeydown);
        interop::hookThisCall(0x00401F70, &update_movie);
        interop::hookThisCall(0x00401FD0, &set_movie_resolution);
        interop::hookThisCall(0x00402160, &arrange_object_contents);
        interop::hookThisCall(0x004021C0, &add_primitive_front);
        interop::hookThisCall(0x00402210, &add_primitive_scaler);
        interop::hookThisCall(0x00402240, &add_primitive_back);
        interop::hookThisCall(0x00402290, (void(__stdcall*)(Marni*)) & clear_otags);
        interop::hookThisCall(0x004022E0, &request_video_memory);
        interop::hookThisCall(0x00402530, &request_display_mode_count);
        interop::hookThisCall(0x004033F0, &reload_texture);
        interop::hookThisCall(0x00402940, &restore_surfaces);
        interop::hookThisCall(0x00402A80, (void(__stdcall*)(Marni*)) & flip);
        interop::hookThisCall(0x00402BC0, (void(__stdcall*)(Marni*)) & draw);
        interop::hookThisCall(0x00405DD0, &get_z_buffer_caps);
        interop::hookThisCall(0x00404CE0, &unload_texture);
        interop::hookThisCall(0x00404D20, (int(__stdcall*)(Marni*)) & clear);
        interop::hookThisCall(0x00404FA0, &clear_buffers);
        interop::hookThisCall(0x004050C0, &dtor);
        interop::hookThisCall(0x00405320, &init);
        interop::hookThisCall(0x00406450, &move);
        interop::hookThisCall(0x004064D0, &destroy);
#ifdef _WIN32
        interop::hookThisCall(0x00407340, &enum_drivers);
        interop::hookThisCall(0x00407440, &create_gpu);
        interop::hookThisCall(0x00406D90, &create_device);
#endif
        interop::hookThisCall(0x0040EAF0, &do_draw_op);
        interop::hookThisCall(0x0040ECA0, &surfacex_create_texture_object);
        interop::hookThisCall(0x0040EE30, &surfacex_load);
        interop::hookThisCall(0x0040ED90, &surfacex_ctor);
        interop::hookThisCall(0x0040EDB0, &surfacex_dtor);
        interop::hookThisCall(0x0040EE00, &surfacex_vrelease);
        interop::hookThisCall(0x0040F9C0, &surfacex_vpalunlock);
        interop::hookThisCall(0x0040F790, &surfacex_vlock);
        interop::hookThisCall(0x0040FEF0, &surfacey_ctor);
        interop::hookThisCall(0x00405EC0, &create_texture_handle);
        interop::hookThisCall(0x00416500, &ot_add_primitive_as_z);
        interop::hookThisCall(0x004168F0, &search_texture_object_0_from_1_in_condition);
        interop::hookThisCall(0x00416AF0, &search_texture_object_0_from_1);
        interop::hookThisCall(0x00416730, &suspend_texture_use);
        interop::hookThisCall(0x0040C6E0, &draw_line_flat);
        interop::hookThisCall(0x0040C790, &draw_line_gourad);
        interop::writeJmp(0x004DBFD0, &out_internal);
        interop::writeJmp(0x00442CB0, (void (*)())&set_gpu_flag);
        interop::hookThisCall(0x00412BD0, &surface2_vfill);
        interop::hookThisCall(0x0040F380, &surfacex_vfill);
        interop::hookThisCall(0x00412D20, &MarniBits_FileOut);
        interop::hookThisCall(0x0040F580, &surfacey_vrelease);
        interop::hookThisCall(0x0040F600, &surfacex_vpallock);
        interop::hookThisCall(0x0040FAD0, &surfacex_vunlock);
        interop::hookThisCall(0x00414A40, &surface2_vrelease);
        interop::hookThisCall(0x004130D0, &surface_operator_eq);
        interop::hookThisCall(0x004123D0, &surface_pal_blt);
        interop::hookThisCall(0x00412580, &surface2_blt);
        interop::hookThisCall(0x00416D40, &flush_surfaces_marni);
        interop::hookThisCall(0x00430A60, &marni_poly_object_open);
        interop::writeJmp(0x00432CD0, &door_disp1);
        interop::writeJmp(0x00443620, &mapping_tmd);
#ifdef _WIN32
        interop::writeJmp(0x00406A10, &error_routine);
#endif
    }
}
