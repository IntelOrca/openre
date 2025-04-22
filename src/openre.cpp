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
#include "item.h"
#include "itembox.h"
#include "marni.h"
#include "math.h"
#include "player.h"
#include "rdt.h"
#include "re2.h"
#include "scd.h"
#include "sce.h"
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
using namespace openre::itembox;

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
    static uint8_t& _ospMaskFlag = *((uint8_t*)0x6998C0);
    static PlayerEntity*& _em = *((PlayerEntity**)0x689C60);
    static uint32_t& _taskIndex = *((uint32_t*)0x689F30);
    static uint16_t* _tasks = (uint16_t*)0x68A220;
    static uint16_t* word_68A222 = (uint16_t*)0x68A222;
    static uint8_t* byte_68A233 = (uint8_t*)0x68A233;
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

    // 0x00508CC0
    void task_execute(int index, void* fn)
    {
        interop::call<void, int, void*>(0x00508CC0, index, fn);
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

    enum
    {
        INITIAL_INVENTORY_LEON_OFFSET = 0,
        INITIAL_INVENTORY_CLAIRE_OFFSET = 11,
        INITIAL_INVENTORY_HUNK_OFFSET = 22,
        INITIAL_INVENTORY_TOFU_OFFSET = 33,
        INITIAL_INVENTORY_LEON_EX_BATTLE_OFFSET = 44,
        INITIAL_INVENTORY_CLAIRE_EX_BATTLE_OFFSET = 55,
        INITIAL_INVENTORY_ADA_EX_BATTLE_OFFSET = 66,
        INITIAL_INVENTORY_CHRIS_EX_BATTLE_OFFSET = 77,
    };

    static const InventoryDef _initialInventory[89] = {
        // Leon
        { ITEM_TYPE_HANDGUN_LEON, 18, 0 },
        { ITEM_TYPE_KNIFE, 1, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_LIGHTER, 1, 0 },
        // Claire
        { ITEM_TYPE_HANDGUN_CLAIRE, 13, 0 },
        { ITEM_TYPE_KNIFE, 1, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_LOCKPICK, 1, 0 },
        // Hunk
        { ITEM_TYPE_HANDGUN_LEON, 18, 0 },
        { ITEM_TYPE_SHOTGUN, 5, 0 },
        { ITEM_TYPE_MAGNUM, 8, 0 },
        { ITEM_TYPE_AMMO_HANDGUN, 150, 0 },
        { ITEM_TYPE_AMMO_SHOTGUN, 15, 0 },
        { ITEM_TYPE_AMMO_MAGNUM, 8, 0 },
        { ITEM_TYPE_HERB_GB, 1, 0 },
        { ITEM_TYPE_HERB_GB, 1, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_GVIRUS, 1, 0 },
        // Tofu
        { ITEM_TYPE_KNIFE, 0, 0 },
        { ITEM_TYPE_HERB_G, 1, 0 },
        { ITEM_TYPE_HERB_G, 1, 0 },
        { ITEM_TYPE_HERB_B, 1, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_GVIRUS, 1, 0 },
        // Extreme battle Leon
        { ITEM_TYPE_HANDGUN_LEON, 18, 0 },
        { ITEM_TYPE_SHOTGUN, 5, 0 },
        { ITEM_TYPE_MAGNUM, 8, 0 },
        { ITEM_TYPE_INK_RIBBON, 5, 0 },
        { ITEM_TYPE_FIRST_AID_SPRAY, 1, 0 },
        { ITEM_TYPE_HERB_B, 1, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_LIGHTER, 1, 0 },
        // Extreme battle Claire
        { ITEM_TYPE_GRENADE_LAUNCHER_EXPLOSIVE, 6, 0 },
        { ITEM_TYPE_AMMO_FLAME_ROUNDS, 6, 0 },
        { ITEM_TYPE_AMMO_ACID_ROUNDS, 6, 0 },
        { ITEM_TYPE_INK_RIBBON, 5, 0 },
        { ITEM_TYPE_FIRST_AID_SPRAY, 1, 0 },
        { ITEM_TYPE_HERB_B, 1, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_LOCKPICK, 1, 0 },
        // Extreme battle Ada
        { ITEM_TYPE_SUB_MACHINE_GUN, 100, 1 },
        { ITEM_TYPE_SUB_MACHINE_GUN, 100, 2 },
        { ITEM_TYPE_HANDGUN_COLT_SAA, 6, 0 },
        { ITEM_TYPE_INK_RIBBON, 5, 0 },
        { ITEM_TYPE_BOWGUN, 18, 0 },
        { ITEM_TYPE_HERB_GRB, 1, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_PHOTO_ADA, 1, 0 },
        // Extreme battle Chris
        { ITEM_TYPE_ROCKET_LAUNCHER, 5, 1 },
        { ITEM_TYPE_ROCKET_LAUNCHER, 5, 2 },
        { ITEM_TYPE_CUSTOM_SHOTGUN, 7, 0 },
        { ITEM_TYPE_INK_RIBBON, 5, 0 },
        { ITEM_TYPE_BERETTA, 1, 0 },
        { ITEM_TYPE_FIRST_AID_SPRAY, 1, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_LIGHTER, 1, 0 },
    };

    // 0x00500EE0
    void stage_init_item()
    {
        uint32_t initialInventoryOffset = INITIAL_INVENTORY_LEON_OFFSET;
        bool extremeBattleMode = check_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE);
        bool isLeon = check_flag(FlagGroup::Status, FG_STATUS_PLAYER) == 0;
        bool isClaire = !isLeon;

        auto& inventory = gGameTable.inventory;
        auto& itembox = gGameTable.itembox;

        if (extremeBattleMode)
        {
            switch (gGameTable.word_98EB20)
            {
            case 0: initialInventoryOffset = INITIAL_INVENTORY_LEON_EX_BATTLE_OFFSET; break;
            case 1: initialInventoryOffset = INITIAL_INVENTORY_CLAIRE_EX_BATTLE_OFFSET; break;
            case 2: initialInventoryOffset = INITIAL_INVENTORY_ADA_EX_BATTLE_OFFSET; break;
            case 3: initialInventoryOffset = INITIAL_INVENTORY_CHRIS_EX_BATTLE_OFFSET; break;
            }
        }
        // Normal, arrange and 4th initial inventory offsets
        else
        {
            if (isClaire)
            {
                initialInventoryOffset = INITIAL_INVENTORY_CLAIRE_OFFSET;
            }
            else if (gGameTable.pl.id == PLD_HUNK)
            {
                initialInventoryOffset = INITIAL_INVENTORY_HUNK_OFFSET;
            }
            else if (gGameTable.pl.id == PLD_TOFU)
            {
                initialInventoryOffset = INITIAL_INVENTORY_TOFU_OFFSET;
            }
        }

        auto* itemDef = &_initialInventory[(FULL_INVENTORY_SIZE - 1) + initialInventoryOffset];
        for (uint32_t i = FULL_INVENTORY_SIZE; i > 0; i--)
        {
            set_inventory_item(i - 1, itemDef->Type, itemDef->Quantity, itemDef->Part);
            itemDef--;
        }

        gGameTable.byte_691F6A = _initialInventory[initialInventoryOffset].Type;
        gGameTable.byte_691F68 = 0;
        if (!extremeBattleMode && gGameTable.cheat0 <= 9 && gGameTable.super_hard_switch)
        {
            if (isLeon)
            {
                inventory[0].Quantity = INVENTORY_INFINITE_QUANTITY;
                inventory[0].Part = 0;
                gGameTable.byte_691F68 = 0;

                switch (gGameTable.cheat0)
                {
                case 0:
                {
                    inventory[0].Type = ITEM_TYPE_HANDGUN_LEON;
                    gGameTable.byte_691F6A = ITEM_TYPE_HANDGUN_LEON;
                    break;
                }
                case 1:
                {
                    inventory[0].Type = ITEM_TYPE_CUSTOM_HANDGUN;
                    gGameTable.byte_691F6A = ITEM_TYPE_CUSTOM_HANDGUN;
                    break;
                }
                case 2:
                {
                    inventory[0].Type = ITEM_TYPE_MAGNUM;
                    gGameTable.byte_691F6A = ITEM_TYPE_MAGNUM;
                    break;
                }
                case 3:
                {
                    inventory[0].Type = ITEM_TYPE_CUSTOM_MAGNUM;
                    gGameTable.byte_691F6A = ITEM_TYPE_CUSTOM_MAGNUM;
                    break;
                }
                case 4:
                {
                    inventory[0].Type = ITEM_TYPE_SHOTGUN;
                    gGameTable.byte_691F6A = ITEM_TYPE_SHOTGUN;
                    break;
                }
                case 5:
                {
                    inventory[0].Type = ITEM_TYPE_CUSTOM_SHOTGUN;
                    gGameTable.byte_691F6A = ITEM_TYPE_CUSTOM_SHOTGUN;
                    break;
                }
                case 6:
                {
                    set_inventory_item(0, ITEM_TYPE_FLAMETHROWER, INVENTORY_INFINITE_QUANTITY, 1);
                    set_inventory_item(1, ITEM_TYPE_FLAMETHROWER, INVENTORY_INFINITE_QUANTITY, 2);
                    gGameTable.byte_691F6A = ITEM_TYPE_FLAMETHROWER;
                    break;
                }
                case 7:
                {
                    set_inventory_item(0, ITEM_TYPE_SUB_MACHINE_GUN, INVENTORY_INFINITE_QUANTITY, 1);
                    set_inventory_item(1, ITEM_TYPE_SUB_MACHINE_GUN, INVENTORY_INFINITE_QUANTITY, 2);
                    gGameTable.byte_691F6A = ITEM_TYPE_SUB_MACHINE_GUN;
                    break;
                }
                case 8:
                {
                    set_inventory_item(0, ITEM_TYPE_ROCKET_LAUNCHER, INVENTORY_INFINITE_QUANTITY, 1);
                    set_inventory_item(1, ITEM_TYPE_ROCKET_LAUNCHER, INVENTORY_INFINITE_QUANTITY, 2);
                    gGameTable.byte_691F6A = ITEM_TYPE_ROCKET_LAUNCHER;
                    break;
                }
                case 9:
                {
                    set_inventory_item(0, ITEM_TYPE_GATLING_GUN, INVENTORY_INFINITE_QUANTITY, 1);
                    set_inventory_item(1, ITEM_TYPE_GATLING_GUN, INVENTORY_INFINITE_QUANTITY, 2);
                    gGameTable.byte_691F6A = ITEM_TYPE_GATLING_GUN;
                    break;
                }
                }
            }
            // Claire
            else
            {
                inventory[0].Quantity = -1;
                inventory[0].Part = 0;
                gGameTable.byte_691F68 = 0;

                switch (gGameTable.cheat0)
                {
                case 0:
                {
                    inventory[0].Type = ITEM_TYPE_HANDGUN_CLAIRE;
                    gGameTable.byte_691F6A = ITEM_TYPE_HANDGUN_CLAIRE;
                    break;
                }
                case 1:
                {
                    inventory[0].Type = ITEM_TYPE_GRENADE_LAUNCHER_EXPLOSIVE;
                    gGameTable.byte_691F6A = ITEM_TYPE_GRENADE_LAUNCHER_EXPLOSIVE;
                    break;
                }
                case 2:
                {
                    inventory[0].Type = ITEM_TYPE_GRENADE_LAUNCHER_FLAME;
                    gGameTable.byte_691F6A = ITEM_TYPE_GRENADE_LAUNCHER_FLAME;
                    break;
                }
                case 3:
                {
                    inventory[0].Type = ITEM_TYPE_GRENADE_LAUNCHER_ACID;
                    gGameTable.byte_691F6A = ITEM_TYPE_GRENADE_LAUNCHER_ACID;
                    break;
                }
                case 4:
                {
                    inventory[0].Type = ITEM_TYPE_BOWGUN;
                    gGameTable.byte_691F6A = ITEM_TYPE_BOWGUN;
                    break;
                }
                case 5:
                {
                    inventory[0].Type = ITEM_TYPE_HANDGUN_COLT_SAA;
                    gGameTable.byte_691F6A = ITEM_TYPE_HANDGUN_COLT_SAA;
                    break;
                }
                case 6:
                {
                    set_inventory_item(0, ITEM_TYPE_SPARKSHOT, INVENTORY_INFINITE_QUANTITY, 1);
                    set_inventory_item(1, ITEM_TYPE_SPARKSHOT, INVENTORY_INFINITE_QUANTITY, 2);
                    gGameTable.byte_691F6A = ITEM_TYPE_SPARKSHOT;
                    break;
                }
                case 7:
                {
                    set_inventory_item(0, ITEM_TYPE_SUB_MACHINE_GUN, INVENTORY_INFINITE_QUANTITY, 1);
                    set_inventory_item(1, ITEM_TYPE_SUB_MACHINE_GUN, INVENTORY_INFINITE_QUANTITY, 2);
                    gGameTable.byte_691F6A = ITEM_TYPE_SUB_MACHINE_GUN;
                    break;
                }
                case 8:
                {
                    set_inventory_item(0, ITEM_TYPE_ROCKET_LAUNCHER, INVENTORY_INFINITE_QUANTITY, 1);
                    set_inventory_item(1, ITEM_TYPE_ROCKET_LAUNCHER, INVENTORY_INFINITE_QUANTITY, 2);
                    gGameTable.byte_691F6A = ITEM_TYPE_ROCKET_LAUNCHER;
                    break;
                }
                case 9:
                {
                    set_inventory_item(0, ITEM_TYPE_GATLING_GUN, INVENTORY_INFINITE_QUANTITY, 1);
                    set_inventory_item(1, ITEM_TYPE_GATLING_GUN, INVENTORY_INFINITE_QUANTITY, 2);
                    gGameTable.byte_691F6A = ITEM_TYPE_GATLING_GUN;
                    break;
                }
                }
            }
        }

        if (extremeBattleMode)
        {
            if (gGameTable.word_98EB20 >= 2)
            {
                gGameTable.byte_691F68 = 2;
                gGameTable.byte_691F6A = _initialInventory[initialInventoryOffset + 2].Type;
            }

            // Level determines the amount of ink ribbons
            if (gGameTable.ex_battle_mode == EX_BATTLE_MODE_LEVEL_2)
            {
                inventory[3].Quantity = 3;
            }
            else if (gGameTable.ex_battle_mode == EX_BATTLE_MODE_LEVEL_3)
            {
                inventory[3].Quantity = 1;
            }
            else
            {
                inventory[3].Quantity = 5;
            }
        }

        if (check_flag(FlagGroup::System, FG_SYSTEM_12))
        {
            set_inventory_item(0, ITEM_TYPE_SUB_MACHINE_GUN, INVENTORY_INFINITE_QUANTITY, 1);
            set_inventory_item(1, ITEM_TYPE_SUB_MACHINE_GUN, INVENTORY_INFINITE_QUANTITY, 2);
            gGameTable.byte_691F6A = ITEM_TYPE_SUB_MACHINE_GUN;
            gGameTable.byte_691F68 = 0;
        }

        std::memset(&gGameTable.itembox, 0, sizeof(ItemboxItem) * 64);
        gGameTable.inventory_size = 8;
        gGameTable.dword_98E9C4 = 0;
        bitarray_set(&gGameTable.dword_98E9C4, 0x12);
        bitarray_clr(gGameTable.fg_common, 0x7E);

        std::memset(&gGameTable.inventory_files, 0xFF, 24);

        if (extremeBattleMode)
        {
            set_itembox_item(2, ITEM_TYPE_KNIFE, 1, 0);
        }

        if (check_flag(FlagGroup::Status, FG_STATUS_EASY))
        {
            if (!check_flag(FlagGroup::System, FG_SYSTEM_10) && check_flag(FlagGroup::Status, FG_STATUS_SCENARIO))
            {
                if (isClaire)
                {
                    gGameTable.word_53E1B0 = 1;
                    gGameTable.inventory_files[0] = ITEM_TYPE_FILE_ROOKIE_CLAIRE;
                }
                else
                {
                    gGameTable.word_53E1AC = 1;
                    gGameTable.inventory_files[0] = ITEM_TYPE_FILE_ROOKIE_LEON;
                }
            }

            set_itembox_item(1, ITEM_TYPE_FIRST_AID_SPRAY, 1, 0);
            set_itembox_item(2, ITEM_TYPE_FIRST_AID_SPRAY, 1, 0);
            set_itembox_item(3, ITEM_TYPE_FIRST_AID_SPRAY, 1, 0);
        }

        if (check_flag(FlagGroup::System, FG_SYSTEM_EASY))
        {
            if (check_flag(FlagGroup::System, FG_SYSTEM_12))
            {
                set_inventory_item(0, ITEM_TYPE_SUB_MACHINE_GUN, INVENTORY_INFINITE_QUANTITY, 1);
                set_inventory_item(1, ITEM_TYPE_SUB_MACHINE_GUN, INVENTORY_INFINITE_QUANTITY, 2);
                gGameTable.byte_691F6A = ITEM_TYPE_SUB_MACHINE_GUN;
                gGameTable.byte_691F68 = 0;

                set_itembox_item(0, ITEM_TYPE_GATLING_GUN, INVENTORY_INFINITE_QUANTITY, 3);
                set_itembox_item(4, ITEM_TYPE_ROCKET_LAUNCHER, INVENTORY_INFINITE_QUANTITY, 3);
                set_itembox_item(5, ITEM_TYPE_KNIFE, 1, 0);

                if ((int32_t)gGameTable.fg_status >= 0)
                {
                    set_itembox_item(6, ITEM_TYPE_HANDGUN_LEON, 18, 0);
                }
                else
                {
                    set_itembox_item(6, ITEM_TYPE_HANDGUN_CLAIRE, 13, 0);
                }

                set_itembox_item(7, ITEM_TYPE_AMMO_HANDGUN, 120, 0);
            }
            else
            {
                set_inventory_item(3, ITEM_TYPE_AMMO_HANDGUN, 120, 0);
            }
        }

        if (gGameTable.cheat1 != 0xFF)
        {
            set_inventory_item(3, ITEM_TYPE_NONE, 0, 0);
            set_inventory_item(0, ITEM_TYPE_GATLING_GUN, INVENTORY_INFINITE_QUANTITY, 3);

            // TODO: Figure out if this for Chris
            if (check_flag(FlagGroup::System, FG_SYSTEM_12))
            {
                set_itembox_item(4, ITEM_TYPE_ROCKET_LAUNCHER, 2, 3);
                set_itembox_item(5, ITEM_TYPE_GATLING_GUN, 100, 3);
                set_itembox_item(13, ITEM_TYPE_KNIFE, 1, 0);
            }

            itembox[12].Quantity = 100;
            itembox[12].Part = 3;

            if ((int32_t)gGameTable.fg_status >= 0)
            {
                set_itembox_item(6, ITEM_TYPE_HANDGUN_LEON, 18, 0);
                set_itembox_item(8, ITEM_TYPE_MAGNUM, 8, 0);
                set_itembox_item(9, ITEM_TYPE_CUSTOM_MAGNUM, 8, 0);
                set_itembox_item(10, ITEM_TYPE_SHOTGUN, 5, 0);
                set_itembox_item(11, ITEM_TYPE_CUSTOM_SHOTGUN, 7, 0);
                itembox[12].Type = ITEM_TYPE_SPARKSHOT;
            }
            else
            {
                set_itembox_item(6, ITEM_TYPE_HANDGUN_CLAIRE, 13, 0);
                set_itembox_item(8, ITEM_TYPE_GRENADE_LAUNCHER_FLAME, 6, 0);
                set_itembox_item(9, ITEM_TYPE_GRENADE_LAUNCHER_ACID, 6, 0);
                set_itembox_item(10, ITEM_TYPE_BOWGUN, 18, 0);
                set_itembox_item(11, ITEM_TYPE_HANDGUN_COLT_SAA, 6, 0);
                itembox[12].Type = ITEM_TYPE_SPARKSHOT;
            }

            set_itembox_item(7, ITEM_TYPE_AMMO_HANDGUN, 120, 0);
        }

        if (!extremeBattleMode)
        {
            if (gGameTable.cheat0 > 9)
            {
                return;
            }

            if (gGameTable.super_hard_switch)
            {
                if (gGameTable.cheat0 > 5)
                {
                    set_itembox_item(1, ITEM_TYPE_KNIFE, 1, 0);
                }

                itembox[0].Type = isClaire ? ITEM_TYPE_HANDGUN_CLAIRE : ITEM_TYPE_HANDGUN_LEON;
                itembox[0].Quantity = 13;
            }
        }
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

void onAttach()
{
    interop::writeJmp(0x004B7860, &load_init_table_1);
    interop::writeJmp(0x004DE650, &load_init_table_2);
    interop::writeJmp(0x00505B20, &load_init_table_3);
    interop::writeJmp(0x004B2A90, &rnd);

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
