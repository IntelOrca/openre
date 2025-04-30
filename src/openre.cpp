#define _CRT_SECURE_NO_WARNINGS

#include "openre.h"
#include "audio.h"
#include "camera.h"
#include "door.h"
#include "enemy.h"
#include "entity.h"
#include "file.h"
#include "hud.h"
#include "input.h"
#include "interop.hpp"
#include "marni.h"
#include "math.h"
#include "player.h"
#include "rdt.h"
#include "re2.h"
#include "scd.h"
#include "sce.h"
#include "scheduler.h"
#include "title.h"

#include <cstring>
#include <windows.h>

using namespace openre;
using namespace openre::audio;
using namespace openre::door;
using namespace openre::enemy;
using namespace openre::file;
using namespace openre::hud;
using namespace openre::math;
using namespace openre::player;
using namespace openre::rdt;
using namespace openre::scd;
using namespace openre::sce;
using namespace openre::input;
using namespace openre::camera;
using namespace openre::title;

namespace openre
{
    GameTable& gGameTable = *((GameTable*)0x00000000);
    uint32_t& gGameFlags = *((uint32_t*)0x989ED0);
    uint16_t& gCurrentStage = *((uint16_t*)0x98EB14);
    uint16_t& gCurrentRoom = *((uint16_t*)0x98EB16);
    uint16_t& gCurrentCut = *((uint16_t*)0x98EB18);
    uint16_t& gLastCut = *((uint16_t*)0x98EB1A);
    uint32_t& gErrorCode = *((uint32_t*)0x680580);
    PlayerEntity& gPlayerEntity = *((PlayerEntity*)0x00989EF0);
    uint16_t& gPoisonStatus = *((uint16_t*)0x0098A108);
    uint8_t& gPoisonTimer = *((uint8_t*)0x0098A10A);
    uint32_t& _memTop = *((uint32_t*)0x988624);

    static uint8_t* _ospBuffer = (uint8_t*)0x698840;
    static uint8_t& _ospMaskFlag = *((uint8_t*)0x6998C0);
    static PlayerEntity*& _em = *((PlayerEntity**)0x689C60);
    static uint32_t& _timerCurrent = *((uint32_t*)0x680570);
    static uint32_t& _timerLast = *((uint32_t*)0x67C9F4);
    static uint32_t& _timer10 = *((uint32_t*)0x68055C);
    static uint8_t& byte_989E7E = *((uint8_t*)0x989E7E);

    // 0x00509C90
    static uint8_t get_player_num()
    {
        return check_flag(FlagGroup::Status, FG_STATUS_PLAYER) ? 1 : 0;
    }

    // 0x0043DF40
    static void sub_43DF40()
    {
        using sig = void (*)();
        auto p = (sig)0x0043DF40;
        p();
    }

    // 0x004CA2F9
    void mess_print(int x, int y, const uint8_t* str, short a4)
    {
        using sig = void (*)(int, int, const uint8_t*, short);
        auto p = (sig)0x004CA2F9;
        p(x, y, str, a4);
    }

    // 0x004427E0
    void update_timer()
    {
        auto time = timeGetTime();
        _timerCurrent = time;
        _timerLast = time;
        _timer10 = time * 10;
    }

    // 0x004DD360
    static void read_osp()
    {
        _ospMaskFlag = 1;
        auto eax = (gCurrentStage * 32) + gCurrentRoom;
        auto edx = (eax * 33) * 128;
        auto bytesRead = read_partial_file_into_buffer("common\\bin\\osp.bin", _ospBuffer, edx, 4224, 4);
        if (bytesRead == 0)
        {
            gErrorCode = bytesRead;
            _ospMaskFlag = 0;
        }
    }

    // 0x004B2A90
    uint8_t rnd()
    {
        auto hi = (uint16_t)gGameTable.dword_988610 >> 7;
        auto lo = (uint16_t)(258 * gGameTable.dword_988610) >> 8;
        gGameTable.dword_988610 = lo | (hi << 8);
        return lo;
    }

    // 0x004DF4D0
    uint8_t rnd_area()
    {
        auto blk = rdt_get_offset<uint16_t>(RdtOffsetKind::BLK);
        auto v = *blk;
        if (v == 0)
            return 0xFF;

        return rnd() % v;
    }

    // 0x00502DB0
    void set_view(const Vec32p& pVp, const Vec32p& pVr)
    {
        using sig = void (*)(const Vec32p&, const Vec32p&);
        auto p = (sig)0x00502DB0;
        p(pVp, pVr);
    }

    // 0x004C4690
    void bg_set_mode(int mode, int rgb)
    {
        using sig = void (*)(int, int);
        auto p = (sig)0x004C4690;
        p(mode, rgb);
    }

    // 0x00451570
    void set_geom_screen(int prj)
    {
        gGameTable.global_prj = prj;
    }

    bool check_flag(FlagGroup group, uint32_t index)
    {
        auto addr = gGameTable.flag_groups[static_cast<uint32_t>(group)];
        return bitarray_get(addr, index) != 0;
    }

    void set_flag(FlagGroup group, uint32_t index, bool value)
    {
        auto addr = gGameTable.flag_groups[static_cast<uint32_t>(group)];
        bitarray_set(addr, index, value);
    }

    static uint16_t st0_xa_leon[96]
        = { 8, 73, 6,  8, 50, 23, 2, 59, 20, 5, 40, 30, 6, 59, 18, 10, 30, 8,  0,  0,  80, 3,  0, 47,
            1, 71, 9,  3, 47, 32, 9, 22, 17, 4, 46, 32, 9, 39, 16, 10, 16, 14, 9,  55, 16, 5,  0, 40,
            7, 54, 26, 5, 70, 9,  2, 0,  59, 8, 0,  25, 9, 0,  22, 6,  0,  30, 10, 0,  16, 7,  0, 27,
            4, 0,  46, 1, 0,  71, 9, 71, 9,  6, 30, 29, 8, 25, 25, 7,  27, 27, 0,  0,  15, 85, 6, 13 };

    static uint16_t st1_xa_claire[160]
        = { 27, 27, 0,  0,  15, 85, 6,  13, 0,  23, 13, 65, 17, 5,  62, 29, 3,  69, 21, 15, 70, 8,  2,  0,  69, 5,  0,
            62, 13, 82, 9,  6,  59, 32, 14, 0,  16, 10, 0,  32, 15, 34, 9,  15, 78, 7,  14, 75, 12, 10, 62, 29, 7,  52,
            37, 7,  0,  52, 12, 76, 14, 13, 23, 21, 9,  80, 10, 1,  71, 18, 9,  0,  40, 8,  46, 41, 4,  0,  63, 3,  0,
            69, 0,  0,  91, 15, 24, 10, 10, 32, 30, 14, 62, 13, 14, 48, 14, 15, 0,  12, 13, 44, 21, 14, 16, 16, 9,  40,
            40, 12, 0,  26, 15, 43, 9,  6,  0,  59, 12, 26, 25, 2,  69, 22, 11, 0,  30, 15, 52, 9,  15, 12, 12, 14, 32,
            16, 4,  63, 27, 8,  0,  46, 1,  0,  71, 15, 61, 9,  11, 30, 29, 12, 51, 25, 11, 59, 27, 0,  0,  0 };

    // 0x00500E00
    static void stage_0()
    {
        auto isClaire = check_flag(FlagGroup::Status, FG_STATUS_PLAYER);
        gGameTable.dword_98883C = isClaire ? &st1_xa_claire[0] : &st0_xa_leon[0];
        task_exit();
    }

    // 0x00500E20
    static void stage_1()
    {
        interop::call(0x00500E20);
    }

    // 0x00500E40
    static void stage_2()
    {
        interop::call(0x00500E40);
    }

    // 0x00500E60
    static void stage_3()
    {
        interop::call(0x00500E60);
    }

    // 0x00500E80
    static void stage_4()
    {
        interop::call(0x00500E80);
    }

    // 0x00500EA0
    static void stage_5()
    {
        interop::call(0x00500EA0);
    }

    // 0x00500EC0
    static void stage_6()
    {
        interop::call(0x00500EC0);
    }

    // 0x004DEF00
    void set_stage()
    {
        gGameTable.dword_988620 = (uint32_t)&gGameTable.work_buffer;

        switch (gGameTable.current_stage)
        {
        case 0: task_execute(2, stage_0); break;
        case 1: task_execute(2, stage_1); break;
        case 2: task_execute(2, stage_2); break;
        case 3: task_execute(2, stage_3); break;
        case 4: task_execute(2, stage_4); break;
        case 5: task_execute(2, stage_5); break;
        case 6: task_execute(2, stage_6); break;
        }

        task_sleep(1);
    }

    // 0x004C89B2
    void show_message(int a0, int a1, int a2, int a3)
    {
        interop::call<void, int, int, int, int>(0x004C89B2, a0, a1, a2, a3);
    }

    void* work_alloc(size_t len)
    {
        auto mem = gGameTable.mem_top;
        gGameTable.mem_top = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mem) + len);
#ifdef DEBUG
        // Fill allocated memory with standard MS uninitialised byte
        // helps track what bytes have not been initialised.
        std::memset(mem, 0xCD, len);
#endif
        return mem;
    }

    // 0x004428F0
    int set_game_seconds(int a0)
    {
        return interop::call<int, int>(0x004428F0, a0);
    }
}

static void load_init_table(void* tempBuffer, uint8_t index)
{
    if (read_file_into_buffer("common\\data\\init_tbl.dat", tempBuffer, 4) == 0)
    {
        file_error();
        return;
    }

    auto src = &((uint8_t*)tempBuffer)[index * 1944];
    std::memcpy(&gGameTable.table_start, src, 1944);
}

// 0x004B7860
static void load_init_table_1()
{
    load_init_table((void*)0x00999AE0, byte_989E7E);
}

// 0x004DE650
static void load_init_table_2()
{
    load_init_table((void*)0x008BD880, 5);
}

// 0x00505B20
static void load_init_table_3()
{
    _memTop = 0x008FF8A0;
    load_init_table((void*)0x008BD880, byte_989E7E);
}

void snd_se_walk(int, int, PlayerEntity* pEm) {}

// 0x00509CF0
bool ck_installkey()
{
    return true;
}

// 0x00432080
static void rsrc_release()
{
    interop::call(0x00432080);
}

// 0x00433830
static void ssclose()
{
    interop::call(0x00433830);
}

// 0x00431000
static void font_create()
{
    interop::call(0x00431000);
}

// 0x004310A0
static void font_delete()
{
    DeleteObject(gGameTable.hFont);
}

// 0x00441DA0
static void wnd_activate()
{
    interop::call(0x00441DA0);
}

// 0x00441D60
static void wnd_deactivate()
{
    interop::call(0x00441D60);
}

// 0x00442800
static INT_PTR CALLBACK about_dialog(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return interop::stdcall<INT_PTR, HWND, UINT, WPARAM, LPARAM>(0x00442800, hWnd, msg, wParam, lParam);
}

// 0x00442750
static void screenshot()
{
    interop::call(0x00442750);
}

// 0x00442C60
static void cursor_op()
{
    interop::call(0x00442C60);
}

// 0x00441A00
LRESULT CALLBACK WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    auto marni = gGameTable.pMarni;
    if (marni != nullptr)
    {
        auto result = marni::message(marni, hWnd, Msg, (void*)wParam, (void*)lParam);
        if (result == 0)
        {
            return 0;
        }
    }
    gGameTable.vk_press &= 0x1F;
    switch (Msg)
    {
    case WM_CREATE: input_init(&gGameTable.input); break;
    case WM_DESTROY:
        gGameTable.hwnd = nullptr;
        rsrc_release();
        ssclose();
        font_delete();
        PostQuitMessage(0);
        return 0;
    case WM_ACTIVATE: wnd_activate(); break;
    case WM_ACTIVATEAPP:
        if (wParam)
            wnd_activate();
        else
            wnd_deactivate();
        break;
    case WM_KILLFOCUS: input_pause(&gGameTable.input); break;
    case WM_CLOSE: marni::kill(); return DefWindowProc(hWnd, Msg, wParam, lParam);
    case WM_KEYUP: input_wmkeyup(&gGameTable.input, wParam); break;
    case WM_KEYDOWN:
        if (lParam & 0x40000000) // last key state?
            break;
        gGameTable.byte_689ABC = 1;
        gGameTable.vk_press |= 0x80;
        switch (wParam)
        {
        case VK_SNAPSHOT:
            screenshot();
            SetFocus(hWnd);
            break;
        case VK_F1: DialogBoxParamA((HINSTANCE)gGameTable.hInstance, (LPCSTR)0xA6, hWnd, about_dialog, 0); break;
        case VK_F4:
            gGameTable.vk_press |= 1; // inventory
            SetFocus(hWnd);
            break;
        case VK_F5:
            gGameTable.vk_press |= 2; // options
            SetFocus(hWnd);
            break;
        case VK_F7: marni::config_flip_filter(&gGameTable.marni_config); break;
        case VK_F8:
            if (!gGameTable.byte_68059B && gGameTable.tasks[1].fn != (void*)0x004BF760 && !gGameTable.movie_r0) // gallery
            {
                if (marni::change_resolution(gGameTable.pMarni))
                {
                    gGameTable.byte_680591 = 120;
                    cursor_op();
                    gGameTable.is_480p = gGameTable.pMarni->xsize != 320;
                    font_create();
                }
                else
                {
                    marni::out("???", "winmain.cpp");
                }
            }
            break;
        case VK_F9:
            gGameTable.vk_press |= 0x40; // exit to menu
            break;
        default:
            input_wmkeydown(&gGameTable.input, wParam);
            SetFocus(hWnd);
            break;
        }
        break;
    default: return DefWindowProc(hWnd, Msg, wParam, lParam);
    }
    return 0;
}

void onAttach()
{
    // interop::writeJmp(0x004DE7B0, &sub_4DE7B0);
    // interop::writeJmp(0x004EDF40, &snd_se_walk);
    // interop::writeJmp(0x00502D40, &read_file_into_buffer);
    // interop::writeJmp(0x00509540, &read_partial_file_into_buffer);
    interop::writeJmp(0x004B7860, load_init_table_1);
    interop::writeJmp(0x004DE650, load_init_table_2);
    interop::writeJmp(0x00505B20, load_init_table_3);
    interop::writeJmp(0x004B2A90, rnd);
    interop::writeJmp(0x00509CF0, ck_installkey);
    interop::writeJmp(0x00441A00, WndProc);

    scheduler_init_hooks();
    title_init_hooks();
    door_init_hooks();
    scd_init_hooks();
    sce_init_hooks();
    player_init_hooks();
    bgm_init_hooks();
    hud_init_hooks();
    input_init_hooks();
    camera_init_hooks();
    enemy_init_hooks();
    file_init_hooks();
    math_init_hooks();
    marni::init_hooks();
}

extern "C" {
__declspec(dllexport) BOOL WINAPI openre_main(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
#pragma comment(linker, "/EXPORT:" __FUNCTION__ "=" __FUNCDNAME__)
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
    // Perform actions based on the reason for calling.
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        // Initialize once for each new process.
        // Return FALSE to fail DLL load.
        onAttach();
        break;

    case DLL_THREAD_ATTACH:
        // Do thread-specific initialization.
        break;

    case DLL_THREAD_DETACH:
        // Do thread-specific cleanup.
        break;

    case DLL_PROCESS_DETACH:
        // Perform any necessary cleanup.
        break;
    }
    return TRUE; // Successful DLL_PROCESS_ATTACH.
}
}
