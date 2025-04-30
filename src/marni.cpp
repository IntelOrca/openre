#include "marni.h"
#include "interop.hpp"
#include "re2.h"

#define WIN32_LEAN_AND_MEAN
#include <ddraw.h>
#include <windows.h>

namespace openre::marni
{
    // 0x00443620
    void mapping_tmd(int workNo, Md1* pTmd, int id)
    {
        interop::call<void, int, Md1*, int>(0x00443620, workNo, pTmd, id);
    }

    void out()
    {
    }

    // 0x004DBFD0
    void out(const char* message, const char* location)
    {
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
        interop::call<void, int>(0x0043F550, page);
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
    static void move(Marni* marni)
    {
        HWND window = (HWND)marni->hWnd;
        RECT rect = {};
        rect.left = marni->window_rect[0];
        rect.top = marni->window_rect[1];
        rect.right = marni->window_rect[2];
        rect.bottom = marni->window_rect[3];

        POINT point0 = {};
        ClientToScreen(window, &point0);
        POINT point1 = {};
        point1.x = marni->resolutions[marni->modes].width;
        point1.y = marni->resolutions[marni->modes].height;
        ClientToScreen(window, &point1);

        SetRect(&rect, point0.x, point0.y, point1.x, point1.y);
        marni->window_rect[0] = rect.left;
        marni->window_rect[1] = rect.top;
        marni->window_rect[2] = rect.right;
        marni->window_rect[3] = rect.bottom;
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

    // 0x00401F10
    static void syskeydown(Marni* marni)
    {
        interop::thiscall<int, Marni*>(0x00401F10, marni);
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
            if ((self->var_8C83F4 & 0x0400) != 0)
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

    void init_hooks()
    {
        interop::hookThisCall(0x00406450, &move);
    }
}
