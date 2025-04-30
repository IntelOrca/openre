#include "title.h"
#include "audio.h"
#include "camera.h"
#include "enemy.h"
#include "entity.h"
#include "file.h"
#include "hud.h"
#include "interop.hpp"
#include "marni.h"
#include "openre.h"
#include "player.h"
#include "re2.h"
#include "room.h"
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
using namespace openre::scd;

namespace openre::title
{
    using Action = void (*)();

    enum
    {
        TITLE_STATE_CAPCOM_LOGO,
        TITLE_STATE_TITLE_LOAD,
        TITLE_STATE_TITLE_MAIN,
        TITLE_STATE_4TH_SURVIVOR_LOAD,
        TITLE_STATE_4TH_SURVIVOR_MAIN,
        TITLE_STATE_EXTREME_BATTLE_LOAD,
        TITLE_STATE_EXTREME_BATTLE_MAIN,
        TITLE_STATE_7,
    };

    // 0x0050AA80
    static void config_write()
    {
        interop::call(0x0050AA80);
    }

    // 0x00503880
    static void title_bg_reload()
    {
        interop::call(0x00503880);
    }

    // 0x00505B80
    static void moji_set_work()
    {
        interop::call(0x00505B80);
    }

    // 0x005038B0
    static void title_init()
    {
        config_write();
        gGameTable.hard_mode = 0;
        gGameTable.censorship_off = 0;
        gGameTable.byte_68984A = 0;
        gGameTable.word_98EE7E = 2;
        gGameTable.byte_989E91 = 2;
        gGameTable.fg_system &= 0xFFF7FFBF;
        marni::out();
        gGameTable.byte_98F1B7 = 1;
        gGameTable.byte_98F07A = 0;
        gGameTable.dword_99CF6C = 0;
        gGameTable.title_mode = 0;
        gGameTable.title_cursor = 0;
        gGameTable.byte_691B88 = 0;
        gGameTable.byte_691B89 = 1;
        gGameTable.title_disp_add = 0;
        gGameTable.byte_691B82 = 0;
        gGameTable.byte_691B83 = 0;
        gGameTable.byte_691B85 = 0;
        gGameTable.demo_countdown = 0;
        gGameTable.byte_691B8D = 0;
        gGameTable.byte_691B8E = 0;
        gGameTable.word_691B98 = -46;
        gGameTable.word_691B96 = 0;
        gGameTable.dword_691B9C = -1;

        if (check_flag(FlagGroup::System, FG_SYSTEM_2))
        {
            set_flag(FlagGroup::System, FG_SYSTEM_2, false);
            bg_set_mode(2, 0);
            title_bg_reload();
            hud_fade_set(0x100, 0, 7, 1);
            hud_fade_adjust(0, 0x7F80, 0, 0);
            gGameTable.word_691B98 = 240;
            gGameTable.title_disp_add = 5;
        }
        else
        {
            bg_set_mode(0, 0);
            title_bg_reload();
            hud_fade_set(0x200, 0, 7, 1);
            hud_fade_adjust(0, 0x7C00, 0, 0);
            gGameTable.ti_add = -1024;
            gGameTable.ti_kido = 0x7C00;
            gGameTable.title_disp_add = 1;
        }

        moji_set_work();
        gGameTable.title_mv_state++;
    }

    // 0x004450C0
    static void sub_4450C0(int a0)
    {
        interop::call<void, int>(0x004450C0, a0);
    }

    // 0x0043DF40
    static void sub_43DF40()
    {
        interop::call(0x0043DF40);
    }

    // 0x0050B910
    static int sub_50B910(int a0)
    {
        return interop::call<int, int>(0x0050B910, a0);
    }

    // 0x00509CB0
    static void sub_509CB0(int a0)
    {
        interop::call<void, int>(0x00509CB0, a0);
    }

    // 0x004CAEC0
    static void init_movie_work(int id)
    {
        interop::call<void, int>(0x004CAEC0, id);
    }

    // 0x00503810
    static void title_bg_load()
    {
        interop::call(0x00503810);
    }

    // 0x00503680
    static void capcom_logo()
    {
        auto& ctcb = *gGameTable.ctcb;
        switch (ctcb.var_09)
        {
        case 0:
        {
            marni::result_unload_textures();
            sub_4450C0(1);
            sub_43DF40();
            snd_sys_init2();
            ctcb.var_09 = 4;
            [[fallthrough]];
        }
        case 4:
        {
            snd_load_core(0x11, 0);
            if (ctcb.var_13 == 0)
            {
                ctcb.var_09 = 5;
                task_sleep(120);
            }
            break;
        }
        case 5:
        {
            gGameTable.byte_98F1B9 = 2;
            ctcb.var_09 = 6;
            task_sleep(1);
            break;
        }
        case 6:
        {
            if (check_flag(FlagGroup::System, FG_SYSTEM_30))
            {
                title_bg_load();
                if (ctcb.var_13)
                {
                    break;
                }
                ctcb.var_09 = 7;
            LABEL_17:
                if (gGameTable.byte_98F1B9)
                {
                    task_sleep(1);
                    break;
                }
                if (gGameTable.byte_689F24 == 0)
                {
                    auto res = sub_50B910(2);
                    sub_509CB0(res);
                    init_movie_work(0);
                    ctcb.var_09 = 8;
                    if (check_flag(FlagGroup::System, FG_SYSTEM_22))
                    {
                        task_sleep(1);
                        break;
                    }
                }
                if (++gGameTable.byte_689F24 >= 6)
                {
                    gGameTable.byte_689F24 = 0;
                }
                title_bg_reload();
                if (gGameTable.byte_98F1BB == 2)
                {
                    set_flag(FlagGroup::System, FG_SYSTEM_30, false);
                    set_flag(FlagGroup::System, FG_SYSTEM_31, false);
                }
            }
            else
            {
                ctcb.var_09 = 9;
            LABEL_8:
                title_bg_load();
                title_bg_reload();
            }

            gGameTable.byte_981FB7 = 1;
            gGameTable.title_mv_state = TITLE_STATE_TITLE_LOAD;
            if (check_flag(FlagGroup::System, FG_SYSTEM_4TH_SURVIVOR))
            {
                gGameTable.title_mv_state = TITLE_STATE_4TH_SURVIVOR_LOAD;
            }
            if (check_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE))
            {
                gGameTable.title_mv_state = TITLE_STATE_EXTREME_BATTLE_LOAD;
            }
            ctcb.var_09 = 0;
            break;
        }
        case 7:
        {
            goto LABEL_17;
        }
        case 8:
        {
            if (check_flag(FlagGroup::System, FG_SYSTEM_22))
            {
                task_sleep(1);
                break;
            }
        }
        case 9:
        {
            goto LABEL_8;
        }
        }
    }

    static Action title_mv[] = {
        capcom_logo,        title_init,
        (Action)0x00503A20, // title_main
        (Action)0x00505460, // title_survivor_load
        (Action)0x00505670, // title_survivor_main
        (Action)0x00506F90, // title_extreme_load
        (Action)0x00507100, // title_extreme_main
        (Action)0x00507AB0  // ?
    };

    // 0x004D1150
    static void pad_rep_set(uint32_t arg0, uint16_t arg1)
    {
        gGameTable.dword_98F074 = arg0;
        gGameTable.word_98F078 = arg1;
    }

    // 0x00440480
    static void add_sprt_v(int x, int y, int w, int h, int u, int v, int clut, int page, int depth, int isBack)
    {
        using sig = void (*)(int, int, int, int, int, int, int, int, int, int);
        auto p = (sig)0x00440480;
        p(x, y, w, h, u, v, clut, page, depth, isBack);
    }

    // 0x005065C0
    static void move_and_display_title_image()
    {
        auto& gameTable = gGameTable;
        if (gameTable.word_691B96 != 0)
            gameTable.word_691B96 -= 2;
        if (gameTable.dword_691B9C >= 0)
        {
            gameTable.dword_691B9C--;
            gameTable.word_691B98 = 46 * gameTable.dword_691B9C * gameTable.dword_691B9C / 484 - 46;
        }
        gameTable.word_691D7A = gameTable.word_691B98 + 76;
        gameTable.word_691D8E = gameTable.word_691B98 + 76;
        gameTable.word_691DA2 = gameTable.word_691B98 + 76;
        add_sprt_v(gGameTable.word_691D78, gameTable.word_691D7A, 128, 80, 0, 0, 0, 19, 6, 0);
        add_sprt_v(150, gameTable.word_691D8E, 128, 48, 0, 80, 0, 19, 6, 0);
        add_sprt_v(150, gameTable.word_691D8E + 48, 128, 32, 0, 0, 0, 20, 6, 0);
        add_sprt_v(278, gameTable.word_691DA2, 128, 80, 0, 32, 0, 20, 6, 0);
    }

    // 0x005035B0
    static void title()
    {
        gGameTable.timer_r2 = 1;
        if (!(gGameTable.vk_press & 0x40))
        {
            if (gGameTable.vk_press & 8)
                gGameTable.error_no = 255;
        }
        else if (!(gGameTable.vk_press & 8))
        {
            gGameTable.reset_r0 = 4;
            gGameTable.vk_press &= ~0x40;
            if (gGameTable.vk_press & 8)
                gGameTable.error_no = 255;
        }
        else
        {
            gGameTable.error_no = 255;
        }

        auto& ctcb = *gGameTable.ctcb;
        if (ctcb.var_08 != 0)
        {
            if (ctcb.var_08 != 1)
                return;
        }
        else
        {
            pad_rep_set(0x5000, 522);
            gGameTable.title_mv_state = TITLE_STATE_CAPCOM_LOGO;
            moji_set_work();
            if (gGameTable.byte_989E90 != 0)
            {
                gGameTable.title_mv_state = TITLE_STATE_7;
                snd_load_core(0x10u, 0);
                snd_room_load();
                title_bg_load();
            }
            ctcb.var_08 = 1;
        }
        title_mv[gGameTable.title_mv_state]();
        if (ctcb.var_13 == 0)
        {
            if (gGameTable.title_mv_state == TITLE_STATE_TITLE_MAIN
                || gGameTable.title_mv_state == TITLE_STATE_4TH_SURVIVOR_LOAD)
                move_and_display_title_image();
            task_sleep(1);
        }
    }

    // 0x004CEFF0
    static void prologue()
    {
        interop::call(0x004CEFF0);
    }

    // 0x00500EE0
    static void stage_init_item()
    {
        interop::call(0x00500EE0);
    }

    // 0x004E3A20
    static void sub_4E3A20()
    {
        interop::call(0x004E3A20);
    }

    // 0x005018B0
    static void sub_5018B0()
    {
        interop::call(0x005018B0);
    }

    // 0x004F04B0
    static void spl_set()
    {
        interop::call(0x004F04B0);
    }

    // 0x004B7FF0
    static void esp_init_c()
    {
        interop::call(0x004B7FF0);
    }

    // 0x004EA320
    static void ex_battle()
    {
        interop::call(0x004EA320);
    }

    // 0x00506750
    static void title_game_init()
    {
        auto& ctcb = *gGameTable.ctcb;
        switch (ctcb.var_09)
        {
        case 1: goto LABEL_53;
        case 2:
        {
            // Skip prologue if playing extreme battle, 4th survivor or demo
            if (check_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE) || check_flag(FlagGroup::System, FG_SYSTEM_DEMO)
                || check_flag(FlagGroup::System, FG_SYSTEM_4TH_SURVIVOR))
            {
                goto LABEL_5;
            }
            task_execute(1, &prologue);
            ctcb.var_09 = 3;
            task_sleep(1);
            return;
        }
        case 3:
        {
        LABEL_5:
            gGameTable.dword_99CF6C = 1;

            if (!check_flag(FlagGroup::System, FG_SYSTEM_14))
            {
                if (check_flag(FlagGroup::System, FG_SYSTEM_DEMO))
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
                    gGameTable.byte_99270E = gGameTable.byte_98E9AA;
                    gGameTable.byte_98E9AA = pdemo.key_idx;
                    gGameTable.word_98E9B6 = 200;
                    gGameTable.pl.life = 200;
                    gGameTable.byte_98F1BB = 0;
                    gGameTable.dword_99CF6C = 0;
                }
                else if (check_flag(FlagGroup::System, FG_SYSTEM_4))
                {
                    gGameTable.pl.sca_old_x = 18802;
                    gGameTable.pl.old_pos.x = 18802;
                    gGameTable.pl.m.pos.x = 18802;
                    gGameTable.pl.sca_old_z = -3164;
                    gGameTable.pl.old_pos.z = -3164;
                    gGameTable.pl.m.pos.z = -3164;
                    gGameTable.current_stage = 0;
                    gGameTable.current_room = 25;
                    gGameTable.pl.ground = 0;
                    gGameTable.pl.m.pos.y = 0;
                    gGameTable.pl.cdir.y = 2048;
                    gGameTable.pl.nFloor = 0;
                    gGameTable.current_cut = 0;
                    gGameTable.saved_splayer_health = 200;
                    gGameTable.byte_98EE7B = 0;
                    gGameTable.pl.id = check_flag(FlagGroup::Status, FG_STATUS_PLAYER);
                    gGameTable.word_98E9B6 = 200;
                    gGameTable.pl.life = 200;
                    gGameTable.poison_status = 0;
                    gGameTable.poison_timer = 0;
                    gGameTable.byte_98E9AA = gGameTable.byte_98F1B6;
                    stage_init_item();
                    gGameTable.dword_689F20 = 1;
                    bg_set_mode(2, 0);
                    gGameTable.dword_99CF6C = 0;
                }
                else
                {
                    // New game Scenario B or Load Game
                    gGameTable.pl.m.pos.x = gGameTable.word_98E9BE;
                    gGameTable.pl.id = gGameTable.byte_98E9A6;
                    gGameTable.pl.m.pos.y = gGameTable.word_98E9C0;
                    gGameTable.pl.m.pos.z = gGameTable.word_98E9C2;
                    gGameTable.pl.cdir.y = gGameTable.word_98EE78;
                    gGameTable.pl.nFloor
                        = ((int)(((uint64_t)(0x6E5D4C3B * gGameTable.word_98E9C0) >> 32) - gGameTable.word_98E9C0) < 0)
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

                gGameTable.last_cut = 255;
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
                // 4th survivor mode
                if (check_flag(FlagGroup::System, FG_SYSTEM_4TH_SURVIVOR))
                {
                    gGameTable.current_stage = 2;
                    gGameTable.current_room = 4;
                    gGameTable.current_cut = 3;
                    gGameTable.pl.m.pos.x = -7423;
                    gGameTable.pl.m.pos.y = 0;
                    gGameTable.pl.m.pos.z = -24492;
                    gGameTable.pl.cdir.y = 2048;
                    gGameTable.pl.nFloor = 0;

                    // TOFU 4th survivor mode
                    if (check_flag(FlagGroup::System, FG_SYSTEM_31))
                    {
                        gGameTable.pl.id = PLD_TOFU;
                        gGameTable.pl.life = 400;
                    }
                    // Normal 4th survivor mode
                    else
                    {
                        gGameTable.pl.id = PLD_HUNK;
                        gGameTable.pl.life = 200;
                    }
                }
                // Extreme battle mode
                else if (check_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE))
                {
                    gGameTable.word_98EB20 = gGameTable.byte_540780[gGameTable.pl.id];
                    gGameTable.word_98EB22 = gGameTable.byte_691EF3 + 1;
                    gGameTable.word_98EE7E = gGameTable.byte_989E91;
                    std::memcpy(&gGameTable.dword_98EEF0, &gGameTable.dword_989E94, 0x3C);
                    gGameTable.current_stage = 5;
                    gGameTable.byte_98E798 = 9;
                    gGameTable.current_room = 18;
                    gGameTable.current_cut = 1;
                    gGameTable.pl.m.pos.x = -17674;
                    gGameTable.pl.m.pos.y = 0;
                    gGameTable.pl.m.pos.z = -7923;
                    gGameTable.pl.cdir.y = -1707;
                    gGameTable.pl.nFloor = 0;
                    gGameTable.pl.life = 200;
                    sub_4E3A20();
                }
                // New game
                else
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

                gGameTable.pl.old_pos.x = gGameTable.pl.m.pos.x;
                gGameTable.pl.sca_old_x = gGameTable.pl.m.pos.z;
                gGameTable.pl.old_pos.z = gGameTable.pl.m.pos.z;
                gGameTable.pl.ground = gGameTable.pl.m.pos.y;
                gGameTable.poison_status = 0;
                gGameTable.poison_timer = 0;
                gGameTable.byte_98E9AA = gGameTable.byte_98F1B6;
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
                case 4:
                {
                    if (check_flag(FlagGroup::System, FG_SYSTEM_31))
                    {
                        snd_load_core(13, 4);
                    }
                    else
                    {
                        snd_load_core(12, 4);
                    }
                    break;
                }
                case 5:
                {
                    switch (gGameTable.byte_540780[gGameTable.pl.id])
                    {
                    case 0: snd_load_core(0, 4); break;
                    case 1: snd_load_core(1, 4); break;
                    case 2: snd_load_core(14, 4); break;
                    case 3: snd_load_core(11, 4); break;
                    default: break;
                    }
                    break;
                }
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
            if (check_flag(FlagGroup::System, FG_SYSTEM_14) && !check_flag(FlagGroup::System, FG_SYSTEM_4TH_SURVIVOR))
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
            if (check_flag(FlagGroup::System, FG_SYSTEM_4TH_SURVIVOR))
            {
                gGameTable.byte_989E7E = 4;
                set_flag(FlagGroup::Status, FG_STATUS_BONUS, true);
            }
            if (check_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE))
            {
                gGameTable.byte_989E7E = 5;
                if (check_flag(FlagGroup::System, FG_SYSTEM_14))
                {
                    task_execute(1, &ex_battle);
                }
            }
            ctcb.var_09 = 2;
            task_sleep(1);
        }
        }
    }

    void title_init_hooks()
    {
        interop::writeJmp(0x005035B0, &title);
        interop::writeJmp(0x00506750, &title_game_init);
    }
}
