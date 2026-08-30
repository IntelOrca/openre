#include "title.h"
#include "audio.h"
#include "camera.h"
#include "enemy.h"
#include "entity.h"
#include "file.h"
#include "hud.h"
#include "interop.hpp"
#include "logger.h"
#include "marni.h"
#include "marni_renderer.h"
#include "openre.h"
#include "player.h"
#include "rdt.h"
#include "re2.h"
#include "room.h"
#include "save.h"
#include "scd.h"
#include "scheduler.h"

#include <cstring>

using namespace openre::audio;
using namespace openre::camera;
using namespace openre::enemy;
using namespace openre::file;
using namespace openre::hud;
using namespace openre::player;
using namespace openre::room;
using namespace openre::save;
using namespace openre::scd;

namespace openre::title
{
    // 0x0043F5A0
    void bg_to_surface(uint8_t* data)
    {
        interop::call<uint8_t*>(0x0043F5A0, data);
    }

    // 0x005018B0
    static void sub_5018B0()
    {
        interop::call(0x005018B0);
    }

    // 0x004EF110
    static int16_t Spl(PlayerEntity* pl)
    {
        return interop::call<int16_t, PlayerEntity*>(0x004EF110, pl);
    }

    // 0x004F04B0
    static void spl_set()
    {
        for (auto i = 64; i <= 91; i++)
            gGameTable.enemy_init_map[i] = reinterpret_cast<void*>(&Spl);
    }

    // 0x004B7FF0
    static void esp_init_c()
    {
        interop::call(0x004B7FF0);
    }

    // Module room-warp placement: puts the player at a room's spawn point with
    // the same fresh inventory as a new game. FG_SYSTEM_DEMO is deliberately
    // NOT set so input playback, demo timeout and BGM suppression stay
    // inactive. This is the module replacement for the removed command-line
    // -p/-r title divert; the placement survives the boot wipe because it is
    // re-applied here from the warp latch (core.startRoomWarp ->
    // legacy_request_warp).
    // 0x00506750 (part of title_game_init)
    static void title_game_setup_warp(int32_t stage, int32_t room, int32_t player)
    {
        logging::logDebug("[cmdline] warp init stage={} room={} player={}", stage, room, player);

        gGameTable.current_stage = stage;
        gGameTable.current_room = room;
        gGameTable.pl.id = static_cast<uint8_t>(player > PLD_SHERRY ? PLD_SHERRY : player);

        // Spawn position and cut come from the room table (rdt.cpp); unknown
        // rooms fall back to the world origin with cut 0.
        int16_t x = 0;
        int16_t y = 0;
        int16_t z = 0;
        int16_t cdir = 2048;
        uint8_t cut = 0;
        if (const auto* spawn = rdt::rdt_get_spawn_point(stage, room))
        {
            x = spawn->x;
            y = spawn->y;
            z = spawn->z;
            cdir = spawn->cdir;
            cut = spawn->cut;
        }
        gGameTable.current_cut = cut;

        gGameTable.pl.m.pos.x = x;
        gGameTable.pl.m.pos.y = y;
        gGameTable.pl.m.pos.z = z;
        gGameTable.pl.cdir.y = cdir;
        // Floor is derived from the spawn height the same way the demo branch does.
        const auto mul = static_cast<int64_t>(0x6E5D4C3B) * y;
        const auto hi32 = static_cast<int32_t>(mul >> 32);
        const auto diff = hi32 - y;
        gGameTable.pl.nFloor = static_cast<uint8_t>((diff >> 10) + ((diff >> 31) & 1));

        gGameTable.pl.ground = y;
        gGameTable.pl.old_pos.x = x;
        gGameTable.pl.old_pos.z = z;
        gGameTable.pl.sca_old_x = x; // mirrors the new game branches
        gGameTable.pl.sca_old_z = z;

        set_flag(FlagGroup::System, FG_SYSTEM_EASY, false);
        set_flag(FlagGroup::Status, FG_STATUS_EASY, false);
        // Give the same inventory as a new game (8 slots: handgun, knife, ...
        // plus the character's special item).
        stage_init_item();
        gGameTable.word_98E9B6 = 200;
        gGameTable.pl.life = 200;
        gGameTable.byte_99270E = gGameTable.input_mapping_idx;
        gGameTable.input_mapping_idx = 0;
        gGameTable.demo_ended = 0;
        gGameTable.dword_99CF6C = 0;
    }

    // Demo session setup: position/inventory come from the demo header in
    // gGameTable.pdemo (read by core.loadDemo; outside the boot wipe range).
    // Re-applied in title_game_init LABEL_5 when FG_SYSTEM_DEMO is latched.
    // 0x00506750 (part of title_game_init)
    static void title_game_setup_demo()
    {
        auto& pdemo = gGameTable.pdemo;

        gGameTable.current_stage = pdemo.stage_no;
        set_flag(FlagGroup::System, FG_SYSTEM_EASY, false);
        gGameTable.word_989EB4 = pdemo.equip_id;
        gGameTable.byte_691F6A = pdemo.equip_id;
        gGameTable.current_room = pdemo.room_no;
        gGameTable.byte_691F68 = pdemo.equip_no;
        gGameTable.current_cut = pdemo.cut_no;
        gGameTable.pl.id = pdemo.id;
        gGameTable.pl.m.pos.x = pdemo.pos.x;
        gGameTable.pl.cdir.y = pdemo.pos.y;
        gGameTable.pl.m.pos.z = pdemo.pos.z;
        gGameTable.pl.m.pos.y = pdemo.pos.y;
        gGameTable.pl.cdir.y = static_cast<int16_t>(pdemo.cdir_y);
        auto mul = static_cast<int64_t>(0x6E5D4C3B) * pdemo.pos.y;
        auto hi32 = static_cast<int32_t>(mul >> 32);
        auto diff = hi32 - pdemo.pos.y;
        auto floorDiv = diff >> 10;
        auto correction = (diff >> 31) & 1;
        gGameTable.pl.nFloor = floorDiv + correction;
        set_flag(FlagGroup::Status, FG_STATUS_EASY, false);
        gGameTable.inventory_size = 8;
        std::memcpy(gGameTable.inventory, &pdemo.inventory, 44);
        gGameTable.byte_99270E = gGameTable.input_mapping_idx;
        gGameTable.input_mapping_idx = pdemo.key_idx;
        gGameTable.word_98E9B6 = 200;
        gGameTable.pl.life = 200;
        gGameTable.demo_ended = 0;
        gGameTable.dword_99CF6C = 0;
    }

    // Save restore / scenario-B new game: position, inventory and flags come
    // from the save image in the card buffer. The save image was read into the
    // card buffer by core.loadGame/core.loadSave but the boot chain's
    // init_global() wiped the table afterwards, so apply_saved_image() is
    // deferred here so the restored stage/room/position survive.
    // 0x00506750 (part of title_game_init)
    static void title_game_setup_load()
    {
        apply_saved_image();
        gGameTable.pl.m.pos.x = gGameTable.word_98E9BE;
        gGameTable.pl.id = gGameTable.byte_98E9A6;
        gGameTable.pl.m.pos.y = gGameTable.word_98E9C0;
        gGameTable.pl.m.pos.z = gGameTable.word_98E9C2;
        gGameTable.pl.cdir.y = gGameTable.word_98EE78;
        gGameTable.pl.nFloor = ((int)(((uint64_t)(0x6E5D4C3B * gGameTable.word_98E9C0) >> 32) - gGameTable.word_98E9C0) < 0)
            + +((int)(((uint64_t)(0x6E5D4C3B * gGameTable.word_98E9C0) >> 32) - gGameTable.word_98E9C0) >> 10);
        gGameTable.pl.life = gGameTable.word_98E9B6;
        if (check_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE))
        {
            gGameTable.byte_98E798 = 9;
        }
        gGameTable.hard_mode = gGameTable.byte_98EF2C;
        gGameTable.censorship_off = gGameTable.byte_98EF2D;
        if (gGameTable.byte_98EF2D)
        {
            set_flag(FlagGroup::System, FG_SYSTEM_ARRANGE, true);
        }
        else
        {
            // TODO: Use check_flag
            gGameTable.fg_system &= 0xFFFFFFBF;
        }
    }

    // Fresh-game placement for the FG_SYSTEM_14 new-game branch: stage 0, the
    // scenario A/B start room and the character's starting position.
    // 0x00506750 (part of title_game_init)
    static void title_game_setup_new_game()
    {
        gGameTable.pl.nFloor = 0;
        gGameTable.pl.m.pos.y = 0;
        gGameTable.current_cut = 0;
        gGameTable.current_stage = 0;

        if (check_flag(FlagGroup::Status, FG_STATUS_SCENARIO))
        {
            gGameTable.current_room = 4;
            gGameTable.pl.m.pos.x = -17920;
            gGameTable.pl.m.pos.z = -21722;
            gGameTable.pl.cdir.y = 400;
        }
        // Scenario A
        else
        {
            gGameTable.current_room = 0;
            gGameTable.pl.m.pos.x = 18802;
            gGameTable.pl.m.pos.z = -3164;
            gGameTable.pl.cdir.y = 2048;
        }

        gGameTable.byte_98EE7B = 0;
        gGameTable.saved_splayer_health = 200;
        gGameTable.pl.id = check_flag(FlagGroup::Status, FG_STATUS_PLAYER);
        gGameTable.word_98E9B6 = 200;
        gGameTable.pl.life = 200;
    }

    // 0x00506750
    //
    // Game-setup state machine driven from game_loop case 0. Every start is
    // module-driven now (core.runGame latched demo / new game / save / room
    // warp before the boot), so the native title and its prologue are gone and
    // the legacy branches (command-line warp divert, FG_SYSTEM_4, 4th
    // survivor, extreme battle) no longer exist here. The state machine only
    // walks the module branches then hands the room/player setup to cases 4-6.
    void title_game_init()
    {
        auto& ctcb = *gGameTable.ctcb;
        switch (ctcb.var_09)
        {
        case 1: goto LABEL_53;
        case 2: [[fallthrough]];
                case 3:
                {
                    gGameTable.dword_99CF6C = 1;

            // Module demo start: the boot chain's init_global() wiped
            // FG_SYSTEM_DEMO, so re-apply it from the latch (set by
            // core.loadDemo) before the demo branch below is selected.
            // Consume the latch so a later non-demo start is not hijacked.
            if (legacy_demo_requested())
            {
                set_flag(FlagGroup::System, FG_SYSTEM_DEMO, true);
                legacy_clear_demo_requested();
            }

            // Module new-game start: init_global() also wiped the
            // FG_SYSTEM_14 path and the player/scenario/difficulty flags
            // core.startNewGame latched, so re-apply them here so the
            // new-game branch below (sub_505B20 + fresh stage/inventory)
            // runs with the menu's chosen character and difficulty.
            if (legacy_new_game_requested())
            {
                set_flag(FlagGroup::System, FG_SYSTEM_14, true);
                set_flag(FlagGroup::Status, FG_STATUS_PLAYER, legacy_new_game_player() != 0);
                set_flag(FlagGroup::Status, FG_STATUS_SCENARIO, legacy_new_game_scenario() != 0);
                set_flag(FlagGroup::System, FG_SYSTEM_EASY, legacy_new_game_difficulty() == 1);
                gGameTable.hard_mode = legacy_new_game_difficulty() == 2 ? 1 : 0;
                legacy_clear_new_game_requested();
            }

            if (!check_flag(FlagGroup::System, FG_SYSTEM_14))
            {
                // Module room warp (core.startRoomWarp): places the player at
                // the room's spawn point, re-applying the player/scenario
                // flags that the command-line -p/-r divert used to set before
                // entering game_loop.
                if (legacy_warp_requested())
                {
                    const auto player = legacy_warp_player();
                    set_flag(FlagGroup::Status, FG_STATUS_PLAYER, (player & 1) != 0);
                    set_flag(FlagGroup::Status, FG_STATUS_SCENARIO, player == PLD_LEON_1 || player == PLD_CLAIRE_1);
                    title_game_setup_warp(legacy_warp_stage(), legacy_warp_room(), player);
                    legacy_clear_warp_requested();
                }
                else if (check_flag(FlagGroup::System, FG_SYSTEM_DEMO))
                {
                    title_game_setup_demo();
                }
                else
                {
                    title_game_setup_load();
                }

                gGameTable.last_cut = 255;
                cmdline_consume_start();
                goto LABEL_32;
            }

            if (check_flag(FlagGroup::System, FG_SYSTEM_EASY))
            {
                set_flag(FlagGroup::Status, FG_STATUS_EASY, true);
                if (gGameTable.censorship_off)
                {
                    set_flag(FlagGroup::System, FG_SYSTEM_12, true);
                }
            }
            sub_505B20();
            if (!ctcb.var_13)
            {
                // New game
                title_game_setup_new_game();

                gGameTable.pl.old_pos.x = gGameTable.pl.m.pos.x;
                gGameTable.pl.sca_old_x = gGameTable.pl.m.pos.z;
                gGameTable.pl.old_pos.z = gGameTable.pl.m.pos.z;
                gGameTable.pl.ground = gGameTable.pl.m.pos.y;
                gGameTable.poison_status = 0;
                gGameTable.poison_timer = 0;
                gGameTable.input_mapping_idx = gGameTable.byte_98F1B6;
                stage_init_item();
                gGameTable.byte_98EF2C = gGameTable.hard_mode;
                gGameTable.byte_98EF2D = gGameTable.censorship_off;
                gGameTable.dword_98E99C = 0;

            LABEL_32:
                sub_5018B0();
                switch (gGameTable.byte_989E7E)
                {
                case 0: [[fallthrough]];
                case 1: snd_load_core(0, 4); break;
                case 2: [[fallthrough]];
                case 3: snd_load_core(1, 4); break;
                default: break;
                }

                snd_sys_init_sub2();
                esp_init_c();
                if (!ctcb.var_13)
                {
                    em_move_tbl_set();
                    set_stage();
                    ctcb.var_09 = 4;
                }
            }
            return;
        }
        case 4:
        {
            gGameTable.mem_top = reinterpret_cast<void*>(gGameTable.dword_988620);
            gGameTable.player_work = &gGameTable.pl;
            gGameTable.pl.be_flg = 1;
            player_set(&gGameTable.pl);
            if (ctcb.var_13)
            {
                return;
            }
            spl_set();
            kage_work_init();
            kage_work9_init();
            scd_init();
            ctcb.var_09 = 5;
        LABEL_45:
            room_set();
            if (!ctcb.var_13)
            {
                set_flag(FlagGroup::System, FG_STATUS_SCENARIO, true);
                ctcb.var_09 = 6;
            LABEL_47:
                if (gGameTable.fg_message < 0)
                {
                    task_sleep(1);
                }
                set_flag(FlagGroup::System, FG_STATUS_SCENARIO, true);
                set_game_seconds(gGameTable.dword_98E99C);
                gGameTable.byte_98F07A = 2;
                bg_set_mode(0, 0);
                gGameTable.byte_98F07B = 1;
                cut_change(static_cast<uint8_t>(gGameTable.current_cut));
                cut_check(1);
                ctcb.var_09 = 0;
            }
            return;
        }
        case 5: goto LABEL_45;
        case 6: goto LABEL_47;
        default:
        {
            gGameTable.byte_991F80 = 3;
            gGameTable.byte_98F1B7 = 0;
            bg_set_mode(2, 0);
            hud_fade_off(0);
            if (check_flag(FlagGroup::System, FG_SYSTEM_14))
            {
                ctcb.var_09 = 1;
            }
        LABEL_53:
            gGameTable.byte_98E9A5 = (gGameTable.byte_9888D9 | gGameTable.byte_98E9A5) & 1;
            gGameTable.byte_989E7E = (gGameTable.fg_status >> 30) & 2;
            if (check_flag(FlagGroup::Status, FG_STATUS_SCENARIO))
            {
                gGameTable.byte_989E7E++;
            }
            ctcb.var_09 = 2;
            task_sleep(1);
        }
        }
    }
}