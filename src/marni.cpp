#include "marni.h"
#include "interop.hpp"
#include "openre.h"
#include "re2.h"

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
    static int __stdcall surface2_create_work(MarniSurface2* self, int width, int height, int depth, int palBpp, int palCnt);
    static void __stdcall destroy(Marni* marni);
    static int __stdcall do_draw_op(Marni* self, int index);
    static void __stdcall do_render(Marni* self, MarniOt* pOt);
    static int __stdcall init_all(Marni* self);
    static int __stdcall clear_buffers(Marni* self);
    static void __stdcall move(Marni* marni);
    static int __stdcall movie_open(
        MarniMovie* self, LPCSTR path, HWND hWnd, LPRECT pRect, LPDIRECTDRAW2 pDD2, LPDIRECTDRAWSURFACE pSurface);
    static MarniMovie* __stdcall movie_ctor(MarniMovie* self, int mode);
    static void __stdcall movie_dtor(MarniMovie* self);
    static void __stdcall movie_release(MarniMovie* self);
    static int __stdcall movie_seek(MarniMovie* self);
    static int __stdcall movie_update(MarniMovie* self);
    static int __stdcall movie_update_window(MarniMovie* self);
    static void __stdcall polygon_object_dtor(PolygonObject* self);
    static MarniOt* __stdcall ot_ctor(MarniOt* self, size_t a2, int a3);
    static Prim* __stdcall ot_get_primitive(MarniOt* self);
    static int __stdcall ot_add_primitive_as_z(MarniOt* self, Prim* pPrim, int z);
    static int __stdcall ot_clear(MarniOt* self);
    static int __stdcall ot_alloc(MarniOt* self, int depth, int a3);
    static void __stdcall ot_dtor(MarniOt* self);
    static void __stdcall resize(Marni* marni, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static uint16_t __stdcall search_texture_object_0_from_1(Marni* self, int handle, int index);
    static void set_filtering(Marni* self, uint8_t a2);
    static void __stdcall sub_40E800(Marni* self, uint8_t a2);
    static int invalidate_window(HWND hWnd, int width, int height, int fullscreen, LPRECT lpResRect);
    static void __stdcall sub_40EC10(Marni* self);
    static int ddrawdesc2surfdesc(LPDDSURFACEDESC pDDesc, MarniSurfaceDesc* pDesc);
    static int enum_display_mode(LPDIRECTDRAW2 lpDD2, MarniRes* res, size_t max, size_t* count);
    static HRESULT get_surface_desc(LPDDSURFACEDESC lpDDSurfaceDesc, LPDIRECTDRAWSURFACE lpDDSurface);
    static int create_ddraw(bool bEnumDevices, LPDIRECTDRAW* lplpDD, LPDWORD lpIsDefault);
    static int __stdcall sub_414B30(MarniMovie* self);
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
    static OldStdString* __stdcall oldstring_set(OldStdString* self, const std::string& s);
    static bool __stdcall oldstring_eq(OldStdString* self, const std::string& s);
    static void __stdcall surface3_dtor(MarniSurface3* self);
    static int __stdcall get_z_buffer_caps(Marni* self);

    // 0x0050D905
    void* cstd_malloc(size_t len)
    {
        return interop::call<void*, size_t>(0x0050D905, len);
    }

    // 0x0050D89C
    void cstd_free(void* mem)
    {
        interop::call<void, void*>(0x0050D89C, mem);
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
        interop::thiscall<int, MarniSurface2*>((uintptr_t)self->vtbl[8], self);
    }

    static void surface_fill(MarniSurface* self, int r, int g, int b)
    {
        interop::thiscall<int, MarniSurface*, int, int, int>((uintptr_t)self->vtbl[0], self, r, g, b);
    }

    static int surface_lock(MarniSurface2* self, int a2, int a3)
    {
        return interop::thiscall<int, MarniSurface2*, int, int>((uintptr_t)self->vtbl[4], self, a2, a3);
    }

    static void surface_unlock(MarniSurface2* self)
    {
        interop::thiscall<int, MarniSurface2*>((uintptr_t)self->vtbl[5], self);
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
    static void __stdcall syskeydown(Marni* self)
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
            rc.top = -(GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CYCAPTION));

        if (self->resolutions[self->modes].width == 640)
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
        interop::thiscall<int, Marni*>(0x004022E0, self);
    }

    // 0x00402500
    bool __stdcall change_resolution(Marni* self)
    {
        return change_display_mode(self, self->modes + 1 >= (uint32_t)self->res_count ? 0 : self->modes + 1);
    }

    // 0x00402530
    int __stdcall request_display_mode_count(Marni* self)
    {
        if (self->is_gpu_active)
            return self->res_count;

        out("", "Direct3D::RequestDisplayModeCount");
        return 0;
    }

    // 0x00402940
    static int __stdcall restore_surfaces(Marni* self)
    {
        if (((LPDIRECTDRAWSURFACE)self->surface2.pDDsurface)->IsLost() == DDERR_SURFACELOST)
        {
            gGameTable.error = ((LPDIRECTDRAWSURFACE)self->surface2.pDDsurface)->Restore();
            if (gGameTable.error)
            {
                out("Restoring of a Back surface failed.", "Direct3D::RestoreSurfaces");
                error(gGameTable.error);
                return 0;
            }
        }

        if (((LPDIRECTDRAWSURFACE)self->surface0.pDDsurface)->IsLost() == DDERR_SURFACELOST)
        {
            gGameTable.error = ((LPDIRECTDRAWSURFACE)self->surface0.pDDsurface)->Restore();
            if (gGameTable.error)
            {
                out("Restoring of a Back surface failed.", "Direct3D::RestoreSurfaces");
                error(gGameTable.error);
                return 0;
            }
        }

        if ((self->gpu_flag & GpuFlags::GPU_13) != 0)
            return 1;

        if (((LPDIRECTDRAWSURFACE)self->surfaceZ.pDDsurface)->IsLost() == DDERR_SURFACELOST)
        {
            gGameTable.error = ((LPDIRECTDRAWSURFACE)self->surfaceZ.pDDsurface)->Restore();
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
            if (v6 != nullptr && (v6->bOpen != 1 || ((LPDIRECTDRAWSURFACE)v6->pDDsurface)->IsLost() != DDERR_SURFACELOST))
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
        auto src = ((LPDIRECTDRAWSURFACE2)self->surface0.pDDsurface);
        auto dst = ((LPDIRECTDRAWSURFACE2)self->surface2.pDDsurface);

        RECT srcRect;
        SetRect(&srcRect, 0, 0, width, height);

        RECT dstRect;
        CopyRect(&dstRect, (LPRECT)&self->window_rect);

        DDBLTFX ddbltfx;
        ZeroMemory(&ddbltfx, sizeof(DDBLTFX));
        ddbltfx.dwSize = sizeof(DDBLTFX);
        ddbltfx.dwDDFX = DDBLTFX_NOTEARING;

        dst->Blt(&dstRect, src, &srcRect, DDBLT_DDFX | DDBLT_WAIT, &ddbltfx);
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

        if (self->gpu_flag & GpuFlags::GPU_FULLSCREEN)
        {
            auto fullscreen = self->resolutions[self->modes].fullscreen;
            if (fullscreen == 1)
            {
                auto surface0 = (LPDIRECTDRAWSURFACE7)self->surface0.pDDsurface;
                auto surface2 = (LPDIRECTDRAWSURFACE7)self->surface2.pDDsurface;
                surface2->Flip(surface0, DDFLIP_WAIT);
            }
            else if (fullscreen == 2 || fullscreen == 3)
            {
                flip_blt(self, (int32_t)(self->render_w * self->aspect_x), (int32_t)(self->render_h * self->aspect_y));
            }
        }
        else
        {
            flip_blt(self, self->xsize, self->ysize);
        }
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
            self->modes = mode;
            const auto& r = self->resolutions[mode];
            if (change_mode(self, r.width, r.height, r.depth))
            {
                out("Direct3D::ChangeDisplayMode - (%d->%d) w:%d h:%d bpp:%d", "");
                oldstring_set(&gGameTable.marni_config.display_mode, generate_res_string(&r));
                self->var_8C8318 = 0;
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
        return interop::thiscall<int, Marni*, int>(0x004033F0, self, texture);
    }

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

        auto pViewport = (LPDIRECT3DVIEWPORT)self->pViewport;

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
                gGameTable.error = pViewport->Clear(1, &rect, D3DCLEAR_ZBUFFER);
            }
        }
        else
        {
            if ((self->gpu_flag & GpuFlags::GPU_13) == 0)
            {
                gGameTable.error = pViewport->Clear(1, &rect, D3DCLEAR_ZBUFFER | D3DCLEAR_TARGET);
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

        auto pD3D2 = (LPDIRECT3DDEVICE2)self->pDirectDevice2;
        if (pD3D2 == nullptr || self->pViewport == nullptr)
        {
            out("tried to render regardless of not initializint to viewport or device", "Direct3D::do_render");
            return;
        }

        gGameTable.error = pD3D2->BeginScene();
        sub_40E800(self, sub_416670(pOt));
        pD3D2->SetRenderState(D3DRENDERSTATE_ANISOTROPY, 0);
        pD3D2->SetRenderState(D3DRENDERSTATE_EDGEANTIALIAS, 0);
        pD3D2->SetRenderState(D3DRENDERSTATE_ANTIALIAS, 0);
        pD3D2->SetRenderState(D3DRENDERSTATE_SUBPIXEL, 0);
        pD3D2->SetRenderState(D3DRENDERSTATE_LASTPIXEL, 1);
        if (FAILED(gGameTable.error))
        {
            d3d_error_routine(gGameTable.error);
            return;
        }

        trans_priority_list(self, pOt);
        sub_40EC10(self);
        gGameTable.error = pD3D2->EndScene();
        if (FAILED(gGameTable.error))
        {
            d3d_error_routine(gGameTable.error);
            return;
        }

        D3DSTATS stats;
        ZeroMemory(&stats, sizeof(D3DSTATS));
        stats.dwSize = sizeof(D3DSTATS);
        pD3D2->GetStats(&stats);
        self->triangles_drawn = stats.dwTrianglesDrawn - gGameTable.d3d_triangles_drawn;
        self->vertices_processed = stats.dwVerticesProcessed - gGameTable.d3d_vertices_processed;
        gGameTable.d3d_triangles_drawn = stats.dwTrianglesDrawn;
        gGameTable.d3d_vertices_processed = stats.dwVerticesProcessed;
    }

    // 0x00403F30
    static int __stdcall init_all(Marni* self)
    {
        // return interop::thiscall<int, Marni*>(0x00403F30, self);

        const auto& r = self->resolutions[self->modes];
        if (r.fullscreen <= 0)
            self->gpu_flag &= ~GpuFlags::GPU_FULLSCREEN;
        else
            self->gpu_flag |= GpuFlags::GPU_FULLSCREEN;
        self->xsize = r.width;
        self->ysize = r.height;
        self->bpp = r.depth;
        self->is_gpu_busy = 1;
        gGameTable.error
            = dd_set_coop_level((HWND)self->hWnd, self->gpu_flag & GpuFlags::GPU_FULLSCREEN, (LPDIRECTDRAW2)self->pDirectDraw2);
        self->is_gpu_busy = 0;
        if (gGameTable.error)
        {
            out();
            error(gGameTable.error);
            self->is_gpu_active = 0;
            return 0;
        }
        get_z_buffer_caps(self);
        invalidate_window(
            (HWND)self->hWnd, self->xsize, self->ysize, self->gpu_flag & GpuFlags::GPU_FULLSCREEN, (LPRECT)&self->window_rect);
        if (self->gpu_flag & GpuFlags::GPU_FULLSCREEN)
        {
            self->is_gpu_busy = 1;
            gGameTable.error = ((LPDIRECTDRAW2)self->pDirectDraw2)->SetDisplayMode(self->xsize, self->ysize, self->bpp, 0, 0);
            self->is_gpu_busy = 0;
            if (gGameTable.error)
            {
                out();
                error(gGameTable.error);
                self->gpu_flag &= ~GpuFlags::GPU_FULLSCREEN;
                return 0;
            }

            const auto& r2 = self->resolutions[self->modes];
            switch (r2.fullscreen)
            {
            case 1: SetRect((LPRECT)&self->window_rect, 0, 0, r2.width, r2.height); break;
            case 2:
                self->xsize = self->render_w;
                self->ysize = self->render_h;
                SetRect(
                    (LPRECT)&self->window_rect,
                    (r2.width / 2) - (self->render_w / 2),
                    (r2.height / 2) - (self->render_h / 2),
                    (r2.width / 2) + (self->render_w / 2),
                    (r2.height / 2) + (self->render_h / 2));
                break;
            case 3:
                self->xsize = self->render_w;
                self->ysize = self->render_h;
                SetRect((LPRECT)&self->window_rect, 0, 0, r2.width, r2.height);
                break;
            }
            gGameTable.dword_54413C = 1;
        }
        else if (self->resolutions[self->modes].fullscreen == 0xFFFFFFFF)
        {
            self->xsize = self->render_w;
            self->ysize = self->render_h;
        }

        DDSURFACEDESC desc;
        ZeroMemory(&desc, sizeof(DDSURFACEDESC));
        desc.dwSize = sizeof(DDSURFACEDESC);
        self->aspect_x = (float)((double)self->xsize / self->render_w);
        self->aspect_y = (float)((double)self->ysize / self->render_h);

        if (self->gpu_flag & GpuFlags::GPU_FULLSCREEN)
        {
            auto fullscreen = self->resolutions[self->modes].fullscreen;
            if (fullscreen == 1)
            {
                desc.dwBackBufferCount = 1;
                desc.dwFlags = DDSD_BACKBUFFERCOUNT | DDSD_CAPS;
                desc.ddsCaps.dwCaps = DDSCAPS_3DDEVICE | DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
                gGameTable.error
                    = ((LPDIRECTDRAW2)self->pDirectDraw2)
                          ->CreateSurface((LPDDSURFACEDESC)&desc, (LPDIRECTDRAWSURFACE*)&self->surface2.pDDsurface, NULL);
                if (gGameTable.error != 0)
                {
                    out();
                    return 0;
                }

                DDSCAPS v55;
                v55.dwCaps = DDSCAPS_BACKBUFFER;
                gGameTable.error = ((LPDIRECTDRAWSURFACE)self->surface2.pDDsurface)
                                       ->GetAttachedSurface((LPDDSCAPS)&v55, (LPDIRECTDRAWSURFACE*)&self->surface0.pDDsurface);
                if (gGameTable.error != 0)
                {
                    out();
                    return 0;
                }
            }
            else if (fullscreen > 1 && fullscreen <= 3)
            {
                desc.dwFlags = DDSD_CAPS;
                desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
                gGameTable.error
                    = ((LPDIRECTDRAW2)self->pDirectDraw2)
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
                gGameTable.error
                    = ((LPDIRECTDRAW2)self->pDirectDraw2)
                          ->CreateSurface((LPDDSURFACEDESC)&desc, (LPDIRECTDRAWSURFACE*)&self->surface0.pDDsurface, NULL);
                if (gGameTable.error)
                {
                    out();
                    return 0;
                }
                gGameTable.error
                    = ((LPDIRECTDRAW2)self->pDirectDraw2)->CreateClipper(0, (LPDIRECTDRAWCLIPPER*)&self->pClipper, NULL);
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
                gGameTable.error
                    = ((LPDIRECTDRAWSURFACE)self->surface2.pDDsurface)->SetClipper((LPDIRECTDRAWCLIPPER)self->pClipper);
                if (gGameTable.error)
                {
                    out();
                    return 0;
                }
            }
            get_surface_desc(&desc, (LPDIRECTDRAWSURFACE)self->surface2.pDDsurface);
            RECT rc;
            SetRect(&rc, 0, 0, desc.dwWidth, desc.dwHeight);

            DDBLTFX ddbltfx;
            ZeroMemory(&ddbltfx, sizeof(DDBLTFX));
            ddbltfx.dwSize = sizeof(DDBLTFX);
            ((LPDIRECTDRAWSURFACE)self->surface2.pDDsurface)->Blt(&rc, 0, 0, DDBLT_WAIT | DDBLT_COLORFILL, (LPDDBLTFX)&ddbltfx);
        }
        else
        {
            desc.dwFlags = DDSD_CAPS;
            desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
            gGameTable.error
                = ((LPDIRECTDRAW2)self->pDirectDraw2)
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
            gGameTable.error
                = ((LPDIRECTDRAW2)self->pDirectDraw2)
                      ->CreateSurface((LPDDSURFACEDESC)&desc, (LPDIRECTDRAWSURFACE*)&self->surface0.pDDsurface, NULL);
            if (gGameTable.error)
            {
                out();
                return 0;
            }
            gGameTable.error
                = ((LPDIRECTDRAW2)self->pDirectDraw2)->CreateClipper(0, (LPDIRECTDRAWCLIPPER*)&self->pClipper, NULL);
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
            gGameTable.error
                = ((LPDIRECTDRAWSURFACE)self->surface2.pDDsurface)->SetClipper((LPDIRECTDRAWCLIPPER)self->pClipper);
            if (gGameTable.error)
            {
                out();
                return 0;
            }
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

            LPDIRECTDRAWSURFACE lpZBuffer;
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
            gGameTable.error = ((LPDIRECT3DVIEWPORT2)self->pViewport)->SetViewport2(&vp);
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
            ((LPDIRECT3DVIEWPORT2)self->pViewport)->SetBackground(self->MaterialHandle);
            self->is_gpu_active = 1;
            return 1;
        }
        if ((self->gpu_flag & GpuFlags::GPU_FULLSCREEN) != 0)
            surface_fill(&self->surface2, 0, 0, 0);
        self->is_gpu_active = 1;
        return 1;
    }

    // 0x00404FA0
    static int __stdcall clear_buffers(Marni* self)
    {
        return interop::thiscall<int, Marni*>(0x00404FA0, self);
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
    }

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
            auto isEq = oldstring_eq(&gGameTable.marni_config.device_name, std::string(d.lpDeviceName));
            exception = 9;
            if (isEq)
            {
                self->device_cnt = i;
                break;
            }
        }

        exception = 12;
        oldstring_set(&gGameTable.marni_config.device_name, gGameTable.d3d_devices[self->device_cnt].lpDeviceName);
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

        for (size_t i = 0; i < numDisplayModes; i++)
        {
            const auto& r = res[i];
            if (r.depth == 16 && r.width == 640 && r.height == 480)
            {
                auto v26 = &self->resolutions[self->res_count];
                v26->width = r.width;
                v26->height = r.height;
                v26->depth = r.depth;
                v26->fullscreen = 1;
                self->res_count++;
            }
        }

        if ((self->gpu_flag & GpuFlags::INCLUDE_2X) != 0)
        {
            auto& r = self->resolutions[self->res_count];
            r.width = 2 * self->render_w;
            r.height = 2 * self->render_h;
            r.depth = self->desktop_bpp;
            r.fullscreen = 0;
            self->modes = self->res_count;
            self->res_count++;
        }

        self->modes = 0;
        for (auto i = 0; i < self->res_count; i++)
        {
            exception = 13;
            auto isEq = oldstring_eq(&gGameTable.marni_config.display_mode, generate_res_string(&self->resolutions[i]));
            exception = 9;
            if (isEq)
            {
                self->modes = i;
                break;
            }
        }

        exception = 14;
        oldstring_set(&gGameTable.marni_config.display_mode, generate_res_string(&self->resolutions[self->modes]));
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

    // 0x00405DD0
    static int __stdcall get_z_buffer_caps(Marni* self)
    {
        return interop::thiscall<int, Marni*>(0x00405DD0, self);
    }

    // 0x00405EC0
    int __stdcall create_texture_handle(Marni* self, MarniSurface2* pSrcSurface, uint32_t mode)
    {
        return interop::thiscall<int, Marni*, MarniSurface2*, uint32_t>(0x00405EC0, self, pSrcSurface, mode);
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
        interop::thiscall<int, Marni*>(0x004064D0, marni);
    }

    // 0x004065C0
    static void __stdcall resize(Marni* marni, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        interop::thiscall<int, Marni*, HWND, UINT, WPARAM, LPARAM>(0x004065C0, marni, hWnd, msg, wParam, lParam);
    }

    // 0x00406A10
    static void d3d_error_routine(int errorCode)
    {
        interop::call<void, int>(0x00406A10, errorCode);
    }

    // 0x00406860
    static int query_ddraw2(LPDIRECTDRAW pDD, LPDIRECTDRAW2* lpDD2)
    {
        return pDD->QueryInterface(IID_IDirectDraw2, (LPVOID*)lpDD2);
    }

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

    // 0x00406D90
    static int __stdcall create_device(Marni* self)
    {
        return interop::thiscall<int, Marni*>(0x00406D90, self);
    }

    // 0x00407020
    static int __stdcall create_zbuffer(Marni* self, int width, int height, LPDIRECTDRAWSURFACE* pDDsurfaceZ)
    {
        return interop::thiscall<int, Marni*, int, int, LPDIRECTDRAWSURFACE*>(0x00407020, self, width, height, pDDsurfaceZ);
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
        interop::thiscall<int, Marni*, Prim*>(0x00407480, self, pOt);
    }

    // 0x004074C0
    static void __stdcall trans_matrix(Marni* self, Prim* pPrim)
    {
        interop::thiscall<int, Marni*, Prim*>(0x004074C0, self, pPrim);
    }

    // 0x00408140
    static void __stdcall trans_object(Marni* self, MarniOt* pOt, Prim* pPrim)
    {
        interop::thiscall<int, Marni*, MarniOt*, Prim*>(0x00408140, self, pOt, pPrim);
    }

    static int __stdcall sub_40A4B0(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return interop::thiscall<int, Marni*, Prim*, DrawInfo*>(0x0040A4B0, self, pPrim, drawInfo);
    }
    static int __stdcall sub_40A830(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return interop::thiscall<int, Marni*, Prim*, DrawInfo*>(0x0040A830, self, pPrim, drawInfo);
    }
    static int __stdcall sub_40AB60(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return interop::thiscall<int, Marni*, Prim*, DrawInfo*>(0x0040AB60, self, pPrim, drawInfo);
    }
    // 0x0040B260
    static int __stdcall sub_40B260(Marni* self, Prim* pPrim, void* a3)
    {
        return interop::thiscall<int, Marni*, Prim*, void*>(0x0040B260, self, pPrim, a3);
    }
    static int __stdcall sub_40B560(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return interop::thiscall<int, Marni*, Prim*, DrawInfo*>(0x0040B560, self, pPrim, drawInfo);
    }
    static int __stdcall sub_40B8D0(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return interop::thiscall<int, Marni*, Prim*, DrawInfo*>(0x0040B8D0, self, pPrim, drawInfo);
    }
    static int __stdcall sub_40BCF0(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return interop::thiscall<int, Marni*, Prim*, DrawInfo*>(0x0040BCF0, self, pPrim, drawInfo);
    }
    static int __stdcall sub_40C100(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return interop::thiscall<int, Marni*, Prim*, DrawInfo*>(0x0040C100, self, pPrim, drawInfo);
    }
    static int __stdcall sub_40C470(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return interop::thiscall<int, Marni*, Prim*, DrawInfo*>(0x0040C470, self, pPrim, drawInfo);
    }

    // 0x0040C6E0
    static void __stdcall draw_line_flat(Marni* self, PrimLine2* pPrim)
    {
        interop::thiscall<int, Marni*, PrimLine2*>(0x0040C6E0, self, pPrim);
    }

    // 0x0040C790
    static void __stdcall draw_line_gourad(Marni* self, PrimLine2* pPrim)
    {
        interop::thiscall<int, Marni*, PrimLine2*>(0x0040C790, self, pPrim);
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

    static int __stdcall sub_40CFD0(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return interop::thiscall<int, Marni*, Prim*, DrawInfo*>(0x0040CFD0, self, pPrim, drawInfo);
    }
    static int __stdcall sub_40D300(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return interop::thiscall<int, Marni*, Prim*, DrawInfo*>(0x0040D300, self, pPrim, drawInfo);
    }
    static int __stdcall sub_40D560(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return interop::thiscall<int, Marni*, Prim*, DrawInfo*>(0x0040D560, self, pPrim, drawInfo);
    }
    static int __stdcall sub_40D8D0(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return interop::thiscall<int, Marni*, Prim*, DrawInfo*>(0x0040D8D0, self, pPrim, drawInfo);
    }
    // 0x0040DBA0
    static int __stdcall MarniDrawPolyFT4(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return interop::thiscall<int, Marni*, Prim*, DrawInfo*>(0x0040DBA0, self, pPrim, drawInfo);
    }
    static int __stdcall sub_40DD90(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return interop::thiscall<int, Marni*, Prim*, DrawInfo*>(0x0040DD90, self, pPrim, drawInfo);
    }
    static int __stdcall sub_40DF60(Marni* self, Prim* pPrim, DrawInfo* drawInfo)
    {
        return interop::thiscall<int, Marni*, Prim*, DrawInfo*>(0x0040DF60, self, pPrim, drawInfo);
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

        auto dd2 = (LPDIRECT3DDEVICE2)self->pDirectDevice2;
        dd2->SetCurrentViewport((LPDIRECT3DVIEWPORT2)self->pViewport);
        dd2->SetRenderState(D3DRENDERSTATE_TEXTUREHANDLE, textureHandle);
        dd2->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, drawInfo.zWriteEnable);
        dd2->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_TRUE);
        dd2->SetRenderState(D3DRENDERSTATE_ZFUNC, D3DCMP_LESSEQUAL);
        auto v32 = v41 && (pPrim->type & 0x10000000) == 0;
        dd2->SetRenderState(D3DRENDERSTATE_COLORKEYENABLE, FALSE);
        dd2->SetRenderState(D3DRENDERSTATE_SHADEMODE, drawInfo.shadeMode);
        dd2->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
        dd2->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, drawInfo.specularEnable);
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
        dd2->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, FALSE);
        dd2->SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, D3DTBLEND_MODULATEALPHA);
        set_filtering(self, 0);
        dd2->DrawPrimitive(D3DPT_TRIANGLESTRIP, D3DVT_TLVERTEX, vertices, drawInfo.vertexCount, D3DDP_WAIT);
    LABEL_94:
        if (!gGameTable.error)
            return 1;
        out("", "");
        return 0;
    }

    // 0x0040E770
    static void set_filtering(Marni* self, uint8_t a2)
    {
        interop::thiscall<int, Marni*, uint8_t>(0x0040E770, self, a2);
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
        auto dd2 = (LPDIRECT3DDEVICE2)self->pDirectDevice2;
        set_filtering(self, op->filter);
        dd2->SetCurrentViewport((LPDIRECT3DVIEWPORT2)self->pViewport);
        dd2->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
        dd2->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_TRUE);
        dd2->SetRenderState(D3DRENDERSTATE_COLORKEYENABLE, FALSE);
        dd2->SetRenderState(D3DRENDERSTATE_TEXTUREMAPBLEND, D3DTBLEND_MODULATEALPHA);
        dd2->SetRenderState(D3DRENDERSTATE_TEXTUREHANDLE, op->texture_handle);
        dd2->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, FALSE);
        dd2->SetRenderState(D3DRENDERSTATE_ZFUNC, op->z_func);
        dd2->SetRenderState(D3DRENDERSTATE_SHADEMODE, op->shade_mode);
        dd2->SetRenderState(D3DRENDERSTATE_CULLMODE, op->cull_mode);
        dd2->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, FALSE);
        dd2->SetRenderState(D3DRENDERSTATE_SRCBLEND, op->src_blend);
        dd2->SetRenderState(D3DRENDERSTATE_DESTBLEND, op->dst_blend);
        return dd2->DrawPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, op->vertices, 3, D3DDP_WAIT);
    }

    // 0x0040EE60
    static int invalidate_window(HWND hWnd, int width, int height, int fullscreen, LPRECT lpResRect)
    {
        return interop::call<int, HWND, int, int, int, LPRECT>(0x0040EE60, hWnd, width, height, fullscreen, lpResRect);
    }

    // 0x0040EC10
    static void __stdcall sub_40EC10(Marni* self)
    {
        auto pD3D2 = (LPDIRECT3DDEVICE2)self->pDirectDevice2;
        pD3D2->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, FALSE);
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

        auto pDDsurface = (LPDIRECTDRAWSURFACE)self->pDDsurface;
        auto hr = pDDsurface->QueryInterface(IID_IDirect3DTexture2, (LPVOID*)&pDDtexture);
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

    // 0x0040EF50
    static int ddrawdesc2surfdesc(LPDDSURFACEDESC pDDesc, MarniSurfaceDesc* pDesc)
    {
        return interop::call<int, LPDDSURFACEDESC, MarniSurfaceDesc*>(0x0040EF50, pDDesc, pDesc);
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
        return interop::call<HRESULT, LPDDSURFACEDESC, LPDIRECTDRAWSURFACE>(0x0040F170, lpDDSurfaceDesc, lpDDSurface);
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

    // 0x0040F580
    static void __stdcall surfacey_vrelease(MarniSurface2* self)
    {
        interop::thiscall<int, MarniSurface2*>(0x0040F580, self);
    }

    // 0x0040FEF0
    MarniSurfaceY* __stdcall surfacey_ctor(MarniSurfaceY* self)
    {
        return interop::thiscall<MarniSurfaceY*, MarniSurfaceY*>(0x0040FEF0, self);
    }

    // 0x0040FF20
    void __stdcall surfacey_dtor(MarniSurface2* self)
    {
        self->vtbl = (void**)0x0051737C;
        surfacey_vrelease(self);
        surface2_release(self);
    }

    // 0x00412BD0
    static int __stdcall surface2_vfill(MarniSurface2* self, LPRECT pSrcRect, uint32_t color, int mode)
    {
        return interop::thiscall<int, MarniSurface2*, LPRECT, uint32_t, int>(0x00412BD0, self, pSrcRect, color, mode);
    }

    // 0x00414750
    static int __stdcall surface2_create_work(MarniSurface2* self, int width, int height, int depth, int palBpp, int palCnt)
    {
        return interop::thiscall<int, MarniSurface2*, int, int, int, int, int>(
            0x00414750, self, width, height, depth, palBpp, palCnt);
    }

    // 0x004149D0
    void __stdcall surface2_ctor(MarniSurface2* self)
    {
        std::memset(self, 0, sizeof(*self));
        self->vtbl = (void**)0x005173B0;
    }

    // 0x00414A30
    void __stdcall surface2_release(MarniSurface2* self)
    {
        self->vtbl = (void**)0x005173B0;
        surface2_vrelease(self);
    }

    // 0x00414A40
    void __stdcall surface2_vrelease(MarniSurface2* self)
    {
        interop::thiscall<int, MarniSurface2*>(0x00414A40, self);
    }

    // 0x00414AC0
    static void __stdcall surface3_vrelease(MarniSurface3* self)
    {
        surface2_vrelease(self);
        self->pDDsurface = nullptr;
    }

    // 0x00414AE0
    static void __stdcall surface3_dtor(MarniSurface3* self)
    {
        self->vtbl = (void**)0x005173D4;
        surface3_vrelease(self);
        surface2_release(self);
    }

    // 0x00414B30
    static int __stdcall sub_414B30(MarniMovie* self)
    {
        if (!(self->flag & 0x01))
            return 0;

        return interop::thiscall<int, MarniMovie*>(0x00414B30, self);
    }

    // 0x00414B50
    static int __stdcall movie_update_window(MarniMovie* self)
    {
        return interop::thiscall<int, MarniMovie*>(0x00414B50, self);
    }

    // 0x00414C00
    static int __stdcall movie_update(MarniMovie* self)
    {
        return interop::thiscall<int, MarniMovie*>(0x00414C00, self);
    }

    // 0x00414C80
    static int __stdcall movie_seek(MarniMovie* self)
    {
        return interop::thiscall<int, MarniMovie*>(0x00414C80, self);
    }

    // 0x00414CF0
    static int __stdcall movie_open(
        MarniMovie* self, LPCSTR path, HWND hWnd, LPRECT pRect, LPDIRECTDRAW2 pDD2, LPDIRECTDRAWSURFACE pSurface)
    {
        return interop::thiscall<int, MarniMovie*, LPCSTR, HWND, LPRECT, LPDIRECTDRAW2, LPDIRECTDRAWSURFACE>(
            0x00414CF0, self, path, hWnd, pRect, pDD2, pSurface);
    }

    // 0x00414F50
    static MarniMovie* __stdcall movie_ctor(MarniMovie* self, int mode)
    {
        return interop::thiscall<MarniMovie*, MarniMovie*, int>(0x00414F50, self, mode);
    }

    // 0x00414FC0
    static void __stdcall movie_dtor(MarniMovie* self)
    {
        movie_release(self);
        CoUninitialize();
    }

    // 0x00414FD0
    static void __stdcall movie_release(MarniMovie* self)
    {
        interop::thiscall<int, MarniMovie*>(0x00414FD0, self);
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

            auto pDDsurface = (LPDIRECTDRAWSURFACE2)result->surface->pDDsurface;
            auto pDDpalette = (LPDIRECTDRAWPALETTE)result->surface->pDDpalette[index];
            pDDsurface->SetPalette(pDDpalette);
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
    static void __stdcall sub_416B90(Marni* self, int a2)
    {
        interop::thiscall<int, Marni*, int>(0x00416B90, self, a2);
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
        interop::thiscall<void, MarniConfig*>(0x0050ACB0, self);
    }

    // 0x0050B020
    void config_flush_all(MarniConfig* self)
    {
        interop::thiscall<void, MarniConfig*>(0x0050B020, self);
    }

    // 0x0050B900
    void config_shutdown()
    {
        interop::call(0x00509C70);
        interop::thiscall<void, MarniConfig*>(0x0050ACA0, &gGameTable.marni_config);
    }

    // 0x0050BC60
    static OldStdString* __stdcall oldstring_set_2(OldStdString* self, const char* s)
    {
        return interop::thiscall<OldStdString*, void*, const char*>(0x50BC60, self, s);
    }

    // 0x0050C400
    static OldStdString* __stdcall oldstring_set(OldStdString* self, const std::string& s)
    {
        oldstring_set_2(self, s.c_str());
        return self;
    }

    // 0x0050C550
    static bool __stdcall oldstring_eq(OldStdString* self, const std::string& s)
    {
        if (self->length - 1 != s.size())
            return false;
        return std::memcmp(self->data, s.c_str(), s.size()) == 0;
    }

    // 0x00442CB0
    void set_gpu_flag()
    {
        switch (gGameTable.byte_680592)
        {
        case 0:
        {
            gGameTable.pMarni->gpu_flag &= 0xFFFDFFFF;
            gGameTable.pMarni->gpu_flag &= ~0x40000;
            break;
        }
        case 1:
        {
            gGameTable.pMarni->gpu_flag |= 0x20000;
            gGameTable.pMarni->gpu_flag &= ~0x40000;
            break;
        }
        case 2:
        {
            gGameTable.pMarni->gpu_flag &= ~0x20000;
            gGameTable.pMarni->gpu_flag |= 0x40000;
            break;
        }
        case 3:
        {
            gGameTable.pMarni->gpu_flag |= 0x60000;
            break;
        }
        }
    }

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
        interop::thiscall<void, MarniFont*, MarniSurface*>(0x00411360, self, surface);
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
        interop::hookThisCall(0x00402530, &request_display_mode_count);
        interop::hookThisCall(0x00402940, &restore_surfaces);
        interop::hookThisCall(0x00402A80, &flip);
        interop::hookThisCall(0x00402BC0, &draw);
        interop::hookThisCall(0x00404CE0, &unload_texture);
        interop::hookThisCall(0x00404D20, &clear);
        interop::hookThisCall(0x004050C0, &dtor);
        interop::hookThisCall(0x00405320, &init);
        interop::hookThisCall(0x00406450, &move);
        interop::hookThisCall(0x00407340, &enum_drivers);
        interop::hookThisCall(0x00407440, &create_d3d);
        interop::hookThisCall(0x0040EAF0, &do_draw_op);
        interop::hookThisCall(0x0040ECA0, &surfacex_create_texture_object);
        interop::hookThisCall(0x00416500, &ot_add_primitive_as_z);
        interop::hookThisCall(0x004168F0, &search_texture_object_0_from_1_in_condition);
        interop::hookThisCall(0x00416AF0, &search_texture_object_0_from_1);
        interop::writeJmp(0x00406860, &query_ddraw2);
        interop::writeJmp(0x0040F1A0, &create_ddraw);
        interop::writeJmp(0x0040F2F0, &dd_set_coop_level);
        interop::writeJmp(0x004DBFD0, &out_internal);
        interop::writeJmp(0x00442CB0, &set_gpu_flag);
    }
}
