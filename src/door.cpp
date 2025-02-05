#include "audio.h"
#include "camera.h"
#include "entity.h"
#include "hud.h"
#include "interop.hpp"
#include "marni.h"
#include "model.h"
#include "openre.h"
#include "re2.h"
#include "room.h"
#include "scd.h"
#include "sce.h"

#include <cstring>

using namespace openre::audio;
using namespace openre::camera;
using namespace openre::scd;
using namespace openre::sce;
using namespace openre::hud;
using namespace openre::room;

namespace openre::door
{
    using DoorAction = void (*)();
    using DoorTransitionMv = void (*)();

    enum
    {
        DOOR_STATE_INIT,
        DOOR_STATE_MOVE,
        DOOR_STATE_EXIT,
    };

    static void door_init();
    static void door_move();
    static void door_exit();

    enum
    {
        DOOR_TRANS_MV_00,
        DOOR_TRANS_MV_01,
        DOOR_TRANS_MV_02,
        DOOR_TRANS_MV_03,
        DOOR_TRANS_MV_04,
    };

    // 0x005331C4
    static DoorTransitionMv _doorTransitionMvs[] = {
        (DoorTransitionMv)0x004CB2C0, // Ex_trans_mv00
        (DoorTransitionMv)0x004CB4D0, // Ex_trans_mv01
        (DoorTransitionMv)0x004CB620, // Ex_trans_mv02
        (DoorTransitionMv)0x004CB770, // Ex_trans_mv03
        (DoorTransitionMv)0x004CB7B0, // Ex_trans_mv04
        nullptr                       // padding
    };

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
        if (hud_fade_status(0))
        {
            hud_fade_set(512, 0, 7, 1);
            hud_fade_adjust(0, 0x7FFF, 0xFFFFFF, 0);
            if (gGameTable.door->sound_flg != 0)
                snd_se_on(0x10000, gGameTable.player_work->m.pos);
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

    // 0x004507E0
    static void door_snd_trans()
    {
        interop::call(0x004507E0);
    }

    // 0x00441870
    static void movie_set(int id)
    {
        interop::call(0x00441870);
    }

    // 0x004CB260
    static void exec_door_trans_mv()
    {
        _doorTransitionMvs[gGameTable.door_trans_mv]();
        if (gGameTable.ctcb->var_13 == 0)
        {
            gGameTable.door_trans_mv++;
            if (_doorTransitionMvs[gGameTable.door_trans_mv] == nullptr)
            {
                gGameTable.byte_680598 = 0;
                marni::unload_texture_page(18);
                movie_set(1);
                task_exit();
            }
        }
    }

    // 0x004505C0
    static void door_load()
    {
        interop::call(0x004505C0);
    }

    // 0x004C0840
    static void door_set()
    {
        auto& ctcb = *gGameTable.ctcb;
        auto& player = gGameTable.pl;
        auto doorAotData = reinterpret_cast<SceAotDoorData*>(gGameTable.door_aot_data);
        auto zappingFlagAddr = gGameTable.flag_groups[static_cast<uint32_t>(FlagGroup::Zapping)];

        switch (ctcb.var_09)
        {
        case 2: goto LABEL_14;
        case 3:
        {
            door_load();
            if (!ctcb.var_13)
            {
                ctcb.var_09 = 4;
                task_sleep(1);
            }
            break;
        }
        case 4:
        {
            if (check_flag(FlagGroup::Status, FG_SYSTEM_14))
            {
                task_sleep(1);
                return;
            }
            task_execute(1, &door_main);
            goto LABEL_5;
        }
        case 5: goto LABEL_18;
        case 6: goto LABEL_23;
        case 7: goto LABEL_25;
        case 8: goto LABEL_27;
        case 10: goto LABEL_29;
        case 11:
        {
            if (check_flag(FlagGroup::Status, FG_STATUS_9))
            {
                bg_set_mode(2, 0);
                set_flag(FlagGroup::Status, FG_STATUS_9, false);
            }
            ctcb.var_09 = 0;
            return;
        }
        default:
        {
            if (check_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE))
            {
                door_snd_trans();
            }
            ctcb.var_09 = 2;
        LABEL_14:
            if (check_flag(FlagGroup::System, FG_SYSTEM_15))
            {
                task_sleep(1);
                return;
            }
            if (check_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE) && bitarray_get(zappingFlagAddr, 0x3f))
            {
                gGameTable.byte_989EEA = static_cast<uint8_t>(gGameTable.current_cut);
                set_flag(FlagGroup::System, FG_SYSTEM_DOOR_TRANSITION, true);
                gGameTable.door_trans_mv = DOOR_TRANS_MV_00;
                task_execute(1, &exec_door_trans_mv);
                ctcb.var_09 = 5;
            LABEL_18:
                if (check_flag(FlagGroup::System, FG_SYSTEM_DOOR_TRANSITION) && gGameTable.door_trans_mv != DOOR_TRANS_MV_04)
                {
                    task_sleep(1);
                    return;
                }
            LABEL_5:
                player.old_pos.x = doorAotData->TargetX;
                player.sca_old_x = player.old_pos.x;
                player.m.pos.x = player.old_pos.x;
                player.old_pos.y = doorAotData->TargetY;
                player.m.pos.y = player.old_pos.y;
                player.old_pos.z = doorAotData->TargetZ;
                player.sca_old_z = player.old_pos.z;
                player.m.pos.z = player.old_pos.z;
                player.cdir.y = doorAotData->TargetD;
                player.nFloor = doorAotData->TargetFloor;
                player.ground = player.nFloor * -1800;
                gGameTable.byte_989EEA = doorAotData->TargetCut;
                gGameTable.current_cut = gGameTable.byte_989EEA;
                gGameTable.last_cut = gGameTable.current_room + ((gGameTable.current_stage + 1) << 8);
                gGameTable.current_room = doorAotData->TargetRoom;

                if (gGameTable.current_stage != doorAotData->TargetStage % 9)
                {
                    gGameTable.current_stage = doorAotData->TargetStage % 9;
                    set_stage();
                    if (ctcb.var_13)
                    {
                        ctcb.var_09 = 6;
                        return;
                    }
                }
                ctcb.var_09 = 6;
            LABEL_23:
                if (check_flag(FlagGroup::Status, FG_STATUS_14))
                {
                    task_sleep(1);
                    return;
                }
                ctcb.var_09 = 7;
            LABEL_25:
                snd_bgm_ck();
                if (!ctcb.var_13)
                {
                    kage_work_init();
                    kage_work9_init();
                    ctcb.var_09 = 8;
                LABEL_27:
                    room_set();
                    if (!ctcb.var_13)
                    {
                        ctcb.var_09 = 10;
                    LABEL_29:
                        if (check_flag(FlagGroup::System, FG_SYSTEM_DOOR_TRANSITION))
                        {
                            task_sleep(1);
                            return;
                        }
                        bg_set_mode(0, 0);
                        cut_check(1);
                        gGameTable.byte_98F07B = 1;
                        ctcb.var_09 = 11;
                        task_sleep(1);
                    }
                }
            }
            else
            {
                bg_set_mode(2, 0);
                ctcb.var_09 = 3;
                door_load();
                if (!ctcb.var_13)
                {
                    ctcb.var_09 = 4;
                    task_sleep(1);
                }
            }
        }
        }
    }

    void door_init_hooks()
    {
        interop::writeJmp(0x0044FEA0, &door_main);
        interop::writeJmp(0x004C0840, &door_set);
    }
}
