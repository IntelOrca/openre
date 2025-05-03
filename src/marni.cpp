#include "marni.h"
#include "interop.hpp"
#include "openre.h"
#include "re2.h"

#define WIN32_LEAN_AND_MEAN
#include <d3d.h>
#include <ddraw.h>
#include <windows.h>

namespace openre::marni
{
    namespace GpuFlags
    {
        constexpr uint32_t GPU_7 = 0x80;
        constexpr uint32_t GPU_9 = 0x200;
        constexpr uint32_t GPU_FULLSCREEN = 0x400;
        constexpr uint32_t GPU_13 = 0x2000;
        constexpr uint32_t GPU_19 = 0x80000;
    }

    static int sub_414B30(MarniMovie* self);
    static int
    movie_open(MarniMovie* self, LPCSTR path, HWND hWnd, LPRECT pRect, LPDIRECTDRAW2 pDD2, LPDIRECTDRAWSURFACE pSurface);
    static int movie_seek(MarniMovie* self);
    static int movie_update(MarniMovie* self);
    static int movie_update_window(MarniMovie* self);
    static void movie_release(MarniMovie* self);
    static void __stdcall flip(Marni* self);
    static void surface_fill(MarniSurface* self, int r, int g, int b);

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

    // 0x00414B30
    static int sub_414B30(MarniMovie* self)
    {
        if (!(self->flag & 0x01))
            return 0;

        return interop::thiscall<int, MarniMovie*>(0x00414B30, self);
    }

    // 0x00414CF0
    static int
    movie_open(MarniMovie* self, LPCSTR path, HWND hWnd, LPRECT pRect, LPDIRECTDRAW2 pDD2, LPDIRECTDRAWSURFACE pSurface)
    {
        return interop::thiscall<int, MarniMovie*, LPCSTR, HWND, LPRECT, LPDIRECTDRAW2, LPDIRECTDRAWSURFACE>(
            0x00414CF0, self, path, hWnd, pRect, pDD2, pSurface);
    }

    // 0x00414C80
    static int movie_seek(MarniMovie* self)
    {
        return interop::thiscall<int, MarniMovie*>(0x00414C80, self);
    }

    // 0x00414C00
    static int movie_update(MarniMovie* self)
    {
        return interop::thiscall<int, MarniMovie*>(0x00414C00, self);
    }

    // 0x00414B50
    static int movie_update_window(MarniMovie* self)
    {
        return interop::thiscall<int, MarniMovie*>(0x00414B50, self);
    }

    // 0x00414FD0
    static void movie_release(MarniMovie* self)
    {
        interop::thiscall<int, MarniMovie*>(0x00414FD0, self);
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

    // 0x00432BB0
    void unload_door_texture()
    {
        interop::call(0x00432BB0);
    }

    // 0x00442E40
    bool sub_442E40()
    {
        using sig = bool (*)();
        auto p = (sig)0x00442E40;
        return p();
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

    // 0x00441710
    void flush_surfaces()
    {
        interop::call(0x00441710);
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

    // 0x004065C0
    static void resize(Marni* marni, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        interop::thiscall<int, Marni*, HWND, UINT, WPARAM, LPARAM>(0x004065C0, marni, hWnd, msg, wParam, lParam);
    }

    // 0x004064D0
    static void destroy(Marni* marni)
    {
        interop::thiscall<int, Marni*>(0x004064D0, marni);
    }

    // 0x004063D0
    long message(Marni* self, void* hWnd, uint32_t msg, void* wParam, void* lParam)
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

    // 0x004419A0
    void kill()
    {
        interop::call(0x004419A0);
    }

    // 0x00402500
    bool change_resolution(Marni* self)
    {
        return interop::thiscall<bool, Marni*>(0x00402500, self);
    }

    // 0x0050B220
    void config_flip_filter(MarniConfig* self)
    {
        self->bilinear ^= 1;
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

    static void flip_blt(Marni* self, DWORD width, DWORD height)
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

    // 0x00402530
    static int __stdcall request_display_mode_count(Marni* self)
    {
        if (self->is_gpu_active)
            return self->res_count;

        out("", "Direct3D::RequestDisplayModeCount");
        return 0;
    }

    // 0x00402A80
    static void __stdcall flip(Marni* self)
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

    // 0x00416670
    static uint8_t __stdcall sub_416670(MarniOt* pOt)
    {
        return pOt->var_10;
    }

    // 0x0040E800
    static void __stdcall sub_40E800(Marni* self, uint8_t a2)
    {
        self->field_700C = a2 == 0 ? -1 : 0;
        self->field_8C7010 = a2 == 0 ? -1 : 0;
    }

    // 0x00406A10
    static void d3d_error_routine(int errorCode)
    {
        interop::call<void, int>(0x00406A10, errorCode);
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

    // 0x004164D0
    static Prim* __stdcall ot_get_primitive(MarniOt* self)
    {
        return interop::thiscall<Prim*, MarniOt*>(0x004164D0, self);
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

    // 0x0040B260
    static int __stdcall sub_40B260(Marni* self, Prim* pPrim, void* a3)
    {
        return interop::thiscall<int, Marni*, Prim*, void*>(0x0040B260, self, pPrim, a3);
    }

    // 0x0040E770
    static void set_filtering(Marni* self, uint8_t a2)
    {
        interop::thiscall<int, Marni*, uint8_t>(0x0040E770, self, a2);
    }

    // 0x0040DF70
    static int __stdcall trans_spr_poly(Marni* self, MarniOt* pOt, PrimSprite* pPrim)
    {
        return interop::thiscall<int, Marni*, MarniOt*, Prim*>(0x0040DF70, self, pOt, pPrim);
    }

    // 0x00407480
    static void __stdcall sub_407480(Marni* self, Prim* pOt)
    {
        interop::thiscall<int, Marni*, Prim*>(0x00407480, self, pOt);
    }

    // 0x0040C790
    static void draw_line_gourad(Marni* self, PrimLine2* pPrim)
    {
        interop::thiscall<int, Marni*, PrimLine2*>(0x0040C790, self, pPrim);
    }

    // 0x0040C6E0
    static void draw_line_flat(Marni* self, PrimLine2* pPrim)
    {
        interop::thiscall<int, Marni*, PrimLine2*>(0x0040C6E0, self, pPrim);
    }

    // 0x0040C840
    static void trans_priority_list(Marni* self, MarniOt* pOt)
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

    // 0x0040EAF0
    static void sub_40EAF0(Marni* self, int index)
    {
        interop::thiscall<int, Marni*, int>(0x0040EAF0, self, index);
    }

    static void __stdcall sub_40EC10(Marni* self)
    {
        auto pD3D2 = (LPDIRECT3DDEVICE2)self->pDirectDevice2;
        pD3D2->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, FALSE);
        for (auto i = 0; i < self->field_8C7010; i++)
        {
            sub_40EAF0(self, i);
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

    // 0x00402BC0
    static void __stdcall draw(Marni* self)
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

    // 0x0040F2F0
    static HRESULT dd_set_coop_level(HWND hWnd, int fullscreen, LPDIRECTDRAW pDD)
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

    // 0x004149D0
    void surface2_ctor(MarniSurface2* self)
    {
        std::memset(self, 0, sizeof(*self));
        self->vtbl = (void**)0x005173B0;
    }

    // 0x00414A30
    void surface2_release(MarniSurface2* self)
    {
        self->vtbl = (void**)0x005173B0;
        surface2_vrelease(self);
    }

    // 0x00414A40
    void surface2_vrelease(MarniSurface2* self)
    {
        interop::thiscall<int, MarniSurface2*>(0x00414A40, self);
    }

    // 0x00405EC0
    int create_texture_handle(Marni* self, MarniSurface2* pSrcSurface, uint32_t mode)
    {
        return interop::thiscall<int, Marni*, MarniSurface2*, uint32_t>(0x00405EC0, self, pSrcSurface, mode);
    }

    // 0x004022E0
    static void request_video_memory(Marni* self)
    {
        interop::thiscall<int, Marni*>(0x004022E0, self);
    }

    // 0x00416B90
    static void __stdcall sub_416B90(Marni* self, int a2)
    {
        interop::thiscall<int, Marni*, int>(0x00416B90, self, a2);
    }

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

    void init_hooks()
    {
        interop::hookThisCall(0x00401E40, &prepare_movie);
        interop::hookThisCall(0x00401EF0, &kill_movie);
        interop::hookThisCall(0x00401F00, &sub_401F00);
        interop::hookThisCall(0x00401F10, &syskeydown);
        interop::hookThisCall(0x00401F70, &update_movie);
        interop::hookThisCall(0x00401FD0, &set_movie_resolution);
        interop::hookThisCall(0x00402160, &arrange_object_contents);
        interop::hookThisCall(0x00406450, &move);
        interop::hookThisCall(0x00402530, &request_display_mode_count);
        interop::hookThisCall(0x00402A80, &flip);
        interop::hookThisCall(0x00402BC0, &draw);
        interop::hookThisCall(0x00407440, &create_d3d);
        interop::hookThisCall(0x00407340, &enum_drivers);
        interop::hookThisCall(0x0040ECA0, &surfacex_create_texture_object);
        interop::hookThisCall(0x00404CE0, &unload_texture);
        interop::hookThisCall(0x00416AF0, &search_texture_object_0_from_1);
        interop::hookThisCall(0x004168F0, &search_texture_object_0_from_1_in_condition);
        interop::writeJmp(0x0040F1A0, &create_ddraw);
        interop::writeJmp(0x00406860, &query_ddraw2);
        interop::writeJmp(0x004DBFD0, &out_internal);
        interop::writeJmp(0x0040F2F0, &dd_set_coop_level);
    }
}
