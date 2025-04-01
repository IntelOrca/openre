#include "marni.h"
#include "interop.hpp"
#include "re2.h"

#include <ddraw.h>
#include <windows.h>

namespace openre::marni
{
    // 0x00443620
    void mapping_tmd(int workNo, Md1* pTmd, int id)
    {
        interop::call<void, int, Md1*, int>(0x00443620, workNo, pTmd, id);
    }

    // 0x004DBFD0
    void out() {}

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

    // 0x00406450
    static void marni_move(Marni* marni)
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

    void marni_init_hooks()
    {
        interop::hookThisCall(0x00406450, marni_move);
    }
}
