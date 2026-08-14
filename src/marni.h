#pragma once

#include "re2.h"

#include "win_compat.h"

#include <cstdint>

struct Md1;

namespace openre::marni
{
    namespace GpuFlags
    {
        constexpr uint32_t TEXTURE_PAL4 = 0x1;
        constexpr uint32_t TEXTURE_PAL8 = 0x2;
        constexpr uint32_t INCLUDE_2X = 0x4;
        constexpr uint32_t CLEAR_TARGET = 0x8;
        constexpr uint32_t SURFACE_NO_PALETTE = 0x10;
        constexpr uint32_t ENUMERATE_DEVICES = 0x40;
        constexpr uint32_t DEFAULT_SURFACE = 0x80;
        constexpr uint32_t GPU_ENABLED = 0x200;
        constexpr uint32_t GPU_FULLSCREEN = 0x400;
        constexpr uint32_t RGB555 = 0x800;
        constexpr uint32_t SOFTWARE_GPU = 0x2000;
        constexpr uint32_t FILTER_BIT_0 = 0x20000;
        constexpr uint32_t FILTER_BIT_1 = 0x40000;
        constexpr uint32_t PER_PRIM_TEXTURE = 0x80000;
    }

    Marni* __stdcall init(Marni* self, void* hWnd, int width, int height);
    long __stdcall message(Marni* self, void* hWnd, uint32_t msg, void* wParam, void* lParam);
    bool __stdcall change_resolution(Marni* self);
    bool __stdcall toggle_fullscreen(Marni* self);
    int __stdcall create_texture_handle(Marni* self, MarniSurface2* pSrcSurface, uint32_t mode);
    void __stdcall unload_texture(Marni* self, int handle);
    int __stdcall add_primitive_front(Marni* self, Prim* pPrim, int z);
    int __stdcall add_primitive_back(Marni* self, Prim* pPrim, int z);
    int __stdcall add_primitive_scaler(Marni* self, Prim* pPrim, int z);
    void __stdcall draw(Marni* self);
    int __stdcall clear(Marni* self);
    void __stdcall clear_otags(Marni* self);
    void __stdcall flip(Marni* self);
    int __stdcall marni_movie_update(Marni* self);
    int __stdcall request_display_mode_count(Marni* self);
    void __stdcall syskeydown(Marni* self);
    void __stdcall movie_kill(Marni* self);

    MarniSurfaceY* __stdcall surfacey_ctor(MarniSurfaceY* self);
    void __stdcall surfacey_dtor(MarniSurface2* self);
    void __stdcall surface2_ctor(MarniSurface2* self);
    void __stdcall surface2_release(MarniSurface2* self);
    int __stdcall surface2_vrelease(MarniSurface2* self);
    int __stdcall surface2_create_work(MarniSurface2* self, int width, int height, int depth, int palBpp, int palCnt);
    int __stdcall surface2_blt(MarniSurface2* self, RECT* pDstRect, RECT* pSrcRect, MarniSurface2* pSrc, int a5, int a6);

    void config_flip_filter(MarniConfig* self);
    void config_read_all(MarniConfig* self);
    void config_flush_all(MarniConfig* self);
    void config_shutdown();

    void font_trans(MarniFont* self, MarniSurface* surface);
    char* surface_calc_address(MarniSurface* self, int x, int y);

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
    void kill();
    int unload_register_surfaces(int a1);
    int release_object_textures();
    void add_tile(void* primPtr, int z, int is_back);
    void set_gpu_flag();

    void init_hooks();
}
