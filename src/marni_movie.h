#pragma once

#include "re2.h"

#ifdef _WIN32
// Preserved: marni_movie.cpp relies on the lean windows.h surface
// (win_compat.h deliberately does not define WIN32_LEAN_AND_MEAN itself).
#define WIN32_LEAN_AND_MEAN
#endif
#include "win_compat.h"

namespace openre::marni
{
    int __stdcall movie_open(MarniMovie* self, LPCSTR path, HWND hWnd, LPRECT pRect, void* pDraw2, void* pSurface);
    MarniMovie* __stdcall movie_ctor(MarniMovie* self, int mode);
    void __stdcall movie_dtor(MarniMovie* self);
    void __stdcall movie_release(MarniMovie* self);
    int __stdcall movie_seek(MarniMovie* self);
    int __stdcall movie_update(MarniMovie* self);
    int __stdcall movie_update_window(MarniMovie* self);
    int __stdcall sub_414B30(MarniMovie* self);
}
