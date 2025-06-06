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
#include "scheduler.h"
#include "tim.h"
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
    bool gClassicRebirthEnabled;

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

    static const char* windowTitle = "BIOHAZARD(R) 2 PC";

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

    // 0x004FAF80
    uint32_t check_room_no(uint32_t stage, uint32_t room)
    {
        return interop::call<uint32_t, uint32_t, uint32_t>(0x004FAF80, stage, room);
    }

    // 0x00509CE0
    bool cutscene_active()
    {
        return check_flag(FlagGroup::Status, FG_STATUS_CUTSCENE);
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

    // 0x0050AA00
    void* operator_new(const size_t size)
    {
        return interop::call<void*, size_t>(0x0050AA00, size);
    }

    // 0x0050AA10
    void operator_delete(void* memoryBlock)
    {
        interop::call<void*>(0x0050AA10, memoryBlock);
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

    // 0x00441910
    static int cheat_line_cmd0(LPSTR lpCmdLine, int a1)
    {
        return interop::call<int, LPSTR, int>(0x00441910, lpCmdLine, a1);
    }

    // 0x00441890
    static int cheat_line_cmd1(LPSTR lpCmdLine, int a1, int a2)
    {
        return interop::call<int, LPSTR, int, int>(0x00441890, lpCmdLine, a1, a2);
    }

    // 0x0050AA60
    static void config_read()
    {
        marni::config_read_all(&gGameTable.marni_config);
        marni::config_flush_all(&gGameTable.marni_config);
    }

    // 0x0050AA80
    static void config_write()
    {
        marni::config_flush_all(&gGameTable.marni_config);
    }

    // 0x00441880
    static void make_font()
    {
        interop::call(0x00441880);
    }

    // 0x00442920
    static void draw_monitor_effect(int a0)
    {
        interop::call(0x00442920);
    }

    // 0x004DD3B0
    static void psp_trans()
    {
        interop::call(0x004DD3B0);
    }

    // 0x004CD090
    static void om_trans()
    {
        interop::call(0x004CD090);
    }

    // 0x00441DC0
    bool init_instance(HINSTANCE hInstance, HINSTANCE hPrevInstance)
    {
        // Is gGameTable.hwnd a pointer ?
        // reserve window styles
        // Value of CW_USEDEFAULT

        gGameTable.hInstance = hInstance;
        if (!hPrevInstance)
        {
            WNDCLASSA wndClass;
            wndClass.lpfnWndProc = WndProc;
            wndClass.cbClsExtra = 0;
            wndClass.cbWndExtra = 0;
            wndClass.hInstance = hInstance;
            wndClass.hIcon = LoadIconA(hInstance, (LPCSTR)0xA3);
            wndClass.hCursor = LoadCursorA(0, (LPCSTR)0x7F00);
            wndClass.hbrBackground = (HBRUSH)GetStockObject(4);
            wndClass.lpszMenuName = 0;
            wndClass.lpszClassName = windowTitle;
            RegisterClassA(&wndClass);
        }

        DWORD windowStyleFlags = 0x2CA0000u;

        // WS_EX_NOREDIRECTIONBITMAP 0x00200000

        RECT windowRect;
        windowRect.left = 0;
        windowRect.right = 640;
        windowRect.top = 0;
        windowRect.bottom = 480;
        AdjustWindowRect(&windowRect, windowStyleFlags, 0);

        int windowXPos = 0x80000000;
        int windowYPos = 0x80000000;

        gGameTable.hwnd = (void*)CreateWindowExA(
            0,
            windowTitle,
            windowTitle,
            windowStyleFlags,
            windowXPos,
            windowYPos,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            NULL,
            NULL,
            hInstance,
            NULL);

        auto window = (HWND)gGameTable.hwnd;

        ShowWindow(window, WM_SHOWWINDOW);
        SetForegroundWindow(window);
        UpdateWindow(window);

        return true;
    }

    static const char* aHighColor16bit = (const char*)0x00525098;
    static const char* aInNIN = (const char*)0x0052506C;

    int win_main(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
    {
        const char* mutexName = "bio2.658b45ea117473d4.game";

        int timeTable[4] = {};

        gGameTable.hMutex = OpenMutexA(MUTEX_ALL_ACCESS, 0, mutexName);
        if (gGameTable.hMutex)
        {
            gGameTable.error_no = 18;
            // goto LABEL_84
        }
        gGameTable.hMutex = CreateMutexA(0, 0, mutexName);

        marni::out();
        config_read();

        gGameTable.cheat0 = cheat_line_cmd0(lpCmdLine, 9);
        gGameTable.cheat1 = cheat_line_cmd1(lpCmdLine, 11, 1);
        if (cheat_line_cmd1(lpCmdLine, 12, 1) != -1)
        {
            gGameTable.ushinabe = 1;
        }
        SystemParametersInfoA(SPI_GETSCREENSAVEACTIVE, FALSE, &gGameTable.byte_680590, 0);
        if (gGameTable.byte_680590)
        {
            SystemParametersInfoA(SPI_SETSCREENSAVEACTIVE, 0, FALSE, SPIF_SENDWININICHANGE);
        }

        if (init_instance(hInstance, hPrevInstance))
        {
            auto window = (HWND)gGameTable.hwnd;

            ImmAssociateContext(window, NULL);
            auto marniPtr = (Marni*)operator_new(sizeof(Marni));
            if (marniPtr)
            {
                gGameTable.pMarni = marni::init(marniPtr, window, 320, 240);
            }
            if (!gGameTable.pMarni->is_gpu_active)
            {
                gGameTable.error_no = 13;
                DestroyWindow(window);
                window = 0;
            }
            cursor_op();
            gGameTable.pMarni->gpu_flag |= marni::GpuFlags::GPU_3;
            marni::set_gpu_flag();
            if (gGameTable.pMarni->gpu_flag & marni::GpuFlags::GPU_13)
            {
                gGameTable.graphics_ptr_data = 1;
            }
            else
            {
                gGameTable.graphics_ptr_data = (gGameTable.pMarni->gpu_flag & marni::GpuFlags::GPU_3) ? 0 : 2;
            }
            update_timer();

            gGameTable.timer_r2 = 0;

            while (true)
            {
                MSG msg;
                while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE))
                {
                    if (msg.message == WM_QUIT)
                    {
                        break;
                    }

                    TranslateMessage(&msg);
                    DispatchMessageA(&msg);
                }

                timeTable[0] = 166;
                timeTable[1] = 333;
                timeTable[2] = 666;
                timeTable[3] = 166;
                lpCmdLine = (LPSTR)0x1040201;

                auto currentFrameTime = 0;

                if (!gGameTable.timer_r0)
                {
                    break;
                }
                if (!gGameTable.timer_r0 == 1)
                {
                    currentFrameTime = gGameTable.timer_current;
                    goto LABEL_64;
                }

            LABEL_64:
                gGameTable.timer_frame = 0;
                gGameTable.timer_last = currentFrameTime;
                make_font();
                if (gGameTable.pMarni)
                {
                    if (gGameTable.movie_idx)
                    {
                        gGameTable.movie_idx--;
                    }
                    else
                    {
                        if (!gGameTable.byte_6805B4 && !gGameTable.byte_680598)
                        {
                            gGameTable.pMarni->gpu_flag |= marni::GpuFlags::GPU_3;
                        }
                        // 0x004BF760: gallery function
                        if ((uint32_t)gGameTable.tasks[1].fn == 0x004BF760)
                        {
                            gGameTable.byte_680593 = gGameTable.byte_680592;
                            gGameTable.byte_680592 |= 1;
                            marni::set_gpu_flag();
                            gGameTable.byte_680592 = gGameTable.byte_680593;
                        }
                        else
                        {
                            marni::set_gpu_flag();
                            gGameTable.dword_67CA00 = 15872;
                            if (gGameTable.pMarni->xsize == 640)
                            {
                                gGameTable.dword_67CA1C = 0x40000000;
                                gGameTable.dword_67CA18 = 0x40000000;
                            }
                            else
                            {
                                gGameTable.dword_67CA1C = 0x3F800000;
                                gGameTable.dword_67CA18 = 0x3F800000;
                            }
                            gGameTable.dword_67CA04 = gGameTable.global_prj;
                            gGameTable.dword_67CA08 = gGameTable.global_rgb;
                            gGameTable.dword_67CA10 = gGameTable.global_cx + 160;
                            gGameTable.dword_67CA14 = gGameTable.global_cy + 120;
                            marni::add_primitive_scaler(gGameTable.pMarni, gGameTable.scaler, 4095);
                        }

                        marni::clear(gGameTable.pMarni);
                        marni::draw(gGameTable.pMarni);
                        if (gGameTable.can_draw)
                        {
                            draw_monitor_effect(gGameTable.can_draw);
                            marni::clear_otags(gGameTable.pMarni);
                            psp_trans();
                            om_trans();
                        }
                    }
                }
            }
        }

        marni::kill();
        config_write();
        if (gGameTable.byte_680592 == 1)
        {
            gGameTable.byte_680592 = 0;
            ShowCursor(true);
        }
        // TODO
        SystemParametersInfoA(0x11, gGameTable.byte_680592, 0, 2);

        switch (gGameTable.error_no)
        {
        case 0: [[fallthrough]];
        case 1: [[fallthrough]];
        case 2: [[fallthrough]];
        case 11: [[fallthrough]];
        case 18: [[fallthrough]];
        case 255: break;

        case 13:
        {
            MessageBoxA(0, "Failed to initialize DIRECTX(R).", windowTitle, MB_ICONEXCLAMATION);
            break;
        }
        case 16:
        {
            MessageBoxA(0, "Please insert BIOHAZARD(R) 2 PC DISC", windowTitle, MB_ICONEXCLAMATION);
            break;
        }
        case 17:
        {
            MessageBoxA(0, aHighColor16bit, windowTitle, MB_ICONEXCLAMATION);
            break;
        }
        case 19:
        {
            MessageBoxA(0, aInNIN, windowTitle, MB_ICONEXCLAMATION);
            break;
        }
        default:
        {
            MessageBoxA(0, "Fatal error.", windowTitle, MB_ICONEXCLAMATION);
            break;
        }
        }

        marni::config_shutdown();
        if (gGameTable.hMutex)
        {
            CloseHandle((HANDLE)gGameTable.hMutex);
        }

        return gGameTable.error_no;
    }
}

void onAttach()
{
    uint8_t b{};
    interop::readMemory(0x401E40, &b, sizeof(b));
    gClassicRebirthEnabled = (b == 0xE9);

    interop::writeJmp(0x004B7860, load_init_table_1);
    interop::writeJmp(0x004DE650, load_init_table_2);
    interop::writeJmp(0x00505B20, load_init_table_3);
    interop::writeJmp(0x004B2A90, rnd);
    interop::writeJmp(0x00509CF0, ck_installkey);
    interop::writeJmp(0x00441A00, WndProc);
    // interop::writeJmp(0x00441ED0, win_main);

    scheduler_init_hooks();
    title_init_hooks();
    door_init_hooks();
    scd_init_hooks();
    sce_init_hooks();
    player_init_hooks();
    bgm_init_hooks();
    hud_init_hooks();
    camera_init_hooks();
    enemy_init_hooks();
    file_init_hooks();
    math_init_hooks();
    tim::tim_init_hooks();
    if (!gClassicRebirthEnabled)
    {
        input_init_hooks();
        marni::init_hooks();
    }
}

extern "C" {
__declspec(dllexport) BOOL /* WINAPI */ openre_main(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
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
