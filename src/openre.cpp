#include "openre.h"
#include "audio.h"
#include "camera.h"
#include "door.h"
#include "enemy.h"
#include "entity.h"
#include "error.h"
#include "file.h"
#include "hud.h"
#include "input.h"
#include "interop.hpp"
#include "item.h"
#include "itembox.h"
#include "logger.h"
#include "marni.h"
#include "marni_config.h"
#include "math.h"
#include "player.h"
#include "rdt.h"
#include "re2.h"
#include "scd.h"
#include "sce.h"
#include "scheduler.h"
#include "tim.h"
#include "title.h"
#include "window.h"
#include <ddraw.h>

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
using namespace openre::marni;
using namespace openre::scd;
using namespace openre::sce;
using namespace openre::input;
using namespace openre::camera;
using namespace openre::title;
using namespace openre::itembox;
using namespace openre::error;

namespace openre
{
    bool gClassicRebirthEnabled;

    GameTable& gGameTable = *((GameTable*)0x00000000);

    static int g_speed_multiplier = 1;

    static const char* windowTitle = "BIOHAZARD(R) 2 PC";
    static const char* fontFaceName = "ＭＳ ゴシック";

    // 0x00509C90
    uint8_t get_player_num()
    {
        return check_flag(FlagGroup::Status, FG_STATUS_PLAYER) ? 1 : 0;
    }

    /// ADT palette data offset within work_buffer (after header + CLUT offsets)
    constexpr int kAdtPaletteOffset = 0x20014;
    /// Size of high-color palette data in bytes
    constexpr int kPaletteSize = 0x5C;

    /// Tile display code flag: enable textured drawing
    constexpr uint8_t kTileCodeDraw = 2;

    // 0x005007B0
    static void pc_credits()
    {
        interop::call(0x005007B0);
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
        gGameTable.timer_current = time;
        gGameTable.timer_last = time;
        gGameTable.timer_10 = time * 10;
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
        gGameTable.osp_mask_flag = 1;
        auto eax = (gGameTable.current_stage * 32) + gGameTable.current_room;
        auto edx = (eax * 33) * 128;
        auto bytesRead = read_partial_file_into_buffer("common\\bin\\osp.bin", gGameTable.psp_lookup, edx, 4224, 4);
        if (bytesRead == 0)
        {
            gGameTable.error_no = bytesRead;
            gGameTable.osp_mask_flag = 0;
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

    bool check_flags(FlagGroup group, std::vector<uint32_t> indexes)
    {
        for (auto index : indexes)
        {
            if (!check_flag(group, index))
            {
                return false;
            }
        }
        return true;
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
    void show_message(int pos_xy, int attr, int mess_no, int stop_data)
    {
        if (gGameTable.fg_message < 0)
            return;
        gGameTable.fg_message = static_cast<int8_t>(0x80);
        {
            auto v5 = gGameTable.fg_status;
            v5 = (v5 & 0xFFFFFF00) | (static_cast<uint8_t>(v5) | 0x40);
            gGameTable.fg_status = v5;
        }
        gGameTable.mess_fg_stop = gGameTable.fg_stop;
        gGameTable.mess_stop = stop_data;
        gGameTable.mess_buf_a = 0;
        gGameTable.mess_attr = 3;

        switch (attr & 0x300)
        {
        case 0x100:
            gGameTable.mess_pos_x = 34;
            gGameTable.mess_pos_y = 185;
            gGameTable.mess_buf_b = 0x80;
            gGameTable.mess_attr |= 0x2000;
            gGameTable.mess_buf_ptr = reinterpret_cast<char*>(&gGameTable.misc_text_j[gGameTable.misc_ptr_j[mess_no]]);
            break;
        case 0x200:
            gGameTable.mess_pos_x = 34;
            gGameTable.mess_pos_y = 186;
            gGameTable.mess_buf_b = 0x80;
            gGameTable.mess_attr |= 0x400;
            gGameTable.mess_buf_ptr = reinterpret_cast<char*>(&gGameTable.desc_txt_j[gGameTable.desc_ptr_j[mess_no]]);
            break;
        case 0x300:
            gGameTable.mess_pos_x = 34;
            gGameTable.mess_pos_y = 185;
            gGameTable.mess_buf_b = 0x80;
            gGameTable.mess_attr |= 0x800;
            {
                auto* base = reinterpret_cast<char*>(gGameTable.rdt->offsets[13]);
                gGameTable.mess_buf_ptr = base + *(uint16_t*)(base + 2 * mess_no);
            }
            break;
        default:
            gGameTable.mess_pos_x = static_cast<int16_t>(pos_xy);
            gGameTable.mess_pos_y = static_cast<int16_t>((pos_xy & 0xFFFF0000) >> 16);
            gGameTable.mess_buf_b = static_cast<uint8_t>((attr & 0x8000) >> 8);
            gGameTable.mess_attr = static_cast<uint16_t>(attr);

            auto v6 = attr & 0xC00;
            if (v6 > 0x800)
            {
                if (v6 == 0xC00)
                    gGameTable.mess_buf_ptr = reinterpret_cast<char*>(static_cast<uintptr_t>(mess_no));
            }
            else if (v6 == 0x800)
            {
                auto* base = reinterpret_cast<char*>(gGameTable.rdt->offsets[13]);
                gGameTable.mess_buf_ptr = base + *(uint16_t*)(base + 2 * mess_no);
            }
            else if ((attr & 0xC00) != 0)
            {
                if (v6 == 0x400)
                    gGameTable.mess_buf_ptr = reinterpret_cast<char*>(&gGameTable.desc_txt_j[gGameTable.desc_ptr_j[mess_no]]);
            }
            else
            {
                gGameTable.mess_buf_ptr = reinterpret_cast<char*>(&gGameTable.misc_text_j[gGameTable.misc_ptr_j[mess_no]]);
            }
            break;
        }
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

    // 0x004E97C0
    void vsync() {}

    // 0x004DBFD0
    static void marni_out() {}

    // 0x004C4FF0
    static void load_disclaimer()
    {
        enum
        {
            kLoadAdt = 0,
            kWaitFadeIn = 4,
            kWaitInput = 5,
            kFadeOut = 6,
        };

        auto& task = *gGameTable.ctcb;
        switch (task.var_08)
        {
        case kLoadAdt:
            gGameTable.byte_98F1B7 = 1;
            bg_set_mode(0, 0);
            if (!load_adt("common\\data\\gw2.adt", gGameTable.bg_buffer, 4))
            {
                file_error();
                return;
            }
            title::bg_to_surface(gGameTable.bg_buffer);
            hud_fade_set(512, -1024, 7, 0);
            task.var_08 = kWaitFadeIn;
            [[fallthrough]];

        case kWaitFadeIn:
            if (!hud_fade_status(0))
            {
                task_sleep(1);
                return;
            }
            gGameTable.byte_98F1B9 = 1;
            task.var_08 = kWaitInput;
            [[fallthrough]];

        case kWaitInput:
            if (gGameTable.byte_98F1B9 != 2)
            {
                task_sleep(1);
                return;
            }
            hud_fade_set(512, 1024, 7, 0);
            task.var_08 = kFadeOut;
            [[fallthrough]];

        case kFadeOut:
            if (hud_fade_status(0))
            {
                gGameTable.byte_98F1B7 = 0;
                bg_set_mode(2, 0);
                gGameTable.byte_98F1B9 = 0;
                if (gGameTable.ushinabe)
                {
                    pc_credits();
                }
                task_exit();
            }
            else
            {
                task_sleep(1);
            }
            return;

        default: return;
        }
    }

    // Graphics rendering mode for font/texture loading
    enum GraphicsMode : uint8_t
    {
        kGfxSoft16bit = 0, // ADT fonts + espcore.bin textures (software 16-bit)
        kGfxHardware = 1,  // Raw TIM files (hardware acceleration)
        kGfxSoft8bit = 2,  // ADT fonts + ADT textures (software 8-bit)
    };

    // 0x004C4000
    static void init_main()
    {
        enum
        {
            kInitAudio = 0,
            kInitDisclaimer = 1,
            kInitFontsAndTex = 2,
        };

        auto& task = *gGameTable.ctcb;
        switch (task.var_08)
        {
        case kInitAudio:
            snd_sys_init();
            marni_out();
            bg_set_mode(2, 0);
            for (int row = 0; row < 4; row++)
            {
                for (int col = 0; col < 2; col++)
                {
                    auto& tile = gGameTable.fade_table[row].tiles[col];
                    tile.psxRect.x = 0;
                    tile.psxRect.y = 0;
                    tile.psxRect.w = 320;
                    tile.psxRect.h = 240;
                    tile.code |= kTileCodeDraw;
                    tile.r = 0;
                    tile.g = 0;
                    tile.b = 0;
                }
            }
            task.var_08 = kInitDisclaimer;
            task_sleep(1);
            break;

        case kInitDisclaimer:
            gGameTable.byte_98F1B9 = 0;
            task_execute(1, load_disclaimer);
            task.var_08 = kInitFontsAndTex;
            [[fallthrough]];

        case kInitFontsAndTex:
            if (!gGameTable.byte_98F1B9)
            {
                task_sleep(1);
                return;
            }
            marni_out();
            // Initialize display list tiles
            {
                auto* v1 = gGameTable.byte_52D8A7;
                auto* v0 = gGameTable.byte_52D8E7;
                do
                {
                    for (int i = 0; i < 2; i++)
                    {
                        *v1 |= kTileCodeDraw;
                        v1 += 16;
                    }
                    auto v3 = *v0;
                    *v0 = v3 | kTileCodeDraw;
                    v0 += 16;
                } while (v1 < gGameTable.byte_52D8E7);
            }
            // Load fonts depending on graphics mode
            if (gGameTable.graphics_ptr_data == kGfxSoft16bit)
            {
                if (!load_adt("common\\data\\font0.adt", gGameTable.work_buffer, 4))
                    goto error;
                std::memcpy(gGameTable.font_rgb, &gGameTable.work_buffer[kAdtPaletteOffset], 15);
                tim_buffer_to_surface(reinterpret_cast<int*>(gGameTable.work_buffer), 8, 1);
                if (!load_adt("common\\data\\font1.adt", gGameTable.work_buffer, 4))
                    goto error;
                tim_buffer_to_surface(reinterpret_cast<int*>(gGameTable.work_buffer), 9, 1);
            }
            else if (gGameTable.graphics_ptr_data == kGfxHardware)
            {
                if (!read_file_into_buffer("common\\data\\font0.tim", gGameTable.work_buffer, 4))
                    goto error;
                tim_buffer_to_surface(reinterpret_cast<int*>(gGameTable.work_buffer), 8, 1);
                if (!read_file_into_buffer("common\\data\\font1.tim", gGameTable.work_buffer, 4))
                    goto error;
                tim_buffer_to_surface(reinterpret_cast<int*>(gGameTable.work_buffer), 9, 1);
            }
            else if (gGameTable.graphics_ptr_data == kGfxSoft8bit)
            {
                if (!load_adt("common\\data\\font0.adt", gGameTable.work_buffer, 4))
                    goto error;
                std::memcpy(gGameTable.font_rgb, &gGameTable.work_buffer[kAdtPaletteOffset], 15);
                tim_buffer_to_surface(reinterpret_cast<int*>(gGameTable.work_buffer), 8, 1);
                if (!load_adt("common\\data\\font1.adt", gGameTable.work_buffer, 4))
                    goto error;
                tim_buffer_to_surface(reinterpret_cast<int*>(gGameTable.work_buffer), 9, 1);
            }

            gGameTable.stage_bk = 0;

            // Load textures depending on graphics mode
            if (gGameTable.graphics_ptr_data == kGfxSoft16bit)
            {
                // TIM sub-image offsets within espcore.bin loaded into work_buffer
                constexpr int kTimOffset0 = 0;
                constexpr int kTimOffset1 = 5416;
                constexpr int kTimOffset2 = 8016;
                constexpr int kTimOffset3 = 10872;
                constexpr int kTimOffset4 = 13480;
                constexpr int kTimOffset5 = 14776;
                if (!read_file_into_buffer("common\\data\\espcore.bin", gGameTable.work_buffer, 4))
                    goto error;
                tim_buffer_to_surface(reinterpret_cast<int*>(gGameTable.work_buffer + kTimOffset0), 10, 0);
                tim_buffer_to_surface(reinterpret_cast<int*>(gGameTable.work_buffer + kTimOffset1), 11, 0);
                tim_buffer_to_surface(reinterpret_cast<int*>(gGameTable.work_buffer + kTimOffset2), 12, 0);
                tim_buffer_to_surface(reinterpret_cast<int*>(gGameTable.work_buffer + kTimOffset3), 13, 0);
                tim_buffer_to_surface(reinterpret_cast<int*>(gGameTable.work_buffer + kTimOffset4), 14, 0);
                tim_buffer_to_surface(reinterpret_cast<int*>(gGameTable.work_buffer + kTimOffset5), 15, 0);
            }
            else if (gGameTable.graphics_ptr_data == kGfxHardware)
            {
                if (!read_file_into_buffer("common\\data\\tex2p.tim", gGameTable.work_buffer, 4))
                    goto error;
                tim_buffer_to_surface(reinterpret_cast<int*>(gGameTable.work_buffer), 10, 0);
                if (!read_file_into_buffer("common\\data\\tex3p.tim", gGameTable.work_buffer, 4))
                    goto error;
                tim_buffer_to_surface(reinterpret_cast<int*>(gGameTable.work_buffer), 11, 0);
            }
            else if (gGameTable.graphics_ptr_data == kGfxSoft8bit)
            {
                if (!load_adt("common\\data\\tex216.adt", gGameTable.work_buffer, 4))
                    goto error;
                std::memcpy(gGameTable.byte_992BE0, &gGameTable.work_buffer[kAdtPaletteOffset], kPaletteSize);
                tim_buffer_to_surface(reinterpret_cast<int*>(gGameTable.work_buffer), 10, 0);
                if (!load_adt("common\\data\\tex316.adt", gGameTable.work_buffer, 4))
                    goto error;
                std::memcpy(gGameTable.byte_992C40, &gGameTable.work_buffer[kAdtPaletteOffset], kPaletteSize);
                tim_buffer_to_surface(reinterpret_cast<int*>(gGameTable.work_buffer), 11, 0);
            }

            gGameTable.byte_989E7D = 0;
            gGameTable.byte_9888D9 = 0;
            marni_out();
            gGameTable.dword_988610 = 0xD20FF024;
            audio::snd_sys_stereo();
            gGameTable.fg_system |= 2;
            task_chain(title::title);
            return;

        error:
            // 0x00508DC0
            file_error();
            return;
        }
    }

    // 0x004CAD29
    static void moji_init()
    {
        interop::call(0x004CAD29);
    }

    // 0x00508B20
    static void trans_work_init()
    {
        interop::call(0x00508B20);
    }

    // 0x004C3C60
    static void line_work_init()
    {
        gGameTable.dword_991F70 = 0;
        gGameTable.dword_991F74 = 0;
    }

    // 0x00502D70
    static void memclr(uint32_t* address, int count)
    {
        do
        {
            *address++ = 0;
            --count;
        } while (count > 0);
    }

    // 0x00508B50
    static void task_null()
    {
        task_exit();
    }

    // 0x004C4D70
    static void init_global()
    {
        memclr(&gGameTable.fg_status, 0x1419);
        gGameTable.fg_message = 0;
        gGameTable.fg_system &= 0x1000899;
        gGameTable.fade_table[0].kido = -1;
        gGameTable.fade_table[0].kido = -1;
        gGameTable.fade_table[1].kido = -1;
        gGameTable.fade_table[2].kido = -1;
        gGameTable.fade_table[3].kido = -1;
        gGameTable.last_cut = -1;
        gGameTable.byte_98E9AA = gGameTable.byte_98F1B6;
        gGameTable.dword_9885AC = 0xFFFF0000;
        gGameTable.dword_9885D0 = 0xFFFF0000;
    }

    static uint8_t input_mapping[32]
        = { 0x68, 0x62, 0x64, 0x66, 0, 0x56, 0, 0x41, 0, 0,    0x58, 0x43, 0x11, 0x5A, 0,    0,
            0,    0,    0,    0,    0, 0,    0, 0,    0, 0x26, 0x28, 0x25, 0x27, 0x0D, 0x20, 0x1B };

    // 0x0043B950
    static void init_input()
    {
        for (uint32_t i = 0; i < 32; i++)
        {
            gGameTable.input.mapping[i] = input_mapping[i];
        }
    }

    // 0x004C3F10
    static void init_system()
    {
        init_input();
        scheduler_init();
        set_flag(FlagGroup::System, FG_SYSTEM_20, true);
        init_global();
        gGameTable.byte_9888D8 = 0;
        gGameTable.sfx_vol = 100;
        gGameTable.bgm_vol = 100;
        gGameTable.byte_9888D9 = 0;
        gGameTable.byte_98F1B6 = 0;
        gGameTable.dword_98F1AC = 0;
        gGameTable.dword_98F1B0 = 15728960;
        moji_init();
        trans_work_init();
        line_work_init();
        task_execute(0, init_main);
        task_execute(1, task_null);
        task_execute(2, task_null);
    }

    // 0x00441870
    static void movie_set(int id)
    {
        gGameTable.movie_idx = id;
    }

    // 0x00507C60
    static void trans_pointer_set()
    {
        interop::call(0x00507C60);
    }

    // 0x004CAE34
    static void moji_mode_init()
    {
        auto idx = gGameTable.byte_9888D8;
        auto* v1 = gGameTable.moji_disp + 192 * idx;
        auto* v2 = reinterpret_cast<int*>(gGameTable.g_table + 8 * idx);
        int v3 = 8;
        do
        {
            v1 += 24;
            ++v2;
            --v3;
        } while (v3);
        gGameTable.moji_work0 = reinterpret_cast<uint32_t>(gGameTable.moji_tbl1 + 5120 * idx);
        auto* result = gGameTable.moji_tbl2 + 2048 * idx;
        gGameTable.moji_work1 = reinterpret_cast<uint32_t>(result);
    }

    // 0x004C8CCA
    static void moji_trans_main()
    {
        interop::call(0x004C8CCA);
    }

    // 0x004C4AF0
    static void system_trans()
    {
        interop::call(0x004C4AF0);
    }

    // 0x004C4460
    static void swap_cbuff()
    {
        interop::call(0x004C4460);
    }

    // 0x004EEDF0
    static void cd_system_control()
    {
        interop::call(0x004EEDF0);
    }

    static void psp_trans();
    static void om_trans();

    // 0x00446D50
    static void exbtl_opening()
    {
        interop::call(0x00446D50);
    }

    // 0x004B57C0
    static void result()
    {
        interop::call(0x004B57C0);
    }

    // 0x0044DF10
    static void die()
    {
        interop::call(0x0044DF10);
    }

    // 0x00449FD0
    static void config()
    {
        interop::call(0x00449FD0);
    }

    // 0x00501BD0
    static void status()
    {
        interop::call(0x00501BD0);
    }

    // 0x004C57E0
    static void mem_card()
    {
        interop::call(0x004C57E0);
    }

    // 0x004D1F50
    static void windows()
    {
        interop::call(0x004D1F50);
    }

    // 0x004C0820
    static void sub_4C0820()
    {
        interop::call(0x004C0820);
    }

    // 0x004CCD70
    static void om_move()
    {
        interop::call(0x004CCD70);
    }

    // 0x004C5720
    static void mirror_matrix_set()
    {
        interop::call(0x004C5720);
    }

    // 0x004C5600
    static void mirror_trans(ActorEntity* entity)
    {
        interop::call<void, ActorEntity*>(0x004C5600, entity);
    }

    // 0x004C0B70
    static void gun_light_set()
    {
        interop::call(0x004C0B70);
    }

    // 0x004C0CA0
    static void gun_light_reset()
    {
        interop::call(0x004C0CA0);
    }

    // 0x00446D30
    static void set_clear_color(int r, int g, int b)
    {
        gGameTable.global_rgb = (static_cast<uint8_t>(r) << 16) | (static_cast<uint8_t>(g) << 8) | static_cast<uint8_t>(b);
    }

    // 0x00503190
    static void rot_vector(uint16_t rot_y, Vec16* in, Vec16* out)
    {
        interop::call<void, uint16_t, Vec16*, Vec16*>(0x00503190, rot_y, in, out);
    }

    // 0x004C0CE0
    static void joint_trans2(ActorEntity* entity, PartsW* parts, int32_t be_flg, Vec32* vec)
    {
        interop::call<void, ActorEntity*, PartsW*, int32_t, Vec32*>(0x004C0CE0, entity, parts, be_flg, vec);
    }

    // 0x004B3050
    static void mul_kage(Kage* kage, uint16_t ground, uint16_t cdir_y, int flg)
    {
        interop::call<void, Kage*, uint16_t, uint16_t, int>(0x004B3050, kage, ground, cdir_y, flg);
    }

    // 0x004B3110
    static void kage_work_sort()
    {
        interop::call(0x004B3110);
    }

    // 0x004B32E0
    static void kage_work9_sort()
    {
        interop::call(0x004B32E0);
    }

    // 0x004B93D0
    static void esp_move()
    {
        interop::call(0x004B93D0);
    }

    // 0x00507C50
    static void prim_trans()
    {
        interop::call(0x00507C50);
    }

    // 0x004C3BC0
    static void scr_effect()
    {
        interop::call(0x004C3BC0);
    }

    // 0x00503350
    static void set_front_pos(ActorEntity* entity)
    {
        interop::call<void, ActorEntity*>(0x00503350, entity);
    }

    // 0x004C8603
    static void prim_14(int x, int y, uint32_t a3, uint32_t a4, const char* format)
    {
        interop::call<void, int, int, uint32_t, uint32_t, const char*>(0x004C8603, x, y, a3, a4, format);
    }

    // 0x004C8603
    static void prim_14(int x, int y, uint32_t a3, uint32_t a4, const char* format, int a5, int a6, int a7, int a8, int a9)
    {
        interop::call<void, int, int, uint32_t, uint32_t, const char*, int, int, int, int, int>(
            0x004C8603, x, y, a3, a4, format, a5, a6, a7, a8, a9);
    }

    // 0x0052D7E8
    static const uint8_t timer_blink_tbl[9] = { 0, 3, 6, 1, 4, 7, 2, 5, 9 };

    // 0x0052D800
    static const char TIMER_FMT[] = "%2d^%d%d=%d%d";

    // 99:59 in seconds
    static constexpr auto GAME_TIMER_MAX = 5999;

    static bool is_demo_timeout()
    {
        return check_flag(FlagGroup::System, FG_SYSTEM_DEMO) && gGameTable.word_98E52A > gGameTable.pdemo.frames;
    }

    static bool game_check_status_trigger()
    {
        if (task_status(1) || gGameTable.current_cut != gGameTable.byte_989EEA || gGameTable.byte_98F07B
            || gGameTable.byte_991F80 != 0 || check_flag(FlagGroup::Status, FG_STATUS_26)
            || check_flag(FlagGroup::Stop, FG_STOP_DISABLE_INPUT) || gGameTable.pl.damage_cnt
            || check_flag(FlagGroup::System, FG_SYSTEM_5) || check_flag(FlagGroup::System, FG_SYSTEM_22))
        {
            return false;
        }
        if ((gGameTable.dword_9885FE & 0x100) && !check_flag(FlagGroup::Status, FG_STATUS_SCREEN))
        {
            set_flag(FlagGroup::System, FG_SYSTEM_4, true);
            set_flag(FlagGroup::Status, FG_STATUS_SCREEN, true);
        }
        if ((gGameTable.dword_9885FE & 0x800) && !check_flag(FlagGroup::Status, FG_STATUS_CUTSCENE))
        {
            set_flag(FlagGroup::Status, FG_STATUS_SCREEN, true);
        }
        if ((gGameTable.key_trg & 0x4000) && !check_flag(FlagGroup::Status, FG_STATUS_CUTSCENE))
        {
            gGameTable.hud_mode = HUD_MODE_MAP_1;
            set_flag(FlagGroup::Status, FG_STATUS_SCREEN, true);
        }
        if (!check_flag(FlagGroup::Status, FG_STATUS_SCREEN))
        {
            return false;
        }
        set_flag(FlagGroup::Status, FG_STATUS_26, true);
        gGameTable.dword_991FC4 = gGameTable.fg_stop;
        gGameTable.fg_stop |= 0xFF000000;
        gGameTable.byte_991F80 = 1;
        if (gGameTable.hud_mode != HUD_MODE_PICKUP_ITEM)
        {
            snd_se_on(0x4060000);
        }
        return true;
    }

    static void game_process_enemies()
    {
        for (auto slot = reinterpret_cast<ActorEntity**>(&gGameTable.splayer_work),
                  slotEnd = reinterpret_cast<ActorEntity**>(gGameTable.dword_98862C);
             slot != slotEnd;
             slot++)
        {
            auto entity = *slot;
            gGameTable.actor_entity = entity;
            if ((entity->be_flg & 1) && !(entity->type & 0x8000))
            {
                auto dx = entity->m.pos.x - gGameTable.pl.m.pos.x;
                auto dz = entity->m.pos.z - gGameTable.pl.m.pos.z;
                entity->l_pl = square_root_0(dx * dx + dz * dz);
                reinterpret_cast<void (*)(ActorEntity*)>(gGameTable.enemy_init_map[entity->id])(entity);
            }
        }
    }

    static void game_render_entities()
    {
        for (auto slot = reinterpret_cast<ActorEntity**>(&gGameTable.player_work),
                  slotEnd = reinterpret_cast<ActorEntity**>(gGameTable.dword_98862C);
             slot != slotEnd;
             slot++)
        {
            auto entity = *slot;
            gGameTable.actor_entity = entity;
            if (!(entity->be_flg & 1))
            {
                continue;
            }
            auto kage = entity->pKage_work;
            auto kagePos = *reinterpret_cast<Vec32**>(reinterpret_cast<uint8_t*>(kage) + 0x14);
            int16_t ground;
            if (entity == reinterpret_cast<ActorEntity*>(&gGameTable.pl))
            {
                gGameTable.vec_6897F4.z = 0;
                gGameTable.vec_6897F4.x = entity->kage_ofs;
                rot_vector(entity->cdir.y, &gGameTable.vec_6897F4, &gGameTable.vec_6897F4);
                gGameTable.vec_6897E0.x = kagePos->x + gGameTable.vec_6897F4.x;
                gGameTable.vec_6897E0.y = kagePos->y;
                gGameTable.vec_6897E0.z = kagePos->z + gGameTable.vec_6897F4.z;
                ground = (int16_t)sca_ck_hit(&gGameTable.vec_6897E0, 0, entity->sc_id << 8, 0);
            }
            else
            {
                ground = (int16_t)sca_ck_hit(kagePos, 0, entity->sc_id << 8, 0);
            }
            entity->ground = ground;
            if (entity->pTbefore_func)
            {
                reinterpret_cast<void (*)(ActorEntity*)>(entity->pTbefore_func)(entity);
            }
            joint_trans2(entity, entity->pSin_parts_ptr, entity->be_flg, &entity->m.pos);
            if (check_flag(FlagGroup::Status, FG_STATUS_MIRROR))
            {
                mirror_trans(entity);
            }
            mul_kage(kage, entity->ground, entity->cdir.y, (entity->be_flg >> 9) & 2);
            if (entity->pTafter_func)
            {
                reinterpret_cast<void (*)(ActorEntity*)>(entity->pTafter_func)(entity);
            }
            entity->old_pos.x = (int16_t)entity->m.pos.x;
            entity->old_pos.y = (int16_t)entity->m.pos.y;
            entity->old_pos.z = (int16_t)entity->m.pos.z;
            entity->old_pos_2.x = (int16_t)entity->atd[0].pos.x;
            entity->old_pos_2.y = (int16_t)entity->atd[0].pos.y;
            entity->old_pos_2.z = (int16_t)entity->atd[0].pos.z;
        }
    }

    // 0x004BF810
    void game_loop()
    {
        auto& ctcb = *gGameTable.ctcb;

        if (gGameTable.byte_680597 & 2)
        {
            gGameTable.pl.life = check_flag(FlagGroup::System, FG_SYSTEM_31) ? 400 : 200;
            gGameTable.byte_680597 &= 1;
        }

        auto vk_press = gGameTable.vk_press;
        if (vk_press & 1)
        {
            vk_press &= ~1u;
            gGameTable.vk_press = vk_press;
            if (!check_flag(FlagGroup::System, FG_SYSTEM_DEMO))
            {
                gGameTable.dword_9885FE |= 0x800;
            }
        }
        if (vk_press & 2)
        {
            vk_press &= ~2u;
            gGameTable.vk_press = vk_press;
            if (!check_flag(FlagGroup::System, FG_SYSTEM_DEMO))
            {
                gGameTable.dword_9885FE |= 0x100;
            }
        }
        if ((vk_press & 0x40) && !(vk_press & 4))
        {
            vk_press &= ~0x40u;
            gGameTable.reset_r0 = 1;
            gGameTable.vk_press = vk_press;
        }
        if (vk_press & 4)
        {
            gGameTable.vk_press = vk_press & ~4u;
            gGameTable.word_9885FC = 0;
            bg_set_mode(2, 0);
            task_kill(0);
            task_kill(1);
            task_kill(2);
            gGameTable.byte_98F1B5 = 0;
            moji_mode_init();
            trans_pointer_set();
            snd_sys_init2();
            init_global();
            gGameTable.byte_680598 = 0;
            gGameTable.word_98E78C = 0;
            task_chain(title::title);
            return;
        }

        if (ctcb.var_08 > 0x16)
        {
            return;
        }

        switch (ctcb.var_08)
        {
        case 0:
            title_game_init();
            if (!ctcb.var_13)
            {
                ctcb.var_08 = 1;
                task_sleep(1);
            }
            return;
        case 1:
            gGameTable.word_6897C4 = 0;
            gGameTable.word_6897DC = 8;
            gGameTable.fg_stop = 0;
            gGameTable.dword_6897C8 = 0;
            if (check_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE) && !check_flag(FlagGroup::Common, 0xFE))
            {
                task_execute(1, exbtl_opening);
                set_flag(FlagGroup::Common, 0xFE, true);
            }
            goto LABEL_94;
        case 2:
            if (check_flag(FlagGroup::System, FG_SYSTEM_10))
            {
                task_chain(result);
                return;
            }
            if (!check_flag(FlagGroup::System, FG_SYSTEM_1))
            {
                if (!check_flag(FlagGroup::System, FG_SYSTEM_24) && !check_flag(FlagGroup::System, FG_SYSTEM_4TH_SURVIVOR)
                    && !check_flag(FlagGroup::Status, FG_STATUS_SCENARIO) && check_flag(FlagGroup::System, FG_SYSTEM_14)
                    && !check_flag(FlagGroup::Common, 6) && gGameTable.word_98E9BC == 0)
                {
                    set_flag(FlagGroup::System, FG_SYSTEM_24, true);
                }
                goto LABEL_80;
            }
            gGameTable.dword_991FC0 = 0;
            if (gGameTable.byte_98F1B8)
            {
                goto LABEL_159;
            }
            if (check_flag(FlagGroup::System, FG_SYSTEM_29))
            {
                if (++gGameTable.word_6897C4 == 16)
                {
                    task_execute(1, die);
                    gGameTable.fg_message = 0;
                    gGameTable.word_6897C4 = -30000;
                }
                goto LABEL_159;
            }
            if (gGameTable.byte_991F80 == 0 && is_demo_timeout())
            {
                gGameTable.byte_98E9AA = gGameTable.byte_99270E;
                gGameTable.fg_stop |= 0xFF000000;
                if (gGameTable.byte_98F1BB)
                {
                    hud_fade_set(512, 2048, 7, 1);
                }
                else
                {
                    hud_fade_set(512, 512, 7, 1);
                }
                gGameTable.byte_991F80 = 2;
            }
            switch ((int8_t)gGameTable.byte_991F80)
            {
            case 1:
                if (gGameTable.fg_message < 0 || !hud_fade_status(0))
                {
                    break;
                }
                gGameTable.fg_stop |= 0xFF000000;
                if (!check_flag(FlagGroup::Status, FG_STATUS_SCREEN) && !check_flag(FlagGroup::System, FG_SYSTEM_13))
                {
                    if (check_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE) && (uint8_t)gGameTable.dword_98EBD0 == 0xFF)
                    {
                        set_flag(FlagGroup::Zapping, 0x3F, true);
                    }
                    if (!check_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE) || !check_flag(FlagGroup::Zapping, 0x3F))
                    {
                        set_flag(FlagGroup::System, FG_SYSTEM_15, true);
                        bg_set_mode(1, 0);
                    }
                    gGameTable.fg_stop = 0;
                    set_flag(FlagGroup::System, FG_SYSTEM_17, true);
                    gGameTable.dword_6897C8 = 0;
                    ctcb.var_08 = 4;
                    goto LABEL_53;
                }
                hud_fade_set(512, 12288, 7, 1);
                gGameTable.byte_991F80 = 2;
                goto LABEL_56;
            case 2:
            LABEL_56:
                if (hud_fade_status(0))
                {
                    hud_fade_set(512, 0, 7, 1);
                    hud_fade_adjust(0, 0x7FFF, 0xFFFFFF, nullptr);
                    task_sleep(2);
                    ctcb.var_08 = 5;
                    return;
                }
                break;
            case 3: goto LABEL_94;
            case 4:
                gGameTable.byte_991F80 = 5;
                gGameTable.fg_stop |= 0xFF000000;
                break;
            case 5:
                if (!hud_fade_status(0))
                {
                    gGameTable.fg_stop = gGameTable.dword_6897C8;
                    gGameTable.byte_991F80 = 0;
                }
                break;
            case 6: ctcb.var_08 = 12; goto LABEL_62;
            case 7:
                task_execute(1, windows);
                gGameTable.byte_991F80 = 0;
                goto LABEL_186;
            default: break;
            }
        LABEL_63:
            if (check_flag(FlagGroup::Status, FG_STATUS_7))
            {
                goto LABEL_153;
            }
            if (!check_flag(FlagGroup::Status, FG_STATUS_4))
            {
                goto LABEL_120;
            }
            if (check_flag(FlagGroup::System, FG_SYSTEM_4TH_SURVIVOR))
            {
                if (gGameTable.word_6897C4 > 29 || !(gGameTable.dword_6897F0 & 0x8000000))
                {
                    gGameTable.word_6897C4 = 0;
                    gGameTable.word_6897DC = 0;
                    gGameTable.word_98EB2A++;
                }
                auto timer = (int16_t)gGameTable.word_98EB2A;
                prim_14(
                    104,
                    32,
                    1,
                    2,
                    TIMER_FMT,
                    timer / 60,
                    timer % 60 / 10,
                    timer % 60 % 10,
                    gGameTable.word_6897C4 / 3,
                    timer_blink_tbl[gGameTable.word_6897DC % 9]);
                if (gGameTable.word_98EB2A == GAME_TIMER_MAX && gGameTable.word_6897C4 == 27)
                {
                    gGameTable.word_6897DC = 8;
                    goto LABEL_120;
                }
                if (gGameTable.byte_991F80 == 0 && hud_fade_status(0)
                    && (gGameTable.current_stage != 0 || gGameTable.current_room != 28))
                {
                    gGameTable.word_6897DC = ++gGameTable.word_6897C4;
                }
                goto LABEL_120;
            }
            else
            {
                if (gGameTable.word_6897C4 > 29 || !(gGameTable.dword_6897F0 & 0x8000000))
                {
                    gGameTable.word_6897C4 = 0;
                    gGameTable.word_6897DC = 8;
                    gGameTable.word_98EB2A--;
                }
                auto timer = (int16_t)gGameTable.word_98EB2A;
                if (timer < 0)
                {
                    // 0x0052D7F4: " 0^00=00"
                    prim_14(104, 32, 2, 0, " 0^00=00");
                }
                else
                {
                    prim_14(
                        104,
                        32,
                        timer < 60 ? 2 : 1,
                        2,
                        TIMER_FMT,
                        timer / 60,
                        timer % 60 / 10,
                        timer % 60 % 10,
                        9 - gGameTable.word_6897C4 / 3,
                        gGameTable.word_6897DC);
                }
                if (timer < 0)
                {
                    if (gGameTable.byte_991F80 == 0 && hud_fade_status(0) && !task_status(1))
                    {
                        set_flag(FlagGroup::System, FG_SYSTEM_29, true);
                        set_flag(FlagGroup::Status, FG_STATUS_4, false);
                        gGameTable.word_6897C4 = 0;
                        gGameTable.word_6897DC = 0;
                    }
                    goto LABEL_159;
                }
                if (gGameTable.byte_991F80 != 0 || !hud_fade_status(0))
                {
                    goto LABEL_120;
                }
                gGameTable.word_6897C4++;
                gGameTable.word_6897DC -= 3;
                if (gGameTable.word_6897DC >= 1)
                {
                    goto LABEL_120;
                }
                gGameTable.word_6897DC = 8;
                goto LABEL_120;
            }
        LABEL_120:
            gGameTable.dword_6897F0 = gGameTable.fg_status;
            if (gGameTable.dword_98E790)
            {
                reinterpret_cast<void (*)()>(gGameTable.dword_98E790)();
            }
            if (!check_flag(FlagGroup::Status, FG_STATUS_CAMERA_LOCKED))
            {
                cut_check(0);
            }
            sce_scheduler();
            if (game_check_status_trigger())
            {
                goto LABEL_159;
            }
            gGameTable.dword_991FC0 |= 1;
            gGameTable.fg_room_enemy &= 0x00FF;
            game_process_enemies();
            gGameTable.actor_entity = reinterpret_cast<ActorEntity*>(&gGameTable.pl);
            if (!(gGameTable.pl.type & 0x8000))
            {
                player_move(&gGameTable.pl);
                oba_ck_em2(reinterpret_cast<ActorEntity*>(&gGameTable.pl));
                sca_ck_em(reinterpret_cast<EnemyEntity*>(&gGameTable.pl), 0x8000);
                set_front_pos(reinterpret_cast<ActorEntity*>(&gGameTable.pl));
                gGameTable.f_pos.x = gGameTable.pl.f_pos.x;
                gGameTable.f_pos.z = gGameTable.pl.f_pos.z;
                gGameTable.pl.Sca_info |= sca_ck_info(&gGameTable.f_pos, 0x1C2, 1 << gGameTable.pl.nFloor, 0x8000) << 16;
            }
            om_move();
            sce_at();
            if (gGameTable.action_fn)
            {
                reinterpret_cast<void (*)()>(gGameTable.action_fn)();
            }
        LABEL_153:
            if (check_flag(FlagGroup::System, FG_SYSTEM_5) && !task_status(1) && !gGameTable.byte_98F07B
                && gGameTable.current_cut == gGameTable.byte_989EEA)
            {
                task_execute(1, die);
                set_flag(FlagGroup::System, FG_SYSTEM_5, false);
            }
        LABEL_159:
            if (check_flag(FlagGroup::Status, FG_STATUS_MIRROR))
            {
                mirror_matrix_set();
            }
            if (!gGameTable.can_draw && !check_flag(FlagGroup::System, FG_SYSTEM_11))
            {
                psp_trans();
            }
            marni::out();
            if (gGameTable.byte_991F81 & 1)
            {
                gun_light_set();
            }
            {
                auto cutInfo = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(gGameTable.rdt) + 0x2C);
                auto cut = (int16_t)gGameTable.current_cut;
                set_clear_color(cutInfo[cut * 40 + 13], cutInfo[cut * 40 + 14], cutInfo[cut * 40 + 15]);
            }
            game_render_entities();
            if (gGameTable.byte_991F81 != 0)
            {
                gun_light_reset();
            }
            kage_work_sort();
            kage_work9_sort();
            if (!gGameTable.can_draw)
            {
                om_trans();
            }
            if (gGameTable.dword_991FC0)
            {
                esp_move();
            }
            prim_trans();
            scr_effect();
            marni::out();
        LABEL_186:
            if (check_flag(FlagGroup::System, FG_SYSTEM_10))
            {
                bg_set_mode(2, 0);
            }
            ctcb.var_08 = 2;
        LABEL_189:
            task_sleep(1);
            return;
        case 3:
        case 0xA:
        case 0xD:
        case 0xE:
        case 0xF:
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15: return;
        case 4:
        LABEL_53:
            door_set();
            if (!ctcb.var_13)
            {
                goto LABEL_95;
            }
            return;
        case 5:
            if (is_demo_timeout())
            {
                marni::out();
            LABEL_80:
                bg_set_mode(2, 0);
                if (!check_flag(FlagGroup::System, FG_SYSTEM_DEMO) || gGameTable.byte_98F1BB)
                {
                    gGameTable.byte_98F1BB = 1;
                    init_global();
                    task_chain(title::title);
                    ctcb.var_08 = 0;
                }
                else
                {
                    init_global();
                    gGameTable.byte_98F1B9 = 0;
                    set_flag(FlagGroup::System, FG_SYSTEM_30, true);
                    task_execute(1, sub_4C0820);
                    task_chain(title::title);
                    ctcb.var_08 = 0;
                }
            }
            else
            {
                gGameTable.fg_stop = 0;
                set_flag(FlagGroup::System, FG_SYSTEM_17, true);
                gGameTable.dword_6897C8 = 0;
                if (check_flag(FlagGroup::Status, FG_STATUS_SCREEN))
                {
                    if (check_flag(FlagGroup::System, FG_SYSTEM_4))
                    {
                        set_flag(FlagGroup::System, FG_SYSTEM_4, false);
                        gGameTable.dword_99CF6C = 1;
                        task_execute(1, config);
                        task_sleep(1);
                        ctcb.var_08 = 8;
                    }
                    else
                    {
                    LABEL_89:
                        task_execute(1, status);
                        ctcb.var_08 = 8;
                        task_sleep(1);
                    }
                }
                else if (check_flag(FlagGroup::System, FG_SYSTEM_13))
                {
                    set_flag(FlagGroup::System, FG_SYSTEM_13, false);
                    task_execute(1, mem_card);
                    gGameTable.dword_6897C8 = gGameTable.dword_991FC4;
                    gGameTable.dword_991FC4 = 0;
                    ctcb.var_08 = 9;
                    task_sleep(1);
                }
                else
                {
                LABEL_91:
                    ctcb.var_08 = 11;
                    task_sleep(1);
                }
            }
            return;
        case 6:
        case 8:
            gGameTable.dword_6897C8 = gGameTable.dword_991FC4;
            gGameTable.dword_991FC4 = 0;
            set_flag(FlagGroup::Status, FG_STATUS_SCREEN, false);
            set_flag(FlagGroup::Status, FG_STATUS_26, false);
            goto LABEL_91;
        case 7: goto LABEL_89;
        case 9: set_flag(FlagGroup::System, FG_SYSTEM_20, false); goto LABEL_93;
        case 0xB:
        LABEL_93:
            gGameTable.byte_991F80 = 3;
        LABEL_94:
            gGameTable.actor_entity = reinterpret_cast<ActorEntity*>(&gGameTable.pl);
            gGameTable.pl.routine_0 = 0;
            gGameTable.pl.routine_1 = 0;
            gGameTable.pl.routine_2 = 0;
            gGameTable.pl.routine_3 = 0;
            player_move(&gGameTable.pl);
            goto LABEL_95;
        case 0xC:
        LABEL_62:
            if (gGameTable.byte_991F80 != 6)
            {
                goto LABEL_63;
            }
            goto LABEL_189;
        case 0x16:
        LABEL_95:
            gGameTable.dword_6897C8 |= gGameTable.fg_stop;
            set_flag(FlagGroup::System, FG_SYSTEM_17, false);
            gGameTable.fg_stop |= 0xEF000000;
            hud_fade_set(512, -6144, 7, 1);
            if (check_flag(FlagGroup::System, FG_SYSTEM_4TH_SURVIVOR) && gGameTable.current_stage == 0
                && gGameTable.current_room == 28)
            {
                if (gGameTable.word_6897C4 != 0)
                {
                    gGameTable.word_6897C4--;
                }
                else
                {
                    gGameTable.word_6897C4 = 29;
                    gGameTable.word_98EB2A--;
                }
                gGameTable.word_6897DC = gGameTable.word_6897C4;
            }
            gGameTable.byte_991F80 = 5;
            goto LABEL_63;
        }
    }

    // 0x004C3C70
    static void psx_main()
    {
        if (!gGameTable.systemInitialized)
        {
            init_system();
            gGameTable.systemInitialized = 1;
        }

        gGameTable.byte_98F1B8 = 0;

        auto idx = *reinterpret_cast<uint32_t*>(&gGameTable.byte_9888D8) & 0xFF;

        gGameTable.dword_986520 = (uint32_t)&gGameTable.g_table + (idx << 5);
        gGameTable.dword_988524 = (uint32_t)&gGameTable.byte_986524 + (idx << 12);
        gGameTable.dword_9885A8 = (uint32_t)&gGameTable.byte_988528 + (idx << 6);
        auto mainOffset = (((idx * 8) + idx) * 2 + idx) * 8;
        gGameTable.dword_98F070 = (uint32_t)&gGameTable.main + mainOffset;

        pad_set();

        if (
            // clang-format off
        gGameTable.censorship_off
        && check_flag(FlagGroup::System, FG_SYSTEM_1)
        && !gGameTable.byte_98F07B
        && !gGameTable.byte_991F80
        && !check_flag(FlagGroup::Stop, FG_STOP_DISABLE_INPUT)
        && check_flags(FlagGroup::System, { FG_SYSTEM_DOOR_TRANSITION, FG_SYSTEM_10, FG_SYSTEM_BGM_DISABLED, FG_SYSTEM_22 })
        && check_flags(FlagGroup::Status, { FG_STATUS_11, FG_STATUS_CUTSCENE })
            // clang-format on
        )
        {
            if (gGameTable.pause)
            {
                movie_set(1);
                if (gGameTable.dword_9885F8 & 2 || gGameTable.vk_press & 0x20)
                {
                    marni::out();
                    gGameTable.pause = 0;
                    set_game_seconds(gGameTable.dword_689800);
                }
                vsync();
                return;
            }

            if (gGameTable.dword_9885F8 & 2 || gGameTable.vk_press & 0x20)
            {
                gGameTable.pause = 1;
                update_timer();
                gGameTable.dword_689800 = set_game_seconds(1);
                auto v0 = 16 * gGameTable.byte_9888D8;
                gGameTable.byte_52D8E7[v0] = 2;
                marni::add_tile(&gGameTable.curtain2[v0], 5, 0);
                // marni::prim14
                interop::call<void, int, int, int, int, int>(0x004C8603, 135, 107, 0, 0x4000, gGameTable.pause);
                marni::out();
            }
        }

        trans_pointer_set();
        moji_mode_init();
        scheduler();
        set_flag(FlagGroup::Status, FG_STATUS_INTERACT, false);
        set_flag(FlagGroup::Status, FG_STATUS_11, false);

        if (check_flag(FlagGroup::System, FG_SYSTEM_15))
        {
            if (hud_fade_status(0))
            {
                hud_fade_adjust(0, 31, 526344, 0);
            }

            auto tileIdx = gGameTable.byte_9888D8;
            auto& tile = gGameTable.fade_table->tiles[tileIdx];
            tile.code = 2;
            tile.tag = gGameTable.fade_table->hrate & 3;
            marni::add_tile(&tile, 0, 0);

            if (--gGameTable.fade_table->kido < 0)
            {
                set_flag(FlagGroup::System, FG_SYSTEM_15, false);
                return;
            }
        }
        else
        {
            if (!gGameTable.can_draw)
            {
                moji_trans_main();
            }

            system_trans();
            swap_cbuff();
            cd_system_control();
            gGameTable.vsync_rate = gGameTable.byte_98F07A;
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
        load_init_table((void*)0x00999AE0, gGameTable.byte_989E7E);
    }

    // 0x004DE650
    static void load_init_table_2()
    {
        load_init_table((void*)0x008BD880, 5);
    }

    // 0x00505B20
    static void load_init_table_3()
    {
        gGameTable.mem_top = (void*)0x008FF8A0;
        load_init_table((void*)0x008BD880, gGameTable.byte_989E7E);
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
        if (gGameTable.Cards)
        {
            GlobalUnlock(GlobalHandle(gGameTable.Cards));
            GlobalFree(GlobalHandle(gGameTable.Cards));
        }
        if (gGameTable.Names)
        {
            GlobalUnlock(GlobalHandle(gGameTable.Names));
            GlobalFree(GlobalHandle(gGameTable.Names));
        }
        if (gGameTable.pMem)
        {
            GlobalUnlock(GlobalHandle(gGameTable.pMem));
            GlobalFree(GlobalHandle(gGameTable.pMem));
        }
        gGameTable.Cards = nullptr;
        gGameTable.Names = nullptr;
        gGameTable.pMem = nullptr;
    }

    // 0x00433830
    static void ssclose()
    {
        interop::call(0x00433830);
    }

    // 0x00431000
    static void font_create()
    {
        if (gGameTable.hFont)
            DeleteObject((HFONT)gGameTable.hFont);
        if (gGameTable.is_480p)
        {
            gGameTable.hFont = CreateFontA(
                24,
                12,
                0,
                0,
                500,
                0,
                0,
                0,
                SHIFTJIS_CHARSET,
                OUT_CHARACTER_PRECIS,
                CLIP_DEFAULT_PRECIS,
                DRAFT_QUALITY,
                DEFAULT_PITCH,
                fontFaceName);
            gGameTable.byte_6634F8 = 30;
            gGameTable.FontH = 24;
        }
        else
        {
            gGameTable.hFont = CreateFontA(
                12,
                6,
                0,
                0,
                400,
                0,
                0,
                0,
                SHIFTJIS_CHARSET,
                OUT_CHARACTER_PRECIS,
                CLIP_DEFAULT_PRECIS,
                DRAFT_QUALITY,
                DEFAULT_PITCH,
                fontFaceName);
            gGameTable.byte_6634F8 = 15;
            gGameTable.FontH = 12;
        }
    }

    // 0x004310A0
    static void font_delete()
    {
        DeleteObject(gGameTable.hFont);
    }

    // 0x00441780
    static void movie_kill()
    {
        if (gGameTable.movie_playing)
        {
            marni::movie_kill(gGameTable.pMarni);
            marni::syskeydown(gGameTable.pMarni);
            gGameTable.movie_playing = 0;
        }
    }

    // 0x00441DA0
    static void wnd_activate()
    {
        gGameTable.window_active = 1;
        set_game_seconds(gGameTable.dword_6805C4);
        marni::out();
    }

    // 0x00441D60
    static void wnd_deactivate()
    {
        gGameTable.window_active = 0;
        if (gGameTable.movie_r0 >= 2)
        {
            movie_kill();
            gGameTable.movie_r0 = 5;
        }
        gGameTable.dword_6805C4 = set_game_seconds(1);
        marni::out();
    }

    // 0x00442800
    static INT_PTR CALLBACK about_dialog(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_INITDIALOG)
        {
            SetDlgItemTextA(hDlg, 1017, "BIOHAZARD(R) 2 PC\nVersion: 1.1.0");
            auto hParent = GetParent(hDlg);
            if (hParent)
            {
                RECT rcParent, rcDlg;
                GetWindowRect(hParent, &rcParent);
                GetWindowRect(hDlg, &rcDlg);
                MoveWindow(
                    hDlg,
                    (rcParent.left + rcParent.right) / 2 - (rcDlg.right - rcDlg.left) / 2,
                    (rcParent.top + rcParent.bottom) / 2 - (rcDlg.bottom - rcDlg.top) / 2,
                    rcDlg.right - rcDlg.left,
                    rcDlg.bottom - rcDlg.top,
                    TRUE);
            }
            return TRUE;
        }

        if (msg == WM_COMMAND && wParam && (uint32_t)wParam <= 2)
        {
            EndDialog(hDlg, -1);
        }
        return FALSE;
    }

    // 0x00442750
    static void screenshot()
    {
        interop::call(0x00442750);
    }

    // 0x00442C60
    static void cursor_op()
    {
        auto marni = gGameTable.pMarni;
        if (marni == nullptr)
            return;

        auto gpu_flg = *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(marni) + 0x8C83F4);

        if (gpu_flg & 0x400)
        {
            if (!gGameTable.byte_6805B2)
            {
                gGameTable.byte_6805B2 = 1;
                ShowCursor(FALSE);
                interop::call<int>(0x00433870, 0); // SsSetCoopLevel(0)
            }
        }
        else if (gGameTable.byte_6805B2 == 1)
        {
            gGameTable.byte_6805B2 = 0;
            ShowCursor(TRUE);
            interop::call<int>(0x00433870, 1); // SsSetCoopLevel(1)
        }
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
#ifndef DEBUG
            else
                wnd_deactivate();
#endif
            break;
        case WM_KILLFOCUS:
#ifndef DEBUG
            input_pause(&gGameTable.input);
#endif
            break;
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
            case VK_F2:
                g_speed_multiplier -= 1;
                if (g_speed_multiplier < 1)
                    g_speed_multiplier = 1;
                SetFocus(hWnd);
                break;
            case VK_F3:
                g_speed_multiplier += 1;
                if (g_speed_multiplier > 5)
                    g_speed_multiplier = 5;
                SetFocus(hWnd);
                break;
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
        auto* v2 = strchr(lpCmdLine, '/');
        if (!v2)
        {
            v2 = strchr(lpCmdLine, '-');
            if (!v2)
                return -1;
        }
        auto* v3 = _strlwr(v2);
        auto* v4 = strstr(v3, gGameTable.cheat_cmds[a1]);
        if (!v4)
            return -1;
        auto* v5 = &v4[strlen(gGameTable.cheat_cmds[a1])];
        auto result = *v5 - '0';
        if (result > 9)
            return -1;
        auto v7 = v5[1] - '0';
        if (v7 <= 9)
            return v7 + 10 * result;
        return result;
    }

    // 0x00441890
    static int cheat_line_cmd1(LPSTR lpCmdLine, int a1, int a2)
    {
        auto* v3 = strchr(lpCmdLine, '/');
        if (v3 || (v3 = strchr(lpCmdLine, '-')) != nullptr)
        {
            auto* v4 = _strlwr(v3);
            auto v5 = a1;
            if (a1 < 13)
            {
                while (v5 < a1 + a2)
                {
                    if (strstr(v4, gGameTable.cheat_cmds[v5]))
                        return v5 - a1;
                    if (++v5 >= 13)
                        return -1;
                }
            }
        }
        return -1;
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

    // 0x004310B0
    static void save_reset()
    {
        auto* v0 = &gGameTable.FontXY[1];
        char* v1 = gGameTable.String;
        memset(gGameTable.FontColor, 0, sizeof(gGameTable.FontColor));
        do
        {
            *v1 = 0;
            *(v0 - 1) = 0;
            *v0 = 0;
            v1 += 261;
            v0 += 2;
        } while (v1 < reinterpret_cast<char*>(&gGameTable.hFont));
        gGameTable.FontIndex = 0;
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

    // 0x004310F0
    static int merge_surface_gdi()
    {
        if (!gGameTable.FontIndex)
            return 1;

        auto* pSurface = (LPDIRECTDRAWSURFACE7)gGameTable.pMarni->surface0.pDDsurface;
        HDC hdc;
        if (pSurface->GetDC(&hdc) != DD_OK)
            return 1;

        auto oldFont = SelectObject(hdc, gGameTable.hFont);
        if (!oldFont)
            return 0;
        auto oldMode = SetBkMode(hdc, TRANSPARENT);

        for (int i = 0; i < gGameTable.FontIndex; i++)
        {
            auto* str = &gGameTable.String[261 * i];
            if (!*str)
                continue;

            auto x = gGameTable.FontXY[2 * i];
            auto y = gGameTable.FontXY[2 * i + 1];

            if (x != 0)
            {
                // Centered text with shadow (offset by +1 pixel)
                auto shadowR = (std::max)(GetRValue(gGameTable.FontColor[i]) - 64, 0);
                auto shadowG = (std::max)(GetGValue(gGameTable.FontColor[i]) - 64, 0);
                auto shadowB = (std::max)(GetBValue(gGameTable.FontColor[i]) - 64, 0);
                auto shadowColor = RGB(shadowR, shadowG, shadowB);

                if (SetTextColor(hdc, shadowColor) == CLR_INVALID)
                    return 0;
                TextOutA(hdc, gGameTable.is_480p + x + 1, y, str, (int)strlen(str));

                if (SetTextColor(hdc, gGameTable.FontColor[i]) == CLR_INVALID)
                    return 0;
                if (!TextOutA(hdc, x, y, str, (int)strlen(str)))
                    return 0;
            }
            else
            {
                // Left-aligned text with clipping rectangle and shadow
                SIZE psizl;
                GetTextExtentPoint32A(hdc, str, (int)strlen(str), &psizl);
                auto cx = psizl.cx;

                RECT rect;
                int v6;
                if (gGameTable.is_480p)
                {
                    if (cx > 576)
                        cx = 576;
                    rect.left = 32;
                    rect.right = 608;
                    v6 = (576 - cx) / 2 + 32;
                    rect.bottom = y + gGameTable.byte_6634F8;
                }
                else
                {
                    if (cx > 288)
                        cx = 288;
                    rect.left = 16;
                    rect.right = 304;
                    v6 = (288 - cx) / 2 + 16;
                    rect.bottom = y + gGameTable.byte_6634F8;
                }
                rect.top = y;

                auto shadowR = (std::max)(GetRValue(gGameTable.FontColor[i]) - 64, 0);
                auto shadowG = (std::max)(GetGValue(gGameTable.FontColor[i]) - 64, 0);
                auto shadowB = (std::max)(GetBValue(gGameTable.FontColor[i]) - 64, 0);
                auto shadowColor = RGB(shadowR, shadowG, shadowB);

                if (SetTextColor(hdc, shadowColor) == CLR_INVALID)
                    return 0;
                ExtTextOutA(hdc, gGameTable.is_480p + v6 + 1, y, ETO_CLIPPED, &rect, str, (int)strlen(str), nullptr);

                if (SetTextColor(hdc, gGameTable.FontColor[i]) == CLR_INVALID)
                    return 0;
                if (!ExtTextOutA(hdc, v6, y, ETO_CLIPPED, &rect, str, (int)strlen(str), nullptr))
                    return 0;
            }
        }

        SelectObject(hdc, oldFont);
        SetBkMode(hdc, oldMode);
        pSurface->ReleaseDC(hdc);
        return 1;
    }

    // 0x00431470
    static void SavePrint(int x, int y, const char* str, int color, int len)
    {
        interop::call<void, int, int, const char*, int, int>(0x00431470, x, y, str, color, len);
    }

    // 0x00509840
    static char* GetSaveFolder()
    {
        return interop::call<char*>(0x00509840);
    }

    // 0x00509930
    static int unknown_libname_19()
    {
        return interop::call<int>(0x00509930);
    }

    // 0x00432860
    static void sub_432860(char* str)
    {
        interop::call<void, char*>(0x00432860, str);
    }

    // 0x004315D0
    static BOOL __cdecl sub_4315D0(int a1, int a2, int a3, int a4, int a5, int a6, char a7, uint8_t* a8)
    {
        static const char* aExit = (const char*)0x5220A4;
        static const char* aCreateNew = (const char*)0x5220B8;

        int y0[2] = { 30, 60 };
        int y1[2] = { 50, 100 };
        int y2[2] = { 288, 576 };
        char String[264];

        auto* pSurface = (LPDIRECTDRAWSURFACE7)gGameTable.pMarni->surface0.pDDsurface;
        HDC hdc;
        pSurface->GetDC(&hdc);
        auto oldFont = SelectObject(hdc, gGameTable.hFont);

        auto folderLen = unknown_libname_19();
        auto saveFolder = GetSaveFolder();
        SIZE psizl;
        GetTextExtentPoint32A(hdc, saveFolder, folderLen, &psizl);

        auto is_480p = gGameTable.is_480p;
        int8_t scroll;
        int y2_val = y2[is_480p];
        if (psizl.cx <= y2_val)
            scroll = 0;
        else
            scroll = (int8_t)((psizl.cx - y2_val) / (gGameTable.FontH / 2));

        int scroll_pos = *(int32_t*)a8;
        if (scroll_pos < scroll)
            scroll = (int8_t)scroll_pos;

        int v12 = scroll;
        int v37 = scroll;
        SavePrint(0, y0[is_480p], saveFolder, 0, scroll);

        if (gGameTable.dword_986394 > 0)
        {
            int v14 = 0;
            int v15 = a3;
            do
            {
                if (a7 || (v14 + v15))
                {
                    int v16 = v14 + v15;
                    if (v16 >= gGameTable.cnt0 - a7 + 1)
                    {
                        int v27 = gGameTable.cnt1 - a7 + gGameTable.cnt0 + 1;
                        if (v16 >= v27)
                        {
                            if (v16 == v27)
                            {
                                SavePrint(0, y1[is_480p] + v14 * gGameTable.byte_6634F8, aExit, 0, 0);
                                break;
                            }
                        }
                        else
                        {
                            int v28 = v14 + a7 - gGameTable.cnt0 + v15 - 1;
                            wsprintfA(String, "[%s]", (const char*)(a5 + 261 * v28));
                            GetTextExtentPoint32A(hdc, String, (int)strlen(String), &psizl);
                            int8_t v30;
                            int v29 = y2[is_480p];
                            if (psizl.cx <= v29)
                                v30 = 0;
                            else
                                v30 = (int8_t)((psizl.cx - v29) / (gGameTable.FontH / 2));
                            if (*(int32_t*)a8 < v30)
                                v30 = (int8_t)*(int32_t*)a8;
                            if (v37 < v30)
                                v37 = v30;
                            SavePrint(0, y1[is_480p] + v14 * gGameTable.byte_6634F8, String, 4, v30);
                            v12 = v37;
                        }
                    }
                    else
                    {
                        int v17 = v15 + v14 + a7;
                        int v18 = a4 + 276 * v17;
                        strcpy(String, (char*)(v18 - 276));
                        sub_432860(String);
                        GetTextExtentPoint32A(hdc, String, (int)strlen(String), &psizl);
                        int8_t v23;
                        int v22 = y2[is_480p];
                        if (psizl.cx <= v22)
                            v23 = 0;
                        else
                            v23 = (int8_t)((psizl.cx - v22) / (gGameTable.FontH / 2));
                        if (*(int32_t*)a8 < v23)
                            v23 = (int8_t)*(int32_t*)a8;
                        int v24 = v23;
                        if (v37 < v23)
                            v37 = v23;
                        auto v25 = *(uint8_t*)(v18 - 15);
                        int v26;
                        if (v25 < 4)
                        {
                            if (*(uint8_t*)(v18 - 1))
                                v26 = 0;
                            else
                                v26 = ((v25 & 1) != 0) + 1;
                        }
                        else
                            v26 = 3;
                        SavePrint(0, y1[is_480p] + v14 * gGameTable.byte_6634F8, String, v26, v24);
                        v12 = v37;
                    }
                }
                else
                {
                    SavePrint(0, y1[is_480p], aCreateNew, 0, 0);
                }
                ++v14;
            } while (v14 < (int)gGameTable.dword_986394);
        }

        SelectObject(hdc, oldFont);
        pSurface->ReleaseDC(hdc);
        *(int32_t*)a8 = v12;
        auto result = gGameTable.dword_669B00 != v12;
        gGameTable.dword_669B00 = v12;
        return result;
    }

    // 0x004CAF90
    static void movie()
    {
        interop::call(0x004CAF90);
    }

    // 0x00440250
    static void reset_geom()
    {
        interop::call(0x00440250);
    }

    // 0x00442A50
    static void reset_screen()
    {
        interop::call(0x00442A50);
    }

    static int win_exit(uint32_t error)
    {
        static const char* aHighColor16bit = (const char*)0x00525098;
        static const char* aInNIN = (const char*)0x0052506C;

        switch (error)
        {
        case ERROR_0: [[fallthrough]];
        case ERROR_1: [[fallthrough]];
        case ERROR_2: [[fallthrough]];
        case ERROR_11: [[fallthrough]];
        case ERROR_18: [[fallthrough]];
        case ERROR_255: break;

        case ERROR_FAILED_TO_INITIALIZE_DIRECTX:
        {
            MessageBoxA(0, "Failed to initialize DIRECTX(R).", windowTitle, MB_ICONEXCLAMATION);
            break;
        }
        case ERROR_INSERT_DISC:
        {
            MessageBoxA(0, "Please insert BIOHAZARD(R) 2 PC DISC", windowTitle, MB_ICONEXCLAMATION);
            break;
        }
        case ERROR_17:
        {
            MessageBoxA(0, aHighColor16bit, windowTitle, MB_ICONEXCLAMATION);
            break;
        }
        case ERROR_19:
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

        return error;
    }

    // 0x00441DC0
    static bool init_instance(HINSTANCE hInstance, HINSTANCE hPrevInstance)
    {
        gGameTable.hInstance = hInstance;
        if (!hPrevInstance)
        {
            WNDCLASSA wndClass = {};
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

        DWORD windowStyleFlags = WS_CLIPCHILDREN | WS_BORDER | WS_DLGFRAME | WS_SYSMENU | WS_MINIMIZEBOX;

        RECT windowRect;
        windowRect.left = 0;
        windowRect.right = 640;
        windowRect.top = 0;
        windowRect.bottom = 480;
        AdjustWindowRect(&windowRect, windowStyleFlags, 0);

        gGameTable.hwnd = (void*)CreateWindowExA(
            0,
            windowTitle,
            windowTitle,
            windowStyleFlags,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            NULL,
            NULL,
            hInstance,
            NULL);

        auto window = (HWND)gGameTable.hwnd;

        ShowWindow(window, SW_NORMAL);
        SetForegroundWindow(window);
        UpdateWindow(window);

        return true;
    }

    static void loopthing() {}

    // ── Helper functions ──────────────────────────────────────────────────

    // Returns false when WM_QUIT received (caller should exit immediately)
    static bool process_messages()
    {
        MSG msg;
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                timeEndPeriod(1);
                marni::kill();
                config_write();
                if (gGameTable.byte_680592 == 1)
                {
                    gGameTable.byte_680592 = 0;
                    ShowCursor(true);
                }
                SystemParametersInfoA(SPI_SETSCREENSAVEACTIVE, gGameTable.byte_680592, 0, 2);
                return false;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        return true;
    }

    // Returns true if a special state (movie / reset) consumed this frame
    static bool handle_special_states()
    {
        if (gGameTable.movie_r0)
        {
            marni::clear_otags(gGameTable.pMarni);
            reset_geom();
            gGameTable.pMarni->gpu_flag &= ~marni::GpuFlags::GPU_3;
            movie();
            marni::clear(gGameTable.pMarni);
            marni::marni_movie_update(gGameTable.pMarni);
            return true;
        }
        if (gGameTable.reset_r0)
        {
            marni::clear_otags(gGameTable.pMarni);
            reset_geom();
            gGameTable.pMarni->gpu_flag |= marni::GpuFlags::GPU_3;
            reset_screen();
            marni::clear(gGameTable.pMarni);
            marni::draw(gGameTable.pMarni);
            marni::flip(gGameTable.pMarni);
            return true;
        }
        return false;
    }

    static void update_game()
    {
        // Sync catch-up flag from OG (System_trans may set timer_r2 during fades)
        gGameTable.timer_r2 = 0;

        // Speed multiplier: run game logic N times per frame
        int iterations = g_speed_multiplier > 1 ? g_speed_multiplier : 1;
        for (int i = 0; i < iterations; i++)
        {
            // Each psx_main() call must start with a clean ordering table
            // and reset geometry state — psx_main populates draw commands
            // via marni::add_tile/swap_cbuff and leaves state behind.
            marni::clear_otags(gGameTable.pMarni);
            reset_geom();
            gGameTable.byte_6805B4 = 0;
            gGameTable.pMarni->gpu_flag &= ~marni::GpuFlags::GPU_3;

            save_reset();
            if (gGameTable.byte_680597 & 1)
                gGameTable.byte_680597 |= 2;

            psx_main();
        }

        // Frame and game time accounting
        constexpr uint8_t tickMult[3] = { 1, 2, 4 };
        gGameTable.frame_current += tickMult[gGameTable.vsync_rate / 2];
        if (gGameTable.frame_current > 60)
        {
            gGameTable.frame_current = 0;
            ++gGameTable.game_seconds;
        }
    }

    static void render_frame()
    {
        make_font();
        if (!gGameTable.pMarni)
            return;

        if (gGameTable.movie_idx)
        {
            gGameTable.movie_idx--;
            return;
        }

        if (!gGameTable.byte_6805B4 && !gGameTable.byte_680598)
            gGameTable.pMarni->gpu_flag |= marni::GpuFlags::GPU_3;

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
            gGameTable.scaler.type = 15872;
            if (gGameTable.pMarni->xsize == 640)
            {
                gGameTable.scaler.rate_x = 2.0f;
                gGameTable.scaler.rate_y = 2.0f;
            }
            else
            {
                gGameTable.scaler.rate_x = 1.0f;
                gGameTable.scaler.rate_y = 1.0f;
            }
            gGameTable.scaler.prj = gGameTable.global_prj;
            gGameTable.scaler.rgb0 = gGameTable.global_rgb;
            gGameTable.scaler.c_x = gGameTable.global_cx + 160;
            gGameTable.scaler.c_y = gGameTable.global_cy + 120;
            marni::add_primitive_scaler(gGameTable.pMarni, &gGameTable.scaler, 4095);
        }

        marni::clear(gGameTable.pMarni);
        marni::draw(gGameTable.pMarni);

        if (gGameTable.can_draw)
        {
            draw_monitor_effect(gGameTable.can_draw);
            marni::clear_otags(gGameTable.pMarni);
            psp_trans();
            om_trans();
            moji_trans_main();
            marni::draw(gGameTable.pMarni);
            gGameTable.can_draw = 0;
        }

        merge_surface_gdi();
        marni::font_trans(&gGameTable.marni_font, &gGameTable.pMarni->surface0);
        marni::flip(gGameTable.pMarni);
    }

    // ── WinMain ──────────────────────────────────────────────────────────

    // 0x00441ED0
    int win_main(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
    {
        const char* mutexName = "bio2.658b45ea117473d4.game";

        gGameTable.hMutex = OpenMutexA(MUTEX_ALL_ACCESS, 0, mutexName);
        if (gGameTable.hMutex)
        {
            return win_exit(ERROR_18);
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
            gGameTable.pMarni = marni::init(marniPtr, window, 320, 240);
            if (!gGameTable.pMarni->is_gpu_active || !marni::request_display_mode_count(gGameTable.pMarni))
            {
                win_exit(ERROR_FAILED_TO_INITIALIZE_DIRECTX);
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

            // Increase timer resolution for accurate Sleep(1)
            constexpr uint32_t frameRateTable[4] = { 166, 333, 666, 166 };
            timeBeginPeriod(1);

            uint32_t lastFrameTime = gGameTable.timer_last;

            while (true)
            {
                // 1. Process messages (quit → return)
                if (!process_messages())
                    return 0;

                // 2. Window inactive → sleep until next message
                if (!gGameTable.window_active)
                {
                    WaitMessage();
                    continue;
                }

                // 3. Frame rate throttle
                auto now = timeGetTime();
                auto budget = frameRateTable[gGameTable.vsync_rate / 2];
                auto elapsed = 10 * (now - lastFrameTime);

                if (elapsed < budget)
                {
                    if (budget - elapsed >= 30) // >= 3ms margin
                        Sleep(1);
                    continue;
                }

                // 4. Special states (movie / reset)
                if (handle_special_states())
                    continue;

                // 5. Update game logic
                update_game();

                // 6. Render
                render_frame();

                // Keep OG-visible timer fields updated for other hooks
                gGameTable.timer_last = now;
                gGameTable.timer_current = now;
                gGameTable.timer_10 = 10 * now;

                lastFrameTime = now;
            }
        }

        return 0;
    }
}

void onAttach()
{
    logging::initConsoleLogger(logging::LogVerbosity::info);
    logging::logInfo("OpenRE v{} Initializing...", OPENRE_VERSION);

    uint8_t b{};
    interop::readMemory(0x401E40, &b, sizeof(b));
    gClassicRebirthEnabled = (b == 0xE9);

    interop::writeJmp(0x004B7860, load_init_table_1);
    interop::writeJmp(0x004DE650, load_init_table_2);
    interop::writeJmp(0x00505B20, load_init_table_3);
    interop::writeJmp(0x004B2A90, rnd);
    interop::writeJmp(0x00509CF0, ck_installkey);
    interop::writeJmp(0x00441A00, WndProc);
    interop::writeJmp(0x004C3C70, psx_main);
    interop::writeJmp(0x00441ED0, win_main);
    interop::writeJmp(0x004315D0, sub_4315D0);

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
    marni_config_init_hooks();
    math_init_hooks();
    tim::tim_init_hooks();
    window::window_init_hooks();
    if (!gClassicRebirthEnabled)
    {
        input_init_hooks();
        marni::init_hooks();
    }
}

extern "C" {
__declspec(dllexport) BOOL /* WINAPI */
openre_main(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
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
