#pragma once

#include "re2.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <d3d.h>
#include <ddraw.h>
#include <windows.h>

namespace openre::marni
{
    int __stdcall
    movie_open(MarniMovie* self, LPCSTR path, HWND hWnd, LPRECT pRect, LPDIRECTDRAW2 pDD2, LPDIRECTDRAWSURFACE pSurface);
    MarniMovie* __stdcall movie_ctor(MarniMovie* self, int mode);
    void __stdcall movie_dtor(MarniMovie* self);
    void __stdcall movie_release(MarniMovie* self);
    int __stdcall movie_seek(MarniMovie* self);
    int __stdcall movie_update(MarniMovie* self);
    int __stdcall movie_update_window(MarniMovie* self);
    int __stdcall sub_414B30(MarniMovie* self);
}
