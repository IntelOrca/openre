#pragma once

#include "re2.h"

struct Md1;

namespace openre::marni
{
    void mapping_tmd(int workNo, Md1* pTmd, int id);
    void out();
    void out(const char* message, const char* location);
    void unload_door_texture();
    bool sub_442E40();
    void unload_texture_page(int page);
    void door_disp0(int doorId, int a1, int a2, int a3);
    void door_disp1(int doorId);
    void result_unload_textures();
    void flush_surfaces();
    long message(Marni* self, void* hWnd, uint32_t msg, void* wParam, void* lParam);
    void kill();
    bool change_resolution(Marni* self);

    void config_flip_filter(MarniConfig* self);

    void surface2_ctor(MarniSurface2* self);
    void surface2_release(MarniSurface2* self);
    void surface2_vrelease(MarniSurface2* self);

    int create_texture_handle(Marni* self, MarniSurface2* pSrcSurface, uint32_t mode);
    void __stdcall unload_texture(Marni* self, int handle);

    void init_hooks();
}
