#define _CRT_SECURE_NO_WARNINGS

#include "openre.h"
#include "audio.h"
#include "camera.h"
#include "door.h"
#include "enemy.h"
#include "file.h"
#include "hud.h"
#include "input.h"
#include "interop.hpp"
#include "player.h"
#include "re2.h"
#include "scd.h"
#include "sce.h"
#include <cassert>
#include <cstring>
#include <windows.h>

using namespace openre;
using namespace openre::audio;
using namespace openre::door;
using namespace openre::enemy;
using namespace openre::file;
using namespace openre::hud;
using namespace openre::player;
using namespace openre::scd;
using namespace openre::sce;
using namespace openre::input;
using namespace openre::camera;

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
    Unknown68A204*& dword_68A204 = *((Unknown68A204**)0x68A204);

    static uint8_t* _ospBuffer = (uint8_t*)0x698840;
    static char* _rdtPathBuffer = (char*)0x689C20;
    static const char* _stageSymbols = "123456789abcdefg";

    static uint8_t& _graphicsPtr = *((uint8_t*)0x524EB9);
    static uint8_t& _ospMaskFlag = *((uint8_t*)0x6998C0);
    static PlayerEntity*& _em = *((PlayerEntity**)0x689C60);
    static uint32_t& _taskIndex = *((uint32_t*)0x689F30);
    static uint16_t* _tasks = (uint16_t*)0x68A220;
    static uint16_t* word_68A222 = (uint16_t*)0x68A222;
    static uint8_t* byte_68A233 = (uint8_t*)0x68A233;
    static uint32_t& dword_98861C = *((uint32_t*)0x98861C);
    static uint32_t& dword_988620 = *((uint32_t*)0x988620);
    static uint16_t& word_98A616 = *((uint16_t*)0x98A616);
    static uint16_t& word_98A61A = *((uint16_t*)0x98A61A);
    static uint16_t& word_98EB24 = *((uint16_t*)0x98EB24);
    static void*& byte_98861C = *((void**)0x98861C);
    static uint32_t& dword_98862C = *((uint32_t*)0x98862C);
    static uint32_t& _rdtnCount = *((uint32_t*)0x689C10);
    static uint32_t& dword_989E68 = *((uint32_t*)0x989E68);
    static uint8_t& byte_989E7D = *((uint8_t*)0x989E7D);
    static uint8_t& byte_989EEB = *((uint8_t*)0x989EEB);
    static void** dword_98A110 = (void**)0x98A110;
    static ObjectEntity*& dword_98E51C = *((ObjectEntity**)0x98E51C);
    static char* dword_98E544 = (char*)0x98E544;
    static uint16_t& word_98E78C = *((uint16_t*)0x98E78C);
    static uint32_t& dword_98E798 = *((uint32_t*)0x98E798);
    static uint16_t& word_989EE8 = *((uint16_t*)0x989EE8);
    static ObjectEntity* _om = (ObjectEntity*)0x98A61C;
    static uint8_t& byte_99270F = *((uint8_t*)0x99270F);
    static uint32_t& _timerCurrent = *((uint32_t*)0x680570);
    static uint32_t& _timerLast = *((uint32_t*)0x67C9F4);
    static uint32_t& _timer10 = *((uint32_t*)0x68055C);
    static uint8_t& byte_989E7E = *((uint8_t*)0x989E7E);

    // 0x00509C90
    static uint8_t get_player_num()
    {
        return check_flag(FlagGroup::Status, FG_STATUS_PLAYER) ? 1 : 0;
    }

    static void get_rdt_path(char* buffer, uint8_t player, uint8_t stage, uint8_t room)
    {
        auto stageSym = _stageSymbols[(dword_98E798 & 0xFF) + gCurrentStage];
        std::sprintf(buffer, "Pl%d\\Rdt\\room%c%02x%d.rdt", player, stageSym, room, player);
    }

    // 0x004EC9C0
    static void snd_bgm_set()
    {
        using sig = void (*)();
        auto p = (sig)0x004EC9C0;
        p();
    }

    // 0x004450C0
    static void sub_4450C0()
    {
        using sig = void (*)();
        auto p = (sig)0x004450C0;
        p();
    }

    // 0x0043DF40
    static void sub_43DF40()
    {
        using sig = void (*)();
        auto p = (sig)0x0043DF40;
        p();
    }

    // 0x00508CE0
    void task_sleep(int frames)
    {
        auto eax = _taskIndex * 36;
        word_68A222[eax / 2] = frames;
        _tasks[eax / 2] = 1;
        byte_68A233[eax] = 1;
    }

    // 0x00508D10
    void task_exit()
    {
        interop::call(0x00508D10);
    }

    // 0x004C4AD0
    bool fade_status(int no)
    {
        using sig = int (*)(int);
        auto p = (sig)0x004C4AD0;
        return (p(no) & 0xFF) != 0;
    }

    // 0x004C49C0
    void fade_set(short a0, short add, char mask, char pri)
    {
        using sig = void (*)(short, short, char, char);
        auto p = (sig)0x004C49C0;
        p(a0, add, mask, pri);
    }

    // 0x004C4A50
    void fade_adjust(int no, short kido, int rgb, PsxRect* rect)
    {
        using sig = void (*)(int, short, int, PsxRect*);
        auto p = (sig)0x004C4A50;
        p(no, kido, rgb, rect);
    }

    // 0x004CA2F9
    void mess_print(int x, int y, const uint8_t* str, short a4)
    {
        using sig = void (*)(int, int, const uint8_t*, short);
        auto p = (sig)0x004CA2F9;
        p(x, y, str, a4);
    }

    // 0x004427E0
    static void update_timer()
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

    // 0x004DE7B0
    void room_set()
    {
        while (true)
        {
            switch (dword_68A204->var_0D)
            {
            case 0:
            {
                auto buffer2 = (char*)0x00689C40;
                if (_graphicsPtr == 0 || _graphicsPtr == 2)
                {
                    std::strcpy(buffer2, "common\\data\\font1.adt");
                }
                else if (_graphicsPtr == 1)
                {
                    std::strcpy(buffer2, "common\\data\\font1.tim");
                }
                _em = &gPlayerEntity;
                get_rdt_path(_rdtPathBuffer, get_player_num(), (dword_98E798 & 0xFF) + gCurrentStage, gCurrentRoom & 0xFF);

                switch (gCurrentStage)
                {
                case 0:
                    word_98E78C = 0;
                    dword_989E68 = 0;
                    gGameFlags &= 0xFFF04000;
                    _em->pOn_om = 0;
                    _em->status_flg &= 0xF9FF;
                    _memTop = dword_988620;
                    dword_98861C = dword_988620;
                    dword_68A204->var_0D = 10;
                    break;
                }
                break;
            }
            case 2:
                dword_68A204->var_0D = 3;
                byte_99270F = 0;
                task_sleep(1);
                break;
            case 3:
                if (gCurrentStage == byte_989E7D)
                {
                    dword_68A204->var_0D = 5;
                }
                else
                {
                    // loc_4DEBC4
                }
                break;
            case 4:
            case 5:
                word_989EE8 = 3333;
                read_osp();
                if (read_file_into_buffer(_rdtPathBuffer, byte_98861C, 8) == 0)
                {
                    file_error();
                }
                // loc_4DECB9
                break;
            case 10:
                snd_bgm_set();
                if (dword_68A204->var_13 == 0)
                {
                    sub_4450C0();
                    dword_98862C = 0x0098A114;
                    byte_989EEB = 0;
                    for (auto i = 0; i < 33; i++)
                    {
                        dword_98A110[i] = dword_98E544;
                    }
                    _rdtnCount = 0;
                    word_98A616 = 0;
                    word_98A61A = 0;
                    sub_43DF40();
                    dword_98E51C = _om;
                    _rdtnCount = 32;
                    for (auto i = 0; i < 32; i++)
                    {
                        _om->be_flg = 0;
                    }
                    if (_em->id == word_98EB24)
                    {
                        dword_68A204->var_0D = 2;
                    }
                    else
                    {
                        // loc_4DEA4E
                    }
                }
                break;
            default: return;
            }
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

    // 0x00502D90
    void mapping_tmd(int a1, void* pTmd, int page, int clut)
    {
        using sig = void (*)(int, void*, int, int);
        auto p = (sig)0x00502D90;
        p(a1, pTmd, page, clut);
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

    void* work_alloc(size_t len)
    {
        auto mem = gGameTable.mem_top;
        gGameTable.mem_top = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mem) + len);
#ifdef DEBUG
        std::memset(mem, 0xCD, len);
#endif
        return mem;
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

    door_init_hooks();
    scd_init_hooks();
    sce_init_hooks();
    player_init_hooks();
    bgm_init_hooks();
    hud_init_hooks();
    input_init_hooks();
    camera_init_hooks();
    enemy_init_hooks();
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
