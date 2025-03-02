#include "title.h"
#include "audio.h"
#include "hud.h"
#include "interop.hpp"
#include "marni.h"
#include "openre.h"
#include "re2.h"

using namespace openre::audio;
using namespace openre::hud;

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

    static Action title_mv[] = {
        (Action)0x00503680, // capcom_logo
        // (Action)0x005038B0,
        title_init,
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

    // 0x00503810
    static void title_bg_load()
    {
        interop::call(0x00503810);
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

    void title_init_hooks()
    {
        interop::writeJmp(0x005035B0, &title);
    }
}
