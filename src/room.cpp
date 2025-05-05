#define _CRT_SECURE_NO_WARNINGS

#include "room.h"
#include "audio.h"
#include "camera.h"
#include "enemy.h"
#include "entity.h"
#include "file.h"
#include "interop.hpp"
#include "marni.h"
#include "openre.h"
#include "player.h"
#include "rdt.h"
#include "re2.h"
#include "sce.h"
#include "scheduler.h"

#include <cstring>

using namespace openre::audio;
using namespace openre::camera;
using namespace openre::file;
using namespace openre::player;
using namespace openre::sce;
using namespace openre::rdt;
using namespace openre::enemy;

namespace openre::room
{
    static const char* font1 = "common\\data\\font1.tim";
    static const char* font2 = "common\\data\\font1.adt";

    // 0x00442EA0
    static void set_registry_flag(int a0, int a1)
    {
        interop::call<void, int, int>(0x00442EA0, a0, a1);
    }

    // 0x004450C0
    static void sub_4450C0(int a0)
    {
        interop::call<void, int>(0x004450C0, a0);
    }

    // 0x0043DF40
    static int sub_43DF40()
    {
        return interop::call<int>(0x0043DF40);
    }

    // 0x00502190
    static void st_chenge_pl(int a0)
    {
        interop::call<void, int>(0x00502190, a0);
    }

    // 0x004DD0C0
    static void psp_init0()
    {
        interop::call(0x004DD0C0);
    }

    // 0x004DD0E0
    static void psp_init1()
    {
        interop::call(0x004DD0E0);
    }

    // 0x005023D0
    static void st_room_set()
    {
        interop::call(0x005023D0);
    }

    // 0x004B8080
    static void esp_init_r()
    {
        interop::call(0x004B8080);
    }

    static void memset32(void* dest, uint32_t value, size_t count)
    {
        auto* ptr = static_cast<uint32_t*>(dest);
        for (size_t i = 0; i < count; i++)
        {
            ptr[i] = value;
        }
    }

    // 0x004DE7B0
    void room_set()
    {
        auto& ctcb = *gGameTable.ctcb;

        while (true)
        {
            switch (ctcb.var_0D)
            {
            case 0:
            {
                strcpy(gGameTable.room_path, "Pl0\\Rdt\\room1000.rdt");
                if (gGameTable.graphics_ptr_data == 1)
                {
                    std::memcpy(gGameTable.stage_font_name, font1, 22);
                }
                else
                {
                    std::memcpy(gGameTable.stage_font_name, font2, 22);
                }
                if (check_flag(FlagGroup::Status, FG_STATUS_PLAYER))
                {
                    ++gGameTable.room_path[2];
                    ++gGameTable.room_path[15];
                }
                gGameTable.p_em = &gGameTable.pl;
                gGameTable.room_path[12] = gStageSymbols[(gGameTable.byte_98E798) + gGameTable.current_stage];
                gGameTable.room_path[13] += gGameTable.current_room / 16;
                const auto mod = gGameTable.current_room % 16;
                if (mod >= 10)
                {
                    gGameTable.room_path[14] = mod + 87;
                }
                else
                {
                    gGameTable.room_path[14] = mod + 48;
                }

                switch (gGameTable.current_stage)
                {
                case 0:
                {
                    if (gGameTable.current_room == 22)
                    {
                        set_registry_flag(0, 56);
                    }
                    break;
                }
                case 1:
                {
                    switch (gGameTable.current_room)
                    {
                    case 0: set_registry_flag(0, 48); break;
                    case 2: set_registry_flag(0, 49); break;
                    case 4: set_registry_flag(0, 51); break;
                    case 7: set_registry_flag(0, 50); break;
                    case 8: set_registry_flag(0, 53); break;
                    case 10: set_registry_flag(0, 52); break;
                    case 11: set_registry_flag(0, 55); [[fallthrough]];
                    case 27: set_registry_flag(0, 54); break;
                    default: goto LABEL_34;
                    }
                }
                case 2:
                {
                    if (gGameTable.current_room == 5)
                    {
                        set_registry_flag(0, 57);
                    }
                    break;
                }
                case 3:
                {
                    if (gGameTable.current_room == 5)
                    {
                        set_registry_flag(0, 59);
                    }
                    break;
                }
                case 4:
                {
                    if (gGameTable.current_room == 0)
                    {
                        set_registry_flag(0, 58);
                    }
                    break;
                }
                }
            LABEL_34:
                auto* pEm = gGameTable.p_em;
                gGameTable.word_98E78C = 0;
                gGameTable.fg_rbj_set = 0;
                gGameTable.fg_status &= 0xFFF04000;
                pEm->pOn_om = 0;
                pEm->status_flg = (pEm->status_flg & 0xf900) << 8 | (pEm->status_flg & 0xff);
                gGameTable.mem_top = reinterpret_cast<void*>(gGameTable.dword_988620);
                gGameTable.rdt = reinterpret_cast<Rdt*>(gGameTable.dword_988620);
                ctcb.var_0D = 10;
                goto LABEL_35;
            }
            case 1: goto LABEL_62;
            case 2: goto LABEL_63;
            case 3:
            {
                if (gGameTable.current_stage == gGameTable.byte_989E7D)
                {
                    ctcb.var_0D = 5;
                    continue;
                }

                gGameTable.byte_989E7D = gGameTable.current_stage & 0xff;
                if (gGameTable.stage_bk == gGameTable.current_stage)
                {
                    ctcb.var_0D = 4;
                    task_sleep(1);
                    return;
                }
                void* fBuff = file_alloc(0x20014);
                gGameTable.stage_font_name[16] += gGameTable.byte_989E7D;
                switch (gGameTable.graphics_ptr_data)
                {
                case 1:
                {
                    if (!read_file_into_buffer(gGameTable.stage_font_name, fBuff, 4))
                    {
                        file_error();
                        return;
                    }
                    break;
                }
                case 0:
                case 2:
                {
                    if (!load_adt(gGameTable.stage_font_name, (uint32_t*)fBuff, 4))
                    {
                        file_error();
                        return;
                    }
                    break;
                }
                }
                tim_buffer_to_surface(reinterpret_cast<int*>(fBuff), 9, 1);
                file_alloc(0);
                gGameTable.stage_bk = gGameTable.byte_989E7D;
                ctcb.var_0D = 4;
                task_sleep(1);
                return;
            }
            case 4:
            case 5:
            {
                gGameTable.word_989EE8 = 3333;
                osp_read();
                gGameTable.byte_689C64 = 1;
                gGameTable.rdt_size = read_file_into_buffer(gGameTable.room_path, gGameTable.rdt, 8);
                if (!gGameTable.rdt_size)
                {
                    file_error();
                    return;
                }
                for (int i = 0; i < 23; i++)
                {
                    auto baseRdt = (uint32_t) & (*gGameTable.rdt);

                    if (gGameTable.rdt->offsets[i])
                    {
                        auto offset = rdt_get_offset<uintptr_t>(static_cast<RdtOffsetKind>(i));
                        gGameTable.rdt->offsets[i] = (void*)(baseRdt + (uint32_t)offset);
                    }
                }

                gGameTable.rdt_count = 0;
                if (gGameTable.rdt->header.num_cuts)
                {
                    auto baseRdt = (uint32_t) & (*gGameTable.rdt);
                    auto cameras = rdt_get_offset<RdtCamera>(RdtOffsetKind::RID);
                    for (int i = 0; i < gGameTable.rdt->header.num_cuts; i++)
                    {
                        cameras[i].offset += baseRdt;
                        gGameTable.rdt_count++;
                    }
                }

                gGameTable.mem_top = reinterpret_cast<void*>((uintptr_t)gGameTable.mem_top + gGameTable.rdt_size);
                gGameTable.rdt_count = 0;
                if (gGameTable.rdt->header.num_models)
                {
                    auto baseRdt = (uint32_t) & (*gGameTable.rdt);
                    auto models = rdt_get_offset<RdtModel>(RdtOffsetKind::MODELS);
                    for (int i = 0; i < gGameTable.rdt->header.num_models; i++)
                    {
                        models[i].texture_offset += baseRdt;
                        models[i].model_offset += baseRdt;
                        gGameTable.rdt_count++;
                    }
                }

                cut_change(gGameTable.current_cut & 0xff);
                esp_init_r();
                ctcb.var_0D = 6;
            LABEL_84:
                snd_room_load();
                if (!ctcb.var_13)
                {
                    marni::unload_texture_page(17);
                    sce_model_init();
                    snd_bgm_play_ck();
                    if (rdt_get_offset<void*>(RdtOffsetKind::VB))
                    {
                        gGameTable.mem_top = rdt_get_offset<void*>(RdtOffsetKind::VB);
                    }
                    gGameTable.actor_entity = &gGameTable.pl;
                    gGameTable.pl.routine_0 = 0;
                    gGameTable.pl.routine_1 = 0;
                    gGameTable.pl.routine_2 = 0;
                    gGameTable.pl.routine_3 = 0;
                    player_move(&gGameTable.pl);
                    sce_scheduler_set();
                    ctcb.var_0D = 7;
                LABEL_88:
                    snd_load_enemy();
                    if (!ctcb.var_13)
                    {
                        rbj_set();
                        psp_init0();
                        ctcb.var_0D = 8;
                    LABEL_90:
                        marni::out();
                        if (!ctcb.var_13)
                        {
                            em_init_move();
                            psp_init1();
                            ctcb.var_0D = 9;
                        LABEL_92:
                            if (gGameTable.byte_99270F)
                            {
                                task_sleep(1);
                            }
                            else
                            {
                                st_room_set();
                                marni::flush_surfaces();
                                ctcb.var_0D = 0;
                            }
                        }
                    }
                }
                return;
            }
            case 6: goto LABEL_84;
            case 7: goto LABEL_88;
            case 8: goto LABEL_90;
            case 9: goto LABEL_92;
            case 10:
            {
            LABEL_35:
                snd_bgm_set();
                if (ctcb.var_13)
                {
                    return;
                }
                sub_4450C0(0);
                gGameTable.dword_98862C = &gGameTable.enemies;
                gGameTable.enemy_count = 0;
                memset32(&gGameTable.splayer_work, 0x0098E544, 33);
                gGameTable.enemy_init_entries[0].enabled = 0;
                gGameTable.enemy_init_entries[1].enabled = 0;
                sub_43DF40();

                gGameTable.obj_ptr = gGameTable.pOm;
                gGameTable.rdt_count = 32;
                gGameTable.pOm->be_flg = 0;
                if (gGameTable.p_em->id == (gGameTable.next_pld & 0xff))
                {
                    ctcb.var_0D = 2;
                    continue;
                }
                if (gGameTable.next_pld < 12)
                {
                    if (gGameTable.next_pld & 1)
                    {
                        if (check_flag(FlagGroup::Zapping, FG_ZAPPING_6))
                        {
                            gGameTable.next_pld = 9;
                        }
                    }
                    else
                    {
                        if (check_flag(FlagGroup::Zapping, FG_ZAPPING_5))
                        {
                            gGameTable.next_pld = 8;
                        }
                        if (check_flag(FlagGroup::Zapping, FG_ZAPPING_15))
                        {
                            gGameTable.next_pld = 10;
                        }
                    }
                }
                gGameTable.dword_689C1C = gGameTable.p_em->id;
                gGameTable.p_em->id = static_cast<uint8_t>(gGameTable.next_pld);
                st_chenge_pl(gGameTable.next_pld);
                player_set(gGameTable.p_em);

                if (!ctcb.var_13)
                {
                    gGameTable.p_em->routine_0 = 0;
                    gGameTable.p_em->routine_1 = 0;
                    gGameTable.p_em->routine_2 = 0;
                    gGameTable.p_em->routine_3 = 0;

                    if (gGameTable.next_pld == 14 || gGameTable.next_pld == 15)
                    {
                        gGameTable.word_98E9B6 = gGameTable.pl.life;
                        gGameTable.byte_98E9AB = gGameTable.poison_timer;
                        gGameTable.word_98E9AC = gGameTable.poison_status;
                        gGameTable.poison_timer = 0;
                        gGameTable.poison_status = 0;
                        gGameTable.pl.life = gGameTable.pl.max_life;
                    }
                    else if ((gGameTable.dword_689C1C & 0xff) >= 12)
                    {
                        gGameTable.poison_timer = gGameTable.byte_98E9AB;
                        gGameTable.poison_status = gGameTable.word_98E9AC;
                        gGameTable.pl.life = gGameTable.word_98E9B6;
                    }
                }

                gGameTable.byte_691F7B = 1;
                ctcb.var_0D = 1;
            LABEL_62:
                snd_load_core(gGameTable.next_pld & 0xff, 1);
                if (!ctcb.var_13)
                {
                LABEL_63:
                    gGameTable.byte_99270F = 0;
                    ctcb.var_0D = 3;
                    task_sleep(1);
                }
            }
                return;
            }
        }
    }
}
