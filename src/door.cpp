#include "audio.h"
#include "interop.hpp"
#include "marni.h"
#include "openre.h"
#include "re2.h"
#include "scd.h"
#include "sce.h"

#include <cstring>

using namespace openre::audio;
using namespace openre::scd;
using namespace openre::sce;

namespace openre::door
{
    using DoorAction = void (*)();

    enum
    {
        DOOR_STATE_INIT,
        DOOR_STATE_MOVE,
        DOOR_STATE_EXIT,
    };

    static void door_init();
    static void door_move();
    static void door_exit();

    // 0x00526254
    static DoorAction _doorActions[] = {
        door_init, door_move, door_exit, nullptr, nullptr,
    };

    static uint8_t str_please_wait[] = { 0x00, 0x00, 0x00, 0x00, 0x62, 0x94, 0x7D, 0x5E, 0x5B, 0xEF, 0x7A, 0x67, 0x5E, 0x8F,
                                         0x61, 0x58, 0xFE, 0x00, 0x00, 0x00, 0xB1, 0xA8, 0xE3, 0xE9, 0xEE, 0x0C, 0xAE, 0x83,
                                         0xEF, 0x67, 0xEF, 0x83, 0x62, 0x69, 0x58, 0x75, 0x63, 0xFE, 0x00, 0x00 };
    static const short _word_525F58 = 0x00;
    static const short _word_525F5A = 0x12;

    // 0x0044FEA0
    static void door_main()
    {
        auto& ctcb = *gGameTable.ctcb;
        if (ctcb.var_08 != 0)
        {
            if (ctcb.var_08 != 1)
                return;
        }
        else
        {
            gGameTable.door_state = DOOR_STATE_INIT;
            ctcb.var_08 = 1;
        }

        auto action = _doorActions[gGameTable.door_state];
        if (action != nullptr)
        {
            action();
            if (ctcb.var_13 == 0)
                gGameTable.door_state++;
        }
    }

    // 0x00450230
    static void door_scheduler_main()
    {
        interop::call(0x00450230);
    }

    // 0x00450350
    static void door_trans()
    {
        interop::call(0x00450350);
    }

    // 0x00432A40
    static void door_unload(void* pTim, void* pTmd)
    {
        using sig = void (*)(void*, void*);
        auto p = (sig)0x00432A40;
        p(pTim, pTmd);
    }

    // 0x0044FEF0
    static void door_init()
    {
        gGameTable.dword_6893F0 = 1;
        marni::out();
        gGameTable.door = &gGameTable.door_info;
        set_flag(FlagGroup::System, FG_SYSTEM_DOOR_TRANSITION, true);
        for (auto i = 0; i < 10; i++)
        {
            gGameTable.doors[i] = &gGameTable.door_data[i];
        }

        auto d = gGameTable.doors[0];
        auto end = (void*)(gGameTable.dword_99CEC4 + 0x146);
        std::memset(d, 0, (size_t)end - (size_t)d);

        gGameTable.door_tim = (void*)((uintptr_t)&gGameTable.tmd + (uintptr_t)gGameTable.door_tim);
        gGameTable.tmd = (TmdEntry*)((uintptr_t)&gGameTable.tmd + (uintptr_t)gGameTable.tmd);
        gGameTable.word_989EE8 = 7957;

        auto door = gGameTable.door;
        door->prepacket = gGameTable.door_tim;
        marni::out();
        door->tmd_adr = gGameTable.tmd;

        auto doorAotData = (SceAotDoorData*)gGameTable.door_aot_data;
        if (doorAotData->Texture == 40)
            mapping_tmd(1, (Md1*)door->tmd_adr, 0, 0);
        else
            mapping_tmd(1, (Md1*)door->tmd_adr, 21, 31);
        door_unload(gGameTable.door_tim, gGameTable.tmd);
        gGameTable.dword_6893F0 = 0;

        door->tmd_adr = (TmdEntry*)((uintptr_t)door->tmd_adr + 12);
        door->var_0C = 3;
        door->var_10 = 320;
        door->var_14 = 240;
        door->ctr2 = 0;

        for (auto i = 10; i < 14; i++)
        {
            auto t = get_task(i);
            t->task_level = i;
            t->status = SCD_STATUS_EMPTY;
            t->sub_ctr = 0;
            t->ifel_ctr[0] = 0xFF;
            t->loop_ctr[0] = 0xFF;
        }
        gGameTable.scd = gGameTable.byte_8C6888;
        scd_event_init(get_task(10), 0);
        set_geom_screen(0x122);

        Vec32p p = { 10000, 0, 0, 0 };
        Vec32p r = { 0, 0, 0, 0 };
        set_view(p, r);

        bg_set_mode(2, 0);
        gGameTable.word_98EAFE = 1;
        gGameTable.word_98EAFC = doorAotData->DoorType & 0x7F;
        gGameTable.word_98EB00 = doorAotData->DoorType & 0x80;
        gGameTable.scd_var_temp = doorAotData->Texture;
        door->sound_flg = 0;
    }

    // 0x00450120
    static void door_move()
    {
        auto& ctcb = *gGameTable.ctcb;
        if (ctcb.var_09 != 1)
        {
            if (check_flag(FlagGroup::System, FG_SYSTEM_15))
            {
                task_sleep(1);
                return;
            }

            auto d = GetDoorEntity(0);
            if (!(d->attribute_3 & 0x80))
                gGameTable.byte_98F07A = 0;
            ctcb.var_09 = 1;
        }

        auto t = get_task(10);
        if (t != nullptr && t->status != SCD_STATUS_EMPTY)
        {
            if (!check_flag(FlagGroup::Status, FG_STATUS_14))
                gGameTable.word_98EAFE = 0;
            door_scheduler_main();
            door_trans();
            auto v = gGameTable.door->ctr2;
            if (v > 40 && v < 260)
            {
                if (gGameTable.scd_var_temp == 50)
                    mess_print(32, 200, &str_please_wait[_word_525F58], 0);
                else if (gGameTable.scd_var_temp == 52)
                    mess_print(32, 200, &str_please_wait[_word_525F5A], 0);
            }
            gGameTable.door->ctr2++;
            if ((gGameTable.dword_9885F8 & 0x8D0) != 0)
                t->status = SCD_STATUS_EMPTY;
            task_sleep(1);
        }
        else
        {
            ctcb.var_09 = 0;
        }
    }

    // 0x004502D0
    static void door_exit()
    {
        if (fade_status(0))
        {
            fade_set(512, 0, 7, 1);
            fade_adjust(0, 0x7FFF, 0xFFFFFF, 0);
            if (gGameTable.door->sound_flg != 0)
                snd_se_on(0x10000, gGameTable.player_work->pos);
            marni::unload_door_texture();
            gGameTable.byte_98F07A = 2;
            set_flag(FlagGroup::System, FG_SYSTEM_DOOR_TRANSITION, false);
            task_exit();
        }
        else
        {
            task_sleep(1);
        }
    }

    void door_init_hooks()
    {
        interop::writeJmp(0x0044FEA0, &door_main);
    }
}
