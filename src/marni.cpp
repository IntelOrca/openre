#include "marni.h"
#include "gfx_backend.h"
#include "interop.hpp"
#include "logger.h"
#include "marni_movie.h"
#include "openre.h"
#include "re2.h"
#include "str.h"
#include "system_config.h"
#include "system_window.h"

#include <algorithm>
#include <cstring>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <d3d.h>
#include <ddraw.h>
#include <windows.h>

namespace openre::marni
{
    struct DrawInfo
    {
        int zWriteEnable;
        int shadeMode;
        int cullMode;
        int specularEnable;
        int vertexCount;
        LPD3DTLVERTEX vertices;
        void* texture;
    };

    static void d3d_error_routine(int errorCode);
    static int query_ddraw2(LPDIRECTDRAW pDD, LPDIRECTDRAW2* lpDD2);
    static int __stdcall create_device(Marni* self);
    static int __stdcall create_zbuffer(Marni* self, int width, int height, LPDIRECTDRAWSURFACE* pDDsurfaceZ);
    static int __stdcall enum_drivers(Marni* self);
    static int __stdcall create_d3d(Marni* self);
    static BOOL CALLBACK ddrawEnumCallback(GUID* lpGUID, LPSTR lpName, LPSTR lpDesc, LPVOID lpContext);
    static HRESULT dd_set_coop_level(HWND hWnd, int fullscreen, LPDIRECTDRAW2 pDD);
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
    static int __stdcall resize(Marni* marni, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static uint16_t __stdcall search_texture_object_0_from_1(Marni* self, int handle, int index);
    static void set_filtering(Marni* self, uint8_t a2);
    static void sub_40E6E0(D3DTLVERTEX* v);
    static void __stdcall sub_40E800(Marni* self, uint8_t a2);
    static int invalidate_window(HWND hWnd, int width, int height, int fullscreen, LPRECT lpResRect);
    static void __stdcall sub_40EC10(Marni* self);
    static int ddrawdesc2surfdesc(LPDDSURFACEDESC pDDesc, MarniSurfaceDesc* pDesc);
    static int enum_display_mode(LPDIRECTDRAW2 lpDD2, MarniRes* res, size_t max, size_t* count);
    static HRESULT get_surface_desc(LPDDSURFACEDESC lpDDSurfaceDesc, LPDIRECTDRAWSURFACE lpDDSurface);
    static int create_ddraw(bool bEnumDevices, LPDIRECTDRAW* lplpDD, LPDWORD lpIsDefault);
    static uint8_t __stdcall sub_416670(MarniOt* pOt);
    static MarniTextureNode* __stdcall search_texture_object_0_from_1_in_condition(Marni* self, int handle, int index);
    static void __stdcall tessellate_insert_draw_op(
        Marni* self, int filter, int a1, int srcBlend, int dstBlend, int textureHandle, int zWriteEnable, int shadeMode,
        int cullMode, int specularEnable, int zFunc, LPD3DTLVERTEX vertices, int vertexCount);
    static void __stdcall texture_surface_release(Marni* self, int handle);
    static void tex_spr(
        MarniSurface2* surface, void* a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11, int a12,
        int a13, int a14, int a15, int a16);
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
    static int __stdcall surfacex_get_texture_handle(MarniSurfaceX* self, LPDIRECT3DDEVICE2 device);
    static void __stdcall surfacex_vrelease(MarniSurfaceX* self);
    static int __stdcall surfacex_create_work(MarniSurfaceX* self, LPDIRECTDRAW pDD, LPDDSURFACEDESC pDesc, int a4);
    static void surfacex_create_surface(MarniSurfaceX* self);
    static int surface_get_alpha_bits(MarniSurfaceX* self);
    char* surface_calc_address(MarniSurface* self, int x, int y);
    static int surface_set_color(MarniSurface2* self, int x, int y, uint32_t color, int alpha);
    static int __stdcall surfacex_vpalunlock(MarniSurfaceX* self);
    static int __stdcall insert_draw_op(
        Marni* self, int filter, int a3, int srcBlend, int dstBlend, int textureHandle, int zWriteEnable, int shadeMode,
        int cullMode, int specularEnable, int zFunc, LPD3DTLVERTEX* vertices);
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

    static int __stdcall com_nop(LPUNKNOWN obj)
    {
        return 0;
    }

    static void clear_com_interface(LPUNKNOWN obj, size_t methodCount)
    {
        auto newTable = new void*[methodCount];
        for (size_t i = 0; i < methodCount; i++)
        {
            newTable[i] = com_nop;
        }

        auto vtable = (void**)obj;
        vtable[0] = newTable;
    }

    // 0x00401000
    int error(HRESULT hr)
    {
        return interop::call(0x00401000);
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
            ((LPDIRECTDRAW)self->pDirectDraw)->FlipToGDISurface();
            auto dwStyle = GetWindowLongA((HWND)self->hWnd, GWL_STYLE);
            SetWindowLongA((HWND)self->hWnd, GWL_STYLE, dwStyle & ~(WS_CAPTION | WS_SIZEBOX | WS_TABSTOP));
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

        auto dwValue = GetWindowLongA((HWND)self->hWnd, GWL_STYLE);
        SetWindowLongA((HWND)self->hWnd, GWL_STYLE, (dwValue & ~WS_SIZEBOX) | WS_MAXIMIZEBOX);
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

        auto dwValue = GetWindowLongA((HWND)self->hWnd, GWL_STYLE);
        SetWindowLongA(
            (HWND)self->hWnd,
            GWL_STYLE,
            (dwValue & ~WS_POPUP) | (WS_TABSTOP | WS_GROUP | WS_SIZEBOX | WS_SYSMENU | WS_DLGFRAME | WS_BORDER));
    }

    // 0x00401FD0
    static int __stdcall set_movie_resolution(Marni* self, const char* path, int mode)
    {
        if (!self->is_gpu_active)
            return 0;

        RECT rc;
        ZeroMemory(&rc, sizeof(RECT));
        if (self->gpu_flag & GpuFlags::GPU_FULLSCREEN)
        {
            // Borderless fullscreen: the movie window covers the whole screen.
            // (The original offset it up by the frame/caption height to
            // compensate for the exclusive-fullscreen window frame.)
            GetClientRect((HWND)self->hWnd, &rc);
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
        return movie_open(
            self->pMovie,
            path,
            (HWND)self->hWnd,
            &rc,
            (LPDIRECTDRAW2)self->pDirectDraw2,
            (LPDIRECTDRAWSURFACE)self->surface2.pDDsurface);
    }

    // 0x00402160
    static int __stdcall arrange_object_contents(Marni* self, int a2, int* a3)
    {
        auto v3 = *(int*)(*((int*)self + 0x231DA6) + 4 * a2);
        auto v4 = *(int*)(v3 + 52);
        if ((v4 & 1) == 0)
        {
            out("invalid handle", "Direct3D::ArrangeObjectContents");
            return 0;
        }
        if ((v4 & 0x10000) != 0)
        {
            out("this object is optimized! (required not be optimized)", "Direct3D::ArrangeObjectContents");
            return 0;
        }
        *a3 = v3;
        return 1;
    }

    // 0x004021B0

    // 0x004021C0
    static int __stdcall add_primitive_front(Marni* self, Prim* pPrim, int z)
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
    static int __stdcall add_primitive_back(Marni* self, Prim* pPrim, int z)
    {
        if (!self->is_gpu_active)
            return 0;

        if ((pPrim->type & 8) != 0)
        {
            out("you can't hang this primitive up to the priority list because this is ZCAL.", "Direct3D::AddPrimitiveBack");
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
            out("though this class is unable you have tried to call.", "Direct3D::RequestVideoMemory");
            return;
        }

        auto cnt_2k_buffer = self->polygons_count; // cnt_2K_buffer
        auto count_use_gpu1 = 0;                   // textures with GPU_9 + GPU_1 flags
        self->field_8C8418 = 0;
        self->field_8C8420 = 0;
        self->field_8C841C = 0;
        auto count_use_gpu0 = 0; // textures with GPU_9 + GPU_0 flags
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

        if (self->gpu_flag & GpuFlags::GPU_13)
        {
            for (auto i = 0; i < 256; i++)
            {
                if (self->textures[i].var_00)
                {
                    self->field_8C8418++;
                    if (self->textures[i].var_00 & GpuFlags::GPU_1)
                        self->field_8C8420++;
                    if (self->textures[i].var_00 & GpuFlags::GPU_0)
                        self->field_8C841C++;
                }
            }
            return;
        }

        if (self->pDirectDraw == nullptr)
        {
            out("invalid class.", "Direct3D::RequestVideoMemory");
            return;
        }

        // 380 bytes (95 dwords); the original sets dwSize to 0x17C (380),
        // which is larger than the modern DDCAPS (0x178), so use a raw buffer.
        DWORD ddcaps[95] = {};
        ddcaps[0] = 380; // DDCAPS dwSize
        gGameTable.error = ((LPDIRECTDRAW)self->pDirectDraw)->GetCaps((LPDDCAPS)ddcaps, 0);
        if (gGameTable.error)
            error(gGameTable.error);
        else
            self->dwVidMemFree = ddcaps[0x10]; // DDCAPS dwVidMemFree

        auto count_use = 0; // textures with GPU_9 flag
        for (auto i = 0; i < 256; i++)
        {
            auto flags = self->texture_nodes[i].var_14;
            if (flags && (flags & GpuFlags::GPU_13) == 0)
            {
                self->field_8C8418++;
                if (flags & GpuFlags::GPU_1)
                    self->field_8C8420++;
                if (flags & GpuFlags::GPU_0)
                    self->field_8C841C++;
                if (flags & GpuFlags::GPU_9)
                {
                    count_use++;
                    if (flags & GpuFlags::GPU_1)
                        count_use_gpu1++;
                    if (flags & GpuFlags::GPU_0)
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
        return change_display_mode(self, self->modes + 1 >= (uint32_t)self->res_count ? 0 : self->modes + 1);
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

        out("", "Direct3D::RequestDisplayModeCount");
        return 0;
    }

    // 0x00402560

    // 0x00402940
    static int __stdcall restore_surfaces(Marni* self)
    {
        if (gfx::surface_is_lost((IUnknown*)self->surface2.pDDsurface) == DDERR_SURFACELOST)
        {
            gGameTable.error = gfx::surface_restore((IUnknown*)self->surface2.pDDsurface);
            if (gGameTable.error)
            {
                out("Restoring of a Back surface failed.", "Direct3D::RestoreSurfaces");
                error(gGameTable.error);
                return 0;
            }
        }

        if (gfx::surface_is_lost((IUnknown*)self->surface0.pDDsurface) == DDERR_SURFACELOST)
        {
            gGameTable.error = gfx::surface_restore((IUnknown*)self->surface0.pDDsurface);
            if (gGameTable.error)
            {
                out("Restoring of a Back surface failed.", "Direct3D::RestoreSurfaces");
                error(gGameTable.error);
                return 0;
            }
        }

        if ((self->gpu_flag & GpuFlags::GPU_13) != 0)
            return 1;

        if (gfx::surface_is_lost((IUnknown*)self->surfaceZ.pDDsurface) == DDERR_SURFACELOST)
        {
            gGameTable.error = gfx::surface_restore((IUnknown*)self->surfaceZ.pDDsurface);
            if (gGameTable.error)
            {
                out("Restoring of a Z surface failed.", "Direct3D::RestoreSurfaces");
                error(gGameTable.error);
                return 0;
            }
        }

        if ((self->pMovie->flag & 2) != 0)
            return 1;

        for (auto i = 0; i < 256; i++)
        {
            const auto& node = self->texture_nodes[i];
            if (node.var_14 == 0)
                continue;

            if ((node.var_14 & 0x2000) != 0)
                continue;

            auto v6 = node.surface;
            if (v6 != nullptr && (v6->bOpen != 1 || gfx::surface_is_lost((IUnknown*)v6->pDDsurface) != DDERR_SURFACELOST))
                continue;

            for (auto j = 0; j < 256; j++)
            {
                if (!reload_texture(self, j))
                {
                    out("failed to reload texture...", "Direct3D::RestoreSurfaces");
                    return 0;
                }
            }
            break;
        }
        return 1;
    }

    static void __stdcall flip_blt(Marni* self, DWORD width, DWORD height)
    {
        auto src = (IUnknown*)self->surface0.pDDsurface;
        auto dst = (IUnknown*)self->surface2.pDDsurface;

        RECT srcRect;
        SetRect(&srcRect, 0, 0, width, height);

        RECT dstRect;
        CopyRect(&dstRect, (LPRECT)&self->window_rect);

        DDBLTFX ddbltfx;
        ZeroMemory(&ddbltfx, sizeof(DDBLTFX));
        ddbltfx.dwSize = sizeof(DDBLTFX);
        ddbltfx.dwDDFX = DDBLTFX_NOTEARING;

        gfx::surface_blt(dst, &dstRect, src, &srcRect, DDBLT_DDFX | DDBLT_WAIT, &ddbltfx);
        gfx::notify_present();
    }

    // 0x00402A80
    void __stdcall flip(Marni* self)
    {
        if (self->var_8C7EE0)
            return;
        if (self->pMovie->flag & 2)
            return;
        if (!(self->gpu_flag & GpuFlags::GPU_9))
            return;
        if ((self->gpu_flag & GpuFlags::GPU_13) && (self->gpu_flag & GpuFlags::GPU_FULLSCREEN) && self->var_8C8318 < 4)
        {
            surface_fill(&self->surface2, 0, 0, 0);
        }

        // Both windowed and borderless fullscreen present the render surface by
        // blitting it into the window (the original exclusive fullscreen used a
        // DirectDraw flip chain, which is no longer created).
        flip_blt(self, self->xsize, self->ysize);
    }

    // 0x00402BC0
    void __stdcall draw(Marni* self)
    {
        if (self->var_8C7EE0 || !(self->gpu_flag & GpuFlags::GPU_9))
            return;

        if (gGameTable.dword_54413C > 0 && (self->gpu_flag & GpuFlags::GPU_FULLSCREEN) != 0)
        {
            gGameTable.dword_54413C--;
            surface_fill(&self->surface2, 0, 0, 1024);
        }

        if (self->gpu_flag & GpuFlags::GPU_13)
        {
            // Not implemented yet
        }

        self->cutscene_bars = cutscene_active();
        do_render(self, &self->otag[3]); // backgrounds
        do_render(self, &self->otag[1]); // objects
        do_render(self, &self->otag[0]); // fg text
        self->var_8C8318++;
    }

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
                out("Direct3D::ChangeDisplayMode - (%d->%d) w:%d h:%d bpp:%d", "");
                str::string_assign(&gGameTable.marni_config.display_mode, generate_res_string(&r));
                self->var_8C8318 = 0;
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
            out("you were about to set up invalid mode.", "MarniSystem Direct3D::ChangeMode");
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
        if ((self->gpu_flag & GpuFlags::GPU_9) == 0)
            return 0;

        if ((self->gpu_flag & GpuFlags::GPU_13) != 0)
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
                "Direct3D::ChangeMode");
            out("this method will change not mode you specified but previous mode.", "Direct3D::ChangeMode");
            self->xsize = self->xsize_old;
            self->ysize = self->ysize_old;
            self->bpp = self->bpp_old;
            out("previous x=%d y=%d bpp=%d request x=%d y=%d bpp=%d", "Direct3D::ChangeMode");
            clear_buffers(self);
            if (!init_all(self))
            {
                out("occurred fatal error. this method couldn't come back for somethings.", "Direct3D::ChangeMode");
                clear_buffers(self);
                return 0;
            }
        }
        if (!restore_surfaces(self))
        {
            out("this method failed for some problems to Reload that for change mode of display for some problems.",
                "Direct3D::ChangeMode");
            out("this method will change not mode you specified but previous mode.", "Direct3D::ChangeMode");
            self->xsize = self->xsize_old;
            self->ysize = self->ysize_old;
            self->bpp = self->bpp_old;
            out("previous x=%d y=%d bpp=%d request x=%d y=%d bpp=%d", "Direct3D::ChangeMode");
            clear_buffers(self);
            if (!init_all(self) || !restore_surfaces(self))
            {
                out("occurred fatal error at Reload. this method couldn't come back for somethings.", "Direct3D::ChangeMode");
                clear_buffers(self);
                return 0;
            }
        }
        self->gpu_flag |= GpuFlags::GPU_9;
        return 1;
    }

    // 0x004033F0
    static int __stdcall reload_texture(Marni* self, int texture)
    {
        if (!self->is_gpu_active)
            return 0;

        auto& tex = self->textures[texture];
        uint32_t vTbl = tex.var_00;
        if (!vTbl)
            return 1;
        if ((vTbl & 0x2000) != 0)
            return 1;
        if ((self->gpu_flag & 0x2000) != 0)
            return 1;

        // Destroy the old texture node chain.
        uint16_t v6 = tex.head;
        do
        {
            auto& node = self->texture_nodes[v6];
            if (node.surface)
            {
                surfacex_dtor(node.surface);
                operator_delete(node.surface);
            }
            node.surface = nullptr;
            v6 = node.next;
        } while (v6 != 0);

        DDSURFACEDESC ddesc = {};
        auto pSrc = &tex.surface;

        // Find the matching texture format for the source surface (fills ddesc).
        if (!interop::thiscall<int, Marni*, MarniSurface2*, DDSURFACEDESC*, int>(
                0x00402560, self, &tex.surface, &ddesc, vTbl & 4))
        {
            out("there's no format matching. Direct3D::ReloadTexture", "");
            return 0;
        }

        ddesc.dwWidth = tex.surface.width;
        ddesc.dwHeight = tex.surface.height;

        MarniSurfaceX surfX;
        surfacex_ctor(&surfX);

        auto compress_create = [&](MarniTextureNode& node) {
            if (node.surface->is_vmem)
            {
                node.var_14 |= 0x0100;
            }
            else
            {
                while (interop::thiscall<int, Marni*>(0x004170F0, self))
                {
                    surfacex_create_work(node.surface, (LPDIRECTDRAW)self->pDirectDraw2, &ddesc, -1);
                    if (node.surface->is_vmem)
                    {
                        node.var_14 |= 0x0100;
                        break;
                    }
                }
                if (!node.surface->is_vmem)
                    node.var_14 |= 0x1000;
            }
            surfacex_create_texture_object(node.surface);
        };

        switch (vTbl & 0xFFFFFFEB)
        {
        case 1:
        case 2:
        {
            ddesc.ddsCaps.dwCaps |= 0x800;
            uint16_t nodeIndex = tex.head;
            surfacex_create_work(&surfX, (LPDIRECTDRAW)self->pDirectDraw2, &ddesc, -1);
            surfacex_create_texture_object(&surfX);
            ddesc.ddsCaps.dwCaps &= ~0x800;
            tex.surface.var_22 = 0;

            auto& node = self->texture_nodes[nodeIndex];
            node.var_14 = vTbl;
            node.texture_id = (uint16_t)texture;
            node.page = tex.surface.var_22;
            if (node.surface)
            {
                surfacex_dtor(node.surface);
                operator_delete(node.surface);
            }
            node.surface = (MarniSurfaceX*)operator_new(0x44);
            if (node.surface)
                surfacex_ctor(node.surface);
            surfacex_create_work(node.surface, (LPDIRECTDRAW)self->pDirectDraw2, &ddesc, -1);
            if (!node.surface->bOpen)
            {
                out("failed to generate the surface. Direct3D::CreateTextureObject0Surface", "");
                if (node.surface)
                {
                    surfacex_dtor(node.surface);
                    operator_delete(node.surface);
                }
                node.surface = nullptr;
                surfacex_dtor(&surfX);
                return 0;
            }

            compress_create(node);

            node.width = surfX.width;
            node.height = surfX.height;
            interop::thiscall<void, MarniSurfaceX*, RECT*, RECT*, MarniSurface2*, int, int>(
                0x0040F370, &surfX, nullptr, nullptr, pSrc, 0, 0);
            gGameTable.error = surfacex_load(node.surface, &surfX);
            if (!gGameTable.error)
            {
                surfacex_get_texture_handle(node.surface, (LPDIRECT3DDEVICE2)self->pDirectDevice2);
                surfacex_dtor(&surfX);
                return 1;
            }

            // Failed to load the texture.
            out("failed to load texture.", "Direct3D::ReloadTexture");
            sub_416B90(self, texture);
            surfacex_dtor(&surfX);
            return 0;
        }

        case 0x22:
        case 0x41:
        case 0x42:
        {
            ddesc.ddsCaps.dwCaps |= 0x800;
            uint16_t nodeIndex = tex.head;
            surfacex_create_work(&surfX, (LPDIRECTDRAW)self->pDirectDraw2, &ddesc, -1);
            surfacex_create_texture_object(&surfX);
            ddesc.ddsCaps.dwCaps &= ~0x800;
            tex.surface.var_22 = 0;

            uint16_t counter = 0;
            for (;;)
            {
                auto& node = self->texture_nodes[nodeIndex];
                node.var_14 = vTbl;
                node.texture_id = (uint16_t)texture;
                node.page = counter;
                if (node.surface)
                {
                    surfacex_dtor(node.surface);
                    operator_delete(node.surface);
                }
                node.surface = (MarniSurfaceX*)operator_new(0x44);
                if (node.surface)
                    surfacex_ctor(node.surface);
                surfacex_create_work(node.surface, (LPDIRECTDRAW)self->pDirectDraw2, &ddesc, -1);
                if (!node.surface->bOpen)
                {
                    out("failed to generate the surface. Direct3D::ReloadTexture", "");
                    if (node.surface)
                    {
                        surfacex_dtor(node.surface);
                        operator_delete(node.surface);
                    }
                    node.surface = nullptr;
                    surfacex_dtor(&surfX);
                    return 0;
                }

                compress_create(node);

                RECT rc;
                RECT pRectDst;
                SetRect(&rc, 0, 0, pSrc->width - 1, pSrc->height - 1);
                SetRect(&pRectDst, 0, 0, pSrc->width - 1, pSrc->height - 1);
                node.width = surfX.width;
                node.height = surfX.height;
                tex.surface.var_22 = (int16_t)counter;
                surfX.var_2A = 1;
                interop::thiscall<void, MarniSurfaceX*, RECT*, RECT*, MarniSurface2*, int, int>(
                    0x0040F370, &surfX, &rc, &pRectDst, pSrc, 0, 0);
                gGameTable.error = surfacex_load(node.surface, &surfX);
                if (!gGameTable.error)
                {
                    surfacex_get_texture_handle(node.surface, (LPDIRECT3DDEVICE2)self->pDirectDevice2);
                    nodeIndex = node.next;
                    ++counter;
                    if (node.next == 0)
                    {
                        surfacex_dtor(&surfX);
                        return 1;
                    }
                    continue;
                }

                // Failed to load the texture.
                out("failed to load texture.", "Direct3D::ReloadTexture");
                sub_416B90(self, texture);
                surfacex_dtor(&surfX);
                return 0;
            }
        }

        case 0x81:
        case 0x82:
        case 0xA1:
        case 0xA2:
        {
            ddesc.ddsCaps.dwCaps |= 0x800;
            uint16_t nodeIndex = tex.head;
            surfacex_create_work(&surfX, (LPDIRECTDRAW)self->pDirectDraw2, &ddesc, -1);
            surfacex_create_texture_object(&surfX);
            ddesc.ddsCaps.dwCaps &= ~0x800;
            tex.surface.var_22 = 0;

            auto& node = self->texture_nodes[nodeIndex];
            node.var_14 = vTbl;
            node.texture_id = (uint16_t)texture;
            node.page = tex.surface.var_22;
            if (node.surface)
            {
                surfacex_dtor(node.surface);
                operator_delete(node.surface);
            }
            node.surface = (MarniSurfaceX*)operator_new(0x44);
            if (node.surface)
                surfacex_ctor(node.surface);
            surfacex_create_work(node.surface, (LPDIRECTDRAW)self->pDirectDraw2, &ddesc, tex.surface.pal_cnt);
            if (!node.surface->bOpen)
            {
                out("failed to generate the surface. Direct3D::CreateTextureObject0Surface", "");
                if (node.surface)
                {
                    surfacex_dtor(node.surface);
                    operator_delete(node.surface);
                }
                node.surface = nullptr;
                surfacex_dtor(&surfX);
                return 0;
            }

            compress_create(node);

            RECT rc;
            RECT pRectDst;
            SetRect(&rc, 0, 0, pSrc->width - 1, pSrc->height - 1);
            SetRect(&pRectDst, 0, 0, pSrc->width - 1, pSrc->height - 1);
            node.width = surfX.width;
            node.height = surfX.height;
            surfX.var_2A = 1;
            interop::thiscall<void, MarniSurfaceX*, RECT*, RECT*, MarniSurface2*, int, int>(
                0x0040F370, &surfX, &rc, &pRectDst, pSrc, 0, 0);
            for (int i = 0; i < tex.surface.pal_cnt; ++i)
                surface_pal_blt(node.surface, pSrc, i, i);
            gGameTable.error = surfacex_load(node.surface, &surfX);
            if (!gGameTable.error)
            {
                surfacex_get_texture_handle(node.surface, (LPDIRECT3DDEVICE2)self->pDirectDevice2);
                surfacex_dtor(&surfX);
                return 1;
            }

            // Failed to load the texture.
            out("failed to load texture.", "Direct3D::ReloadTexture");
            sub_416B90(self, texture);
            surfacex_dtor(&surfX);
            return 0;
        }

        case 0xC1:
        case 0xC2:
        {
            ddesc.ddsCaps.dwCaps |= 0x800;
            uint16_t nodeIndex = tex.head;
            surfacex_create_work(&surfX, (LPDIRECTDRAW)self->pDirectDraw2, &ddesc, -1);
            surfacex_create_texture_object(&surfX);
            ddesc.ddsCaps.dwCaps &= ~0x800;
            tex.surface.var_22 = 0;

            uint16_t counter = 0;
            for (;;)
            {
                auto& node = self->texture_nodes[nodeIndex];
                node.var_14 = vTbl;
                node.texture_id = (uint16_t)texture;
                node.page = counter;
                if (node.surface)
                {
                    surfacex_dtor(node.surface);
                    operator_delete(node.surface);
                }
                node.surface = (MarniSurfaceX*)operator_new(0x44);
                if (node.surface)
                    surfacex_ctor(node.surface);
                surfacex_create_work(node.surface, (LPDIRECTDRAW)self->pDirectDraw2, &ddesc, -1);
                if (!node.surface->bOpen)
                {
                    out("failed to generate the surface. Direct3D::CreateTextureObject0Surface", "");
                    if (node.surface)
                    {
                        surfacex_dtor(node.surface);
                        operator_delete(node.surface);
                    }
                    node.surface = nullptr;
                    surfacex_dtor(&surfX);
                    return 0;
                }

                compress_create(node);

                node.width = surfX.width;
                node.height = surfX.height;
                surfX.var_2A = 1;
                interop::thiscall<void, MarniSurfaceX*, RECT*, RECT*, MarniSurface2*, int, int>(
                    0x0040F370, &surfX, nullptr, nullptr, pSrc, 0, 0);
                gGameTable.error = surfacex_load(node.surface, &surfX);
                surface_pal_blt(node.surface, pSrc, counter, 0);
                if (gGameTable.error)
                {
                    // Failed to load the texture.
                    out("failed to load texture.", "Direct3D::ReloadTexture");
                    sub_416B90(self, texture);
                    surfacex_dtor(&surfX);
                    return 0;
                }
                surfacex_get_texture_handle(node.surface, (LPDIRECT3DDEVICE2)self->pDirectDevice2);
                nodeIndex = node.next;
                ++counter;
                if (node.next == 0)
                {
                    surfacex_dtor(&surfX);
                    return 1;
                }
            }
        }

        default:
            out("not support. Direct3D::ReloadTexture", "");
            surfacex_dtor(&surfX);
            return 0;
        }
    }

    // 0x00403ec0

    // Computes the letterboxed 4:3 rectangle (in screen coordinates) used to
    // present the render in borderless fullscreen. The rect is derived from the
    // actual window rect (GetWindowRect) rather than SDL display bounds: the
    // DirectDraw window-attached primary surface is sized to the window client
    // area in the process's DPI coordinate space, and GetWindowRect reports the
    // window in exactly that space (SDL_GetDisplayBounds can report a different,
    // physical-pixel DPI scale, which made the letterbox rect cover the whole
    // window and look stretched).
    static void compute_fullscreen_window_rect(Marni* self)
    {
        RECT window;
        if (GetWindowRect((HWND)self->hWnd, &window) && (window.right - window.left) > 0 && (window.bottom - window.top) > 0)
        {
            auto winW = window.right - window.left;
            auto winH = window.bottom - window.top;
            double scale = std::min((double)winW / self->xsize, (double)winH / self->ysize);
            auto rectW = (int)(self->xsize * scale);
            auto rectH = (int)(self->ysize * scale);
            SetRect(
                (LPRECT)&self->window_rect,
                window.left + (winW - rectW) / 2,
                window.top + (winH - rectH) / 2,
                window.left + (winW + rectW) / 2,
                window.top + (winH + rectH) / 2);
        }
        else
        {
            SetRect((LPRECT)&self->window_rect, 0, 0, self->xsize, self->ysize);
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
        // Fullscreen is handled via the SDL3 borderless window, so DirectDraw
        // always stays in windowed cooperative level (exclusive fullscreen with
        // SetDisplayMode is unsupported on modern Windows).
        gGameTable.error = dd_set_coop_level((HWND)self->hWnd, 0, (LPDIRECTDRAW2)self->pDirectDraw2);
        self->is_gpu_busy = 0;
        if (gGameTable.error)
        {
            out();
            error(gGameTable.error);
            self->is_gpu_active = 0;
            return 0;
        }
        get_z_buffer_caps(self);
        if (self->gpu_flag & GpuFlags::GPU_FULLSCREEN)
        {
            // Borderless fullscreen: let SDL3 cover the display and render at the
            // fullscreen mode's native resolution (a 4:3 mode sized for the
            // display), then letterbox it into the window. Keeping xsize/ysize at
            // the mode resolution is essential - overriding it with the window
            // rect size made DirectDraw surface/D3D device creation fail, leaving
            // a frozen black screen.
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

        DDSURFACEDESC desc;
        ZeroMemory(&desc, sizeof(DDSURFACEDESC));
        desc.dwSize = sizeof(DDSURFACEDESC);
        self->aspect_x = (float)((double)self->xsize / self->render_w);
        self->aspect_y = (float)((double)self->ysize / self->render_h);

        // Both windowed and borderless fullscreen use a window-attached primary
        // surface plus an offscreen render surface. The original exclusive
        // fullscreen flip chain is never used (it required SetDisplayMode).
        desc.dwFlags = DDSD_CAPS;
        desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
        gGameTable.error = ((LPDIRECTDRAW2)self->pDirectDraw2)
                               ->CreateSurface((LPDDSURFACEDESC)&desc, (LPDIRECTDRAWSURFACE*)&self->surface2.pDDsurface, NULL);
        if (gGameTable.error != 0)
        {
            out();
            return 0;
        }
        desc.dwWidth = self->xsize;
        desc.ddsCaps.dwCaps
            = ((self->gpu_flag & GpuFlags::GPU_4) ? 0 : DDSCAPS_PALETTE) | DDSCAPS_3DDEVICE | DDSCAPS_OFFSCREENPLAIN;
        desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
        desc.dwHeight = self->ysize;
        gGameTable.error = ((LPDIRECTDRAW2)self->pDirectDraw2)
                               ->CreateSurface((LPDDSURFACEDESC)&desc, (LPDIRECTDRAWSURFACE*)&self->surface0.pDDsurface, NULL);
        if (gGameTable.error)
        {
            out();
            return 0;
        }
        gGameTable.error = ((LPDIRECTDRAW2)self->pDirectDraw2)->CreateClipper(0, (LPDIRECTDRAWCLIPPER*)&self->pClipper, NULL);
        if (gGameTable.error)
        {
            out();
            return 0;
        }
        gGameTable.error = ((LPDIRECTDRAWCLIPPER)self->pClipper)->SetHWnd(0, (HWND)self->hWnd);
        if (gGameTable.error)
        {
            out();
            return 0;
        }
        gGameTable.error = gfx::surface_set_clipper((IUnknown*)self->surface2.pDDsurface, (IUnknown*)self->pClipper);
        if (gGameTable.error)
        {
            out();
            return 0;
        }

        if (self->gpu_flag & GpuFlags::GPU_FULLSCREEN)
        {
            // Black-fill the primary so the letterbox bars are clean.
            get_surface_desc(&desc, (LPDIRECTDRAWSURFACE)self->surface2.pDDsurface);
            RECT rc;
            SetRect(&rc, 0, 0, desc.dwWidth, desc.dwHeight);

            DDBLTFX ddbltfx;
            ZeroMemory(&ddbltfx, sizeof(DDBLTFX));
            ddbltfx.dwSize = sizeof(DDBLTFX);
            gfx::surface_blt(
                (IUnknown*)self->surface2.pDDsurface, &rc, nullptr, nullptr, DDBLT_WAIT | DDBLT_COLORFILL, (LPDDBLTFX)&ddbltfx);
        }

        gGameTable.error = get_surface_desc(&desc, (LPDIRECTDRAWSURFACE)self->surface0.pDDsurface);
        if (gGameTable.error)
        {
            out();
            return 0;
        }
        self->surface0.pDDpalette = nullptr;
        ddrawdesc2surfdesc(&desc, &self->surface0.desc);
        self->surface0.height = self->ysize;
        self->surface0.width = self->xsize;
        self->surface0.bpp = (uint8_t)desc.ddpfPixelFormat.dwRGBBitCount;
        self->surface0.var_25 = 0;
        self->surface0.pitch = (int16_t)desc.lPitch;
        self->surface0.var_27 = 1;
        self->surface0.var_28 = 0;
        self->surface0.var_29 = 0;
        self->surface0.bOpen = 1;
        self->surface0.is_vmem = desc.ddsCaps.dwCaps & DDSCAPS_HWCODEC ? 1 : 0;

        if (self->surface0.bpp == 16)
        {
            if (self->surface0.desc.b_bitcnt + self->surface0.desc.r_bitcnt + self->surface0.desc.g_bitcnt == 15)
                self->gpu_flag |= GpuFlags::GPU_11;
            else
                self->gpu_flag &= ~GpuFlags::GPU_11;
        }
        surface_fill(&self->surface0, 0, 0, 0);
        gGameTable.error = get_surface_desc(&desc, (LPDIRECTDRAWSURFACE)self->surface2.pDDsurface);
        if (gGameTable.error)
        {
            out();
            return 0;
        }
        self->surface2.pDDpalette = nullptr;
        ddrawdesc2surfdesc(&desc, &self->surface2.desc);
        self->surface2.width = (int16_t)desc.dwWidth;
        self->surface2.bpp = (uint8_t)desc.ddpfPixelFormat.dwRGBBitCount;
        self->surface2.desc.a_bitcnt = 0;
        self->surface2.is_vmem = desc.ddsCaps.dwCaps & DDSCAPS_HWCODEC ? 1 : 0;
        self->surface2.var_25 = 0;
        self->surface2.height = (int16_t)desc.dwHeight;
        self->surface2.pitch = (int16_t)desc.lPitch;
        self->surface2.var_27 = 1;
        self->surface2.var_28 = 0;
        self->surface2.var_29 = 0;
        self->surface2.bOpen = 1;
        if (!(self->gpu_flag & GpuFlags::GPU_13))
        {
            if (self->surfaceZ.pDDsurface != nullptr)
            {
                surface_release(&self->surfaceZ);
                self->surfaceZ.pDDsurface = nullptr;
            }

            LPDIRECTDRAWSURFACE lpZBuffer = nullptr;
            if (!create_zbuffer(self, self->xsize, self->ysize, &lpZBuffer))
            {
                out();
                return 0;
            }

            self->surfaceZ.desc.r_bitcnt = 5;
            self->surfaceZ.desc.g_shift = 5;
            self->surfaceZ.desc.g_bitcnt = 5;
            self->surfaceZ.desc.b_bitcnt = 5;
            self->surfaceZ.desc.r_mask = 31;
            self->surfaceZ.desc.g_mask = 31;
            self->surfaceZ.desc.b_mask = 31;
            self->surfaceZ.pDDsurface = lpZBuffer;
            self->surfaceZ.height = self->ysize;
            self->surfaceZ.pDDpalette = 0;
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

            if (!create_device(self))
            {
                out();
                return 0;
            }

            gGameTable.error = ((LPDIRECT3D2)self->pDirect3D2)->CreateViewport((LPDIRECT3DVIEWPORT2*)&self->pViewport, NULL);
            if (gGameTable.error != 0)
            {
                out();
                return 0;
            }

            gGameTable.error = ((LPDIRECT3DDEVICE2)self->pDirectDevice2)->AddViewport((LPDIRECT3DVIEWPORT2)self->pViewport);
            if (gGameTable.error != 0)
            {
                out();
                return 0;
            }

            D3DVIEWPORT2 vp;
            ZeroMemory(&vp, sizeof(D3DVIEWPORT2));
            vp.dwSize = sizeof(D3DVIEWPORT2);
            vp.dwX = 0;
            vp.dwY = 0;
            vp.dwWidth = self->xsize;
            vp.dwHeight = self->ysize;
            vp.dvClipX = -1.0;
            vp.dvClipY = -1.0;
            vp.dvClipWidth = 2.0;
            vp.dvClipHeight = 2.0;
            vp.dvMinZ = 0.0;
            vp.dvMaxZ = 1.0;
            gGameTable.error = gfx::viewport_set_viewport2((IUnknown*)self->pViewport, &vp);
            if (gGameTable.error)
                error(gGameTable.error);

            if (self->field_8C8300 > 6)
            {
                out();
                return 0;
            }

            gGameTable.error = ((LPDIRECT3D2)self->pDirect3D2)->CreateMaterial((LPDIRECT3DMATERIAL2*)&self->pMaterial, NULL);
            if (gGameTable.error != 0)
            {
                out();
                return 0;
            }

            ((LPDIRECT3DMATERIAL2)self->pMaterial)
                ->GetHandle((LPDIRECT3DDEVICE2)self->pDirectDevice2, (LPD3DMATERIALHANDLE)&self->MaterialHandle);
            D3DMATERIAL mat;
            ZeroMemory(&mat, sizeof(D3DMATERIAL));
            mat.dwSize = sizeof(D3DMATERIAL);
            mat.ambient.r = (float)(self->ambient_r * 0.0039215689);
            mat.ambient.g = (float)(self->ambient_g * 0.0039215689);
            mat.ambient.b = (float)(self->ambient_b * 0.0039215689);
            mat.ambient.a = 1.0;
            mat.diffuse.r = mat.ambient.r;
            mat.diffuse.g = mat.ambient.g;
            mat.diffuse.b = mat.ambient.b;
            mat.emissive.r = 0.0;
            mat.dwRampSize = 32;
            ((LPDIRECT3DMATERIAL2)self->pMaterial)->SetMaterial(&mat);
            gfx::viewport_set_background((IUnknown*)self->pViewport, self->MaterialHandle);
            self->is_gpu_active = 1;
            return 1;
        }
        if ((self->gpu_flag & GpuFlags::GPU_FULLSCREEN) != 0)
            surface_fill(&self->surface2, 0, 0, 0);
        self->is_gpu_active = 1;
        return 1;
    }

    // 0x00404bb0

    // 0x00404ca0

    // 0x00404CE0
    void __stdcall unload_texture(Marni* self, int handle)
    {
        if (handle == 0)
            return;

        auto& texture = self->textures[handle];
        if (texture.var_00 != 0)
        {
            texture_surface_release(self, handle);
            request_video_memory(self);
        }
    }

    // 0x00404D20
    int __stdcall clear(Marni* self)
    {
        if (!(self->gpu_flag & GpuFlags::GPU_9) || !self->is_gpu_active || self->var_8C7EE0
            || !(self->gpu_flag & GpuFlags::GPU_13) && (self->pDirectDevice2 == nullptr || self->pViewport == nullptr))
        {
            return 0;
        }

        if ((self->pMovie->flag & 2) != 0)
            return 1;

        D3DRECT rect;
        rect.x1 = 0;
        rect.y1 = 0;
        rect.x2 = self->xsize;
        rect.y2 = self->ysize;

        restore_surfaces(self);
        if ((self->gpu_flag & GpuFlags::GPU_3) == 0)
        {
            if ((self->gpu_flag & GpuFlags::GPU_13) == 0)
            {
                gGameTable.error = gfx::viewport_clear((IUnknown*)self->pViewport, 1, &rect, D3DCLEAR_ZBUFFER);
            }
        }
        else
        {
            if ((self->gpu_flag & GpuFlags::GPU_13) == 0)
            {
                gGameTable.error
                    = gfx::viewport_clear((IUnknown*)self->pViewport, 1, &rect, D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET);
            }
            else
            {
                surface_fill(&self->surface0, 0, self->ambient_b, 0);
            }
        }
        if (gGameTable.error == 0)
            return 1;
        out();
        return 0;
    }

    // 0x00404E40
    static void __stdcall do_render(Marni* self, MarniOt* pOt)
    {
        if (self->gpu_flag & GpuFlags::GPU_13)
            return;

        auto* device = (IUnknown*)self->pDirectDevice2;
        if (device == nullptr || self->pViewport == nullptr)
        {
            out("tried to render regardless of not initializint to viewport or device", "Direct3D::do_render");
            return;
        }

        gGameTable.error = gfx::device_begin_scene(device);
        sub_40E800(self, sub_416670(pOt));
        gfx::device_set_render_state(device, D3DRENDERSTATE_ANISOTROPY, 0);
        gfx::device_set_render_state(device, D3DRENDERSTATE_EDGEANTIALIAS, 0);
        gfx::device_set_render_state(device, D3DRENDERSTATE_ANTIALIAS, 0);
        gfx::device_set_render_state(device, D3DRENDERSTATE_SUBPIXEL, 0);
        gfx::device_set_render_state(device, D3DRENDERSTATE_LASTPIXEL, 1);
        if (FAILED(gGameTable.error))
        {
            d3d_error_routine(gGameTable.error);
            return;
        }

        trans_priority_list(self, pOt);
        sub_40EC10(self);
        gGameTable.error = gfx::device_end_scene(device);
        if (FAILED(gGameTable.error))
        {
            d3d_error_routine(gGameTable.error);
            return;
        }

        D3DSTATS stats;
        ZeroMemory(&stats, sizeof(D3DSTATS));
        stats.dwSize = sizeof(D3DSTATS);
        gfx::device_get_stats(device, &stats);
        self->triangles_drawn = stats.dwTrianglesDrawn - gGameTable.d3d_triangles_drawn;
        self->vertices_processed = stats.dwVerticesProcessed - gGameTable.d3d_vertices_processed;
        gGameTable.d3d_triangles_drawn = stats.dwTrianglesDrawn;
        gGameTable.d3d_vertices_processed = stats.dwVerticesProcessed;
    }

    // 0x00404FA0
    static int __stdcall clear_buffers(Marni* self)
    {
        // Deactivate the GPU and clear the "device created" flag.
        self->is_gpu_active = 0;
        self->gpu_flag &= ~GpuFlags::GPU_9;

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

        if (self->pMaterial != nullptr)
        {
            ((LPDIRECT3DMATERIAL2)self->pMaterial)->Release();
            self->pMaterial = nullptr;
        }
        if (self->pViewport != nullptr)
        {
            ((LPDIRECT3DVIEWPORT2)self->pViewport)->Release();
            self->pViewport = nullptr;
        }
        if (self->pDirectDevice2 != nullptr)
        {
            ((LPDIRECT3DDEVICE2)self->pDirectDevice2)->Release();
            self->pDirectDevice2 = nullptr;
        }
        if (self->pClipper != nullptr)
        {
            ((LPDIRECTDRAWCLIPPER)self->pClipper)->Release();
            self->pClipper = nullptr;
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

        if ((self->gpu_flag & GpuFlags::GPU_FULLSCREEN) != 0 && self->pDirectDraw != nullptr)
        {
            self->is_gpu_busy = 1;
            if (self->pDirectDraw2 != nullptr)
            {
                ((LPDIRECTDRAW2)self->pDirectDraw2)->RestoreDisplayMode();
                dd_set_coop_level((HWND)self->hWnd, 0, (LPDIRECTDRAW2)self->pDirectDraw2);
            }
            self->is_gpu_busy = 0;
        }

        if (self->pDirect3D2 != nullptr)
        {
            ((LPDIRECT3D2)self->pDirect3D2)->Release();
            self->pDirect3D2 = nullptr;
        }

        if (self->pDirectDraw2 != nullptr)
        {
            ((LPDIRECTDRAW2)self->pDirectDraw2)->Release();
            self->pDirectDraw2 = nullptr;
        }

        if (self->pDirectDraw != nullptr)
        {
            ((LPDIRECTDRAW)self->pDirectDraw)->Release();
            self->pDirectDraw = nullptr;
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

        gfx::shutdown();
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
        gfx::init();
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
        self->desktop_w = GetSystemMetrics(SM_CXSCREEN);
        self->desktop_h = GetSystemMetrics(SM_CYSCREEN);
        auto dc = GetDC(NULL);
        self->desktop_bpp = GetDeviceCaps(dc, BITSPIXEL) * GetDeviceCaps(dc, PLANES);
        ReleaseDC(NULL, dc);
        self->bpp = self->desktop_bpp;
        self->gpu_flag |= GpuFlags::GPU_4;
        self->is_gpu_busy = 0;
        *((uint32_t*)&self->ambient_b) = 0;
        self->var_8C8318 = 0;
        std::memset(self->var_8C76A0, 0, sizeof(self->var_8C76A0));
        self->pClipper = nullptr;
        self->pDirectDraw = 0;
        self->pMaterial = 0;
        self->pViewport = 0;
        self->pDirectDevice2 = nullptr;
        self->pDirect3D2 = 0;
        self->pDirectDraw2 = 0;
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

        DWORD isDefault;
        gGameTable.error = create_ddraw(self->gpu_flag & GpuFlags::ENUM_DEVICES, (LPDIRECTDRAW*)&self->pDirectDraw, &isDefault);
        if (gGameTable.error != 0)
        {
            out("The Marni failed to generate DirectDraw com.", "MarniSystem Direct3D::Direct3D");
            error(gGameTable.error);
            return self;
        }

        self->gpu_flag |= isDefault == 0 ? 0 : GpuFlags::GPU_7;
        gGameTable.error = query_ddraw2((LPDIRECTDRAW)self->pDirectDraw, (LPDIRECTDRAW2*)&self->pDirectDraw2);
        if (gGameTable.error != 0)
        {
            out("failed to generate DirectDraw2 COM", "MarniSystem Direct3D::Direct3D");
            error(gGameTable.error);
            return self;
        }

        DDCAPS ddCaps;
        ddCaps.dwSize = sizeof(DDCAPS);
        gGameTable.error = ((LPDIRECTDRAW)self->pDirectDraw)->GetCaps(&ddCaps, NULL);
        if (gGameTable.error != 0)
        {
            out("GetCaps failed err", "MarniSystem Direct3D::Direct3D");
            error(gGameTable.error);
            return self;
        }

        out("you will be able to use the VideoMemory...%dbyte", "MarniSystem Direct3D::Direct3D");
        self->dwVidMemTotal = ddCaps.dwVidMemTotal;
        if (create_d3d(self) != 0)
        {
            out("failed to generate the object of Direct3D.", "MarniSystem Direct3D::Direct3D");
            return self;
        }

        if (!enum_drivers(self))
        {
            out("failed to detect some driver.", "MarniSystem Direct3D::Direct3D");
            return self;
        }

        for (auto i = 0; i < gGameTable.d3d_device_count; i++)
        {
            const auto& d = gGameTable.d3d_devices[i];
            exception = 11;
            auto isEq = str::string_eq(&gGameTable.marni_config.device_name, std::string(d.lpDeviceName));
            exception = 9;
            if (isEq)
            {
                self->device_cnt = i;
                break;
            }
        }

        exception = 12;
        str::string_assign(&gGameTable.marni_config.device_name, gGameTable.d3d_devices[self->device_cnt].lpDeviceName);
        exception = 11;

        if (gGameTable.d3d_devices[self->device_cnt].hwAccelerated2 || (self->gpu_flag & GpuFlags::GPU_13))
        {
            self->gpu_flag |= GpuFlags::INCLUDE_2X;
        }

        for (auto i = 0; i < gGameTable.d3d_device_count; i++)
        {
            const auto& d = gGameTable.d3d_devices[i];
            // d.lpDeviceName
            out("detect %s", "MarniSystem Direct3D::Direct3D");
        }

        size_t numDisplayModes;
        MarniRes res[64];
        gGameTable.error = enum_display_mode((LPDIRECTDRAW2)self->pDirectDraw2, res, std::size(res), &numDisplayModes);
        if (gGameTable.error != 0)
        {
            out("failed to detect the mode.", "MarniSystem Direct3D::Direct3D");
            return self;
        }

        // Build the display mode list. The original game only offered a single 640x480
        // fullscreen mode (plus an optional 2x windowed mode), so F8 could never cycle
        // between different resolutions and the DirectDraw exclusive fullscreen switch
        // fails on modern systems. Instead we offer a set of windowed resolutions plus
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
            out("you specified invalid mode. correct disp num to 0 automatically.", "MarniSystem Direct3D::Direct3D");
            self->modes = 0;
        }

        if (!init_all(self))
        {
            out("failed to initialize.", "MarniSystem Direct3D::Direct3D");
            return self;
        }

        out("you will be able to use the following mode", "MarniSystem Direct3D::Direct3D");

        for (auto i = 0; i < self->res_count; i++)
        {
            out("%d x %d x %d full=%d", "MarniSystem Direct3D::Direct3D");
        }

        if (self->gpu_flag & GpuFlags::GPU_13)
        {
            self->is_gpu_active = 1;
            self->gpu_flag |= GpuFlags::GPU_9;
        }
        else
        {
            auto descA = (LPD3DDEVICEDESC)self->field_8C7088;
            descA->dwSize = sizeof(D3DDEVICEDESC);
            auto descB = (LPD3DDEVICEDESC)self->field_8C7184;
            descB->dwSize = sizeof(D3DDEVICEDESC);
            gGameTable.error = ((LPDIRECT3DDEVICE2)self->pDirectDevice2)->GetCaps(descA, descB);

            MarniSurface2 surface;
            surface2_ctor(&surface);
            exception = 15;
            surface2_create_work(&surface, 16, 16, 32, 0, -1);
            surface.desc.a_bitcnt = 0;
            surface2_vfill(&surface, 0, 0xFFFFFF, 0);
            gGameTable.dword_6449BC = create_texture_handle(self, &surface, 2);
            self->gpu_flag |= GpuFlags::GPU_9;
            self->is_gpu_active = 1;
            request_video_memory(self);
            exception = 9;
            surface2_release(&surface);
        }
        return self;
    }

    // 0x00405dc0

    // 0x00405DD0
    static int __stdcall get_z_buffer_caps(Marni* self)
    {
        DDCAPS driverCaps = {};
        DDCAPS helCaps = {};
        driverCaps.dwSize = sizeof(DDCAPS);
        helCaps.dwSize = sizeof(DDCAPS);

        gGameTable.error = ((LPDIRECTDRAW2)self->pDirectDraw2)->GetCaps(&driverCaps, &helCaps);
        if (gGameTable.error)
        {
            out("GetCaps failed in while checking driver capabilities", "MarniSystem Direct3D Class");
            return 0;
        }

        if (driverCaps.dwZBufferBitDepths == 0)
            self->zbuffer_depth = 16;
        else if (driverCaps.dwZBufferBitDepths & DDBD_32)
            self->zbuffer_depth = 32;
        else if (driverCaps.dwZBufferBitDepths & DDBD_24)
            self->zbuffer_depth = 24;
        else if (driverCaps.dwZBufferBitDepths & DDBD_16)
            self->zbuffer_depth = 16;
        else if (driverCaps.dwZBufferBitDepths & DDBD_8)
            self->zbuffer_depth = 8;
        else
        {
            out("No valid Z-Buffer depths available", "MarniSystem Direct3D Class");
            return 0;
        }

        self->field_8C7284 = driverCaps.ddsCaps.dwCaps;
        return (int)driverCaps.ddsCaps.dwCaps;
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
            out("there is no available work of texture object level0. Direct3D::SearchTextureObject1",
                "Direct3D::SearchTextureObject1");
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
                    out("there is no available work of texture object level0. Direct3D::SearchTextureObject1",
                        "Direct3D::SearchTextureObject1");
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
            out("invalid bits specified. Direct3D::CreateTextureHandle", "Direct3D::CreateTextureHandle");
            return 0;
        }

        auto gpu_flg = self->gpu_flag;
        if ((gpu_flg & GpuFlags::GPU_13) != 0 || (mode & 0x4000) != 0)
        {
            // Temporary texture object: allocate the slot and copy the source
            // surface into it without creating a GPU texture (no reload).
            auto texture_id = search_texture_object_1(self, 0);
            if (!texture_id)
            {
                out("failed to allocate on not enough memory. Direct3D::CreateTextureHandle", "Direct3D::CreateTextureHandle");
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
            out("size greater than 256x256 is not supported. Direct3D::CreateTextureHandle", "Direct3D::CreateTextureHandle");
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
        if ((gpu_flg & (GpuFlags::GPU_0 | GpuFlags::GPU_1)) == 0 && (mode & 0x20) != 0)
        {
            mode = (mode & ~0x20) | 0x40;
        }
        else if (
            pSrcSurface->var_28
            && ((
                (bpp == 4 && ((gpu_flg & GpuFlags::GPU_1) != 0 || (gpu_flg & GpuFlags::GPU_0) != 0))
                || (bpp == 8 && (gpu_flg & GpuFlags::GPU_1) != 0))))
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
        default: out("not supported type...0x%08x Direct3D::CreateTextureHandle", "Direct3D::CreateTextureHandle"); return 0;
        }

        self->textures[texture_id].var_00 = mode;
        if ((mode & 0x2000) != 0)
            return texture_id;

        if (!reload_texture(self, texture_id))
        {
            out("failed to generate the texutre Direct3D::CreateTextureHandle", "Direct3D::CreateTextureHandle");
            texture_surface_release(self, texture_id);
            return 0;
        }

        request_video_memory(self);
        return texture_id;

    alloc_failed:
        out("failed to allocate on not enough memory. Direct3D::CreateTextureHandle", "Direct3D::CreateTextureHandle");
        return 0;
    }

    // 0x004063D0
    long __stdcall message(Marni* self, void* hWnd, uint32_t msg, void* wParam, void* lParam)
    {
        switch (msg)
        {
        case WM_MOVE: move(self); break;
        case WM_SIZE: resize(self, (HWND)hWnd, msg, (WPARAM)wParam, (LPARAM)lParam); break;
        case WM_DESTROY: destroy(self); break;
        case WM_SYSKEYDOWN:
            if ((self->gpu_flag & GpuFlags::GPU_FULLSCREEN) != 0)
            {
                syskeydown(self);
            }
            break;
        }
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
        auto window = (HWND)marni->hWnd;
        POINT point0 = {};
        ClientToScreen(window, &point0);
        POINT point1 = {};
        point1.x = marni->resolutions[marni->modes].width;
        point1.y = marni->resolutions[marni->modes].height;
        ClientToScreen(window, &point1);
        SetRect((LPRECT)&marni->window_rect, point0.x, point0.y, point1.x, point1.y);
    }

    // 0x004064D0
    static void __stdcall destroy(Marni* marni)
    {
        marni->gpu_flag &= ~GpuFlags::GPU_9;

        clear_buffers(marni);

        for (auto i = 0; i < 256; i++)
            unload_texture(marni, i);

        auto pClipper = (LPDIRECTDRAWCLIPPER)marni->pClipper;
        marni->hWnd = nullptr;
        if (pClipper != nullptr)
        {
            pClipper->Release();
            marni->pClipper = nullptr;
        }

        surface_release(&marni->surface0);
        surface_release(&marni->surface2);

        if ((marni->gpu_flag & GpuFlags::GPU_FULLSCREEN) != 0)
        {
            marni->is_gpu_busy = 1;
            ((LPDIRECTDRAW2)marni->pDirectDraw2)->RestoreDisplayMode();
            dd_set_coop_level((HWND)marni->hWnd, 0, (LPDIRECTDRAW2)marni->pDirectDraw2);
            marni->is_gpu_busy = 0;
        }

        movie_release(marni->pMovie);

        if (marni->pDirect3D2 != nullptr)
        {
            ((LPDIRECT3D2)marni->pDirect3D2)->Release();
            marni->pDirect3D2 = nullptr;
        }

        if (marni->pDirectDraw2 != nullptr)
        {
            ((LPDIRECTDRAW2)marni->pDirectDraw2)->Release();
            marni->pDirectDraw2 = nullptr;
        }
    }

    // 0x004065C0
    static int __stdcall resize(Marni* marni, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (!marni->is_gpu_active)
            return 1;

        if (wParam == 1)
        {
            marni->var_8C7EE0 = 1;
            sub_401F00(marni);
            DefWindowProcA(hWnd, msg, 1, lParam);
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
            DefWindowProcA(hWnd, msg, wParam, lParam);
            surface_fill(&marni->surface2, 0, 0, 0);
            marni->var_8C8318 = 0;
            return 1;
        }

        if (marni->var_8C7EE0)
        {
            marni->var_8C7EE0 = 0;
            DefWindowProcA(hWnd, msg, wParam, lParam);
            prepare_movie(marni);
            return 1;
        }

        marni->is_gpu_busy = 1;
        if (!(gpu_flg & GpuFlags::GPU_9))
            return 0;

        clear_buffers(marni);
        auto result = init_all(marni);
        if (result)
        {
            restore_surfaces(marni);
            marni->is_gpu_busy = 0;
            marni->gpu_flag |= GpuFlags::GPU_9;
            DefWindowProcA(hWnd, msg, wParam, lParam);
            return 1;
        }
        return result;
    }

    // 0x00406860
    static int query_ddraw2(LPDIRECTDRAW pDD, LPDIRECTDRAW2* lpDD2)
    {
        return pDD->QueryInterface(IID_IDirectDraw2, (LPVOID*)lpDD2);
    }

    // 0x00406880

    // 0x00406920

    // 0x00406970
    static int D3DIBPPToDDBD(int bpp)
    {
        switch (bpp)
        {
        case 1: return DDBD_1;
        case 2: return DDBD_2;
        case 4: return DDBD_4;
        case 8: return DDBD_8;
        case 16: return DDBD_16;
        case 24: return DDBD_24;
        case 32: return DDBD_32;
        default: out("", "D3DIBPPToDDBD"); return 0;
        }
    }

    // 0x00406A10
    static void d3d_error_routine(int errorCode)
    {
        interop::call<void, int>(0x00406A10, errorCode);
    }

    // 0x00406D90
    static int __stdcall create_device(Marni* self)
    {
        // Texture format table stored inline in the Marni object
        // (count at +0x8C78A0, entries from +0x8C78A4).
        constexpr size_t kFormatEntrySize = 0x6C; // legacy DDSURFACEDESC size (modern ddraw.h is 0x7C)

        // Release any previously created D3D device.
        if (self->pDirectDevice2)
        {
            ((LPDIRECT3DDEVICE2)self->pDirectDevice2)->Release();
            self->pDirectDevice2 = nullptr;
        }

        // Software renderer (GPU_13): no D3D device is created.
        if (self->gpu_flag & GpuFlags::GPU_13)
            return 1;

        // Pick the device class: use the HAL device when the selected driver is
        // hardware accelerated, otherwise fall back to the software RGB device.
        const GUID* pDeviceGuid = &IID_IDirect3DHALDevice;
        if (!gGameTable.d3d_devices[self->device_cnt].hwAccelerated)
            pDeviceGuid = &IID_IDirect3DRGBDevice;

        gGameTable.error
            = ((LPDIRECT3D2)self->pDirect3D2)
                  ->CreateDevice(
                      *pDeviceGuid, (LPDIRECTDRAWSURFACE)self->surface0.pDDsurface, (LPDIRECT3DDEVICE2*)&self->pDirectDevice2);
        if (gGameTable.error)
        {
            error(gGameTable.error);
            out("failed to generate the D3DDevice2 for Draw Primitive.", "MarniSystem Direct3D::MD3DCreateDevice");
            return 0;
        }

        out("driver...%s", gGameTable.d3d_devices[self->device_cnt].lpDeviceDescription);

        if (gGameTable.d3d_devices[self->device_cnt].supportsFloat)
        {
            // The game enumerates the texture formats the device supports and
            // keeps the high-colour ones (15+ total RGB bits with an alpha
            // channel, non-paletted) in its private format table.
            constexpr size_t kMaxFormats = 20;
            uint8_t formats[kMaxFormats * kFormatEntrySize];

            // D3DEnumTextureFormats (0x00406880), not yet decompiled.
            int formatCount = interop::call<int, LPDIRECT3DDEVICE2, int, LPDDSURFACEDESC>(
                0x00406880, (LPDIRECT3DDEVICE2)self->pDirectDevice2, (int)kMaxFormats, (LPDDSURFACEDESC)formats);
            if (formatCount != 0)
            {
                int matched = 0;
                if (formatCount > 0)
                {
                    auto* pTable = reinterpret_cast<uint8_t*>(self) + 0x8C78A4;
                    for (int i = 0; i < formatCount; i++)
                    {
                        auto* pFormat = (DDSURFACEDESC*)(formats + kFormatEntrySize * i);
                        MarniSurfaceDesc sDesc;
                        ddrawdesc2surfdesc(pFormat, &sDesc);
                        if (sDesc.r_bitcnt + sDesc.g_bitcnt + sDesc.b_bitcnt >= 15 && sDesc.a_bitcnt != 0
                            && (pFormat->ddpfPixelFormat.dwFlags & (DDPF_PALETTEINDEXED4 | DDPF_PALETTEINDEXED8)) == 0)
                        {
                            auto* pDest = (DDSURFACEDESC*)(pTable + kFormatEntrySize * matched);
                            memcpy(pDest, pFormat, kFormatEntrySize);
                            pDest->dwFlags = 0x1007;           // DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM
                            pDest->ddsCaps.dwCaps = 0x4001000; // DDSCAPS2_TEXTUREMANAGE | DDSCAPS_TEXTURE
                            matched++;
                        }
                    }
                }
                *(int32_t*)((uint8_t*)self + 0x8C78A0) = matched;
            }
            else
            {
                out("failed to detect a texture formats.", "MarniSystem Direct3D::MD3DCreateDevice");
                if (self->pDirectDevice2)
                {
                    ((LPDIRECT3DDEVICE2)self->pDirectDevice2)->Release();
                    self->pDirectDevice2 = nullptr;
                }
                return 0;
            }
        }

        out("texture formats detected here...%d", "MD3DCreateDevice");
        self->gpu_flag &= ~(GpuFlags::GPU_0 | GpuFlags::GPU_1);

        auto formatCount = *(int32_t*)((uint8_t*)self + 0x8C78A0);
        if (formatCount > 0)
        {
            auto* pTable = reinterpret_cast<uint8_t*>(self) + 0x8C78A4;
            for (int i = 0; i < formatCount; i++)
            {
                auto* pEntry = (DDSURFACEDESC*)(pTable + kFormatEntrySize * i);
                if (pEntry->ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED4)
                {
                    out("4 palette index", "");
                }
                else if (pEntry->ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8)
                {
                    out("8 palette index", "");
                }
                else
                {
                    MarniSurfaceDesc sDesc;
                    ddrawdesc2surfdesc(pEntry, &sDesc);
                    out("%d%d%d%d", "");
                }
            }
        }

        return 1;
    }

    // 0x00407020
    static int __stdcall create_zbuffer(Marni* self, int width, int height, LPDIRECTDRAWSURFACE* pDDsurfaceZ)
    {
        if (self->gpu_flag & GpuFlags::GPU_13)
            return 1;

        if (!gGameTable.d3d_devices[self->device_cnt].supportsZbuffer)
        {
            out("it seems that this driver hasn't function of Zbuffer.", "MarniSystem Direct3D::MD3DCreateZBuffer");
            *pDDsurfaceZ = 0;
            return 0;
        }

        // Query the back buffer's desc so the z-buffer inherits its memory type
        // (system or video memory).
        DDSURFACEDESC backBufferDesc;
        memset(&backBufferDesc, 0, sizeof(backBufferDesc));
        backBufferDesc.dwSize = sizeof(DDSURFACEDESC);
        backBufferDesc.dwFlags = DDSD_CAPS;
        gfx::surface_get_surface_desc((IUnknown*)self->surface0.pDDsurface, &backBufferDesc);

        DDSURFACEDESC desc;
        memset(&desc, 0, sizeof(desc));
        desc.dwHeight = height;
        desc.ddsCaps.dwCaps = (backBufferDesc.ddsCaps.dwCaps & (DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY)) | DDSCAPS_ZBUFFER;
        desc.dwWidth = width;
        desc.dwSize = sizeof(DDSURFACEDESC);
        desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_ZBUFFERBITDEPTH;

        // Pick the deepest z-buffer bit depth the device advertised.
        auto zBufferBitDepth = ((LPD3DDEVICEDESC)gGameTable.d3d_devices[self->device_cnt].desc)->dwDeviceZBufferBitDepth;
        if (zBufferBitDepth & 0x100)
            desc.dwZBufferBitDepth = 32;
        else if (zBufferBitDepth & 0x200)
            desc.dwZBufferBitDepth = 24;
        else if (zBufferBitDepth & 0x400)
            desc.dwZBufferBitDepth = 16;
        else if (zBufferBitDepth & 0x800)
            desc.dwZBufferBitDepth = 8;
        else
        {
            out("it seems that this device can't specify a number of bit of Zbuffer.",
                "MarniSystem Direct3D::MD3DCreateZBuffer");
            goto fail;
        }

        gGameTable.error = ((LPDIRECTDRAW)self->pDirectDraw)->CreateSurface((LPDDSURFACEDESC)&desc, pDDsurfaceZ, 0);
        if (gGameTable.error)
        {
            // The device rejected the requested depth; retry with the next lower
            // one (32 -> 24 -> 16) before giving up.
            while (desc.dwZBufferBitDepth > 16)
            {
                if (desc.dwZBufferBitDepth == 24)
                    desc.dwZBufferBitDepth = 16;
                else if (desc.dwZBufferBitDepth == 32)
                    desc.dwZBufferBitDepth = 24;

                gGameTable.error = ((LPDIRECTDRAW)self->pDirectDraw)->CreateSurface((LPDDSURFACEDESC)&desc, pDDsurfaceZ, 0);
                if (!gGameTable.error)
                    break;
            }
            if (gGameTable.error)
            {
                error(gGameTable.error);
                out("failed to generate the Zbuffer.", "MarniSystem Direct3D::MD3DCreateZBuffer");
                goto fail;
            }
        }

        gGameTable.error = gfx::surface_add_attached_surface((IUnknown*)self->surface0.pDDsurface, (IUnknown*)*pDDsurfaceZ);
        if (gGameTable.error)
        {
            error(gGameTable.error);
            out("failed to attache with Zbuffer.", "MarniSystem Direct3D::MD3DCreateZBuffer");
            goto fail;
        }

        gGameTable.error = get_surface_desc(&desc, *pDDsurfaceZ);
        if (gGameTable.error)
        {
            out("\x83\x54\x81\x5b\x83\x74\x83\x46\x83\x58\x82\xcc\x8f\xf3\x91\xd4\x82\xf0\x8e\xe6\x93\xbe\x82\xc5\x82\xab\x82"
                "\xc8\x82\xa9\x82\xc1\x82\xbd", // サーフェスの状態を取得できなかった
                "MarniSystem Direct3D::MD3DCreateZBuffer");
            goto fail;
        }

        // Remember whether the z-buffer ended up in video memory.
        self->gpu_flag |= (desc.ddsCaps.dwCaps >> 9) & 0x20;
        return 1;

    fail:
        if (*pDDsurfaceZ)
        {
            (*pDDsurfaceZ)->Release();
            *pDDsurfaceZ = 0;
        }
        return 0;
    }

    // 0x00407290
    static HRESULT CALLBACK enum_driver_callback(
        GUID* lpGuid, LPSTR lpDeviceDescription, LPSTR lpDeviceName, LPD3DDEVICEDESC descSw, LPD3DDEVICEDESC descHw,
        LPVOID lpContext)
    {
        auto& device = gGameTable.d3d_devices[gGameTable.d3d_device_count];
        memcpy(device.GUID, lpGuid, sizeof(GUID));
        strncpy(device.lpDeviceDescription, lpDeviceDescription, sizeof(device.lpDeviceDescription));
        strncpy(device.lpDeviceName, lpDeviceName, sizeof(device.lpDeviceName));
        if (descSw->dwFlags == 0)
        {
            device.hwAccelerated = 0;
            memcpy(device.desc, descHw, sizeof(D3DDEVICEDESC));
        }
        else
        {
            device.hwAccelerated = 1;
            memcpy(device.desc, descSw, sizeof(D3DDEVICEDESC));
        }
        gGameTable.d3d_device_count++;
        return gGameTable.d3d_device_count < 4 ? D3DENUMRET_OK : D3DENUMRET_CANCEL;
    }

    // 0x00407340
    static int __stdcall enum_drivers(Marni* self)
    {
        if (self->gpu_flag & GpuFlags::GPU_13)
            return 1;

        auto pD3D2 = (LPDIRECT3D2)self->pDirect3D2;
        gGameTable.d3d_device_count = 0;
        gGameTable.error = pD3D2->EnumDevices(enum_driver_callback, NULL);
        if (FAILED(gGameTable.error))
        {
            out("failed to detect drivers that can use.", "MarniSystem Direct3D::MD3D");
            return 0;
        }

        auto bestScore = -1;
        for (auto i = 0; i < gGameTable.d3d_device_count; i++)
        {
            auto& device = gGameTable.d3d_devices[i];
            auto desc = (LPD3DDEVICEDESC)device.desc;
            device.supportsFloat = desc->dwDevCaps & D3DDEVCAPS_FLOATTLVERTEX;
            device.supportsZbuffer = desc->dwDeviceZBufferBitDepth != 0;
            device.hwAccelerated2 = device.hwAccelerated;
            auto supportsDepth
                = (D3DIBPPToDDBD(self->bpp) & desc->dwDeviceRenderBitDepth) != 0 && ((self->gpu_flag & GpuFlags::GPU_7) != 0);
            auto score = (device.supportsZbuffer != 0) + (supportsDepth ? 2 : 0) + (device.supportsFloat != 0)
                + (device.hwAccelerated != 0 ? 4 : 0);
            if (bestScore < score)
            {
                bestScore = score;
                self->device_cnt = i;
            }
        }
        return 1;
    }

    // 0x00407440
    static int __stdcall create_d3d(Marni* self)
    {
        if (self->gpu_flag & GpuFlags::GPU_13)
            return 0;

        auto dd2 = (LPDIRECTDRAW2)self->pDirectDraw2;
        gGameTable.error = dd2->QueryInterface(IID_IDirect3D2, &self->pDirect3D2);
        if (gGameTable.error != 0)
        {
            error(gGameTable.error);
        }
        return gGameTable.error;
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
        // material unless the GPU is in software mode (GPU_13).
        if ((pScaler->type & 0x1000) != 0)
        {
            *(uint32_t*)&self->ambient_b = pScaler->rgb1;
            if ((self->gpu_flag & GpuFlags::GPU_13) == 0)
            {
                D3DMATERIAL mat;
                ZeroMemory(&mat, sizeof(mat));
                mat.dwSize = sizeof(D3DMATERIAL);
                mat.diffuse.r = (float)self->ambient_r * 0.0039215689f;
                mat.diffuse.g = (float)self->ambient_g * 0.0039215689f;
                mat.diffuse.b = (float)self->ambient_b * 0.0039215689f;
                mat.diffuse.a = 1.0f;
                mat.ambient.r = mat.diffuse.r;
                mat.ambient.g = mat.diffuse.g;
                mat.ambient.b = mat.diffuse.b;
                mat.ambient.a = 1.0f;
                mat.emissive.a = 1.0f;
                mat.dwRampSize = 32;
                ((LPDIRECT3DMATERIAL2)self->pMaterial)->SetMaterial(&mat);
                gfx::viewport_set_background((IUnknown*)self->pViewport, self->MaterialHandle);
            }
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

    // 0x00415E80
    static int __stdcall refer_vertex(PolygonObject* self, int index, float* out)
    {
        return interop::thiscall<int, PolygonObject*, int, float*>(0x00415E80, self, index, out);
    }

    // 0x00415AE0
    static int __stdcall refer_normal(PolygonObject* self, int index, float* out)
    {
        return interop::thiscall<int, PolygonObject*, int, float*>(0x00415AE0, self, index, out);
    }

    // 0x004156E0
    static int __stdcall modify_primitive(PolygonObject* self, int index, void* out)
    {
        return interop::thiscall<int, PolygonObject*, int, void*>(0x004156E0, self, index, out);
    }

    // 0x0040E9D0
    static int sub_40E9D0(
        Marni* self, int filter, int a3, int srcBlend, int dstBlend, int textureHandle, int zWriteEnable, int shadeMode,
        int cullMode, int specularEnable, int zFunc, LPD3DTLVERTEX vertices, int vertexCount)
    {
        int result = vertexCount;
        if (vertexCount > 0)
        {
            auto v14 = &vertices[2];
            auto triangleCount = (vertexCount - 1) / 3 + 1;
            do
            {
                LPD3DTLVERTEX v17[3];
                v17[0] = &v14[-2];
                v17[1] = &v14[-1];
                v17[2] = v14;
                result = insert_draw_op(
                    self,
                    filter,
                    a3,
                    srcBlend,
                    dstBlend,
                    textureHandle,
                    zWriteEnable,
                    shadeMode,
                    cullMode,
                    specularEnable,
                    zFunc,
                    v17);
                v14 += 3;
                --triangleCount;
            } while (triangleCount);
        }
        return result;
    }

    // 0x00407690
    static int __stdcall trans_object_ngtin3_vinsnins(Marni* self, MarniOt* pOt, Prim* pPrim)
    {
        // The 2K object primitive stores a texture handle at offset 0x08; the
        // CLUT index for the base texture section is at offset 0x54.
        const auto* prim = (const uint8_t*)pPrim;
        const uint32_t textureHandle = *(const uint32_t*)(prim + 8);
        if (textureHandle == 0)
        {
            out("specified NULL handle as Texture.", "Direct3D::TransObjectNgTin3_VinsNins");
            return 0;
        }

        MarniTextureNode* textures[0x800] = { nullptr };
        textures[0] = search_texture_object_0_from_1_in_condition(self, textureHandle, *(const uint8_t*)(prim + 0x54));
        if (textures[0] == nullptr)
        {
            out("invalud handle.", "Direct3D::TransObjectTin4_Vfp");
            return 0;
        }

        // Up to 4 texture sections: each additional CLUT is looked up only when
        // the corresponding split-table word (offset 0x58) is non-zero.
        for (int i = 1; i < 4; i++)
        {
            if (*(const uint16_t*)(prim + 0x58 + 2 * (i - 1)) != 0)
            {
                textures[i]
                    = search_texture_object_0_from_1_in_condition(self, textureHandle, *(const uint8_t*)(prim + 0x54 + i));
                if (textures[i] == nullptr)
                {
                    out("invalid clut range.", "Direct3D::TransObjectNgTin3_VinsNins");
                    return 0;
                }
            }
        }

        // The polygon object is held in the 2K buffer; when flagged, copy its
        // header (vertex/normal/primitive counts) from offset 0x10.
        auto* pObject = self->polygons[*(const uint32_t*)(prim + 0x4C)];
        uint32_t header[9] = { 0 };
        if ((((const uint8_t*)pObject)[0x34] & 1) != 0)
            memcpy(header, (const uint8_t*)pObject + 0x10, sizeof(header));

        // Primitive colour: B/G/R are doubled (modulated by the per-normal
        // light), A is used raw; each channel is clamped to 8-bit range.
        const int32_t primType = pPrim->type;
        const uint8_t alpha = *(const uint8_t*)(prim + 0x53);
        int32_t primB = 2 * *(const uint8_t*)(prim + 0x52);
        int32_t primG = 2 * *(const uint8_t*)(prim + 0x51);
        int32_t primR = 2 * *(const uint8_t*)(prim + 0x50);
        if (primB >= 256)
            primB = 255;
        if (primG >= 256)
            primG = 255;
        if (primR >= 256)
            primR = 255;

        uint32_t fallbackColor = 0;
        uint32_t colors[0x400] = { 0 };
        if ((int32_t)primType < 0 || (self->gpu_flag & GpuFlags::GPU_17) != 0)
        {
            // Flat shading: build a single colour from the primitive colour and
            // the alpha byte picked by the 0x100000..0x400000 mode bits.
            uint32_t base;
            switch (primType & 0xF00000)
            {
            case 0x100000: base = (uint32_t)primB | 0xFFFF8000; break;
            case 0x300000: base = (uint32_t)primB | 0x4000; break;
            case 0x400000: base = (uint32_t)primB | ((uint32_t)alpha << 8); break;
            default: base = (uint32_t)primB | 0xFFFFFF00; break;
            }
            fallbackColor = (uint32_t)primR | (((uint32_t)primG | (base << 8)) << 8);
        }
        else
        {
            // Per-normal lighting: transform each normal through the two light
            // matrices, add the ambient colour and modulate by the primitive
            // colour. Components equal to zero (or unordered/NaN) are clamped.
            const float* lightMatrix1 = (const float*)&self->field_8C7E10;
            const float* lightMatrix2 = &self->field_8C7E50;
            const auto* ambient = (const uint8_t*)&self->field_8C7E90;
            for (uint32_t n = 0; n < header[4]; n++)
            {
                float normal[3];
                refer_normal(pObject, n, normal);

                apply_matrix_float(normal, lightMatrix1);
                if (!(normal[0] >= 0.0f))
                    normal[0] = 0.0f;
                if (!(normal[1] >= 0.0f))
                    normal[1] = 0.0f;
                if (!(normal[2] >= 0.0f))
                    normal[2] = 0.0f;

                apply_matrix_float(normal, lightMatrix2);
                if (!(normal[0] >= 0.0f))
                    normal[0] = 0.0f;
                if (!(normal[1] >= 0.0f))
                    normal[1] = 0.0f;
                if (!(normal[2] >= 0.0f))
                    normal[2] = 0.0f;

                // The ambient word at field_8C7E90 is packed B,G,R (bytes 0,1,2).
                normal[0] += (float)ambient[2];
                normal[1] += (float)ambient[1];
                normal[2] += (float)ambient[0];

                if (normal[0] >= 255.0f)
                    normal[0] = 255.0f;
                if (normal[1] >= 255.0f)
                    normal[1] = 255.0f;
                if (normal[2] >= 255.0f)
                    normal[2] = 255.0f;

                const int nB = (int)normal[0];
                const int nG = (int)normal[1];
                const int nR = (int)normal[2];
                const int cR = (primR * nR) / 255;
                const int cG = (primG * nG) / 255;
                const int cB = (primB * nB) / 255;

                switch (primType & 0xF00000)
                {
                case 0x100000: colors[n] = 0x80000000 | (cB << 16) | (cG << 8) | cR; break;
                case 0x300000: colors[n] = 0x40000000 | (cB << 16) | (cG << 8) | cR; break;
                case 0x400000: colors[n] = ((uint32_t)alpha << 24) | (cB << 16) | (cG << 8) | cR; break;
                default: colors[n] = 0xFF000000 | (cB << 16) | (cG << 8) | cR; break;
                }
            }
        }

        // Transform the object's vertices: refer, negate Y, apply the object
        // matrix and translation, then project into screen space. Vertices
        // behind the near plane are flattened to the origin.
        const int32_t prj = (int32_t)self->field_8C7EDC;
        const double projScale = (double)prj;
        const double halfPrj = (double)(prj / 2);
        float verts[0x800 * 3];
        for (uint32_t i = 0; i < header[2]; i++)
        {
            float a1[3];
            refer_vertex(pObject, i, a1);
            a1[1] = -a1[1];
            apply_matrix_float(a1, (const float*)(prim + 0xC));
            a1[0] += *(const float*)(prim + 0x18);
            a1[1] += *(const float*)(prim + 0x28);
            a1[2] += *(const float*)(prim + 0x38);

            // Only an unordered (NaN) depth is flattened to the origin; any
            // ordered value (including negative) is projected and left to the
            // w-clip.
            if (a1[2] != a1[2])
            {
                a1[0] = 0.0f;
                a1[1] = 0.0f;
            }
            else
            {
                a1[0] = (float)(((double)a1[0] * projScale / (double)a1[2] + (double)self->field_8C7EC4)
                                * (double)self->aspect_x);
                a1[1] = (float)(((double)a1[1] * projScale / (double)a1[2] + (double)self->field_8C7EC8)
                                * (double)self->aspect_y);
            }
            verts[3 * i + 0] = a1[0];
            verts[3 * i + 1] = a1[1];
            verts[3 * i + 2] = a1[2];
        }

        // Texture coordinate scale factors (width/height minus one) and the
        // shared U/V adjust value.
        const int texW = textures[0]->width - 1;
        const int texH = textures[0]->height - 1;
        const float texOffset = *reinterpret_cast<const float*>(&self->field_8C7020);

        // The primitive records (from ModifyPrimitive) reference vertex indices,
        // colour (normal) indices and packed U/V bytes. The six U/V bytes map
        // to TU/TV as uN / (width-1), vN / (height-1).
        struct TransPrimRecord
        {
            uint16_t vtx0;   // +0x00
            uint16_t vtx1;   // +0x02
            uint16_t vtx2;   // +0x04
            uint16_t color0; // +0x06
            uint16_t color1; // +0x08
            uint16_t color2; // +0x0A
            uint8_t u0;      // +0x0C
            uint8_t v0;      // +0x0D
            uint8_t u1;      // +0x0E
            uint8_t v1;      // +0x0F
            uint8_t u2;      // +0x10
            uint8_t v2;      // +0x11
        };

        // Expand each primitive into three D3DTLVERTEX and record section splits
        // from the split table at primitive offset 0x58.
        uint32_t chunkStarts[0x800] = { 0 };
        int splitCount = 0;
        uint32_t threshold = *(const uint16_t*)(prim + 0x58);
        const uint16_t* splitTable = (const uint16_t*)(prim + 0x58);
        D3DTLVERTEX packedVertices[0x800 * 3];
        int primIdx;
        for (primIdx = 0; primIdx < (int)header[6]; primIdx++)
        {
            if (*(const uint16_t*)(prim + 0x58) != 0 && primIdx == (int)threshold)
            {
                chunkStarts[splitCount] = primIdx;
                splitCount++;
                splitTable++;
                threshold += *splitTable;
            }

            TransPrimRecord record;
            modify_primitive(pObject, primIdx, &record);

            auto* vout = &packedVertices[3 * primIdx];
            const float* v0 = &verts[3 * record.vtx0];
            const float* v1 = &verts[3 * record.vtx1];
            const float* v2 = &verts[3 * record.vtx2];

            const double invW0 = 1.0 / (double)v0[2];
            vout[0].sx = v0[0];
            vout[0].sy = v0[1];
            vout[0].sz = (float)(1.0 - halfPrj * invW0);
            vout[0].rhw = (float)invW0;
            vout[0].color = colors[record.color0];
            vout[0].specular = 0;
            vout[0].tu = (float)((double)record.u0 / (double)texW + (double)texOffset);
            vout[0].tv = (float)((double)record.v0 / (double)texH + (double)texOffset);

            const double invW1 = 1.0 / (double)v1[2];
            vout[1].sx = v1[0];
            vout[1].sy = v1[1];
            vout[1].sz = (float)(1.0 - halfPrj * invW1);
            vout[1].rhw = (float)invW1;
            vout[1].color = colors[record.color1];
            vout[1].specular = 0;
            vout[1].tu = (float)((double)record.u1 / (double)texW + (double)texOffset);
            vout[1].tv = (float)((double)record.v1 / (double)texH + (double)texOffset);

            const double invW2 = 1.0 / (double)v2[2];
            vout[2].sx = v2[0];
            vout[2].sy = v2[1];
            vout[2].sz = (float)(1.0 - halfPrj * invW2);
            vout[2].rhw = (float)invW2;
            vout[2].color = colors[record.color2];
            vout[2].specular = 0;
            vout[2].tu = (float)((double)record.u2 / (double)texW + (double)texOffset);
            vout[2].tv = (float)((double)record.v2 / (double)texH + (double)texOffset);

            // Software lighting path: override with the flat colour.
            if ((int32_t)primType < 0 || (self->gpu_flag & GpuFlags::GPU_17) != 0)
            {
                vout[0].color = fallbackColor;
                vout[1].color = fallbackColor;
                vout[2].color = fallbackColor;
            }
        }

        chunkStarts[splitCount] = primIdx;
        splitCount++;
        if (splitCount <= 0)
            return 1;

        // Draw each section (texture) of the primitive list.
        auto* device = (IUnknown*)self->pDirectDevice2;
        for (int i = 0; i < splitCount; i++)
        {
            int vertexCount;
            LPD3DTLVERTEX vertexPtr;
            if (*(const uint16_t*)(prim + 0x58) != 0 && i != 0)
            {
                // Section i covers primitives [chunkStarts[i-1], chunkStarts[i]).
                const int start = (int)chunkStarts[i - 1];
                const int end = (int)chunkStarts[i];
                vertexCount = 3 * (end - start);
                vertexPtr = &packedVertices[3 * start];
            }
            else
            {
                vertexCount = 3 * (int)chunkStarts[0];
                vertexPtr = packedVertices;
            }

            if (vertexCount == 0)
                continue;

            gGameTable.error = gfx::device_set_current_viewport(device, (IUnknown*)self->pViewport);
            gfx::device_set_render_state(device, D3DRENDERSTATE_TEXTUREPERSPECTIVE, 1);
            gfx::device_set_render_state(device, D3DRENDERSTATE_ZWRITEENABLE, 1);
            gfx::device_set_render_state(device, D3DRENDERSTATE_SHADEMODE, D3DSHADE_GOURAUD);

            // Allow the drawing-op path only when the base texture has no overlay
            // flag and the primitive does not request texture blending.
            const uint8_t v99 = (textures[0]->var_14 & 4) == 0 && (pPrim->type & 0x10000000) == 0;

            gfx::device_set_render_state(device, D3DRENDERSTATE_COLORKEYENABLE, 0);
            gfx::device_set_render_state(device, D3DRENDERSTATE_SPECULARENABLE, 0);
            gfx::device_set_render_state(device, D3DRENDERSTATE_ZENABLE, D3DZB_TRUE);
            gfx::device_set_render_state(device, D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL);

            const uint32_t texHandle = textures[i]->surface->texture_handle;
            gfx::device_set_render_state(device, D3DRENDERSTATE_TEXTUREHANDLE, texHandle);

            const uint32_t cullMode = (~(uint32_t)primType & 0x40000000 | 0x20000000) >> 29;
            gfx::device_set_render_state(device, D3DRENDERSTATE_CULLMODE, cullMode);

            set_filtering(self, 1);

            const uint32_t mode = (uint32_t)primType & 0xF00000;
            int srcBlend = D3DBLEND_SRCALPHA;
            int dstBlend = D3DBLEND_INVSRCALPHA;
            bool useDrawOp = true;
            if (mode > 0x400000)
            {
                if (mode == 0x600000)
                {
                    // SRCALPHA / INVSRCALPHA via the draw-op path.
                }
                else if (mode == 0x700000)
                {
                    srcBlend = D3DBLEND_SRCCOLOR;
                    dstBlend = D3DBLEND_SRCCOLOR;
                }
                else
                {
                    useDrawOp = v99 != 0 && textures[0]->surface->var_2C != 0;
                }
            }
            else
            {
                if (mode == 0x400000 || mode == 0x100000)
                {
                    // SRCALPHA / INVSRCALPHA via the draw-op path.
                }
                else if (mode == 0x200000 || mode == 0x300000)
                {
                    srcBlend = D3DBLEND_SRCALPHA;
                    dstBlend = D3DBLEND_ONE;
                }
                else
                {
                    useDrawOp = v99 != 0 && textures[0]->surface->var_2C != 0;
                }
            }

            if (useDrawOp)
            {
                sub_40E9D0(self, 1, 1, srcBlend, dstBlend, texHandle, 1, 2, cullMode, 0, 4, vertexPtr, vertexCount);
            }
            else
            {
                gfx::device_set_render_state(device, D3DRENDERSTATE_ALPHABLENDENABLE, 0);
                gfx::device_draw_primitive(device, D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, vertexPtr, vertexCount, D3DDP_WAIT);
            }

            if (gGameTable.error)
            {
                out("error happened.", "Direct3D::TransObjectNgTin3_VinsNins");
                error(gGameTable.error);
                return 0;
            }
        }

        return 1;
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
                out("object didn't exist on the handle you specified.", "MarniSystem Direct3D::TransObject");
            else
                out("object you specified is invalid.", "MarniSystem Direct3D::TransObject");
            self->is_gpu_active = 0;
            return 0;
        }

        // Copy the polygon object header (obj+0x10, 9 dwords). Fields of interest:
        // [2]=vertices count [4]=normals count [6]=primitives count [8]=primitive type.
        uint32_t header[9];
        memcpy(header, (uint8_t*)pObject + 0x10, sizeof(header));
        if (header[4] > 0x400u)
        {
            out("object you specified has too normals then can't proceed. %d", "Direct3D::TransObject");
            return 0;
        }
        if (header[2] > 0x800u)
        {
            out("object you specified has too vertices then can't proceed. %d", "Direct3D::TransObject");
            return 0;
        }
        if (header[6] > 0x800u)
        {
            out("object you specified has too primitives then can't proceed. %d", "Direct3D::TransObject");
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
        out("this type is not supported.", "MarniSystem Direct3D::TransObject");
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
        auto* pTri = (PrimGradTri*)pPrim;
        auto* texture = (MarniTextureNode*)drawInfo->texture;
        auto* vertices = drawInfo->vertices;
        const float invTexW = (float)(1.0 / (double)texture->width);
        const float invTexH = (float)(1.0 / (double)texture->height);

        // Colour channels are doubled; overflow beyond 255 is captured in the
        // specular field, and the "any overflow" flag becomes specularEnable.
        int32_t r = 2 * (int32_t)pTri->r;
        int32_t g = 2 * (int32_t)pTri->g;
        int32_t b = 2 * (int32_t)pTri->b;
        int32_t rOverflow = 0;
        int32_t gOverflow = 0;
        int32_t bOverflow = 0;
        int32_t hasOverflow = 0;
        if (r >= 256)
        {
            hasOverflow = 1;
            rOverflow = r - 256;
            r = 255;
        }
        if (g >= 256)
        {
            hasOverflow = 1;
            gOverflow = g - 256;
            g = 255;
        }
        if (b >= 256)
        {
            hasOverflow = 1;
            bOverflow = b - 256;
            b = 255;
        }

        // Red byte plus the alpha byte picked by the 0x100000..0x400000 type
        // mode bits (same scheme as the sprite builder).
        uint32_t redAlpha;
        const uint32_t typeBits = (uint32_t)pTri->type & 0xF00000;
        if ((self->gpu_flag & 0x4000) != 0)
        {
            if (typeBits == 0x100000 || typeBits == 0x200000)
                redAlpha = (uint32_t)r | 0xFFFF8000;
            else if (typeBits == 0x300000)
                redAlpha = (uint32_t)r | (0x40u << 8);
            else if (typeBits == 0x400000)
                redAlpha = (uint32_t)r | ((uint32_t)pTri->a << 8);
            else
                redAlpha = (uint32_t)r | 0xFFFFFF00;
        }
        else
        {
            if (typeBits == 0x100000)
                redAlpha = (uint32_t)r | 0xFFFF8000;
            else if (typeBits == 0x300000)
                redAlpha = (uint32_t)r | (0x40u << 8);
            else if (typeBits == 0x400000)
                redAlpha = (uint32_t)r | ((uint32_t)pTri->a << 8);
            else
                redAlpha = (uint32_t)r | 0xFFFFFF00;
        }
        const uint32_t color = (uint32_t)b | (((uint32_t)g | (redAlpha << 8)) << 8);

        uint32_t specular;
        if (hasOverflow)
            specular = (uint32_t)bOverflow | (((uint32_t)gOverflow | ((uint32_t)rOverflow << 8)) << 8);
        else
            specular = 0;

        const int16_t z = pTri->z;
        const float sz = (float)(1.0 - (double)((int32_t)self->field_8C7EDC / 2) / (double)z);
        const float rhw = (float)(1.0 / (double)z);
        const double projScale = (double)(int32_t)self->field_8C7EDC;
        const float texOffset = *reinterpret_cast<const float*>(&self->field_8C7020);

        const auto make_sx = [&](int16_t x) {
            return (float)(((double)x * projScale / (double)z + (double)self->field_8C7EC4) * (double)self->aspect_x);
        };
        const auto make_sy = [&](int16_t y) {
            return (float)(((double)y * projScale / (double)z + (double)self->field_8C7EC8) * (double)self->aspect_y);
        };

        vertices[0].sx = make_sx(pTri->x0);
        vertices[0].sy = make_sy(pTri->y0);
        vertices[0].sz = sz;
        vertices[0].rhw = rhw;
        vertices[0].color = color;
        vertices[0].specular = specular;
        vertices[0].tu = (float)((double)pTri->u0 * invTexW + (double)texOffset);
        vertices[0].tv = (float)((double)pTri->v0 * invTexH + (double)texOffset);

        vertices[1].sx = make_sx(pTri->x1);
        vertices[1].sy = make_sy(pTri->y1);
        vertices[1].sz = sz;
        vertices[1].rhw = rhw;
        vertices[1].color = color;
        vertices[1].specular = specular;
        vertices[1].tu = (float)((double)pTri->u1 * invTexW + (double)texOffset);
        vertices[1].tv = (float)((double)pTri->v1 * invTexH + (double)texOffset);

        vertices[2].sx = make_sx(pTri->x2);
        vertices[2].sy = make_sy(pTri->y2);
        vertices[2].sz = sz;
        vertices[2].rhw = rhw;
        vertices[2].color = color;
        vertices[2].specular = specular;
        vertices[2].tu = (float)((double)pTri->u2 * invTexW + (double)texOffset);
        vertices[2].tv = (float)((double)pTri->v2 * invTexH + (double)texOffset);

        drawInfo->zWriteEnable = 1;
        drawInfo->shadeMode = 1;
        drawInfo->cullMode = 1;
        drawInfo->specularEnable = hasOverflow;
        drawInfo->vertexCount = 3;
        return 1;
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
        auto pQuad = (PrimGouraudQuad*)pPrim;
        auto texture = (MarniTextureNode*)drawInfo->texture;
        auto vertices = drawInfo->vertices;
        float invTexW = (float)(1.0 / texture->width);
        float invTexH = (float)(1.0 / texture->height);
        float uvOffset = *(float*)&self->field_8C7020;

        // Each B,G,R channel is doubled for the D3D colour; channels that
        // overflow 255 wrap into the specular colour (signalled to the
        // renderer through specularEnable).
        int b2 = 2 * pQuad->b;
        int g2 = 2 * pQuad->g;
        int r2 = 2 * pQuad->r;
        int specularEnable = 0;
        int bOverflow = 0;
        int gOverflow = 0;
        int rOverflow = 0;
        if (r2 >= 256)
        {
            specularEnable = 1;
            rOverflow = r2 - 256;
            r2 = 255;
        }
        if (g2 >= 256)
        {
            specularEnable = 1;
            gOverflow = g2 - 256;
            g2 = 255;
        }
        if (b2 >= 256)
        {
            specularEnable = 1;
            bOverflow = b2 - 256;
            b2 = 255;
        }
        uint32_t b = (uint32_t)b2;
        uint32_t g = (uint32_t)g2;
        uint32_t r = (uint32_t)r2;

        // The alpha byte depends on the blend-type bits (0x100000..0x400000)
        // and the gpu_flag bit 0x4000 (movie/sprite mode), mirroring the other
        // quad builders; the 0x400000 mode uses the primitive's own alpha byte.
        uint32_t v12;
        uint32_t mode = (uint32_t)pPrim->type & 0xF00000;
        if ((self->gpu_flag & 0x4000) != 0)
        {
            if (mode > 0x300000)
            {
                if (mode == 0x400000)
                    v12 = r | ((uint32_t)pQuad->a << 8);
                else
                    v12 = r | 0xFFFFFF00;
            }
            else if (mode != 0x300000)
            {
                if (mode == 0x100000 || mode == 0x200000)
                    v12 = r | 0xFFFF8000;
                else
                    v12 = r | 0xFFFFFF00;
            }
            else
            {
                v12 = r | 0x4000;
            }
        }
        else
        {
            if (mode == 0x100000)
                v12 = r | 0xFFFF8000;
            else if (mode == 0x300000)
                v12 = r | 0x4000;
            else if (mode == 0x400000)
                v12 = r | ((uint32_t)pQuad->a << 8);
            else
                v12 = r | 0xFFFFFF00;
        }
        const uint32_t color = b | ((g | (v12 << 8)) << 8);
        const uint32_t specular
            = specularEnable ? ((uint32_t)rOverflow << 16) | ((uint32_t)gOverflow << 8) | (uint32_t)bOverflow : 0;

        vertices[0].sx = (float)((double)pQuad->x0 * self->aspect_x);
        vertices[0].sy = (float)((double)pQuad->y0 * self->aspect_y);
        vertices[0].sz = 0.5f;
        vertices[0].rhw = 2.0f;
        vertices[0].color = color;
        vertices[0].specular = specular;
        vertices[0].tu = (float)((double)pQuad->u0 * invTexW + uvOffset);
        vertices[0].tv = (float)((double)pQuad->v0 * invTexH + uvOffset);

        vertices[1].sx = (float)((double)pQuad->x1 * self->aspect_x);
        vertices[1].sy = (float)((double)pQuad->y1 * self->aspect_y);
        vertices[1].sz = 0.5f;
        vertices[1].rhw = 2.0f;
        vertices[1].color = color;
        vertices[1].specular = specular;
        vertices[1].tu = (float)((double)pQuad->u1 * invTexW + uvOffset);
        vertices[1].tv = (float)((double)pQuad->v1 * invTexH + uvOffset);

        vertices[2].sx = (float)((double)pQuad->x2 * self->aspect_x);
        vertices[2].sy = (float)((double)pQuad->y2 * self->aspect_y);
        vertices[2].sz = 0.5f;
        vertices[2].rhw = 2.0f;
        vertices[2].color = color;
        vertices[2].specular = specular;
        vertices[2].tu = (float)((double)pQuad->u2 * invTexW + uvOffset);
        vertices[2].tv = (float)((double)pQuad->v2 * invTexH + uvOffset);

        vertices[3].sx = (float)((double)pQuad->x3 * self->aspect_x);
        vertices[3].sy = (float)((double)pQuad->y3 * self->aspect_y);
        vertices[3].sz = 0.5f;
        vertices[3].rhw = 2.0f;
        vertices[3].color = color;
        vertices[3].specular = specular;
        vertices[3].tu = (float)((double)pQuad->u3 * invTexW + uvOffset);
        vertices[3].tv = (float)((double)pQuad->v3 * invTexH + uvOffset);

        drawInfo->zWriteEnable = 0;
        drawInfo->shadeMode = 1;
        drawInfo->cullMode = 1;
        drawInfo->vertexCount = 4;
        drawInfo->specularEnable = specularEnable;
        return 1;
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
        auto pQuad = (PrimPolyG4*)pPrim;
        auto texture = (MarniTextureNode*)drawInfo->texture;
        auto vertices = drawInfo->vertices;
        float invTexW = (float)(1.0 / texture->width);
        float invTexH = (float)(1.0 / texture->height);
        float uvOffset = *(float*)&self->field_8C7020;

        uint32_t mode = (uint32_t)pPrim->type & 0xF00000;
        int specularEnable = 0;

        // Fold the doubled B/G/R bytes into the D3D colour, pick the alpha
        // byte from the mode bits and wrap channel overflow into the specular.
        auto build_vertex = [&](int idx,
                                int16_t x,
                                int16_t y,
                                uint8_t u,
                                uint8_t v,
                                uint8_t bByte,
                                uint8_t gByte,
                                uint8_t rByte,
                                uint8_t aByte) {
            int32_t b = 2 * (int32_t)bByte;
            int32_t g = 2 * (int32_t)gByte;
            int32_t r = 2 * (int32_t)rByte;
            int32_t bOverflow = 0;
            int32_t gOverflow = 0;
            int32_t rOverflow = 0;
            if (r >= 256)
            {
                specularEnable = 1;
                rOverflow = r - 256;
                r = 255;
            }
            if (g >= 256)
            {
                specularEnable = 1;
                gOverflow = g - 256;
                g = 255;
            }
            if (b >= 256)
            {
                specularEnable = 1;
                bOverflow = b - 256;
                b = 255;
            }

            // Red byte plus the alpha byte picked by the 0x100000..0x400000
            // type mode bits (same scheme as the other quad builders).
            uint32_t redAlpha;
            if ((self->gpu_flag & 0x4000) != 0)
            {
                if (mode > 0x300000)
                {
                    if (mode == 0x400000)
                        redAlpha = (uint32_t)r | ((uint32_t)aByte << 8);
                    else
                        redAlpha = (uint32_t)r | 0xFFFFFF00;
                }
                else if (mode != 0x300000)
                {
                    if (mode == 0x100000 || mode == 0x200000)
                        redAlpha = (uint32_t)r | 0xFFFF8000;
                    else
                        redAlpha = (uint32_t)r | 0xFFFFFF00;
                }
                else
                {
                    redAlpha = (uint32_t)r | 0x4000;
                }
            }
            else
            {
                if (mode == 0x100000)
                    redAlpha = (uint32_t)r | 0xFFFF8000;
                else if (mode == 0x300000)
                    redAlpha = (uint32_t)r | 0x4000;
                else if (mode == 0x400000)
                    redAlpha = (uint32_t)r | ((uint32_t)aByte << 8);
                else
                    redAlpha = (uint32_t)r | 0xFFFFFF00;
            }
            const uint32_t color = (uint32_t)b | (((uint32_t)g | (redAlpha << 8)) << 8);
            const uint32_t specular
                = specularEnable ? ((uint32_t)rOverflow << 16) | ((uint32_t)gOverflow << 8) | (uint32_t)bOverflow : 0;

            vertices[idx].sx = (float)((double)x * self->aspect_x);
            vertices[idx].sy = (float)((double)y * self->aspect_y);
            vertices[idx].sz = 0.5f;
            vertices[idx].rhw = 2.0f;
            vertices[idx].color = color;
            vertices[idx].specular = specular;
            vertices[idx].tu = (float)((double)u * invTexW + uvOffset);
            vertices[idx].tv = (float)((double)v * invTexH + uvOffset);
        };

        build_vertex(0, pQuad->x0, pQuad->y0, pQuad->u0, pQuad->v0, pQuad->b0, pQuad->g0, pQuad->r0, pQuad->a0);
        build_vertex(1, pQuad->x1, pQuad->y1, pQuad->u1, pQuad->v1, pQuad->b1, pQuad->g1, pQuad->r1, pQuad->a1);
        build_vertex(2, pQuad->x2, pQuad->y2, pQuad->u2, pQuad->v2, pQuad->b2, pQuad->g2, pQuad->r2, pQuad->a2);
        build_vertex(3, pQuad->x3, pQuad->y3, pQuad->u3, pQuad->v3, pQuad->b3, pQuad->g3, pQuad->r3, pQuad->a3);

        drawInfo->zWriteEnable = 0;
        drawInfo->shadeMode = 2;
        drawInfo->cullMode = 1;
        drawInfo->specularEnable = specularEnable;
        drawInfo->vertexCount = 4;
        return 1;
    }

    // 0x0040B260
    static int __stdcall sub_40B260(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        // Sprite primitive (type 0x49) -> 4 D3D TL vertices. Same shape as
        // sub_40C470 but with projected coordinates: each x/y is first scaled
        // by the projection factor (field_8C7EDC, set by trans_matrix from
        // PrimScaler::prj) over the primitive width, offset by the centre
        // (field_8C7EC4/field_8C7EC8), then scaled by the aspect ratio.
        auto* vertices = drawInfo->vertices;
        const auto* prim = (const uint8_t*)pPrim;
        const int32_t type = *(const int32_t*)(prim + 4);

        uint32_t v7;
        if ((self->gpu_flag & 0x4000) != 0)
        {
            const uint32_t mode = (uint32_t)type & 0xF00000;
            if (mode == 0x400000)
            {
                v7 = prim[30] | ((uint32_t)prim[31] << 8);
            }
            else if (mode == 0x300000)
            {
                v7 = prim[30] | (0x40u << 8);
            }
            else if (mode == 0x100000 || mode == 0x200000)
            {
                v7 = prim[30] | 0xFFFF8000;
            }
            else
            {
                v7 = *(const uint16_t*)(prim + 30) | 0xFFFFFF00;
            }
        }
        else
        {
            const uint32_t mode = (uint32_t)type & 0xF00000;
            if (mode == 0x100000)
            {
                v7 = prim[30] | 0xFFFF8000;
            }
            else if (mode == 0x300000)
            {
                v7 = prim[30] | (0x40u << 8);
            }
            else if (mode == 0x400000)
            {
                v7 = *(const uint16_t*)(prim + 30);
            }
            else
            {
                v7 = *(const uint16_t*)(prim + 30) | 0xFFFFFF00;
            }
        }
        const uint32_t color = prim[28] | (((uint32_t)prim[29] | (v7 << 8)) << 8);

        const int16_t primW = *(const int16_t*)(prim + 24);
        const int32_t prj = (int32_t)self->field_8C7EDC;
        const float sz = (float)(1.0 - (double)(prj / 2) / (double)primW);
        const float rhw = (float)(1.0 / (double)primW);

        const auto make_sx = [&](int16_t x) {
            return (float)(((double)x * (double)prj / (double)primW + (double)self->field_8C7EC4) * (double)self->aspect_x);
        };
        const auto make_sy = [&](int16_t y) {
            return (float)(((double)y * (double)prj / (double)primW + (double)self->field_8C7EC8) * (double)self->aspect_y);
        };

        vertices[0].sx = make_sx(*(const int16_t*)(prim + 8));
        vertices[0].sy = make_sy(*(const int16_t*)(prim + 10));
        vertices[0].sz = sz;
        vertices[0].rhw = rhw;
        vertices[0].color = color;

        vertices[1].sx = make_sx(*(const int16_t*)(prim + 12));
        vertices[1].sy = make_sy(*(const int16_t*)(prim + 14));
        vertices[1].sz = vertices[0].sz;
        vertices[1].rhw = vertices[0].rhw;
        vertices[1].color = color;

        vertices[2].sx = make_sx(*(const int16_t*)(prim + 16));
        vertices[2].sy = make_sy(*(const int16_t*)(prim + 18));
        vertices[2].sz = vertices[0].sz;
        vertices[2].rhw = vertices[0].rhw;
        vertices[2].color = color;

        vertices[3].sx = make_sx(*(const int16_t*)(prim + 20));
        vertices[3].sy = make_sy(*(const int16_t*)(prim + 22));
        vertices[3].sz = vertices[0].sz;
        vertices[3].rhw = vertices[0].rhw;
        vertices[3].color = color;

        drawInfo->zWriteEnable = 1;
        drawInfo->shadeMode = 1;
        drawInfo->cullMode = 1;
        drawInfo->specularEnable = 0;
        drawInfo->vertexCount = 4;
        return 1;
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
        // Projected textured 4-vertex polygon (type 0x4C): four corners, each
        // with its own int16 coordinate and UV byte, plus a shared scaling
        // divisor (offset 0x20). The corners are projected like the sprite
        // builders (scaled by the projection factor over the poly divisor,
        // offset by the centre, then aspect-scaled), the texture coordinates
        // are scaled by the texture size and offset by field_8C7020, and the
        // colour comes purely from the blend mode bits (0x100000..0x400000)
        // and the 0x4000 gpu_flag, as in sub_40D300. A near-white specular is
        // set on every vertex.
        auto* vertices = drawInfo->vertices;
        const auto* prim = (const uint8_t*)pPrim;
        auto texture = (MarniTextureNode*)drawInfo->texture;
        const int32_t type = *(const int32_t*)(prim + 4);
        float invTexW = (float)(1.0 / texture->width);
        float invTexH = (float)(1.0 / texture->height);

        D3DCOLOR color;
        const uint32_t mode = (uint32_t)type & 0xF00000;
        if ((self->gpu_flag & 0x4000) != 0)
        {
            if (mode == 0x100000 || mode == 0x200000)
                color = 0x80FFFFFF;
            else if (mode == 0x300000)
                color = 0x40FFFFFF;
            else
                color = 0xFFFFFFFF;
        }
        else
        {
            if (mode == 0x100000)
                color = 0x80FFFFFF;
            else if (mode == 0x300000)
                color = 0x40FFFFFF;
            else
                color = 0xFFFFFFFF;
        }

        const int16_t primW = *(const int16_t*)(prim + 32);
        const int32_t prj = (int32_t)self->field_8C7EDC;
        const float sz = (float)(1.0 - (double)(prj / 2) / (double)primW);
        const float rhw = (float)(1.0 / (double)primW);
        const float adjustV = *(const float*)&self->field_8C7020;

        const auto make_sx = [&](int16_t x) {
            return (float)(((double)x * (double)prj / (double)primW + (double)self->field_8C7EC4) * (double)self->aspect_x);
        };
        const auto make_sy = [&](int16_t y) {
            return (float)(((double)y * (double)prj / (double)primW + (double)self->field_8C7EC8) * (double)self->aspect_y);
        };
        const auto make_tu = [&](uint8_t u) { return (float)((double)u * (double)invTexW + (double)adjustV); };
        const auto make_tv = [&](uint8_t v) { return (float)((double)v * (double)invTexH + (double)adjustV); };

        vertices[0].sx = make_sx(*(const int16_t*)(prim + 16));
        vertices[0].sy = make_sy(*(const int16_t*)(prim + 18));
        vertices[0].sz = sz;
        vertices[0].rhw = rhw;
        vertices[0].color = color;
        vertices[0].specular = 0x00FEFEFE;
        vertices[0].tu = make_tu(prim[34]);
        vertices[0].tv = make_tv(prim[35]);

        vertices[1].sx = make_sx(*(const int16_t*)(prim + 20));
        vertices[1].sy = make_sy(*(const int16_t*)(prim + 22));
        vertices[1].sz = vertices[0].sz;
        vertices[1].rhw = vertices[0].rhw;
        vertices[1].color = color;
        vertices[1].specular = 0x00FEFEFE;
        vertices[1].tu = make_tu(prim[36]);
        vertices[1].tv = make_tv(prim[37]);

        vertices[2].sx = make_sx(*(const int16_t*)(prim + 24));
        vertices[2].sy = make_sy(*(const int16_t*)(prim + 26));
        vertices[2].sz = vertices[0].sz;
        vertices[2].rhw = vertices[0].rhw;
        vertices[2].color = color;
        vertices[2].specular = 0x00FEFEFE;
        vertices[2].tu = make_tu(prim[38]);
        vertices[2].tv = make_tv(prim[39]);

        vertices[3].sx = make_sx(*(const int16_t*)(prim + 28));
        vertices[3].sy = make_sy(*(const int16_t*)(prim + 30));
        vertices[3].sz = vertices[0].sz;
        vertices[3].rhw = vertices[0].rhw;
        vertices[3].color = color;
        vertices[3].specular = 0x00FEFEFE;
        vertices[3].tu = make_tu(prim[40]);
        vertices[3].tv = make_tv(prim[41]);

        drawInfo->zWriteEnable = 1;
        drawInfo->shadeMode = 1;
        drawInfo->cullMode = 1;
        drawInfo->specularEnable = 1;
        drawInfo->vertexCount = 4;
        return 1;
    }

    // 0x0040b8d0
    static int __stdcall sub_40B8D0(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        // Projected textured 4-vertex polygon (type 0x4D): same quad layout as
        // sub_40B560 (four int16 corners, a shared projection divisor at 0x20
        // and per-corner UV bytes) but with its own B,G,R,A colour bytes at
        // 0x2C..0x2F. Each channel is doubled and overflow past 8 bits is
        // folded into the per-vertex specular value (flagged via
        // specularEnable). The blend-mode bits select the alpha, and the
        // 0x400000 mode uses the prim's own alpha byte.
        auto* vertices = drawInfo->vertices;
        const auto* prim = (const uint8_t*)pPrim;
        auto texture = (MarniTextureNode*)drawInfo->texture;
        const int32_t type = *(const int32_t*)(prim + 4);
        float invTexW = (float)(1.0 / texture->width);
        float invTexH = (float)(1.0 / texture->height);

        uint32_t v7 = (uint32_t)prim[45] * 2;  // G channel
        uint32_t v8 = (uint32_t)prim[44] * 2;  // B channel
        uint32_t v10 = (uint32_t)prim[46] * 2; // R channel
        int hasOverflow = 0;
        uint32_t ovfR = 0, ovfG = 0, ovfB = 0;
        if (v10 >= 256)
        {
            hasOverflow = 1;
            ovfR = v10 - 256;
            v10 = 255;
        }
        if (v7 >= 256)
        {
            hasOverflow = 1;
            ovfG = v7 - 256;
            v7 = 255;
        }
        if (v8 >= 256)
        {
            hasOverflow = 1;
            ovfB = v8 - 256;
            v8 = 255;
        }

        D3DCOLOR color;
        const uint32_t mode = (uint32_t)type & 0xF00000;
        if ((self->gpu_flag & 0x4000) != 0)
        {
            if (mode == 0x100000 || mode == 0x200000)
                v10 |= 0xFFFF8000;
            else if (mode == 0x300000)
                v10 |= 0x4000;
            else if (mode != 0x400000)
                v10 |= 0xFFFFFF00;
        }
        else
        {
            if (mode == 0x100000)
                v10 |= 0xFFFF8000;
            else if (mode == 0x300000)
                v10 |= 0x4000;
            else if (mode != 0x400000)
                v10 |= 0xFFFFFF00;
        }
        if (mode == 0x400000)
            color = (D3DCOLOR)(v8 | ((v7 | ((v10 | ((uint32_t)prim[47] << 8)) << 8)) << 8));
        else
            color = (D3DCOLOR)(v8 | ((v7 | (v10 << 8)) << 8));
        D3DCOLOR specular = hasOverflow ? (D3DCOLOR)(ovfB | ((ovfG | (ovfR << 8)) << 8)) : 0;

        const int16_t primW = *(const int16_t*)(prim + 32);
        const int32_t prj = (int32_t)self->field_8C7EDC;
        const float sz = (float)(1.0 - (double)(prj / 2) / (double)primW);
        const float rhw = (float)(1.0 / (double)primW);
        const float adjustV = *(const float*)&self->field_8C7020;

        const auto make_sx = [&](int16_t x) {
            return (float)(((double)x * (double)prj / (double)primW + (double)self->field_8C7EC4) * (double)self->aspect_x);
        };
        const auto make_sy = [&](int16_t y) {
            return (float)(((double)y * (double)prj / (double)primW + (double)self->field_8C7EC8) * (double)self->aspect_y);
        };
        const auto make_tu = [&](uint8_t u) { return (float)((double)u * (double)invTexW + (double)adjustV); };
        const auto make_tv = [&](uint8_t v) { return (float)((double)v * (double)invTexH + (double)adjustV); };

        vertices[0].sx = make_sx(*(const int16_t*)(prim + 16));
        vertices[0].sy = make_sy(*(const int16_t*)(prim + 18));
        vertices[0].sz = sz;
        vertices[0].rhw = rhw;
        vertices[0].color = color;
        vertices[0].specular = specular;
        vertices[0].tu = make_tu(prim[34]);
        vertices[0].tv = make_tv(prim[35]);

        vertices[1].sx = make_sx(*(const int16_t*)(prim + 20));
        vertices[1].sy = make_sy(*(const int16_t*)(prim + 22));
        vertices[1].sz = vertices[0].sz;
        vertices[1].rhw = vertices[0].rhw;
        vertices[1].color = color;
        vertices[1].specular = specular;
        vertices[1].tu = make_tu(prim[36]);
        vertices[1].tv = make_tv(prim[37]);

        vertices[2].sx = make_sx(*(const int16_t*)(prim + 24));
        vertices[2].sy = make_sy(*(const int16_t*)(prim + 26));
        vertices[2].sz = vertices[0].sz;
        vertices[2].rhw = vertices[0].rhw;
        vertices[2].color = color;
        vertices[2].specular = specular;
        vertices[2].tu = make_tu(prim[38]);
        vertices[2].tv = make_tv(prim[39]);

        vertices[3].sx = make_sx(*(const int16_t*)(prim + 28));
        vertices[3].sy = make_sy(*(const int16_t*)(prim + 30));
        vertices[3].sz = vertices[0].sz;
        vertices[3].rhw = vertices[0].rhw;
        vertices[3].color = color;
        vertices[3].specular = specular;
        vertices[3].tu = make_tu(prim[40]);
        vertices[3].tv = make_tv(prim[41]);

        drawInfo->zWriteEnable = 1;
        drawInfo->shadeMode = 1;
        drawInfo->cullMode = 1;
        drawInfo->specularEnable = hasOverflow;
        drawInfo->vertexCount = 4;
        return 1;
    }

    // 0x0040BCF0
    static int __stdcall sub_40BCF0(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        // Flat-textured 4-vertex polygon (type 0x1004E): like the 0x1004C
        // quad builder (sub_40C100) the four int16 corners are offset from
        // the middle of the render target, but the primitive also carries its
        // own B,G,R,A colour bytes at 0x2C..0x2F. Each colour channel is
        // doubled (0..127 -> 0..254); channels that would exceed 255 are
        // clamped and the excess is folded into the per-vertex specular value,
        // with specularEnable set only when that happens. The blend-mode bits
        // select the alpha, and the 0x400000 mode uses the prim's own alpha
        // byte at 0x2F.
        auto* vertices = drawInfo->vertices;
        const auto* prim = (const uint8_t*)pPrim;
        auto texture = (MarniTextureNode*)drawInfo->texture;
        const int32_t type = *(const int32_t*)(prim + 4);
        float invTexW = (float)(1.0 / texture->width);
        float invTexH = (float)(1.0 / texture->height);

        int32_t v7 = 2 * prim[45];  // G channel
        int32_t v8 = 2 * prim[44];  // B channel
        int32_t v10 = 2 * prim[46]; // R channel
        int32_t hasOverflow = 0;
        int32_t ovfR = 0, ovfG = 0, ovfB = 0;
        if (v10 >= 256)
        {
            hasOverflow = 1;
            ovfR = v10 - 256;
            v10 = 255;
        }
        if (v7 >= 256)
        {
            hasOverflow = 1;
            ovfG = v7 - 256;
            v7 = 255;
        }
        if (v8 >= 256)
        {
            hasOverflow = 1;
            ovfB = v8 - 256;
            v8 = 255;
        }

        D3DCOLOR color;
        const uint32_t mode = (uint32_t)type & 0xF00000;
        if ((self->gpu_flag & 0x4000) != 0)
        {
            if (mode == 0x100000 || mode == 0x200000)
                v10 |= 0xFFFF8000;
            else if (mode == 0x300000)
                v10 |= 0x4000;
            else if (mode != 0x400000)
                v10 |= 0xFFFFFF00;
        }
        else
        {
            if (mode == 0x100000)
                v10 |= 0xFFFF8000;
            else if (mode == 0x300000)
                v10 |= 0x4000;
            else if (mode != 0x400000)
                v10 |= 0xFFFFFF00;
        }
        if (mode == 0x400000)
            color = (D3DCOLOR)(v8 | ((v7 | ((v10 | ((uint32_t)prim[47] << 8)) << 8)) << 8));
        else
            color = (D3DCOLOR)(v8 | ((v7 | (v10 << 8)) << 8));
        D3DCOLOR specular = hasOverflow ? (D3DCOLOR)(ovfB | ((ovfG | (ovfR << 8)) << 8)) : 0;

        const int16_t primW = *(const int16_t*)(prim + 32);
        const int32_t prj = (int32_t)self->field_8C7EDC;
        const float sz = (float)(1.0 - (double)(prj / 2) / (double)primW);
        const float rhw = (float)(1.0 / (double)primW);
        const float adjustU = self->field_8C701C;
        const float adjustV = *(const float*)&self->field_8C7020;

        // Offset each vertex coordinate from the middle of the render target.
        const int halfW = self->render_w / 2;
        const int halfH = self->render_h / 2;
        const int centreX = self->field_8C7EC4;
        const int centreY = self->field_8C7EC8;
        const int x0t = centreX + *(const int16_t*)(prim + 16) - halfW;
        const int y0t = centreY + *(const int16_t*)(prim + 18) - halfH;
        const int x1t = centreX + *(const int16_t*)(prim + 20) - halfW;
        const int y1t = centreY + *(const int16_t*)(prim + 22) - halfH;
        const int x2t = centreX + *(const int16_t*)(prim + 24) - halfW;
        const int y2t = centreY + *(const int16_t*)(prim + 26) - halfH;
        const int x3t = centreX + *(const int16_t*)(prim + 28) - halfW;
        const int y3t = centreY + *(const int16_t*)(prim + 30) - halfH;

        const auto make_sx = [&](int x) { return (float)((double)x * (double)self->aspect_x + (double)adjustU); };
        const auto make_sy = [&](int y) { return (float)((double)y * (double)self->aspect_y + (double)adjustU); };
        const auto make_tu = [&](uint8_t u) { return (float)((double)u * (double)invTexW + (double)adjustV); };
        const auto make_tv = [&](uint8_t v) { return (float)((double)v * (double)invTexH + (double)adjustV); };

        vertices[0].sx = make_sx(x0t);
        vertices[0].sy = make_sy(y0t);
        vertices[0].sz = sz;
        vertices[0].rhw = rhw;
        vertices[0].color = color;
        vertices[0].specular = specular;
        vertices[0].tu = make_tu(prim[34]);
        vertices[0].tv = make_tv(prim[35]);

        vertices[1].sx = make_sx(x1t);
        vertices[1].sy = make_sy(y1t);
        vertices[1].sz = vertices[0].sz;
        vertices[1].rhw = vertices[0].rhw;
        vertices[1].color = color;
        vertices[1].specular = specular;
        vertices[1].tu = make_tu(prim[36]);
        vertices[1].tv = make_tv(prim[37]);

        vertices[2].sx = make_sx(x2t);
        vertices[2].sy = make_sy(y2t);
        vertices[2].sz = vertices[0].sz;
        vertices[2].rhw = vertices[0].rhw;
        vertices[2].color = color;
        vertices[2].specular = specular;
        vertices[2].tu = make_tu(prim[38]);
        vertices[2].tv = make_tv(prim[39]);

        vertices[3].sx = make_sx(x3t);
        vertices[3].sy = make_sy(y3t);
        vertices[3].sz = vertices[0].sz;
        vertices[3].rhw = vertices[0].rhw;
        vertices[3].color = color;
        vertices[3].specular = specular;
        vertices[3].tu = make_tu(prim[40]);
        vertices[3].tv = make_tv(prim[41]);

        drawInfo->zWriteEnable = 1;
        drawInfo->shadeMode = 1;
        drawInfo->cullMode = 1;
        drawInfo->specularEnable = hasOverflow;
        drawInfo->vertexCount = 4;
        return 1;
    }

    // 0x0040c100
    static int __stdcall sub_40C100(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        // Flat-textured 4-vertex polygon (type 0x1004C): fills the draw op with
        // four D3D TL vertices. Unlike the other quad builders this one keeps its
        // own per-vertex z (int16 at offset 0x20), so sz/rhw are computed from it,
        // and the u/v texture coordinates carry the Adjust_u/v offsets.
        auto pQuad = (PrimPolyFT4*)pPrim;
        auto texture = (MarniTextureNode*)drawInfo->texture;
        auto vertices = drawInfo->vertices;
        float invTexW = (float)(1.0 / texture->width);
        float invTexH = (float)(1.0 / texture->height);

        D3DCOLOR color;
        auto typeBits = pPrim->type & 0xF00000;
        if ((self->gpu_flag & 0x4000) != 0)
        {
            if (typeBits == 0x400000)
                color = 0xFFFFFF;
            else if (typeBits == 0x300000)
                color = 0x40FFFFFF;
            else if (typeBits == 0x100000 || typeBits == 0x200000)
                color = 0x80FFFFFF;
            else
                color = 0xFFFFFFFF;
        }
        else
        {
            if (typeBits == 0x100000)
                color = 0x80FFFFFF;
            else if (typeBits == 0x300000)
                color = 0x40FFFFFF;
            else if (typeBits == 0x400000)
                color = 0xFFFFFF;
            else
                color = 0xFFFFFFFF;
        }

        const int halfW = self->render_w / 2;
        const int halfH = self->render_h / 2;
        const int centreX = self->field_8C7EC4;
        const int centreY = self->field_8C7EC8;

        // Offset each vertex coordinate from the middle of the render target.
        const int x0t = centreX + pQuad->x0 - halfW;
        const int y0t = centreY + pQuad->y0 - halfH;
        const int x1t = centreX + pQuad->x1 - halfW;
        const int y1t = centreY + pQuad->y1 - halfH;
        const int x2t = centreX + pQuad->x2 - halfW;
        const int y2t = centreY + pQuad->y2 - halfH;
        const int x3t = centreX + pQuad->x3 - halfW;
        const int y3t = centreY + pQuad->y3 - halfH;

        const float adjustU = self->field_8C701C;
        const float adjustV = *(const float*)&self->field_8C7020;
        const float sz = 1.0f - (float)((double)((int32_t)self->field_8C7EDC / 2) / pQuad->z);
        const float rhw = 1.0f / pQuad->z;

        const auto make_sx = [&](int x) { return (float)((double)x * (double)self->aspect_x + (double)adjustU); };
        const auto make_sy = [&](int y) { return (float)((double)y * (double)self->aspect_y + (double)adjustU); };
        const auto make_tu = [&](uint8_t u) { return (float)((double)u * (double)invTexW + (double)adjustV); };
        const auto make_tv = [&](uint8_t v) { return (float)((double)v * (double)invTexH + (double)adjustV); };

        vertices[0].sx = make_sx(x0t);
        vertices[0].sy = make_sy(y0t);
        vertices[0].sz = sz;
        vertices[0].rhw = rhw;
        vertices[0].color = color;
        vertices[0].specular = 0;
        vertices[0].tu = make_tu(pQuad->u0);
        vertices[0].tv = make_tv(pQuad->v0);

        vertices[1].sx = make_sx(x1t);
        vertices[1].sy = make_sy(y1t);
        vertices[1].sz = vertices[0].sz;
        vertices[1].rhw = vertices[0].rhw;
        vertices[1].color = color;
        vertices[1].specular = 0;
        vertices[1].tu = make_tu(pQuad->u1);
        vertices[1].tv = make_tv(pQuad->v1);

        vertices[2].sx = make_sx(x2t);
        vertices[2].sy = make_sy(y2t);
        vertices[2].sz = vertices[0].sz;
        vertices[2].rhw = vertices[0].rhw;
        vertices[2].color = color;
        vertices[2].specular = 0;
        vertices[2].tu = make_tu(pQuad->u2);
        vertices[2].tv = make_tv(pQuad->v2);

        vertices[3].sx = make_sx(x3t);
        vertices[3].sy = make_sy(y3t);
        vertices[3].sz = vertices[0].sz;
        vertices[3].rhw = vertices[0].rhw;
        vertices[3].color = color;
        vertices[3].specular = 0;
        vertices[3].tu = make_tu(pQuad->u3);
        vertices[3].tv = make_tv(pQuad->v3);

        drawInfo->zWriteEnable = 1;
        drawInfo->cullMode = 1;
        drawInfo->shadeMode = 1;
        drawInfo->vertexCount = 4;
        drawInfo->specularEnable = 1;
        return 1;
    }

    // 0x0040c470
    static int __stdcall sub_40C470(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        // Sprite primitive (type 0x10049) -> 4 D3D TL vertices.
        // The primitive carries its B,G,R,A colour bytes at offsets 28..31; the
        // alpha channel depends on the type's 0x100000..0x400000 mode bits and
        // on gpu_flag bit 0x4000 (movie/sprite mode).
        auto* vertices = drawInfo->vertices;
        const auto* prim = (const uint8_t*)pPrim;
        const int32_t type = *(const int32_t*)(prim + 4);

        uint32_t v7;
        if ((self->gpu_flag & 0x4000) != 0)
        {
            const uint32_t mode = (uint32_t)type & 0xF00000;
            if (mode == 0x400000)
            {
                v7 = prim[30] | ((uint32_t)prim[31] << 8);
            }
            else if (mode == 0x300000)
            {
                v7 = prim[30] | (0x40u << 8);
            }
            else if (mode == 0x100000 || mode == 0x200000)
            {
                v7 = prim[30] | 0xFFFF8000;
            }
            else
            {
                v7 = *(const uint16_t*)(prim + 30) | 0xFFFFFF00;
            }
        }
        else
        {
            const uint32_t mode = (uint32_t)type & 0xF00000;
            if (mode == 0x100000)
            {
                v7 = prim[30] | 0xFFFF8000;
            }
            else if (mode == 0x300000)
            {
                v7 = prim[30] | (0x40u << 8);
            }
            else if (mode == 0x400000)
            {
                v7 = *(const uint16_t*)(prim + 30);
            }
            else
            {
                v7 = *(const uint16_t*)(prim + 30) | 0xFFFFFF00;
            }
        }
        const uint32_t color = prim[28] | (((uint32_t)prim[29] | (v7 << 8)) << 8);

        const int16_t primW = *(const int16_t*)(prim + 24);
        const float sz = (float)(1.0 - (double)(self->resolutions[0].height / 2) / (double)primW);
        const float rhw = (float)(1.0 / (double)primW);

        const auto make_sx
            = [&](int16_t x) { return (float)((double)x * (double)self->aspect_x + (double)self->field_8C701C); };
        const auto make_sy
            = [&](int16_t y) { return (float)((double)y * (double)self->aspect_y + (double)self->field_8C701C); };

        vertices[0].sx = make_sx(*(const int16_t*)(prim + 8));
        vertices[0].sy = make_sy(*(const int16_t*)(prim + 10));
        vertices[0].sz = sz;
        vertices[0].rhw = rhw;
        vertices[0].color = color;

        vertices[1].sx = make_sx(*(const int16_t*)(prim + 12));
        vertices[1].sy = make_sy(*(const int16_t*)(prim + 14));
        vertices[1].sz = vertices[0].sz;
        vertices[1].rhw = vertices[0].rhw;
        vertices[1].color = color;

        vertices[2].sx = make_sx(*(const int16_t*)(prim + 16));
        vertices[2].sy = make_sy(*(const int16_t*)(prim + 18));
        vertices[2].sz = vertices[0].sz;
        vertices[2].rhw = vertices[0].rhw;
        vertices[2].color = color;

        vertices[3].sx = make_sx(*(const int16_t*)(prim + 20));
        vertices[3].sy = make_sy(*(const int16_t*)(prim + 22));
        vertices[3].sz = vertices[0].sz;
        vertices[3].rhw = vertices[0].rhw;
        vertices[3].color = color;

        drawInfo->zWriteEnable = 1;
        drawInfo->shadeMode = 1;
        drawInfo->cullMode = 1;
        drawInfo->specularEnable = 0;
        drawInfo->vertexCount = 4;
        return 1;
    }

    // 0x004C2C30 WHY DO WE JUMP HERE IN THIS FILE?!?!?
    static void draw_line(
        MarniSurface* surface, int x0, int y0, int x1, int y1, int a5, int a6, int width, int height, int color0, int color1,
        int flg)
    {
        interop::call<void, MarniSurface*, int, int, int, int, int, int, int, int, int, int, int>(
            0x004C2C30, surface, x0, y0, x1, y1, a5, a6, width, height, color0, color1, flg);
    }

    // GPU-backend path for MARNI line primitives (type 17/18: status-screen
    // ECG, item box, weapon frame). When the SDL_GPU backend drives the frame
    // the software rasterizer path is both wrong (its surface0 pixels are
    // wiped by the deferred target clear at present) and slow (each segment
    // does a full 640x480 lock/unlock pair = GPU readback + upload + two
    // SDL_WaitForGPUIdle calls). Instead, emit a solid untextured quad through
    // the wrapped D3D2 device: it lands in the GPU scene pass after the clear,
    // exactly like every other primitive.
    //
    // The software rasterizer (DrawLine with flg&1) paints a 2x2 block at the
    // doubled coordinates for every sample, so the quad spans 2 pixels:
    // [2*x0, 2*x0+2) x [2*y0, 2*y1+2) in the 640x480 render target. Lines
    // without flg&1 are 1 pixel wide at raw coordinates. flg&2 lines are
    // additive (accumulate into the target) -> ONE/ONE blend. The color is
    // 0x00RRGGBB -> D3DCOLOR 0xFFRRGGBB (the TL vertex shader swizzles the
    // B,G,R,A memory bytes to RGBA). All in-game line users are axis-aligned,
    // so the bounding-box quad is exact; a diagonal line would render as its
    // bounding rectangle (no such line exists today).
    static void draw_line_gpu(Marni* self, int x0, int y0, int x1, int y1, uint32_t color0, uint32_t color1, int type)
    {
        auto* device = (IUnknown*)self->pDirectDevice2;
        if (device == nullptr)
            return;

        const bool doubled = (type & 1) != 0;
        const bool additive = (type & 2) != 0;

        // Untextured, no depth interaction, solid overwrite (or ONE/ONE
        // additive for flg&2 lines, mirroring DrawLine's GetCurrentColor+add).
        gfx::device_set_render_state(device, D3DRENDERSTATE_TEXTUREHANDLE, 0);
        gfx::device_set_render_state(device, D3DRENDERSTATE_ZENABLE, FALSE);
        gfx::device_set_render_state(device, D3DRENDERSTATE_ZWRITEENABLE, FALSE);
        gfx::device_set_render_state(device, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
        gfx::device_set_render_state(device, D3DRENDERSTATE_ALPHABLENDENABLE, additive ? TRUE : FALSE);
        if (additive)
        {
            gfx::device_set_render_state(device, D3DRENDERSTATE_SRCBLEND, D3DBLEND_ONE);
            gfx::device_set_render_state(device, D3DRENDERSTATE_DESTBLEND, D3DBLEND_ONE);
        }

        // Endpoint blocks are inclusive: the first sample covers [2*x0, 2*x0+2),
        // the last [2*x1, 2*x1+2) (1px wide for non-doubled lines).
        const float sx0 = doubled ? 2.0f * x0 : static_cast<float>(x0);
        const float sy0 = doubled ? 2.0f * y0 : static_cast<float>(y0);
        const float sx1 = doubled ? 2.0f * x1 + 2.0f : static_cast<float>(x1 + 1);
        const float sy1 = doubled ? 2.0f * y1 + 2.0f : static_cast<float>(y1 + 1);

        const int adx = x1 > x0 ? x1 - x0 : x0 - x1;
        const int ady = y1 > y0 ? y1 - y0 : y0 - y1;
        // Pair the strip along the minor axis so the color interpolates along
        // the line (horizontal-ish lines pair by column, vertical-ish by row).
        D3DTLVERTEX v[4] = {};
        const D3DCOLOR c0 = 0xFF000000u | color0;
        const D3DCOLOR c1 = 0xFF000000u | color1;
        for (auto& vertex : v)
        {
            vertex.sz = 0.0f;
            vertex.rhw = 1.0f;
        }
        if (adx >= ady)
        {
            v[0].sx = sx0;
            v[0].sy = sy0;
            v[0].color = c0;
            v[1].sx = sx0;
            v[1].sy = sy1;
            v[1].color = c0;
            v[2].sx = sx1;
            v[2].sy = sy0;
            v[2].color = c1;
            v[3].sx = sx1;
            v[3].sy = sy1;
            v[3].color = c1;
        }
        else
        {
            v[0].sx = sx0;
            v[0].sy = sy0;
            v[0].color = c0;
            v[1].sx = sx1;
            v[1].sy = sy0;
            v[1].color = c0;
            v[2].sx = sx0;
            v[2].sy = sy1;
            v[2].color = c1;
            v[3].sx = sx1;
            v[3].sy = sy1;
            v[3].color = c1;
        }
        // tu/tv stay 0 (untextured).

        static bool sLogged = false;
        if (!sLogged)
        {
            sLogged = true;
            char cbuf[16];
            std::snprintf(cbuf, sizeof(cbuf), "%#010lx", static_cast<unsigned long>(color0));
            logging::logInfo(
                "[marni] line prim via GPU queue: ({},{})-({},{}) type={} doubled={} additive={} color0={}",
                x0,
                y0,
                x1,
                y1,
                type,
                doubled,
                additive,
                cbuf);
        }

        gfx::device_draw_primitive(device, D3DPT_TRIANGLESTRIP, D3DVT_TLVERTEX, v, 4, D3DDP_WAIT);
    }

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
        if (gfx::active_backend() == 1 && (self->gpu_flag & GpuFlags::GPU_13) == 0)
        {
            // GPU backend: emit the line as a GPU primitive instead of
            // software-rasterizing it into surface0 (see draw_line_gpu).
            draw_line_gpu(self, line->x0, line->y0, line->x1, line->y1, line->color0, line->color0, type);
            return;
        }
        if ((self->gpu_flag & GpuFlags::GPU_13) == 0)
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
        if ((self->gpu_flag & GpuFlags::GPU_13) == 0)
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
        if (gfx::active_backend() == 1 && (self->gpu_flag & GpuFlags::GPU_13) == 0)
        {
            // GPU backend: emit the line as a GPU primitive instead of
            // software-rasterizing it into surface0 (see draw_line_gpu).
            draw_line_gpu(self, line->x0, line->y0, line->x1, line->y1, line->color0, line->color1, type);
            return;
        }
        if ((self->gpu_flag & GpuFlags::GPU_13) == 0)
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
        if ((self->gpu_flag & GpuFlags::GPU_13) == 0)
        {
            surface_unlock(&self->surface0);
        }
    }

    // 0x0040C840
    static void __stdcall trans_priority_list(Marni* self, MarniOt* pOt)
    {
        if (pOt == nullptr)
            return;

        if (self->gpu_flag & GpuFlags::GPU_13)
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
                        out("passed invalid primitive header...", "Direct3D::TransPriorityList");
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
        // Textured quad primitive (type 0x1002D): float z projection, 4 int16
        // corners, 4 texcoords, plus per-prim B,G,R,A colour bytes at 0x20..0x23.
        // The colour channels are doubled and any overflow past 8 bits is folded
        // into the specular value (and flagged via specularEnable).
        auto vertices = drawInfo->vertices;
        auto texture = (MarniTextureNode*)drawInfo->texture;
        const auto* prim = (const uint8_t*)pPrim;
        const int32_t type = *(const int32_t*)(prim + 4);
        const float z = *(const float*)(prim + 16);

        float invTexW = (float)(1.0 / texture->width);
        float invTexH = (float)(1.0 / texture->height);

        uint32_t v6 = prim[33] * 2; // G channel
        uint32_t v7 = prim[32] * 2; // B channel
        uint32_t v9 = prim[34] * 2; // R channel
        int overflow = 0;
        uint32_t ovfR = 0, ovfG = 0, ovfB = 0;
        if (v9 >= 256)
        {
            overflow = 1;
            ovfR = v9 - 256;
            v9 = 255;
        }
        if (v6 >= 256)
        {
            overflow = 1;
            ovfG = v6 - 256;
            v6 = 255;
        }
        if (v7 >= 256)
        {
            overflow = 1;
            ovfB = v7 - 256;
            v7 = 255;
        }

        D3DCOLOR color;
        const uint32_t mode = (uint32_t)type & 0xF00000;
        if ((self->gpu_flag & 0x4000) != 0)
        {
            if (mode == 0x100000 || mode == 0x200000)
            {
                v9 |= 0xFFFF8000;
            }
            else if (mode == 0x300000)
            {
                v9 |= 0x4000;
            }
            else if (mode != 0x400000)
            {
                v9 |= 0xFFFFFF00;
            }
        }
        else
        {
            if (mode == 0x100000)
            {
                v9 |= 0xFFFF8000;
            }
            else if (mode == 0x300000)
            {
                v9 |= 0x4000;
            }
            else if (mode != 0x400000)
            {
                v9 |= 0xFFFFFF00;
            }
        }
        if (mode == 0x400000)
        {
            color = (D3DCOLOR)((v6 | ((v9 | ((uint32_t)prim[35] << 8)) << 8)) << 8 | v7);
        }
        else
        {
            color = (D3DCOLOR)((v6 | (v9 << 8)) << 8 | v7);
        }
        D3DCOLOR specular = overflow ? (D3DCOLOR)(ovfB | ((ovfG | (ovfR << 8)) << 8)) : 0;

        const int32_t halfW = self->render_w / 2;
        const int32_t halfH = self->render_h / 2;
        const int32_t x0 = self->field_8C7EC4 + *(const int16_t*)(prim + 20) - halfW;
        const int32_t y0 = self->field_8C7EC8 + *(const int16_t*)(prim + 22) - halfH;
        const int32_t x1 = self->field_8C7EC4 + *(const int16_t*)(prim + 24) - halfW;
        const int32_t y1 = self->field_8C7EC8 + *(const int16_t*)(prim + 26) - halfH;

        vertices[0].sx = (float)((double)x0 * self->aspect_x);
        vertices[0].sy = (float)((double)y0 * self->aspect_y);
        vertices[0].sz = 1.0f - (float)((double)((int32_t)self->field_8C7EDC / 2) / z);
        vertices[0].rhw = 1.0f / z;
        vertices[0].color = color;
        vertices[0].specular = specular;
        vertices[0].tu = (float)((double)prim[28] * invTexW);
        vertices[0].tv = (float)((double)prim[29] * invTexH);

        vertices[1].sy = vertices[0].sy;
        vertices[1].sz = vertices[0].sz;
        vertices[1].rhw = vertices[0].rhw;
        vertices[1].color = color;
        vertices[1].specular = specular;
        vertices[1].sx = (float)((double)(x1 + 1) * self->aspect_x);
        vertices[1].tv = vertices[0].tv;
        vertices[1].tu = (float)((double)(prim[30] + 1) * invTexW);

        vertices[2].sx = vertices[0].sx;
        vertices[2].sz = vertices[0].sz;
        vertices[2].rhw = vertices[0].rhw;
        vertices[2].color = color;
        vertices[2].specular = specular;
        vertices[2].tu = vertices[0].tu;
        vertices[2].sy = (float)((double)(y1 + 1) * self->aspect_y);
        vertices[2].tv = (float)((double)(prim[31] + 1) * invTexH);

        vertices[3].color = color;
        vertices[3].specular = specular;
        vertices[3].sx = vertices[1].sx;
        vertices[3].sy = vertices[2].sy;
        vertices[3].sz = vertices[0].sz;
        vertices[3].rhw = vertices[0].rhw;
        vertices[3].tu = vertices[1].tu;
        vertices[3].tv = vertices[2].tv;

        sub_40E6E0(vertices);

        drawInfo->zWriteEnable = 1;
        drawInfo->shadeMode = 1;
        drawInfo->cullMode = 1;
        drawInfo->specularEnable = overflow;
        drawInfo->vertexCount = 4;
        return 1;
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
        auto pQuad = (PrimSprQuad*)pPrim;
        auto texture = (MarniTextureNode*)drawInfo->texture;
        auto vertices = drawInfo->vertices;
        float invTexW = (float)(1.0 / texture->width);
        float invTexH = (float)(1.0 / texture->height);

        D3DCOLOR color;
        auto typeBits = pPrim->type & 0xF00000;
        if ((self->gpu_flag & 0x4000) != 0)
        {
            if (typeBits == 0x100000 || typeBits == 0x200000)
                color = 0x80FFFFFF;
            else if (typeBits == 0x300000)
                color = 0x40FFFFFF;
            else
                color = 0xFFFFFFFF;
        }
        else
        {
            if (typeBits == 0x100000)
                color = 0x80FFFFFF;
            else if (typeBits == 0x300000)
                color = 0x40FFFFFF;
            else
                color = 0xFFFFFFFF;
        }

        int v9 = self->render_h;
        int v27 = self->field_8C7EC8 + pQuad->y0 - v9 / 2;
        int v10 = self->render_w / 2;
        int v11 = self->field_8C7EC4 + pQuad->x1 - v10;
        int v12 = self->field_8C7EC8 + pQuad->y1 - v9 / 2;

        vertices[0].sx = (float)((double)(self->field_8C7EC4 + pQuad->x0 - v10) * self->aspect_x);
        vertices[0].sy = (float)((double)v27 * self->aspect_y);
        vertices[0].sz = 1.0f - (float)((double)((int32_t)self->field_8C7EDC / 2) / pQuad->z);
        vertices[0].rhw = 1.0f / pQuad->z;
        vertices[0].color = color;
        vertices[0].tu = (float)((double)pQuad->u0 * invTexW);
        vertices[0].tv = (float)((double)pQuad->v0 * invTexH);

        vertices[1].sy = vertices[0].sy;
        vertices[1].sz = vertices[0].sz;
        vertices[1].rhw = vertices[0].rhw;
        vertices[1].sx = (float)((double)(v11 + 1) * self->aspect_x);
        vertices[1].color = color;
        vertices[1].tv = vertices[0].tv;
        vertices[1].tu = (float)((double)(pQuad->u1 + 1) * invTexW);

        vertices[2].sx = vertices[0].sx;
        vertices[2].sz = vertices[0].sz;
        vertices[2].rhw = vertices[0].rhw;
        vertices[2].color = color;
        vertices[2].tu = vertices[0].tu;
        vertices[2].sy = (float)((double)(v12 + 1) * self->aspect_y);
        vertices[2].tv = (float)((double)(pQuad->v1 + 1) * invTexH);

        vertices[3].color = color;
        vertices[3].sx = vertices[1].sx;
        vertices[3].sy = vertices[2].sy;
        vertices[3].tu = vertices[1].tu;
        vertices[3].sz = vertices[0].sz;
        vertices[3].rhw = vertices[0].rhw;
        vertices[3].tv = vertices[2].tv;

        sub_40E6E0(vertices);

        drawInfo->zWriteEnable = 1;
        drawInfo->cullMode = 1;
        drawInfo->shadeMode = 1;
        drawInfo->vertexCount = 4;
        drawInfo->specularEnable = 0;
        return 1;
    }

    // 0x0040D560
    // Scaled sprite primitive (type 0x2D) -> 4 D3D TL vertices. Unlike the
    // int16 quad builders, the primitive carries a float centre (x, y) and a
    // projection depth z (floats at offsets 0x10..0x18); the quad half-size is
    // half the texture-coordinate span ((u1-u0)/2, (v1-v0)/2) projected by
    // 1/z. Colour bytes B,G,R,A are read from the tail of the primitive; the
    // B/G/R bytes are doubled, with values >= 256 clamping to 255 and the
    // excess spilled into the vertex specular field. The alpha channel depends
    // on the type's 0x100000..0x400000 mode bits and gpu_flag bit 0x4000.
    static int __stdcall sub_40D560(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        auto vertices = drawInfo->vertices;
        auto texture = (MarniTextureNode*)drawInfo->texture;
        const auto* prim = (const uint8_t*)pPrim;

        const float invTexW = (float)(1.0 / texture->width);
        const float invTexH = (float)(1.0 / texture->height);

        // Doubled colour components; values >= 256 clamp to 255 and the excess
        // is packed into the vertex specular field below.
        uint32_t b = 2 * prim[0x28];
        uint32_t g = 2 * prim[0x29];
        uint32_t r = 2 * prim[0x2A];
        bool overflow = false;
        uint32_t b_ovf = 0;
        uint32_t g_ovf = 0;
        uint32_t r_ovf = 0;
        if (r >= 256)
        {
            overflow = true;
            r_ovf = r - 256;
            r = 255;
        }
        if (g >= 256)
        {
            overflow = true;
            g_ovf = g - 256;
            g = 255;
        }
        if (b >= 256)
        {
            overflow = true;
            b_ovf = b - 256;
            b = 255;
        }

        // Fold the blend-mode bits into the red byte; alpha becomes 0x80
        // (0x100000/0x200000), 0x40 (0x300000) or 0xFF (default). For the
        // 0x400000 mode the primitive's own alpha byte is used instead.
        const uint32_t typeBits = (uint32_t)pPrim->type & 0xF00000;
        if ((self->gpu_flag & 0x4000) != 0)
        {
            if (typeBits == 0x100000 || typeBits == 0x200000)
                r |= 0xFFFF8000;
            else if (typeBits == 0x300000)
                r |= 0x40u << 8;
            else if (typeBits != 0x400000)
                r |= 0xFFFFFF00;
        }
        else
        {
            if (typeBits == 0x100000)
                r |= 0xFFFF8000;
            else if (typeBits == 0x300000)
                r |= 0x40u << 8;
            else if (typeBits != 0x400000)
                r |= 0xFFFFFF00;
        }

        uint32_t color;
        if (typeBits == 0x400000)
            color = b | ((g | ((r | ((uint32_t)prim[0x2B] << 8)) << 8)) << 8);
        else
            color = b | ((g | (r << 8)) << 8);

        uint32_t specular;
        if (overflow)
            specular = b_ovf | ((g_ovf | (r_ovf << 8)) << 8);
        else
            specular = 0;

        const float z = *(const float*)(prim + 0x10);
        if (z == 0.0f)
            return 0;

        // Half the texture-coordinate span gives the quad half-size.
        const int32_t xHalf = (prim[0x26] - prim[0x24]) / 2;
        const int32_t yHalf = (prim[0x27] - prim[0x25]) / 2;
        const float cx = *(const float*)(prim + 0x14);
        const float cy = *(const float*)(prim + 0x18);
        const float invZ = (float)(1.0 / (double)z);
        const float scale = (float)(int32_t)self->field_8C7EDC; // signed int -> float (fild in the original)
        const float offX = (float)self->field_8C7EC4;
        const float offY = (float)self->field_8C7EC8;

        const double left
            = (((double)cx - (double)xHalf) * (double)invZ * (double)scale + (double)offX) * (double)self->aspect_x;
        const double right
            = (((double)xHalf + (double)cx) * (double)invZ * (double)scale + (double)offX) * (double)self->aspect_x;
        const double top
            = (((double)cy - (double)yHalf) * (double)invZ * (double)scale + (double)offY) * (double)self->aspect_y;
        const double bottom
            = (((double)yHalf + (double)cy) * (double)invZ * (double)scale + (double)offY) * (double)self->aspect_y;
        const float sz = 1.0f - (float)((double)((int32_t)self->field_8C7EDC / 2) / (double)z);
        const float rhw = (float)(1.0 / (double)z);

        vertices[0].sx = (float)left;
        vertices[0].sy = (float)top;
        vertices[0].sz = sz;
        vertices[0].rhw = rhw;
        vertices[0].color = color;
        vertices[0].specular = specular;
        vertices[0].tu = (float)((double)prim[0x24] * (double)invTexW);
        vertices[0].tv = (float)((double)prim[0x25] * (double)invTexH);

        vertices[1].sx = (float)right;
        vertices[1].sy = vertices[0].sy;
        vertices[1].sz = sz;
        vertices[1].rhw = rhw;
        vertices[1].color = color;
        vertices[1].specular = specular;
        vertices[1].tu = (float)((double)(prim[0x26] + 1) * (double)invTexW);
        vertices[1].tv = vertices[0].tv;

        vertices[2].sx = vertices[0].sx;
        vertices[2].sy = (float)bottom;
        vertices[2].sz = sz;
        vertices[2].rhw = rhw;
        vertices[2].color = color;
        vertices[2].specular = specular;
        vertices[2].tu = vertices[0].tu;
        vertices[2].tv = (float)((double)(prim[0x27] + 1) * (double)invTexH);

        vertices[3].sx = vertices[1].sx;
        vertices[3].sy = vertices[2].sy;
        vertices[3].sz = sz;
        vertices[3].rhw = rhw;
        vertices[3].color = color;
        vertices[3].specular = specular;
        vertices[3].tu = vertices[1].tu;
        vertices[3].tv = vertices[2].tv;

        // Snap the quad to pixel centres and rearrange the texture coordinates.
        sub_40E6E0(vertices);

        drawInfo->zWriteEnable = 1;
        drawInfo->shadeMode = 1;
        drawInfo->cullMode = 1;
        drawInfo->specularEnable = overflow ? 1 : 0;
        drawInfo->vertexCount = 4;
        return 1;
    }

    // 0x40D8D0
    static int __stdcall sub_40D8D0(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        // Textured quad primitive: 2 int16 coordinate pairs (x0,y0),(x1,y1) at
        // 0x10..0x17, 4 texcoords (u0,v0,u1,v1) at 0x18..0x1B and the B,G,R,A
        // colour bytes at 0x1C..0x1F. The B/G/R channels are doubled; values
        // >= 256 clamp to 255 and the excess is packed into the vertex
        // specular field (and flagged via specularEnable). The alpha channel
        // depends on the type's 0x100000..0x400000 mode bits and gpu_flag bit
        // 0x4000 (movie/sprite mode).
        auto pSpr = (PrimSprite*)pPrim;
        auto texture = (MarniTextureNode*)drawInfo->texture;
        auto vertices = drawInfo->vertices;
        const auto* prim = (const uint8_t*)pPrim;
        const int32_t type = pPrim->type;

        float invTexW = (float)(1.0 / texture->width);
        float invTexH = (float)(1.0 / texture->height);

        uint32_t vR = prim[30] * 2; // R channel
        uint32_t vG = prim[29] * 2; // G channel
        uint32_t vB = prim[28] * 2; // B channel
        int overflow = 0;
        uint32_t ovfR = 0, ovfG = 0, ovfB = 0;
        if (vR >= 256)
        {
            overflow = 1;
            ovfR = vR - 256;
            vR = 255;
        }
        if (vG >= 256)
        {
            overflow = 1;
            ovfG = vG - 256;
            vG = 255;
        }
        if (vB >= 256)
        {
            overflow = 1;
            ovfB = vB - 256;
            vB = 255;
        }

        // Fold the blend-mode bits into the red byte; alpha becomes 0x80
        // (0x100000/0x200000), 0x40 (0x300000) or 0xFF (default). For the
        // 0x400000 mode the primitive's own alpha byte (prim[31]) is used
        // instead.
        const uint32_t typeBits = (uint32_t)type & 0xF00000;
        if ((self->gpu_flag & 0x4000) != 0)
        {
            if (typeBits == 0x100000 || typeBits == 0x200000)
                vR |= 0xFFFF8000;
            else if (typeBits == 0x300000)
                vR |= 0x4000;
            else if (typeBits != 0x400000)
                vR |= 0xFFFFFF00;
        }
        else
        {
            if (typeBits == 0x100000)
                vR |= 0xFFFF8000;
            else if (typeBits == 0x300000)
                vR |= 0x4000;
            else if (typeBits != 0x400000)
                vR |= 0xFFFFFF00;
        }

        D3DCOLOR color;
        if (typeBits == 0x400000)
            color = (D3DCOLOR)(vB | ((vG | ((vR | ((uint32_t)prim[31] << 8)) << 8)) << 8));
        else
            color = (D3DCOLOR)(vB | ((vG | (vR << 8)) << 8));
        D3DCOLOR specular = overflow ? (D3DCOLOR)(ovfB | ((ovfG | (ovfR << 8)) << 8)) : 0;

        vertices[0].sx = (float)((double)pSpr->x0 * self->aspect_x);
        vertices[0].sy = (float)((double)pSpr->y0 * self->aspect_y);
        vertices[0].sz = 0.5f;
        vertices[0].rhw = 2.0f;
        vertices[0].color = color;
        vertices[0].specular = specular;
        vertices[0].tu = (float)((double)pSpr->u0 * invTexW);
        vertices[0].tv = (float)((double)pSpr->v0 * invTexH);

        vertices[1].sy = vertices[0].sy;
        vertices[1].sz = 0.5f;
        vertices[1].rhw = 2.0f;
        vertices[1].color = color;
        vertices[1].specular = specular;
        vertices[1].sx = (float)((double)(pSpr->x1 + 1) * self->aspect_x);
        vertices[1].tv = vertices[0].tv;
        vertices[1].tu = (float)((double)(pSpr->u1 + 1) * invTexW);

        vertices[2].sx = vertices[0].sx;
        vertices[2].sz = 0.5f;
        vertices[2].rhw = 2.0f;
        vertices[2].color = color;
        vertices[2].specular = specular;
        vertices[2].tu = vertices[0].tu;
        vertices[2].sy = (float)((double)(pSpr->y1 + 1) * self->aspect_y);
        vertices[2].tv = (float)((double)(pSpr->v1 + 1) * invTexH);

        vertices[3].color = color;
        vertices[3].specular = specular;
        vertices[3].sx = vertices[1].sx;
        vertices[3].sy = vertices[2].sy;
        vertices[3].sz = 0.5f;
        vertices[3].rhw = 2.0f;
        vertices[3].tu = vertices[1].tu;
        vertices[3].tv = vertices[2].tv;

        // Snap the quad to pixel centres and rearrange the texture coordinates.
        sub_40E6E0(vertices);

        drawInfo->zWriteEnable = 0;
        drawInfo->shadeMode = 1;
        drawInfo->cullMode = 1;
        drawInfo->specularEnable = overflow;
        drawInfo->vertexCount = 4;
        return 1;
    }

    // 0x0040E6E0
    static void sub_40E6E0(D3DTLVERTEX* v)
    {
        float tu0 = v[0].tu;
        float tv0 = v[0].tv;
        float tu3 = v[3].tu;

        v[2].tv = v[3].tv;
        v[0].sx -= 0.5f;
        v[0].sy -= 0.5f;
        v[1].sx -= 0.5f;
        v[1].sy -= 0.5f;
        v[1].tu = tu3;
        v[1].tv = tv0;
        v[2].sx -= 0.5f;
        v[2].sy -= 0.5f;
        v[2].tu = tu0;
        v[3].sx -= 0.5f;
        v[3].sy -= 0.5f;
    }

    // 0x0040DBA0
    static int __stdcall MarniDrawPolyFT4(Marni* self, PrimSprite* pPrim, DrawInfo* drawInfo)
    {
        auto texture = (MarniTextureNode*)drawInfo->texture;
        auto vertices = drawInfo->vertices;
        float invTexW = (float)(1.0 / texture->width);
        float invTexH = (float)(1.0 / texture->height);

        D3DCOLOR color;
        auto typeBits = pPrim->type & 0xF00000;
        if ((self->gpu_flag & 0x4000) != 0)
        {
            if (typeBits == 0x100000 || typeBits == 0x200000)
                color = 0x80FFFFFF;
            else if (typeBits == 0x300000)
                color = 0x40FFFFFF;
            else
                color = 0xFFFFFFFF;
        }
        else
        {
            if (typeBits == 0x100000)
                color = 0x80FFFFFF;
            else if (typeBits == 0x300000)
                color = 0x40FFFFFF;
            else
                color = 0xFFFFFFFF;
        }

        vertices[0].sx = (float)((double)pPrim->x0 * self->aspect_x);
        vertices[0].sz = 0.5f;
        vertices[0].rhw = 2.0f;
        vertices[0].color = color;
        vertices[0].sy = (float)((double)pPrim->y0 * self->aspect_y);
        vertices[0].tu = (float)((double)pPrim->u0 * invTexW);
        vertices[0].tv = (float)((double)pPrim->v0 * invTexH);

        vertices[1].sy = vertices[0].sy;
        vertices[1].sz = 0.5f;
        vertices[1].rhw = 2.0f;
        vertices[1].color = color;
        vertices[1].sx = (float)((double)(pPrim->x1 + 1) * self->aspect_x);
        vertices[1].tv = vertices[0].tv;
        vertices[2].sx = vertices[0].sx;
        vertices[1].tu = (float)((double)(pPrim->u1 + 1) * invTexW);

        vertices[2].tu = vertices[0].tu;
        vertices[2].sz = 0.5f;
        vertices[2].rhw = 2.0f;
        vertices[2].color = color;
        vertices[2].sy = (float)((double)(pPrim->y1 + 1) * self->aspect_y);

        vertices[3].sx = vertices[1].sx;
        vertices[3].rhw = 2.0f;
        vertices[3].sy = vertices[2].sy;
        vertices[3].tu = vertices[1].tu;
        vertices[3].sz = 0.5f;
        vertices[3].color = color;
        vertices[2].tv = (float)((double)(pPrim->v1 + 1) * invTexH);
        vertices[3].tv = vertices[2].tv;

        // Snap the quad to pixel centers and rearrange texture coordinates.
        sub_40E6E0(vertices);

        drawInfo->zWriteEnable = 0;
        drawInfo->cullMode = 1;
        drawInfo->shadeMode = 1;
        drawInfo->vertexCount = 4;
        drawInfo->specularEnable = 0;
        return 1;
    }

    // 0x0040DD90
    static int __stdcall sub_40DD90(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        auto line = (PrimLine*)pPrim;
        auto vertices = drawInfo->vertices;

        // Select the colour based on the primitive blend type (pPrim->type bits
        // 0x100000..0x400000) and the 0x4000 gpu_flag. v8 supplies the red/alpha
        // bytes that are folded into the D3DCOLOR below.
        uint32_t v8;
        uint32_t primType = (uint32_t)pPrim->type & 0xF00000;
        if ((self->gpu_flag & 0x4000) != 0)
        {
            if (primType <= 0x300000)
            {
                if (primType != 0x300000)
                {
                    if (primType == 0x100000 || primType == 0x200000)
                        v8 = ((line->color0 >> 16) & 0xFF) | 0xFFFF8000;
                    else
                        v8 = ((line->color0 >> 16) & 0xFFFF) | 0xFFFFFF00;
                }
                else
                {
                    v8 = ((line->color0 >> 16) & 0xFF) | 0x4000;
                }
            }
            else
            {
                if (primType != 0x400000)
                    v8 = ((line->color0 >> 16) & 0xFFFF) | 0xFFFFFF00;
                else
                    v8 = (line->color0 >> 16) & 0xFFFF;
            }
        }
        else
        {
            if (primType != 0x100000)
            {
                if (primType == 0x300000)
                {
                    v8 = ((line->color0 >> 16) & 0xFF) | 0x4000;
                }
                else if (primType != 0x400000)
                {
                    v8 = ((line->color0 >> 16) & 0xFFFF) | 0xFFFFFF00;
                }
                else
                {
                    v8 = (line->color0 >> 16) & 0xFFFF;
                }
            }
            else
            {
                v8 = ((line->color0 >> 16) & 0xFF) | 0xFFFF8000;
            }
        }

        uint32_t color = (line->color0 & 0xFF) | ((((line->color0 >> 8) & 0xFF) | (v8 << 8)) << 8);

        // Build a 4-vertex quad covering (x0,y0) .. (x1+1,y1+1). specular is
        // left untouched by the original.
        vertices[0].sx = (float)((double)line->x0 * self->aspect_x);
        vertices[0].sy = (float)((double)line->y0 * self->aspect_y);
        vertices[0].sz = 0.5f;
        vertices[0].rhw = 2.0f;
        vertices[0].color = color;
        vertices[0].tu = 0.0f;
        vertices[0].tv = 0.0f;

        vertices[1].sx = (float)((double)(line->x1 + 1) * self->aspect_x);
        vertices[1].sy = vertices[0].sy;
        vertices[1].sz = 0.5f;
        vertices[1].rhw = 2.0f;
        vertices[1].color = color;
        vertices[1].tu = 0.0f;
        vertices[1].tv = 0.0f;

        vertices[2].sx = vertices[0].sx;
        vertices[2].sy = (float)((double)(line->y1 + 1) * self->aspect_y);
        vertices[2].sz = 0.5f;
        vertices[2].rhw = 2.0f;
        vertices[2].color = color;
        vertices[2].tu = 0.0f;
        vertices[2].tv = 0.0f;

        vertices[3].sx = vertices[1].sx;
        vertices[3].sy = vertices[2].sy;
        vertices[3].sz = 0.5f;
        vertices[3].rhw = 2.0f;
        vertices[3].color = color;
        vertices[3].tu = 0.0f;
        vertices[3].tv = 0.0f;

        // 0x0040E6E0 (inlined): snap the quad to pixel centres and propagate
        // the (zeroed) tu/tv values across the quad.
        vertices[0].sx -= 0.5f;
        vertices[0].sy -= 0.5f;
        vertices[1].sx -= 0.5f;
        vertices[1].sy -= 0.5f;
        vertices[2].sx -= 0.5f;
        vertices[2].sy -= 0.5f;
        vertices[3].sx -= 0.5f;
        vertices[3].sy -= 0.5f;
        vertices[1].tu = vertices[3].tv;
        vertices[1].tv = vertices[0].tv;
        vertices[2].tu = vertices[0].tu;
        vertices[2].tv = vertices[3].tv;

        drawInfo->zWriteEnable = 0;
        drawInfo->shadeMode = 1;
        drawInfo->cullMode = 1;
        drawInfo->specularEnable = 0;
        drawInfo->vertexCount = 4;
        return 1;
    }

    // 0x0040DF60
    static int __stdcall sub_40DF60(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return 1;
    }

    // 0x0040DF70
    static int __stdcall trans_spr_poly(Marni* self, MarniOt* pOt, PrimSprite* pPrim)
    {
        int v5 = 0;
        int texture = 0;
        char* v7 = nullptr;
        uint32_t v8 = 0;
        int v19 = 0;
        int v20 = 0;
        int v11 = 0;
        int v12 = 0;
        MarniTexture* v15 = 0;
        int v16 = 0;
        int v17 = 0;
        int v18 = 0;
        MarniRes* v21 = 0;
        int v22 = 0;
        int v27 = 0;
        int v28 = 0;
        int v29 = 0;
        int v30 = 0;
        int v31 = 0;
        int v39 = 0;
        double v42 = 0;
        double v44 = 0;
        int textureHandle = 0;
        DrawInfo drawInfo;
        int dstBlend = 0;
        int srcBlend = 0;
        D3DTLVERTEX vertices[4]{};
        drawInfo.vertices = vertices;
        auto v3 = 0;
        auto v41 = 1;
        auto v40 = 0;
        MarniTexture* tex;
        if ((pPrim->type & 4) == 0 && (self->gpu_flag & GpuFlags::GPU_19))
        {
            textureHandle = 0;
            goto LABEL_4;
        }
        if ((pPrim->type & 4) != 0)
            texture = pPrim->texture;
        else
            texture = gGameTable.dword_6449BC;
        if (!texture)
            goto LABEL_26;
        tex = &self->textures[texture];
        if (!tex->var_00)
        {
            out();
            self->is_gpu_active = 0;
            return 0;
        }
        if ((tex->var_00 & 0x4000) != 0)
        {
            v3 = 1;
        }
        else
        {
            auto pTexture = search_texture_object_0_from_1_in_condition(self, texture, pPrim->var_0C);
            if (pTexture == nullptr)
                goto LABEL_26;
            v11 = pTexture->var_14;
            if ((v11 & 0x1000) != 0)
                goto LABEL_26;
            if ((v11 & 4) != 0)
                v41 = 0;
            v12 = pTexture->surface->texture_handle;
            drawInfo.texture = pTexture;
            textureHandle = v12;
        }
        if (tex->surface.var_2C != 0)
            v40 = 1;

        if (v3)
        {
            if ((self->pMovie->flag & 2) == 0)
                return 1;
            if ((pPrim->type & 0xFFFFF) != 0x24)
                goto LABEL_26;
            v39 = 0;
            if ((self->gpu_flag & GpuFlags::GPU_11) != 0)
                v39 = 0x8000;
            v15 = &self->textures[pPrim->texture];
            if ((v15->var_00 & 4) == 0 && (pPrim->type & 0x10000000) == 0)
            {
                v39 = v39 | 1;
            }
            v17 = pPrim->type & 0x700000;
            if (v17 > 0x300000)
            {
                if (v17 == 0x400000)
                {
                    v19 = v39 | 0x40;
                    v39 = v19;
                }
            }
            else
            {
                switch (v17)
                {
                case 0x300000:
                    v19 = v39 | 0x30;
                    v39 = v19;
                    break;
                case 0x100000:
                    v20 = v39 | 0x10;
                    v39 |= 0x10;
                    if ((self->gpu_flag & GpuFlags::GPU_18) != 0)
                    {
                        v19 = v20 | 0x100;
                        v39 = v19;
                    }
                    break;
                case 0x200000:
                    v18 = v39 | 0x20;
                    v39 |= 0x20;
                    if ((self->gpu_flag & GpuFlags::GPU_18) != 0)
                    {
                        v19 = v18 | 0x100;
                        v39 = v19;
                    }
                    break;
                }
            }
            v21 = &self->resolutions[4 * self->modes];
            v44 = 1.0;
            v42 = 1.0;
            if (v21->fullscreen != 2)
            {
                v44 = (double)(v21->width / self->xsize) * self->aspect_x;
                v42 = (double)(v21->height / self->xsize) * self->aspect_y;
            }

            surface_lock(&v15->surface, 0, 0);
            surface_lock(&self->surface2, 0, 0);
            v22 = v39 | 0x50;
            auto v23 = (double)(pPrim->y1 + 1) * v42;
            auto v24 = (double)(pPrim->x1 + 1) * textureHandle;
            auto v25 = (double)(pPrim->y0) * v42;
            auto v26 = (double)(pPrim->x0) * textureHandle;
            tex_spr(
                &self->surface2,
                &v15->surface,
                self->window_rect.left + (int32_t)v26,
                self->window_rect.top + (int32_t)v25,
                self->window_rect.left + (int32_t)v24,
                self->window_rect.top + (int32_t)v23,
                pPrim->u0,
                pPrim->v0,
                pPrim->u1 + 1,
                pPrim->v1 + 1,
                self->window_rect.left,
                self->window_rect.top,
                self->window_rect.right,
                self->window_rect.bottom,
                0xFF808080,
                v22);
            surface_unlock(&self->surface2);
            surface_unlock(&v15->surface);
            return 1;
        }

    LABEL_4:
        v5 = pPrim->type & 0xFFFFF;
        if (v5 > 0x49)
        {
            if (v5 > 0x1002D)
            {
                auto v30 = v5 - 65609;
                if (v30 == 0)
                {
                    v27 = sub_40C470(self, pPrim, &drawInfo);
                    goto LABEL_74;
                }
                auto v31 = v30 - 3;
                if (v31 == 0)
                {
                    v27 = sub_40C100(self, pPrim, &drawInfo);
                    goto LABEL_74;
                }
                if (v31 == 1)
                {
                    v27 = sub_40BCF0(self, pPrim, &drawInfo);
                    goto LABEL_74;
                }
            }
            else
            {
                if (v5 == 65581)
                {
                    v27 = sub_40CFD0(self, pPrim, &drawInfo);
                    goto LABEL_74;
                }
                auto v28 = v5 - 76;
                if (v28 == 0)
                {
                    v27 = sub_40B560(self, pPrim, &drawInfo);
                    goto LABEL_74;
                }
                auto v29 = v28 - 1;
                if (v29 == 0)
                {
                    v27 = sub_40B8D0(self, pPrim, &drawInfo);
                    goto LABEL_74;
                }
                if (v29 == 65503)
                {
                    v27 = sub_40D300(self, pPrim, &drawInfo);
                    goto LABEL_74;
                }
            }
        LABEL_26:
            out();
            return 0;
        }
        if (v5 != 73)
        {
            switch (v5)
            {
            case 33u: v27 = sub_40DD90(self, pPrim, &drawInfo); goto LABEL_74;
            case 36u: v27 = MarniDrawPolyFT4(self, pPrim, &drawInfo); goto LABEL_74;
            case 37u: v27 = sub_40D8D0(self, pPrim, &drawInfo); goto LABEL_74;
            case 44u: v27 = sub_40DF60(self, pPrim, &drawInfo); goto LABEL_74;
            case 45u: v27 = sub_40D560(self, pPrim, &drawInfo); goto LABEL_74;
            case 61u: v27 = sub_40A4B0(self, pPrim, &drawInfo); goto LABEL_74;
            case 69u: v27 = sub_40A830(self, pPrim, &drawInfo); goto LABEL_74;
            case 70u: v27 = sub_40AB60(self, pPrim, &drawInfo); goto LABEL_74;
            default: goto LABEL_26;
            }
        }
        v27 = sub_40B260(self, pPrim, &drawInfo);

    LABEL_74:
        if (!v27)
            return 1;

        auto* device = (IUnknown*)self->pDirectDevice2;
        gfx::device_set_current_viewport(device, (IUnknown*)self->pViewport);
        gfx::device_set_render_state(device, D3DRENDERSTATE_TEXTUREHANDLE, textureHandle);
        gfx::device_set_render_state(device, D3DRENDERSTATE_ZWRITEENABLE, drawInfo.zWriteEnable);
        gfx::device_set_render_state(device, D3DRENDERSTATE_ZENABLE, D3DZB_TRUE);
        gfx::device_set_render_state(device, D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL);
        auto v32 = v41 && (pPrim->type & 0x10000000) == 0;
        gfx::device_set_render_state(device, D3DRENDERSTATE_COLORKEYENABLE, FALSE);
        gfx::device_set_render_state(device, D3DRENDERSTATE_SHADEMODE, drawInfo.shadeMode);
        gfx::device_set_render_state(device, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
        gfx::device_set_render_state(device, D3DRENDERSTATE_SPECULARENABLE, drawInfo.specularEnable);
        auto v33 = pPrim->type & 0xF00000;
        if (v33 > 0x400000)
        {
            if (v33 == 0x600000)
            {
                srcBlend = D3DBLEND_SRCALPHA;
                dstBlend = D3DBLEND_INVSRCALPHA;
                goto LABEL_91;
            }
            if (v33 == 0x700000)
            {
                srcBlend = D3DBLEND_SRCCOLOR;
                dstBlend = D3DBLEND_SRCCOLOR;
                goto LABEL_91;
            }
        }
        else
        {
            if (v33 == 0x400000 || v33 == 0x100000)
            {
                srcBlend = D3DBLEND_SRCALPHA;
                dstBlend = D3DBLEND_INVSRCALPHA;
                goto LABEL_91;
            }
            if (v33 == 0x200000 || v33 == 0x300000)
            {
                srcBlend = D3DBLEND_SRCALPHA;
                dstBlend = D3DBLEND_ONE;
            LABEL_91:
                tessellate_insert_draw_op(
                    self,
                    0,
                    1,
                    srcBlend,
                    dstBlend,
                    textureHandle,
                    drawInfo.zWriteEnable != 0,
                    drawInfo.shadeMode,
                    drawInfo.cullMode,
                    drawInfo.specularEnable != 0,
                    D3DCMP_LESSEQUAL,
                    vertices,
                    drawInfo.vertexCount);
                goto LABEL_94;
            }
        }
        if (v32 && v40)
        {
            srcBlend = D3DBLEND_SRCALPHA;
            dstBlend = D3DBLEND_INVSRCALPHA;
            goto LABEL_91;
        }
        gfx::device_set_render_state(device, D3DRENDERSTATE_ALPHABLENDENABLE, FALSE);
        gfx::device_set_render_state(device, D3DRENDERSTATE_TEXTUREMAPBLEND, D3DTBLEND_MODULATEALPHA);
        set_filtering(self, 0);
        gfx::device_draw_primitive(device, D3DPT_TRIANGLESTRIP, D3DVT_TLVERTEX, vertices, drawInfo.vertexCount, D3DDP_WAIT);
    LABEL_94:
        if (!gGameTable.error)
            return 1;
        out("", "");
        return 0;
    }

    // 0x0040e6e0

    // 0x0040E770
    static void set_filtering(Marni* self, uint8_t a2)
    {
        // NOTE: states 17/18 are the retired D3DRENDERSTATE_TEXTUREMAG/TEXTUREMIN
        // renderstates; the values are D3DTEXTUREFILTER modes (see d3dtypes.h).
        auto* device = (IUnknown*)self->pDirectDevice2;
        gfx::device_set_render_state(device, D3DRENDERSTATE_TEXTUREADDRESS, D3DTADDRESS_WRAP);
        if (a2 && gGameTable.marni_config.bilinear)
        {
            gfx::device_set_render_state(device, D3DRENDERSTATE_TEXTUREMAG, D3DFILTER_LINEAR);
            gfx::device_set_render_state(device, D3DRENDERSTATE_TEXTUREMIN, D3DFILTER_LINEARMIPLINEAR);
        }
        else
        {
            gfx::device_set_render_state(device, D3DRENDERSTATE_TEXTUREMAG, D3DFILTER_NEAREST);
            gfx::device_set_render_state(device, D3DRENDERSTATE_TEXTUREMIN, D3DFILTER_NEAREST);
        }
        if (a2 && gGameTable.marni_config.perswrap)
            gfx::device_set_render_state(device, D3DRENDERSTATE_TEXTUREPERSPECTIVE, 1);
        else
            gfx::device_set_render_state(device, D3DRENDERSTATE_TEXTUREPERSPECTIVE, 0);
    }

    // 0x0040E800
    static void __stdcall sub_40E800(Marni* self, uint8_t a2)
    {
        self->field_700C = a2 == 0 ? -1 : 0;
        self->num_draw_ops = a2 == 0 ? -1 : 0;
    }

    // 0x0040E820
    static int __stdcall insert_draw_op(
        Marni* self, int filter, int a3, int srcBlend, int dstBlend, int textureHandle, int zWriteEnable, int shadeMode,
        int cullMode, int specularEnable, int zFunc, LPD3DTLVERTEX* vertices)
    {
        // Check if any drawing op slots left
        if (self->num_draw_ops >= 0x10000)
            return self->num_draw_ops;

        // Create new drawing op
        auto newOp = &self->draw_ops[std::max(0, self->num_draw_ops)];
        newOp->average_z = 0.0;
        for (auto i = 0; i < 3; i++)
        {
            std::memcpy(&newOp->vertices[i], vertices[i], sizeof(D3DTLVERTEX));
            newOp->average_z += vertices[i]->sz;
        }
        newOp->average_z /= 3;
        newOp->filter = filter;
        newOp->var_68 = a3;
        newOp->src_blend = srcBlend;
        newOp->dst_blend = dstBlend;
        newOp->texture_handle = textureHandle;
        newOp->z_write_enable = zWriteEnable;
        newOp->shade_mode = shadeMode;
        newOp->cull_mode = cullMode;
        newOp->specular_enable = specularEnable;
        newOp->z_func = self->num_draw_ops >= 0 ? zFunc : D3DCMP_ALWAYS;

        // Insert op into draw op list based on sort criteria
        auto num_draw_ops = self->num_draw_ops;
        auto opIndex = 0;
        if (num_draw_ops > 1)
        {
            auto end = num_draw_ops;
            do
            {
                auto midpoint = opIndex + (end - opIndex) / 2;
                auto midpointOp = self->draw_op_ptrs[midpoint];
                if (midpointOp->average_z >= newOp->average_z)
                {
                    opIndex += (end - opIndex) / 2;
                    if (newOp->average_z < midpointOp->average_z)
                        continue;
                }
                end = midpoint;
            } while (opIndex < end - 1);
        }
        while (opIndex < num_draw_ops)
        {
            if (newOp->average_z > self->draw_op_ptrs[opIndex]->average_z)
                break;
            opIndex++;
        }
        if (num_draw_ops != 0 && opIndex < num_draw_ops)
        {
            std::memmove(
                &self->draw_op_ptrs[opIndex + 1],
                &self->draw_op_ptrs[opIndex],
                (num_draw_ops - opIndex) * sizeof(MarniDrawOp*));
        }
        self->draw_op_ptrs[opIndex] = newOp;

        // Increment number of ops for batch draw, or immediately run the draw op
        if (self->num_draw_ops >= 0)
        {
            self->num_draw_ops++;
            return self->num_draw_ops;
        }
        else
        {
            return do_draw_op(self, 0);
        }
    }

    // 0x0040e9d0

    // 0x0040EA60
    static void __stdcall tessellate_insert_draw_op(
        Marni* self, int filter, int a1, int srcBlend, int dstBlend, int textureHandle, int zWriteEnable, int shadeMode,
        int cullMode, int specularEnable, int zFunc, LPD3DTLVERTEX vertices, int vertexCount)
    {
        if (vertexCount > 2)
        {
            auto v14 = &vertices[2];
            for (auto i = 0; i < vertexCount - 2; i++)
            {
                LPD3DTLVERTEX newVertices[3];
                newVertices[0] = &v14[-2];
                newVertices[1] = &v14[-1];
                newVertices[2] = v14;
                insert_draw_op(
                    self,
                    filter,
                    a1,
                    srcBlend,
                    dstBlend,
                    textureHandle,
                    zWriteEnable,
                    shadeMode,
                    cullMode,
                    specularEnable,
                    zFunc,
                    newVertices);
                v14++;
            }
        }
    }

    // 0x0040EAF0
    static int __stdcall do_draw_op(Marni* self, int index)
    {
        auto op = self->draw_op_ptrs[index];
        auto* device = (IUnknown*)self->pDirectDevice2;
        set_filtering(self, op->filter);
        gfx::device_set_current_viewport(device, (IUnknown*)self->pViewport);
        gfx::device_set_render_state(device, D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
        gfx::device_set_render_state(device, D3DRENDERSTATE_ZENABLE, D3DZB_TRUE);
        gfx::device_set_render_state(device, D3DRENDERSTATE_COLORKEYENABLE, FALSE);
        gfx::device_set_render_state(device, D3DRENDERSTATE_TEXTUREMAPBLEND, D3DTBLEND_MODULATEALPHA);
        gfx::device_set_render_state(device, D3DRENDERSTATE_TEXTUREHANDLE, op->texture_handle);
        gfx::device_set_render_state(device, D3DRENDERSTATE_ZWRITEENABLE, FALSE);
        gfx::device_set_render_state(device, D3DRENDERSTATE_ZFUNC, op->z_func);
        gfx::device_set_render_state(device, D3DRENDERSTATE_SHADEMODE, op->shade_mode);
        gfx::device_set_render_state(device, D3DRENDERSTATE_CULLMODE, op->cull_mode);
        gfx::device_set_render_state(device, D3DRENDERSTATE_SPECULARENABLE, FALSE);
        gfx::device_set_render_state(device, D3DRENDERSTATE_SRCBLEND, op->src_blend);
        gfx::device_set_render_state(device, D3DRENDERSTATE_DESTBLEND, op->dst_blend);
        return gfx::device_draw_primitive(device, D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, op->vertices, 3, D3DDP_WAIT);
    }

    // 0x0040EC10
    static void __stdcall sub_40EC10(Marni* self)
    {
        auto* device = (IUnknown*)self->pDirectDevice2;
        gfx::device_set_render_state(device, D3DRENDERSTATE_ZWRITEENABLE, FALSE);
        for (auto i = 0; i < self->num_draw_ops; i++)
        {
            do_draw_op(self, i);
        }

        for (auto i = 0; i < self->field_700C; i++)
        {
            auto& record = self->field_5010[i];
            if (record.gourad != nullptr)
                draw_line_gourad(self, (PrimLine2*)record.flat);
            else
                draw_line_flat(self, (PrimLine2*)record.flat);
        }
    }

    // 0x0040ec90

    // 0x0040ECA0
    static int __stdcall surfacex_create_texture_object(MarniSurfaceX* self)
    {
        if (!self->bOpen)
        {
            out("", "Direct3DSurface::CreateTextureObject");
            return 0;
        }

        auto pDDtexture = (LPDIRECT3DTEXTURE)self->pDDtexture;
        if (pDDtexture != NULL)
        {
            pDDtexture->Release();
            self->pDDtexture = NULL;
        }

        auto pDDsurface = (IUnknown*)self->pDDsurface;
        auto hr = gfx::surface_query_texture_interface(pDDsurface, (LPVOID*)&pDDtexture);
        if (FAILED(hr))
        {
            out("", "Direct3DSurface::CreateTextureObject");
            error(hr);
            surface_release(self);
            return 0;
        }

        self->pDDtexture = pDDtexture;
        return 1;
    }

    // 0x0040ED20
    static int __stdcall surfacex_get_texture_handle(MarniSurfaceX* self, LPDIRECT3DDEVICE2 device)
    {
        // Direct3DSurface::GetTextureHandle — asks the attached D3D texture for its
        // handle, which is what the draw pipeline uses to reference the texture.
        if (self->bOpen)
        {
            auto pDDtexture = (LPDIRECT3DTEXTURE2)self->pDDtexture;
            if (pDDtexture != nullptr)
            {
                if (SUCCEEDED(pDDtexture->GetHandle(device, (LPD3DTEXTUREHANDLE)&self->texture_handle)))
                    return 1;
                out("Could not load texture.", "MarniSystem Direct3DSurface::Direct3DSurface");
            }
            else
            {
                out("", "Direct3DSurface::GetTextureHandle");
            }
        }
        else
        {
            out("", "Direct3DSurface::GetTextureHandle");
        }
        surfacex_vrelease(self);
        return 0;
    }

    // 0x0040ED90
    static MarniSurfaceX* __stdcall surfacex_ctor(MarniSurfaceX* self)
    {
        // MarniSurfaceX::Ctor — MarniSurfaceY::Ctor then override the vtbl with
        // MarniSurfaceX::vTbl (0x517358) and clear the D3D texture fields.
        surfacey_ctor((MarniSurfaceY*)self);
        self->vtbl = (MarniSurfaceVTBL*)0x00517358;
        self->texture_handle = 0;
        self->pDDtexture = nullptr;
        return self;
    }

    // 0x0040EDB0
    static void __stdcall surfacex_dtor(MarniSurfaceX* self)
    {
        // MarniSurfaceX::Dtor — restore the X vtbl, release the D3D texture plus
        // the base surface, then run the MarniSurfaceY dtor chain.
        self->vtbl = (MarniSurfaceVTBL*)0x00517358;
        surfacex_vrelease(self);
        surfacey_dtor((MarniSurface2*)self);
    }

    // 0x0040EE00

    // 0x0040EE30
    static int __stdcall surfacex_load(MarniSurfaceX* self, MarniSurfaceX* pSrc)
    {
        // IDirect3DTexture2::Load(dst, src) copies the source texture's pixels
        // into this texture's backing surface. The COM front-end wraps the
        // IDirect3DTexture2 object and hooks Load (vtable slot 5, offset 0x14)
        // so the GPU backend replays the copy on its own textures.
        int result = 0;
        auto pDDtexture = (LPDIRECT3DTEXTURE2)self->pDDtexture;
        if (pDDtexture)
        {
            result = pDDtexture->Load((LPDIRECT3DTEXTURE2)pSrc->pDDtexture);
            self->var_2D = pSrc->var_2D;
            self->var_2C = pSrc->var_2C;
        }
        return result;
    }

    // 0x0040EE60
    static int invalidate_window(HWND hWnd, int width, int height, int /*fullscreen*/, LPRECT lpResRect)
    {
        // SDL3 owns the Win32 window and ignores raw SetWindowPos size changes
        // (the window is created non-resizable), so resize through SDL instead.
        system::window::set_window_size(width, height);
        // The original binary dropped the window to HWND_BOTTOM here to keep it
        // behind the desktop, but on a modern OS that just hides the game behind
        // other windows on every F8 press, so we skip the Z-order change.

        if (lpResRect)
        {
            POINT p0 = { 0, 0 };
            POINT p1 = { width, height };
            ClientToScreen(hWnd, &p0);
            ClientToScreen(hWnd, &p1);
            SetRect(lpResRect, p0.x, p0.y, p1.x, p1.y);
        }

        InvalidateRect(hWnd, nullptr, TRUE);
        return 1;
    }

    // 0x0040EF50
    static int ddrawdesc2surfdesc(LPDDSURFACEDESC pDDesc, MarniSurfaceDesc* pDesc)
    {
        if ((pDDesc->ddpfPixelFormat.dwFlags & (DDPF_PALETTEINDEXED8 | DDPF_PALETTEINDEXED4)) != 0)
        {
            pDesc->r_shift = 0;
            pDesc->g_mask = 0;
            pDesc->b_bitcnt = 0;
            return 1;
        }

        auto extract_shift_count = [](DWORD mask, uint8_t& shift, uint8_t& count, uint8_t& outMask) {
            shift = 0;
            if (mask)
            {
                while (!(mask & 1) && shift < 32)
                {
                    mask >>= 1;
                    ++shift;
                }
                count = 0;
                while ((mask & 1) && count < 32)
                {
                    mask >>= 1;
                    ++count;
                }
            }
            else
            {
                count = 0;
            }
            outMask = (uint8_t)((1 << count) - 1);
        };

        extract_shift_count(pDDesc->ddpfPixelFormat.dwRBitMask, pDesc->r_shift, pDesc->r_bitcnt, pDesc->r_mask);
        extract_shift_count(pDDesc->ddpfPixelFormat.dwGBitMask, pDesc->g_shift, pDesc->g_bitcnt, pDesc->g_mask);
        extract_shift_count(pDDesc->ddpfPixelFormat.dwBBitMask, pDesc->b_shift, pDesc->b_bitcnt, pDesc->b_mask);

        DWORD aMask = pDDesc->ddpfPixelFormat.dwRGBAlphaBitMask;
        pDesc->a_shift = 0;
        if (aMask)
        {
            while (!(aMask & 1) && pDesc->a_shift < 32)
            {
                aMask >>= 1;
                ++pDesc->a_shift;
            }
            pDesc->a_bitcnt = 0;
            do
            {
                if (!(aMask & 1))
                    break;
                aMask >>= 1;
                ++pDesc->a_bitcnt;
            } while (pDesc->a_bitcnt < 32);
            pDesc->a_mask = (uint8_t)((1 << pDesc->a_bitcnt) - 1);
        }
        else
        {
            pDesc->a_bitcnt = 0;
            pDesc->a_mask = (uint8_t)((1 << pDesc->a_bitcnt) - 1);
        }

        return 1;
    }

    // 0x0040F090
    static HRESULT CALLBACK enum_display_mode_callback(LPDDSURFACEDESC pDesc, LPVOID pContext)
    {
        auto max = ((LPDWORD)pContext)[0];
        auto index = ((LPDWORD)pContext)[2];
        if (index >= max)
            return DDENUMRET_CANCEL;

        auto resolutions = (MarniRes*)((LPDWORD)pContext)[1];
        auto& r = resolutions[index];
        r.width = pDesc->dwWidth;
        r.height = pDesc->dwHeight;
        r.depth = pDesc->ddpfPixelFormat.dwRGBBitCount;

        ((LPDWORD)pContext)[2] = index + 1;
        return DDENUMRET_OK;
    }

    // 0x0040F0F0
    static int enum_display_mode(LPDIRECTDRAW2 lpDD2, MarniRes* res, size_t max, size_t* count)
    {
        // return interop::thiscall<int, LPDIRECTDRAW2, MarniRes*, int, int*>(0x0040F0F0, lpDD2, res, max, cntFound);

        DWORD ctx[3] = { max, (DWORD)res, 0 };
        auto result = lpDD2->EnumDisplayModes(0, NULL, ctx, enum_display_mode_callback);
        if (FAILED(result))
        {
            out("failed to retrieve a display modes", "MarniSystem Direct3D::EnumDisplayMode");
            *count = 0;
            error(result);
        }
        *count = ctx[2];
        return 0;
    }

    // 0x0040F170
    static HRESULT get_surface_desc(LPDDSURFACEDESC lpDDSurfaceDesc, LPDIRECTDRAWSURFACE lpDDSurface)
    {
        memset(lpDDSurfaceDesc, 0, sizeof(*lpDDSurfaceDesc));
        lpDDSurfaceDesc->dwSize = sizeof(DDSURFACEDESC);
        return gfx::surface_get_surface_desc((IUnknown*)lpDDSurface, lpDDSurfaceDesc);
    }

    // 0x0040F1A0
    static int create_ddraw(bool bEnumDevices, LPDIRECTDRAW* lplpDD, LPDWORD lpIsDefault)
    {
        LPDIRECTDRAW lpDD = NULL;
        if (bEnumDevices)
        {
            auto hr = DirectDrawEnumerateA(ddrawEnumCallback, (LPVOID)&lpDD);
            if (FAILED(hr))
            {
                out("Direct3DEnumerate", "MarniSystem Direct3D::MDDCreateDirect3D");
                return hr;
            }
        }
        if (lpDD == NULL)
        {
            auto hr = DirectDrawCreate(NULL, &lpDD, NULL);
            if (FAILED(hr))
            {
                out("Direct3D", "MarniSystem Direct3D::MDDCreateDirect3D");
                return hr;
            }
            *lpIsDefault = 1;
        }
        else
        {
            *lpIsDefault = 0;
        }
        *lplpDD = lpDD;
        gfx::wrap_ddraw(lpDD);

        return 0;
    }

    // 0x0040F250
    static BOOL CALLBACK ddrawEnumCallback(GUID* lpGUID, LPSTR lpName, LPSTR lpDesc, LPVOID lpContext)
    {
        auto lpDDresult = (LPDIRECTDRAW*)lpContext;
        LPDIRECTDRAW lpDD;
        if (lpGUID != NULL && SUCCEEDED(DirectDrawCreate(lpGUID, &lpDD, NULL)))
        {
            DDCAPS ddcapsHW;
            ZeroMemory(&ddcapsHW, sizeof(DDCAPS));
            ddcapsHW.dwSize = sizeof(DDCAPS);
            if (SUCCEEDED(lpDD->GetCaps(&ddcapsHW, NULL)) && (ddcapsHW.dwCaps & DDCAPS_3D))
            {
                *lpDDresult = lpDD;
                return FALSE;
            }
            lpDD->Release();
        }
        return TRUE;
    }

    // 0x0040F2F0
    static HRESULT dd_set_coop_level(HWND hWnd, int fullscreen, LPDIRECTDRAW2 pDD)
    {
        if (fullscreen)
        {
            auto hr = pDD->SetCooperativeLevel(hWnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
            if (FAILED(hr))
            {
                out("SetCooperativeLevel to fullscreen failed", "MarniSystem DDSetCoopLevel");
                return hr;
            }
        }
        else
        {
            auto hr = pDD->SetCooperativeLevel(hWnd, DDSCL_NORMAL);
            if (FAILED(hr))
            {
                out("SetCooperativeLevel to normal failed", "MarniSystem DDSetCoopLevel");
                return hr;
            }

            hr = pDD->RestoreDisplayMode();
            if (FAILED(hr))
            {
                error(hr);
            }
        }
        return S_OK;
    }

    // 0x0040f370

    // 0x0040f380
    static int __stdcall surfacex_vfill(MarniSurfaceX* self, LPRECT pRect, uint32_t color, int mode)
    {
        if (mode)
            return surface2_vfill(self, pRect, color, mode);

        if (!self->bOpen)
        {
            // Original (Shift-JIS at 0x51EAA4): "ブロックが有効でないのにサービスを使おうとしました"
            out("tried to call the service although it is not locked", "DirectDrawSurface::Blt");
            return 0;
        }

        RECT rect;
        RECT rc;
        if (pRect)
        {
            SetRect(&rect, 0, 0, self->width - 1, self->height - 1);
            if (!adjust_rect(&rect, pRect, &rc))
                return 0;
            ++rc.right;
            ++rc.bottom;
        }
        else
        {
            SetRect(&rc, 0, 0, self->width, self->height);
        }

        // Pack the 0x00RRGGBB color into the surface's native pixel format using its masks/shifts.
        uint32_t packed = ((self->desc.b_mask & ((color & 0xFF) >> (8 - self->desc.b_bitcnt))) << self->desc.b_shift)
            | ((self->desc.r_mask & (((color >> 16) & 0xFF) >> (8 - self->desc.r_bitcnt))) << self->desc.r_shift)
            | ((self->desc.g_mask & (((color >> 8) & 0xFF) >> (8 - self->desc.g_bitcnt))) << self->desc.g_shift);

        DDBLTFX ddbltfx;
        ZeroMemory(&ddbltfx, sizeof(DDBLTFX));
        ddbltfx.dwSize = sizeof(DDBLTFX);
        ddbltfx.dwFillColor = packed;

        HRESULT hr = gfx::surface_blt(
            (IUnknown*)self->pDDsurface, &rc, nullptr, nullptr, DDBLT_WAIT | DDBLT_COLORFILL, (LPDDBLTFX)&ddbltfx);
        if (!hr)
            return 1;

        out("failed to blt operation. DirectDrawSurface::Blt", "");
        self->bOpen = 0;
        error(hr);
        return 0;
    }

    // 0x0040f520

    // 0x0040F580
    static int __stdcall surfacey_vrelease(MarniSurface2* self)
    {
        auto surface = (MarniSurface3*)self;

        if (surface->var_27)
        {
            if (surface->pDDsurface)
            {
                ((LPDIRECTDRAWSURFACE)surface->pDDsurface)->Release();
                surface->pDDsurface = nullptr;
            }

            if (surface->pDDpalette)
            {
                for (int i = 0; i < surface->pal_cnt; i++)
                {
                    auto pDDpalette = (LPDIRECTDRAWPALETTE)surface->pDDpalette[i];
                    if (pDDpalette)
                    {
                        pDDpalette->Release();
                        surface->pDDpalette[i] = nullptr;
                    }
                }
            }
        }

        operator_delete(surface->pDDpalette);
        surface->pDDpalette = nullptr;
        surface2_vrelease(self);
        return 1;
    }

    // 0x0040F790
    static int __stdcall surfacex_vlock(MarniSurfaceX* self, int* a2, int* a3)
    {
        if (!self->bOpen)
        {
            out("this class is invalid.", "DirectDrawSurface::Lock");
            return 0;
        }
        if (self->bLocked == 1)
        {
            out("you are about to try the lock double.", "DirectDrawSurface::Lock");
            return 0;
        }
        if (self->bPalLocked == 1)
        {
            out("you must unlock for the palette before calling this function.", "DirectDrawSurface::Lock");
            return 0;
        }

        DDSURFACEDESC desc;
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(DDSURFACEDESC);

        auto pDDsurface = (IUnknown*)self->pDDsurface;
        if (gfx::surface_lock(pDDsurface, nullptr, &desc, DDLOCK_WAIT, nullptr))
            return 0;

        self->pBitmap = desc.lpSurface;
        self->width = (int16_t)desc.dwWidth;
        self->pitch = (int16_t)desc.lPitch;
        self->height = (int16_t)desc.dwHeight;
        self->bpp = (uint8_t)desc.ddpfPixelFormat.dwRGBBitCount;

        if (self->var_28)
        {
            // Is_paletted: make room for pal_cnt palettes of 1 << bpp DWORD entries each.
            self->pPalette = operator_new(4 * self->pal_cnt * (1 << self->bpp));
            if (!self->pPalette)
            {
                out("failed to allocate the work for palette.", "MarniSystem DirectDrawSurface::Lock");
                return 0;
            }

            PALETTEENTRY entries[256];
            for (int i = 0; i < self->pal_cnt; i++)
            {
                auto pDDpalette = (LPDIRECTDRAWPALETTE)self->pDDpalette[i];
                if (pDDpalette->GetEntries(0, 0, 1 << self->bpp, entries))
                {
                    out("failed to read the palette from device.", "MarniSystem DirectDrawSurface::Lock");
                    return 0;
                }

                int paletteSize = 1 << self->bpp;
                auto pPalette = (uint32_t*)self->pPalette;
                for (int j = 0; j < paletteSize; j++)
                {
                    // Pack PALETTEENTRY {peRed, peGreen, peBlue, peFlags} as 0x00RRGGBB.
                    pPalette[i * paletteSize + j]
                        = ((uint32_t)entries[j].peRed << 16) | ((uint32_t)entries[j].peGreen << 8) | entries[j].peBlue;
                }
            }

            // Force the surface description to 8-8-8-8 RGBA (the palette entries
            // were converted to full-colour DWORDs above).
            self->desc.a_shift = 24;
            self->desc.a_mask = 0xFF;
            self->desc.a_bitcnt = 8;
            self->desc.r_shift = 16;
            self->desc.r_mask = 0xFF;
            self->desc.r_bitcnt = 8;
            self->desc.g_shift = 8;
            self->desc.g_mask = 0xFF;
            self->desc.g_bitcnt = 8;
            self->desc.b_shift = 0;
            self->desc.b_mask = 0xFF;
            self->desc.b_bitcnt = 8;
        }
        else if ((desc.ddsCaps.dwCaps & 0x20000) == 0) // DDSCAPS_ZBUFFER
        {
            ddrawdesc2surfdesc(&desc, &self->desc);
        }

        if (a2)
            *a2 = (int)self->pBitmap;
        if (a3)
            *a3 = (int)self->pPalette;

        self->bLocked = 1;
        return 1;
    }

    // 0x0040f600
    static int __stdcall surfacex_vpallock(MarniSurfaceX* self, int* a2)
    {
        if (!self->bOpen)
        {
            out("this class is invalid", "DirectDrawSurface::PalLock");
            return 0;
        }

        if (self->bLocked == 1)
        {
            out("you are about to try the lock double", "MarniBits::PalLock");
            return 0;
        }

        if (!self->var_28)
        {
            out("this class is not palettize", "MarniBits::PalLock");
            return 0;
        }

        self->pPalette = operator_new(4 * self->pal_cnt * (1 << self->bpp));
        if (!self->pPalette)
        {
            out("failed to allocate the work for palette", "MarniSystem DirectDrawSurface::PalLock");
            return 0;
        }

        auto palette = (uint32_t*)self->pPalette;
        for (int v5 = 0; v5 < self->pal_cnt; v5++)
        {
            PALETTEENTRY entries[256];
            HRESULT hr = ((LPDIRECTDRAWPALETTE)self->pDDpalette[v5])->GetEntries(0, 0, 1 << self->bpp, (LPPALETTEENTRY)entries);
            if (hr != 0)
            {
                out("failed to read the palette from device", "MarniSystem DirectDrawSurface::PalLock");
                return 0;
            }

            int count = 1 << self->bpp;
            for (int i = 0; i < count; i++)
            {
                palette[v5 * count + i] = entries[i].peBlue | (entries[i].peRed << 16) | (entries[i].peGreen << 8);
            }
        }

        self->desc.a_bitcnt = 8;
        self->desc.r_bitcnt = 8;
        self->desc.g_shift = 8;
        self->desc.g_bitcnt = 8;
        self->desc.b_bitcnt = 8;
        self->desc.a_shift = 24;
        self->desc.a_mask = 0xFF;
        self->desc.r_shift = 16;
        self->desc.r_mask = 0xFF;
        self->desc.g_mask = 0xFF;
        self->desc.b_shift = 0;
        self->desc.b_mask = 0xFF;

        if (a2)
            *a2 = (int)self->pPalette;
        self->bPalLocked = 1;
        return 1;
    }

    // 0x0040f9c0
    static int __stdcall surfacex_vpalunlock(MarniSurfaceX* self)
    {
        if (!self->bOpen)
        {
            out("tried to unlock regardless of invalid surface", "DirectDrawSurface::PalUnlock");
            return 0;
        }
        if (!self->bPalLocked)
        {
            out("not locked", "MarniBits::PalUnlock");
            return 0;
        }
        if (!self->pPalette)
        {
            out("not supposed situation. lppal is NULL", "MarniSystem DirectDrawSurface::PalUnlock");
            return 0;
        }

        PALETTEENTRY entries[256];
        int v3 = 0;
        if (self->pal_cnt > 0)
        {
            while (1)
            {
                int v4 = 1 << self->bpp;
                int v5 = 0;
                if (v4 > 0)
                {
                    // pPalette holds 0x00RRGGBB little-endian DWORDs; unpack them
                    // into PALETTEENTRYs for SetEntries.
                    auto* src = (uint8_t*)self->pPalette + 4 * v3 * v4;
                    do
                    {
                        src += 4;
                        entries[v5].peRed = src[-2];
                        entries[v5].peGreen = src[-3];
                        entries[v5].peBlue = src[-4];
                        entries[v5].peFlags = 0;
                        v5++;
                    } while (v5 < v4);
                }

                auto pDDpalette = (LPDIRECTDRAWPALETTE)self->pDDpalette[v3];
                if (pDDpalette->SetEntries(0, 0, v4, entries))
                    break;

                v3++;
                if (v3 >= self->pal_cnt)
                    goto done;
            }

            out("failed to set the palettes to the device", "MarniSystem DirectDrawSurface::PalUnlock");
            return 0;
        }

    done:
        operator_delete(self->pPalette);
        self->pPalette = nullptr;
        self->bPalLocked = 0;
        return 0;
    }

    // 0x0040fad0
    static int __stdcall surfacex_vunlock(MarniSurfaceX* self)
    {
        if (!self->bOpen)
        {
            out("tried to unlock a surface that is not open", "MarniBits::Unlock");
            return 0;
        }

        gfx::surface_unlock((IUnknown*)self->pDDsurface, nullptr);

        if (!self->var_28) // Is_paletted
        {
            goto done;
        }

        if (!self->pPalette)
        {
            out("not supposed situation. lppal is NULL. MarniSystem DirectDrawSurface::Unlock", "MarniBits::Unlock");
            return 0;
        }

        // The palette entries are 0x00RRGGBB little-endian DWORDs; unpack them
        // into PALETTEENTRYs for SetEntries.
        for (int v3 = 0; v3 < self->pal_cnt; v3++)
        {
            int count = 1 << self->bpp;
            PALETTEENTRY entries[256];
            for (int v5 = 0; v5 < count; v5++)
            {
                uint32_t color = ((uint32_t*)self->pPalette)[v3 * count + v5];
                entries[v5].peRed = (color >> 16) & 0xFF;
                entries[v5].peGreen = (color >> 8) & 0xFF;
                entries[v5].peBlue = color & 0xFF;
                entries[v5].peFlags = 0;
            }

            auto* palette = (LPDIRECTDRAWPALETTE)self->pDDpalette[v3];
            if (palette->SetEntries(0, 0, count, entries))
            {
                out("failed to set the palettes to the device. MarniSystem DirectDrawSurface::Unlock", "MarniBits::Unlock");
                return 0;
            }
        }

        operator_delete(self->pPalette);

    done:
        self->pBitmap = nullptr;
        self->pPalette = nullptr;
        self->bLocked = 0;
        return 0;
    }

    // MarniSurfaceX::vRelease (0x40EE00) — the release_fn slot of MarniSurfaceX::vTbl
    // (0x517358). Releases the D3D texture if one is attached, then delegates to
    // MarniSurfaceY::vRelease (0x40F580). Used by surfacex_create_work to drop any
    // existing surface when the object is (re-)created in place.
    static void __stdcall surfacex_vrelease(MarniSurfaceX* self)
    {
        auto pDDtexture = (LPDIRECT3DTEXTURE2)self->pDDtexture;
        if (pDDtexture)
        {
            pDDtexture->Release();
            self->pDDtexture = nullptr;
        }
        surfacey_vrelease((MarniSurface2*)self);
    }

    // 0x0040fbe0
    static int __stdcall surfacex_create_work(MarniSurfaceX* self, LPDIRECTDRAW pDD, LPDDSURFACEDESC pDesc, int a4)
    {
        // The object can be re-created in place, so release anything it currently owns.
        surfacex_vrelease(self);

        // pal_cnt == -1 means a single palette (the usual texture case).
        self->pal_cnt = (a4 == -1) ? 1 : (int16_t)a4;

        // Round the requested size up to a power of two (8..256).
        DWORD dwWidth = pDesc->dwWidth;
        int width;
        if (dwWidth > 8)
        {
            if (dwWidth > 0x10)
            {
                if (dwWidth > 0x20)
                {
                    if (dwWidth > 0x40)
                        width = (dwWidth > 0x80) ? 256 : 128;
                    else
                        width = 64;
                }
                else
                {
                    width = 32;
                }
            }
            else
            {
                width = 16;
            }
        }
        else
        {
            width = 8;
        }

        DWORD dwHeight = pDesc->dwHeight;
        int height;
        if (dwHeight > 8)
        {
            if (dwHeight > 0x10)
            {
                if (dwHeight > 0x20)
                {
                    if (dwHeight > 0x40)
                        height = (dwHeight > 0x80) ? 256 : 128;
                    else
                        height = 64;
                }
                else
                {
                    height = 32;
                }
            }
            else
            {
                height = 16;
            }
        }
        else
        {
            height = 8;
        }

        DDSURFACEDESC desc;
        memcpy(&desc, pDesc, sizeof(desc));
        desc.dwHeight = height;
        desc.dwWidth = width;

        HRESULT hr = pDD->CreateSurface(&desc, (LPDIRECTDRAWSURFACE*)&self->pDDsurface, nullptr);
        if (hr)
        {
            out("failed to generate a surface", "DirectDrawSurface::CreateWork");
            error(hr);
            surfacex_vrelease(self);
            return 0;
        }

        DDSURFACEDESC a1;
        get_surface_desc(&a1, (LPDIRECTDRAWSURFACE)self->pDDsurface);
        self->is_vmem = (a1.ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY) != 0;

        DDCOLORKEY colorkey = { 0, 0 };
        // The original passes literal 8 here (DDCKEY_SRCBLT), not
        // DDCKEY_COLORSPACE (1): the colour-space key is rejected with
        // E_INVALIDARG on these surfaces, which would leave bOpen cleared and
        // break the whole texture pipeline.
        hr = gfx::surface_set_color_key((IUnknown*)self->pDDsurface, DDCKEY_SRCBLT, &colorkey);
        if (hr)
        {
            out("failed to set the color key", "MarniSystem DirectDrawSurface::CreateWork");
            error(hr);
            surfacex_vrelease(self);
            return 0;
        }

        PALETTEENTRY paletteEntries[256];
        memset(paletteEntries, 0, sizeof(paletteEntries));
        self->bpp = (uint8_t)a1.ddpfPixelFormat.dwRGBBitCount;
        DWORD pixelFormatFlags = a1.ddpfPixelFormat.dwFlags;
        DWORD paletteFlags;
        int paletteIndex = 0;
        HRESULT createPaletteHr = S_OK;
        // 0x8 = DDPF_PALETTEINDEXED4 (4-bit palette-indexed format).
        if ((pixelFormatFlags & DDPF_PALETTEINDEXED4) != 0)
        {
            self->var_28 = 1;
            self->var_25 = 32;
            paletteFlags = DDPCAPS_8BIT;
        }
        else
        {
            paletteFlags = colorkey.dwColorSpaceLowValue; // 0
        }
        // Second check is 0x20 = DDPF_PALETTEINDEXED8 (8-bit palette-indexed).
        // The flags chosen here (DDPCAPS_4BIT | DDPCAPS_8BITENTRIES) mirror the
        // original, which looks swapped relative to the format bits.
        if ((pixelFormatFlags & 0x20) != 0)
        {
            self->var_28 = 1;
            self->var_25 = 32;
            paletteFlags = DDPCAPS_4BIT | DDPCAPS_8BITENTRIES;
        }

        if (!self->var_28)
        {
            ddrawdesc2surfdesc(&a1, &self->desc);
            self->pal_cnt = 0;
            goto done_surface;
        }

        self->pDDpalette = (void**)operator_new((size_t)(4 * (int)self->pal_cnt));
        if (self->pal_cnt <= 0)
            goto done_palettes;

        for (;;)
        {
            createPaletteHr = pDD->CreatePalette(
                paletteFlags, (LPPALETTEENTRY)paletteEntries, (LPDIRECTDRAWPALETTE*)&self->pDDpalette[paletteIndex], nullptr);
            if (createPaletteHr)
                break;
            if (++paletteIndex >= self->pal_cnt)
                goto done_palettes;
        }
        error(createPaletteHr);
        out("failed to allocate DirectDrawPalette", "MarniSystem DirectDrawSurface::CreateWork");
        surfacex_vrelease(self);
        return 0;

    done_palettes:
        if (gfx::surface_set_palette((IUnknown*)self->pDDsurface, (IUnknown*)self->pDDpalette[0]))
        {
            out("failed to establish DirectDrawPalette", "MarniSystem DirectDrawSurface::CreateWork");
            surfacex_vrelease(self);
            return 0;
        }
        // Paletted surfaces are always read back as 32-bit 0xRRGGBBAA.
        self->desc.a_shift = 24;
        self->desc.a_mask = 0xFF;
        self->desc.a_bitcnt = 8;
        self->desc.r_shift = 16;
        self->desc.r_mask = 0xFF;
        self->desc.r_bitcnt = 8;
        self->desc.g_shift = 8;
        self->desc.g_mask = 0xFF;
        self->desc.g_bitcnt = 8;
        self->desc.b_shift = 0;
        self->desc.b_mask = 0xFF;
        self->desc.b_bitcnt = 8;

    done_surface:
        self->width = (int16_t)a1.dwWidth;
        self->pitch = (int16_t)a1.lPitch;
        self->height = (int16_t)a1.dwHeight;
        self->bOpen = 1;
        self->var_27 = 1;
        self->var_29 = 0;
        surfacex_create_surface(self);
        return 1;
    }

    // 0x0040FF70
    static int surface_get_alpha_bits(MarniSurfaceX* self)
    {
        if (!self->pDDsurface)
            return 0;

        // Query the surface pixel format and count the alpha bits (the number of
        // consecutive set bits in the RGB alpha bit mask, e.g. 8 for 0xFF000000).
        DDSURFACEDESC desc = {};
        desc.dwSize = 0x6C; // matches the original (DDSURFACEDESC dwSize on x86)
        desc.dwFlags = DDSD_PIXELFORMAT;
        gfx::surface_get_surface_desc((IUnknown*)self->pDDsurface, &desc);

        uint32_t alphaMask = desc.ddpfPixelFormat.dwRGBAlphaBitMask;
        int alphaBits = 0;
        if (alphaMask)
        {
            // Skip trailing zero bits to reach the lowest set bit.
            while (!(alphaMask & 1))
                alphaMask >>= 1;

            // Count the consecutive set bits from the lowest set bit upward.
            do
            {
                alphaMask >>= 1;
                ++alphaBits;
            } while (alphaMask & 1);
        }
        return alphaBits;
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
    // DirectDraw surface has been created. First validates the surface's pixel format
    // (surface_get_alpha_bits returns the bit depth of dwRGBAlphaBitMask), then locks
    // the surface and scans every pixel's alpha bits. If any alpha bit is clear the
    // surface is not fully opaque, and self->var_2C is set so the texture is drawn
    // through the alpha-blended draw-op path (see trans_spr_poly / tex_spr).
    static void surfacex_create_surface(MarniSurfaceX* self)
    {
        uint8_t hasTransparency = 0;

        // Only surfaces whose format carries an alpha mask are scanned; surfaces
        // without alpha (or with a zero mask) are treated as fully opaque.
        if (surface_get_alpha_bits(self))
        {
            auto pDDsurface = (IUnknown*)self->pDDsurface;

            DDSURFACEDESC desc;
            memset(&desc, 0, sizeof(desc));
            desc.dwSize = sizeof(DDSURFACEDESC);
            desc.dwFlags = DDSD_PIXELFORMAT;
            gfx::surface_get_surface_desc(pDDsurface, &desc);

            // Keep the pixel format; the descriptor is reused (re-zeroed) for Lock.
            DDPIXELFORMAT pixelFormat = desc.ddpfPixelFormat;

            memset(&desc, 0, sizeof(desc));
            desc.dwSize = sizeof(DDSURFACEDESC);
            gfx::surface_lock(pDDsurface, nullptr, &desc, DDLOCK_WAIT, nullptr);

            // Scan the locked pixels: for each pixel, test the alpha bits (the mask
            // shifted to the pixel's position) against the surface memory. The row
            // base is lpSurface + lPitch, mirroring the original code.
            DWORD height = desc.dwHeight;
            if (height != 0)
            {
                DWORD width = desc.dwWidth;
                DWORD y = 0;
                do
                {
                    if (hasTransparency)
                        break;

                    const uint8_t* row = (const uint8_t*)desc.lpSurface + desc.lPitch;
                    if (width != 0)
                    {
                        DWORD x = 0;
                        uint32_t bit = 0;
                        do
                        {
                            if (hasTransparency)
                                break;

                            DWORD mask = pixelFormat.dwRGBAlphaBitMask << (bit & 7);
                            if ((*(const DWORD*)(row + (bit >> 3)) & mask) != mask)
                                hasTransparency = 1;

                            x++;
                            bit += pixelFormat.dwRGBBitCount;
                        } while (x < width);
                    }
                    y++;
                } while (y < height);
            }

            gfx::surface_unlock(pDDsurface, nullptr);
        }

        self->var_2C = hasTransparency;
    }

    // 0x0040FEF0
    MarniSurfaceY* __stdcall surfacey_ctor(MarniSurfaceY* self)
    {
        // Mirror MarniSurface2::Ctor call: zero the base surface (0x30 bytes) and set its
        // vtbl, then override the vtbl with MarniSurfaceY::vTbl and zero the D3D fields.
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
        if (!self->bOpen)
        {
            out("tried to call the service although it is not locked", "MarniBits::Blt");
            return 0;
        }

        RECT rect;
        RECT rc;
        if (pSrcRect)
        {
            SetRect(&rect, 0, 0, self->width - 1, self->height - 1);
            if (!adjust_rect(&rect, pSrcRect, &rc))
                return 0;
        }
        else
        {
            SetRect(&rc, 0, 0, self->width - 1, self->height - 1);
        }

        if (!surface_lock(self, 0, 0))
        {
            out("failed to lock", "MarniBits::Blt");
            self->bOpen = 0;
            return 0;
        }

        int fillWidth = rc.right - rc.left + 1;
        int fillHeight = rc.bottom - rc.top + 1;
        if (color || mode)
        {
            for (int y = 0; y < fillHeight; y++)
            {
                for (int x = 0; x < fillWidth; x++)
                {
                    // MarniSurface::SetCurrentColor handles the per-bpp (8/16/24/32) pixel write.
                    surface_set_current_color(self, rc.left + x, rc.top + y, color, mode);
                }
            }
        }
        else
        {
            std::memset(self->pBitmap, 0, self->height * self->pitch);
        }

        surface_unlock(self);
        return 1;
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

        DWORD bytesWritten;
        HANDLE hFile = CreateFileA(lpFileName, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        WriteFile(hFile, buffer, dataSize, &bytesWritten, nullptr);
        CloseHandle(hFile);
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

            auto pDDsurface = (IUnknown*)result->surface->pDDsurface;
            auto pDDpalette = (IUnknown*)result->surface->pDDpalette[index];
            gfx::surface_set_palette(pDDsurface, pDDpalette);
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

    // 0x0042F1D0
    static void tex_spr(
        MarniSurface2* surface, void* a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11, int a12,
        int a13, int a14, int a15, int a16)
    {
        return interop::call<void, MarniSurface2*, void*, int, int, int, int, int, int, int, int, int, int, int, int, int, int>(
            0x0042F1D0, surface, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
    }

    // 0x00432BB0
    void unload_door_texture()
    {
        interop::call(0x00432BB0);
    }

    // 0x00432C60
    void door_disp0(int doorId, int a1, int a2, int a3)
    {
        interop::call<void, int, int, int>(0x00432C60, doorId, a1, a2, a3);
    }

    // 0x00432CD0
    void door_disp1(int doorId)
    {
        interop::call<void, int>(0x00432CD0, doorId);
    }

    // 0x0043F550
    void unload_texture_page(int page)
    {
        auto& tp = gGameTable.texture_pages[page];
        if (tp.handle != 0)
        {
            marni::unload_texture(gGameTable.pMarni, tp.handle);
        }
        tp.handle = 0;
        tp.var_04 = 0;
        tp.var_08 = 0;
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

    // 0x00441710
    void flush_surfaces()
    {
        interop::call(0x00441710);
    }

    // 0x00416D40
    static int __stdcall flush_surfaces_marni(Marni* self)
    {
        if ((self->gpu_flag & 0x2000) != 0)
            return 1;

        DDSURFACEDESC a3 = {};
        a3.dwSize = 0x6C;

        MarniSurfaceX pSrc;
        surfacex_ctor(&pSrc);

        for (int i = 0; i < 256; ++i)
        {
            auto& node = self->texture_nodes[i];
            if ((node.var_14 & 0x200) == 0 || (node.var_14 & 0x2000) != 0)
                continue;

            // Fill a3 from the source surface descriptor (only when it is in video memory).
            if (node.surface->is_vmem)
                get_surface_desc(&a3, (LPDIRECTDRAWSURFACE)node.surface->pDDsurface);

            a3.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY;
            a3.dwWidth *= 2;
            a3.dwHeight *= 2;

            auto& tex = self->textures[node.texture_id];

            surfacex_create_work(&pSrc, (LPDIRECTDRAW)self->pDirectDraw2, &a3, -1);
            surfacex_create_texture_object(&pSrc);

            tex.surface.var_22 = node.page;

            RECT rc;
            rc.left = 0;
            rc.top = 0;
            rc.right = tex.surface.width - 1;
            rc.bottom = tex.surface.height - 1;
            interop::thiscall<void, MarniSurfaceX*, RECT*, RECT*, MarniSurface2*, int, int>(
                0x0040F370, &pSrc, &rc, 0, &tex.surface, 0, 0);

            if (node.surface)
            {
                surfacex_dtor(node.surface);
                operator_delete(node.surface);
            }

            node.surface = (MarniSurfaceX*)operator_new(0x44);
            if (node.surface)
                surfacex_ctor(node.surface);

            a3.ddsCaps.dwCaps = DDSCAPS_ALLOCONLOAD | DDSCAPS_TEXTURE;
            surfacex_create_work(node.surface, (LPDIRECTDRAW)self->pDirectDraw2, &a3, -1);
            surfacex_create_texture_object(node.surface);
            gGameTable.error = surfacex_load(node.surface, &pSrc);

            if (node.surface->is_vmem)
            {
                node.var_14 = (node.var_14 & ~0x200) | 0x100;
            }
            else
            {
                // The video memory texture could not be created: fall back to a
                // half-resolution system memory copy.
                a3.dwHeight >>= 1;
                a3.dwWidth >>= 1;
                a3.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY;

                surfacex_create_work(&pSrc, (LPDIRECTDRAW)self->pDirectDraw2, &a3, -1);
                surfacex_create_texture_object(&pSrc);

                rc.left = 0;
                rc.top = 0;
                rc.right = tex.surface.width / 2 - 1;
                rc.bottom = tex.surface.height / 2 - 1;
                interop::thiscall<void, MarniSurfaceX*, RECT*, RECT*, MarniSurface2*, int, int>(
                    0x0040F370, &pSrc, &rc, 0, &tex.surface, 0, 0);

                if (node.surface)
                {
                    surfacex_dtor(node.surface);
                    operator_delete(node.surface);
                }
                node.surface = (MarniSurfaceX*)operator_new(0x44);
                if (node.surface)
                    surfacex_ctor(node.surface);

                a3.ddsCaps.dwCaps = DDSCAPS_ALLOCONLOAD | DDSCAPS_TEXTURE;
                surfacex_create_work(node.surface, (LPDIRECTDRAW)self->pDirectDraw2, &a3, -1);
                surfacex_create_texture_object(node.surface);
                gGameTable.error = surfacex_load(node.surface, &pSrc);
                if (gGameTable.error)
                {
                    out("failed to reload texture. Direct3D::FlashSurfaces", "");
                    surfacex_dtor(&pSrc);
                    return 0;
                }
            }

            surfacex_get_texture_handle(node.surface, (LPDIRECT3DDEVICE2)self->pDirectDevice2);
        }

        request_video_memory(self);
        surfacex_dtor(&pSrc);
        return 1;
    }

    // 0x004419A0
    void kill()
    {
        interop::call(0x004419A0);
    }

    // 0x00441270
    void add_tile(void* primPtr, int z, int is_back)
    {
        interop::call<void, void*, int, int>(0x00441270, primPtr, z, is_back);
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
        interop::call<void, int, Md1*, int>(0x00443620, workNo, pTmd, id);
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
    }

    // 0x0050B020
    void config_flush_all(MarniConfig* self)
    {
        interop::thiscall<void, MarniConfig*>(0x0050B020, self);
        system::config::save();
    }

    // 0x0050B900
    void config_shutdown()
    {
        interop::call(0x00509C70);
        interop::thiscall<void, MarniConfig*>(0x0050ACA0, &gGameTable.marni_config);
    }

    // 0x00442CB0
    void set_gpu_flag()
    {
        switch (gGameTable.byte_680592)
        {
        case 0:
        {
            gGameTable.pMarni->gpu_flag &= ~GpuFlags::GPU_17;
            gGameTable.pMarni->gpu_flag &= ~GpuFlags::GPU_18;
            break;
        }
        case 1:
        {
            gGameTable.pMarni->gpu_flag |= GpuFlags::GPU_17;
            gGameTable.pMarni->gpu_flag &= ~GpuFlags::GPU_18;
            break;
        }
        case 2:
        {
            gGameTable.pMarni->gpu_flag &= ~GpuFlags::GPU_17;
            gGameTable.pMarni->gpu_flag |= GpuFlags::GPU_18;
            break;
        }
        case 3:
        {
            gGameTable.pMarni->gpu_flag |= GpuFlags::GPU_17 | GpuFlags::GPU_18;
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
            auto windowStyles = GetWindowLongA((HWND)self->hWnd, GWL_STYLE);
            SetWindowLongA((HWND)self->hWnd, GWL_STYLE, windowStyles & 0x7F30FFFF | 0xCF0000);
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
        interop::hookThisCall(0x00402290, &clear_otags);
        interop::hookThisCall(0x004022E0, &request_video_memory);
        interop::hookThisCall(0x00402530, &request_display_mode_count);
        interop::hookThisCall(0x004033F0, &reload_texture);
        interop::hookThisCall(0x00402940, &restore_surfaces);
        interop::hookThisCall(0x00402A80, &flip);
        interop::hookThisCall(0x00402BC0, &draw);
        interop::hookThisCall(0x00405DD0, &get_z_buffer_caps);
        interop::hookThisCall(0x00404CE0, &unload_texture);
        interop::hookThisCall(0x00404D20, &clear);
        interop::hookThisCall(0x00404FA0, &clear_buffers);
        interop::hookThisCall(0x004050C0, &dtor);
        interop::hookThisCall(0x00405320, &init);
        interop::hookThisCall(0x00406450, &move);
        interop::hookThisCall(0x004064D0, &destroy);
        interop::hookThisCall(0x00407340, &enum_drivers);
        interop::hookThisCall(0x00407440, &create_d3d);
        interop::hookThisCall(0x00406D90, &create_device);
        interop::hookThisCall(0x00407020, &create_zbuffer);
        interop::hookThisCall(0x0040EAF0, &do_draw_op);
        interop::hookThisCall(0x0040ECA0, &surfacex_create_texture_object);
        interop::hookThisCall(0x0040EE30, &surfacex_load);
        interop::hookThisCall(0x0040ED20, &surfacex_get_texture_handle);
        interop::hookThisCall(0x0040FBE0, &surfacex_create_work);
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
        interop::hookThisCall(0x0040C6E0, &draw_line_flat);
        interop::hookThisCall(0x0040C790, &draw_line_gourad);
        interop::writeJmp(0x00406860, &query_ddraw2);
        interop::writeJmp(0x0040F1A0, &create_ddraw);
        interop::writeJmp(0x0040F2F0, &dd_set_coop_level);
        interop::writeJmp(0x004DBFD0, &out_internal);
        interop::writeJmp(0x00442CB0, &set_gpu_flag);
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
    }
}
