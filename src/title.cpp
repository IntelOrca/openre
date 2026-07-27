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

    // 0x0043F5A0
    void bg_to_surface(uint8_t* data)
    {
        interop::call<uint8_t*>(0x0043F5A0, data);
    }

    // 0x00503880
    static void title_bg_reload()
    {
        auto dst = gGameTable.work_buffer_2 + 0x640;
        if (load_adt("common\\data\\title_bg.adt", dst, 4))
        {
            bg_to_surface(dst);
        }
        else
        {
            file_error();
        }
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

    // 0x00503A70
    static void title_main_wait()
    {
        interop::call(0x00503A70);
    }

    // 0x00503B80
    static void title_main_select()
    {
        interop::call(0x00503B80);
    }

    // 0x00505320
    static void title_main_load()
    {
        interop::call(0x00505320);
    }

    static void title_game_init();

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

    // 0x004C0840
    static void door_set()
    {
        interop::call(0x004C0840);
    }

    // 0x004C4D70
    static void init_global()
    {
        interop::call(0x004C4D70);
    }

    // 0x004CAE34
    static void moji_mode_init()
    {
        interop::call(0x004CAE34);
    }

    // 0x00507C60
    static void trans_pointer_set()
    {
        interop::call(0x00507C60);
    }

    // 0x004E42D0
    static void sce_scheduler()
    {
        interop::call(0x004E42D0);
    }

    // 0x004EA0D0
    static void sce_at()
    {
        interop::call(0x004EA0D0);
    }

    // 0x004CCD70
    static void om_move()
    {
        interop::call(0x004CCD70);
    }

    // 0x004CD090
    static void om_trans()
    {
        interop::call(0x004CD090);
    }

    // 0x004DD3B0
    static void psp_trans()
    {
        interop::call(0x004DD3B0);
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
        interop::call<void, int, int, int>(0x00446D30, r, g, b);
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

    // 0x004CC6D0
    static void oba_ck_em2(ActorEntity* entity)
    {
        interop::call<void, ActorEntity*>(0x004CC6D0, entity);
    }

    // 0x00503350
    static void set_front_pos(ActorEntity* entity)
    {
        interop::call<void, ActorEntity*>(0x00503350, entity);
    }

    // 0x004E03B0
    static int sca_ck_info(Vec32* pos, int a1, int a2, int a3)
    {
        return interop::call<int, Vec32*, int, int, int>(0x004E03B0, pos, a1, a2, a3);
    }

    // 0x004E2AE0
    static int sca_ck_hit(Vec32* vec, int a1, int a2, int a3)
    {
        return interop::call<int, Vec32*, int, int, int>(0x004E2AE0, vec, a1, a2, a3);
    }

    // 0x00451780
    static int square_root_0(int a0)
    {
        return interop::call<int, int>(0x00451780, a0);
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

    // 0x004BF810
    static void game_loop()
    {
        auto& ctcb = *gGameTable.ctcb;

        if (gGameTable.byte_680597 & 2)
        {
            gGameTable.pl.life = (gGameTable.fg_system & 1) ? 400 : 200;
            gGameTable.byte_680597 &= 1;
        }

        auto vk_press = gGameTable.vk_press;
        if (vk_press & 1)
        {
            vk_press &= ~1u;
            gGameTable.vk_press = vk_press;
            if (!(gGameTable.fg_system & 0x2000))
            {
                gGameTable.dword_9885FE |= 0x800;
            }
        }
        if (vk_press & 2)
        {
            vk_press &= ~2u;
            gGameTable.vk_press = vk_press;
            if (!(gGameTable.fg_system & 0x2000))
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
            task_chain(title);
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
            if ((gGameTable.fg_system & 0x1000000) && !check_flag(FlagGroup::Common, 0xFE))
            {
                task_execute(1, exbtl_opening);
                set_flag(FlagGroup::Common, 0xFE, true);
            }
            goto LABEL_94;
        case 2:
            if (gGameTable.fg_system & 0x200000)
            {
                task_chain(result);
                return;
            }
            if (!(gGameTable.fg_system & 0x40000000))
            {
                if (!(gGameTable.fg_system & 0x88) && !(gGameTable.fg_status & 0x40000000) && (gGameTable.fg_system & 0x20000)
                    && !check_flag(FlagGroup::Common, 6) && gGameTable.word_98E9BC == 0)
                {
                    gGameTable.fg_system |= 0x80;
                }
                goto LABEL_80;
            }
            gGameTable.dword_991FC0 = 0;
            if (gGameTable.byte_98F1B8)
            {
                goto LABEL_159;
            }
            if (gGameTable.fg_system & 4)
            {
                if (++gGameTable.word_6897C4 == 16)
                {
                    task_execute(1, die);
                    gGameTable.fg_message = 0;
                    gGameTable.word_6897C4 = -30000;
                }
                goto LABEL_159;
            }
            if ((gGameTable.fg_system & 0x2000) && gGameTable.byte_991F80 == 0
                && gGameTable.word_98E52A > gGameTable.pdemo.frames)
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
                if (!(gGameTable.fg_status & 0x8000) && !(gGameTable.fg_system & 0x40000))
                {
                    if ((gGameTable.fg_system & 0x1000000) && (uint8_t)gGameTable.dword_98EBD0 == 0xFF)
                    {
                        set_flag(FlagGroup::Zapping, 0x3F, true);
                    }
                    if (!(gGameTable.fg_system & 0x1000000) || !check_flag(FlagGroup::Zapping, 0x3F))
                    {
                        gGameTable.fg_system |= 0x10000;
                        bg_set_mode(1, 0);
                    }
                    gGameTable.fg_stop = 0;
                    gGameTable.fg_system |= 0x4000;
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
            if (gGameTable.fg_status & 0x1000000)
            {
                goto LABEL_153;
            }
            if (!(gGameTable.fg_status & 0x8000000))
            {
                goto LABEL_120;
            }
            if (gGameTable.fg_system & 8)
            {
                if (gGameTable.word_6897C4 > 29 || !(gGameTable.dword_6897F0 & 0x8000000))
                {
                    gGameTable.word_6897C4 = 0;
                    gGameTable.word_6897DC = 0;
                    gGameTable.word_98EB2A++;
                }
                auto timer = (int16_t)gGameTable.word_98EB2A;
                // 0x0052D800: "%2d^%d%d=%d%d"
                prim_14(
                    104,
                    32,
                    1,
                    2,
                    "%2d^%d%d=%d%d",
                    timer / 60,
                    timer % 60 / 10,
                    timer % 60 % 10,
                    gGameTable.word_6897C4 / 3,
                    timer_blink_tbl[gGameTable.word_6897DC % 9]);
                if (gGameTable.word_98EB2A == 5999 && gGameTable.word_6897C4 == 27)
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
                    // 0x0052D800: "%2d^%d%d=%d%d"
                    prim_14(
                        104,
                        32,
                        timer < 60 ? 2 : 1,
                        2,
                        "%2d^%d%d=%d%d",
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
                        gGameTable.fg_system |= 4;
                        gGameTable.fg_status &= ~0x8000000u;
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
            if (!(gGameTable.fg_status & 0x100))
            {
                cut_check(0);
            }
            sce_scheduler();
            if (!task_status(1) && gGameTable.current_cut == gGameTable.byte_989EEA && !gGameTable.byte_98F07B
                && gGameTable.byte_991F80 == 0 && !(gGameTable.fg_status & 0x40) && !(gGameTable.fg_stop & 0x1000000)
                && !gGameTable.pl.damage_cnt && !(gGameTable.fg_system & 0x4000200))
            {
                if ((gGameTable.dword_9885FE & 0x100) && !(gGameTable.fg_status & 0x8000))
                {
                    gGameTable.fg_system |= 0x8000000;
                    gGameTable.fg_status |= 0x8000;
                }
                if ((gGameTable.dword_9885FE & 0x800) && !(gGameTable.fg_status & 0x10))
                {
                    gGameTable.fg_status |= 0x8000;
                }
                if ((gGameTable.key_trg & 0x4000) && !(gGameTable.fg_status & 0x10))
                {
                    gGameTable.hud_mode = 3;
                    gGameTable.fg_status |= 0x8000;
                }
                if (gGameTable.fg_status & 0x8000)
                {
                    gGameTable.fg_status |= 0x40;
                    gGameTable.dword_991FC4 = gGameTable.fg_stop;
                    gGameTable.fg_stop |= 0xFF000000;
                    gGameTable.byte_991F80 = 1;
                    if (gGameTable.hud_mode != 2)
                    {
                        snd_se_on(0x4060000);
                    }
                    goto LABEL_159;
                }
            }
            gGameTable.dword_991FC0 |= 1;
            gGameTable.fg_room_enemy &= 0x00FF;
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
            if ((gGameTable.fg_system & 0x4000000) && !task_status(1) && !gGameTable.byte_98F07B
                && gGameTable.current_cut == gGameTable.byte_989EEA)
            {
                task_execute(1, die);
                gGameTable.fg_system &= ~0x4000000u;
            }
        LABEL_159:
            if (gGameTable.fg_status & 0x10000)
            {
                mirror_matrix_set();
            }
            if (!gGameTable.can_draw && !(gGameTable.fg_system & 0x100000))
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
                if (gGameTable.fg_status & 0x10000)
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
            if (gGameTable.fg_system & 0x200000)
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
            if ((gGameTable.fg_system & 0x2000) && gGameTable.word_98E52A > gGameTable.pdemo.frames)
            {
                marni::out();
            LABEL_80:
                bg_set_mode(2, 0);
                if (!(gGameTable.fg_system & 0x2000) || gGameTable.byte_98F1BB)
                {
                    gGameTable.byte_98F1BB = 1;
                    init_global();
                    task_chain(title);
                    ctcb.var_08 = 0;
                }
                else
                {
                    init_global();
                    gGameTable.byte_98F1B9 = 0;
                    gGameTable.fg_system |= 2;
                    task_execute(1, sub_4C0820);
                    task_chain(title);
                    ctcb.var_08 = 0;
                }
            }
            else
            {
                gGameTable.fg_stop = 0;
                gGameTable.fg_system |= 0x4000;
                gGameTable.dword_6897C8 = 0;
                if (gGameTable.fg_status & 0x8000)
                {
                    if (gGameTable.fg_system & 0x8000000)
                    {
                        gGameTable.fg_system &= ~0x8000000u;
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
                else if (gGameTable.fg_system & 0x40000)
                {
                    gGameTable.fg_system &= ~0x40000u;
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
            gGameTable.fg_status &= ~0x8040u;
            goto LABEL_91;
        case 7: goto LABEL_89;
        case 9: gGameTable.fg_system &= ~0x400u; goto LABEL_93;
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
            gGameTable.fg_system &= ~0x4000u;
            gGameTable.fg_stop |= 0xEF000000;
            hud_fade_set(512, -6144, 7, 1);
            if ((gGameTable.fg_system & 8) && gGameTable.current_stage == 0 && gGameTable.current_room == 28)
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

    // 0x00503A30
    static void title_main_game()
    {
        marni::unload_texture_page(18);
        marni::unload_texture_page(19);
        marni::unload_texture_page(20);
        marni::unload_texture_page(21);
        marni::unload_texture_page(22);
        task_chain(game_loop);
    }

    // 0x00505400
    static void title_main_option()
    {
        interop::call(0x00505400);
    }

    // 0x00503B20
    static void sub_503B20()
    {
        interop::call(0x00503B20);
    }

    // 0x00503A20
    static void title_main()
    {
        static Action Title_main_mv[] = {
            title_main_wait,   // Ti_Disp_add = 0
            title_main_select, // Ti_Disp_add = 1
            title_main_load,   // Ti_Disp_add = 2
            title_main_game,   // Ti_Disp_add = 3
            title_main_option, // Ti_Disp_add = 4
            sub_503B20,        // Ti_Disp_add = 5
        };
        Title_main_mv[gGameTable.title_disp_add]();
    }

    // 0x00505460
    static void title_survivor_load()
    {
        interop::call(0x00505460);
    }

    // 0x00505670
    static void title_survivor_main()
    {
        interop::call(0x00505670);
    }

    // 0x00506F90
    static void title_extreme_load()
    {
        interop::call(0x00506F90);
    }

    // 0x00507100
    static void title_extreme_main()
    {
        interop::call(0x00507100);
    }

    // 0x00507AB0
    static void title_state_7()
    {
        interop::call(0x00507AB0);
    }

    static Action title_mv[] = {
        capcom_logo,         title_init,         title_main,         title_survivor_load,
        title_survivor_main, title_extreme_load, title_extreme_main, title_state_7,
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
    void title()
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
                    gGameTable.ex_battle_mode = gGameTable.byte_691EF3 + 1;
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
