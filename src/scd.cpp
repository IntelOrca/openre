#include "scd.h"
#include "audio.h"
#include "camera.h"
#include "enemy.h"
#include "interop.hpp"
#include "math.h"
#include "openre.h"
#include "rdt.h"
#include "re2.h"
#include "sce.h"
#include <cassert>
#include <cstring>

using namespace openre::audio;
using namespace openre::enemy;
using namespace openre::sce;
using namespace openre::rdt;
using namespace openre::camera;
using namespace openre::math;

namespace openre::scd
{
    using ScdOpcode = uint8_t;
    using ItemType = uint8_t;
    using HudKind = uint8_t;

    enum
    {
        SCD_NOP = 0x00,
        SCD_EVT_END = 0x01,
        SCD_EVT_NEXT = 0x02,
        SCD_EVT_CHAIN = 0X03,
        SCD_EVT_EXEC = 0x04,
        SCD_EVT_KILL = 0x05,
        SCD_IFEL_CK = 0x06,
        SCD_END_IF = 0x08,
        SCD_SCE_RND = 0x28,
        SCD_CUT_CH = 0x29,
        SCD_CUT_OLD = 0x2A,
        SCD_MESSAGE_ON = 0x2B,
        SCD_AOT_SET = 0x2C,
        SCD_WORK_SET = 0x2E,
        SCD_SCE_ESPR_ON = 0x3A,
        SCD_DOOR_AOT_SE = 0x3B,
        SCE_CUT_AUTO = 0x3C,
        SCD_PLC_MOTION = 0x3F,
        SCD_SCE_EM_SET = 0x44,
        SCD_CUT_REPLACE = 0x4B,
        SCD_ITEM_AOT_SET = 0x4E,
        SCD_SCE_KEY_CK = 0x4F,
        SCD_SCE_BGM_CONTROL = 0x51,
        SCD_SCE_BGMTBL_SET = 0x57,
        SCD_CUT_BE_SET = 0x61,
        SCD_AOT_SET_4P = 0x67,
        SCD_DOOR_AOT_SET_4P = 0x68,
        SCD_ITEM_AOT_SET_4P = 0x69,
        SCD_HEAL = 0x83,
        SCD_POISON_CK = 0x86,
        SCD_POISON_CLR = 0x87,
        SCD_SCE_HEAL_PARTNER = 0x89,
    };

    enum
    {
        SCD_RESULT_FALSE,
        SCD_RESULT_NEXT,
        SCD_RESULT_NEXT_TICK,
    };

    enum
    {
        WK_NONE,
        WK_PLAYER,
        WK_SPLAYER,
        WK_ENEMY,
        WK_OBJECT,
        WK_DOOR,
        WK_ALL
    };

    struct ScdIfelCk
    {
        uint8_t Opcode;
        uint8_t pad_02;
        uint16_t BlockSize;
    };

    struct SceAot : SceAotBase
    {
        int16_t X;
        int16_t Z;
        uint16_t W;
        uint16_t D;
    };

    struct XZPoint
    {
        int16_t X;
        int16_t Z;
    };

    struct SceAot4p : SceAotBase
    {
        XZPoint Points[4];
    };

    struct SceAotDoor
    {
        SceAot Aot;
        SceAotDoorData Door;
    };

    struct ScdAotSet
    {
        uint8_t Opcode;
        uint8_t Id;
        SceAot Aot;
        uint8_t Data[6];
    };

    struct ScdSceAotDoor
    {
        uint8_t Opcode;
        uint8_t Id;
        SceAotDoor Data;
    };

    struct ScdAotSet4p
    {
        uint8_t Opcode;
        uint8_t Id;
        SceAot4p Aot;
        uint8_t Data[6];
    };

    struct SceAotItem
    {
        SceAot Aot;
        SceAotItemData Item;
    };

    struct ScdSceAotItem
    {
        uint8_t Opcode;
        uint8_t Id;
        SceAotItem Data;
    };

    struct ScdSceBgmControl
    {
        uint8_t Opcode;
        uint8_t var_01;
        uint8_t var_02;
        uint8_t var_03;
        uint8_t var_04;
        uint8_t var_05;
    };

    struct ScdSceBgmTblSet
    {
        uint8_t Opcode;
        uint8_t pad_01;
        uint16_t roomstage;
        uint16_t var_04;
        uint16_t var_06;
    };

    struct ScdCutBeSet
    {
        uint8_t Opcode;
        uint8_t Id;
        uint8_t Value;
        uint8_t OnOff;
    };

    struct ScdSceKeyCk
    {
        uint8_t Opcode;
        uint8_t flag;
        uint16_t key;
    };

    struct ScdCutCh
    {
        uint8_t Opcode;
        uint8_t Id;
    };

    struct ScdCutOld
    {
        uint8_t Opcode;
    };

    struct ScdMessageOn
    {
        uint8_t Opcode;
        uint8_t var_01;
        uint8_t var_02;
        uint8_t var_03;
        uint16_t var_04;
    };

    struct ScdCutAuto
    {
        uint8_t Opcode;
        uint8_t on;
    };

    struct ScdCutReplace
    {
        uint8_t Opcode;
        uint8_t Id;
        uint8_t value;
    };

    struct ScdSceEmSet
    {
        uint8_t opcode;
        uint8_t pad_01;
        uint8_t id;
        uint8_t type;
        uint8_t pose;
        uint8_t behaviour;
        uint8_t floor;
        uint8_t soundBank;
        uint8_t texture;
        uint8_t globalId;
        int16_t x;
        int16_t y;
        int16_t z;
        int16_t d;
        int16_t animation;
        int16_t var_14;
    };

    struct ScdSceEsprOn
    {
        uint8_t opcode;
        uint8_t nop;
        uint8_t esp_id;
        uint8_t esp_dt;
        uint8_t work_kind;
        uint8_t work_no;
        uint16_t espmv;
        int16_t svec_x;
        int16_t svec_y;
        int16_t svec_z;
        uint16_t dir_y;
    };

    struct SceItemAotSet
    {
        uint8_t opcode;
        uint8_t aot_id;
        uint8_t sce;
        uint8_t sat;
        uint8_t nFloor;
        uint8_t super;
        int16_t x;
        int16_t z;
        uint16_t w;
        uint16_t d;
        uint16_t item_id;
        uint16_t item_quantity;
        uint16_t flag;
        uint8_t md1;
        uint8_t action;
    };

    constexpr uint8_t SAT_4P = (1 << 7);

    using ScdOpcodeImpl = int (*)(SceTask*);

    static ScdOpcodeImpl* gScdImplTable = (ScdOpcodeImpl*)0x53AE10;

    static int get_max_tasks()
    {
        return 14;
    }

    SceTask* get_task(SceTaskId index)
    {
        assert(index < get_max_tasks());
        return &((SceTask*)0x00694A00)[index];
    }

    static uint8_t* get_scd_event(int index)
    {
        auto scd = gGameTable.scd;
        auto evtTable = (uint16_t*)scd;
        return scd + evtTable[index];
    }

    // 0x004E39E0
    static void scd_init()
    {
        auto maxTasks = get_max_tasks();
        for (auto i = 0; i < maxTasks; i++)
        {
            auto task = get_task(i);
            task->status = SCD_STATUS_EMPTY;
            task->task_level = maxTasks - i - 1;
            task->sub_ctr = 0;
            task->ifel_ctr[0] = 0xFF;
            task->loop_ctr[0] = 0xFF;
        }
        gGameTable.random_base = 0x138201C3;
    }

    void scd_init_tasks()
    {
        for (auto i = 0; i < 10; i++)
        {
            auto task = get_task(i);
            task->sub_ctr = i;
            task->routine = 0;
            task->status = 0;
            task->task_level = 0xFF;
            task->ifel_ctr[3] = 0xFF;
        }
    }

    static int scd_execute_opcode(SceTask* task, ScdOpcode instruction)
    {
        return gScdImplTable[instruction](task);
    }

    // 0x004E4310
    void sce_scheduler_main()
    {
        for (auto i = 0; i < 10; i++)
        {
            auto task = get_task(i);
            if (task->status != SCD_STATUS_EMPTY)
            {
                while (true)
                {
                    auto opcode = *task->data;
                    auto result = scd_execute_opcode(task, opcode);
                    if (dword_68A204->var_13 != 0)
                        return;
                    if (result == SCD_RESULT_NEXT)
                        continue;
                    if (result == SCD_RESULT_NEXT_TICK)
                        break;
                    auto eax = task->sub_ctr;
                    auto cl = task->ifel_ctr[eax];
                    if (cl & 0x80)
                        break;
                    task->sp--;
                    task->data = *task->sp;
                    task->ifel_ctr[eax]--;
                }
            }
        }
        sce_work_clr();
        sce_work_clr_at();
    }

    // 0x004E3F60
    void scd_event_init(SceTask* task, int evt)
    {
        task->status = SCD_STATUS_1;
        task->routine = 0;
        task->sp = (uint8_t**)((uintptr_t)task + ((task->sub_ctr + 6) * 32));
        task->ifel_ctr[0] = -1;
        task->loop_ctr[0] = -1;
        task->data = get_scd_event(evt);
    }

    static SceTask* get_empty_task(int min, int max)
    {
        for (auto i = min; i < max; i++)
        {
            auto task = get_task(i);
            if (task->status == SCD_STATUS_EMPTY)
            {
                return task;
            }
        }
        return nullptr;
    }

    // 0x004E3FA0
    void scd_event_exec(int taskIndex, int evt)
    {
#if MORE_SCD_EVENTS
        auto min = 32;
        auto max = 100;
        auto cap = 10;
        if (gGameTable.sce_type != SCE_TYPE_MAIN)
        {
            min = 100;
            max = 140;
            cap = 14;
            if (taskIndex < cap)
                taskIndex = 100 + taskIndex;
        }
#else
        auto min = 2;
        auto max = 10;
        auto cap = 10;
        if (gGameTable.sce_type != SCE_TYPE_MAIN)
        {
            min = 10;
            max = 14;
            cap = 14;
        }
#endif

        auto task = taskIndex >= cap ? get_empty_task(min, max) : get_task(taskIndex);
        if (task != nullptr)
        {
            task->sub_ctr = 0;
            memset(task->spd, 0, (size_t)&task->r_no_bak - (size_t)task->spd);
            scd_event_init(task, evt);
        }
    }

    // 0x004E43B0
    static int scd_nop(SceTask* sce)
    {
        sce->data++;
        return SCD_RESULT_NEXT;
    }

    // 0x004E4490
    static int scd_evt_kill(SceTask* sce)
    {
        sce->data++;
        auto taskId = *sce->data++;
        auto taskToKill = get_task(taskId);
        taskToKill->status = SCD_STATUS_EMPTY;
        return SCD_RESULT_NEXT;
    }

    // 0x004E4460
    static int scd_evt_exec(SceTask* sce)
    {
        sce->data++;
        auto taskIndex = *sce->data++;
        sce->data++;
        auto eventIndex = *sce->data++;
        scd_event_exec(taskIndex, eventIndex);
        return SCD_RESULT_NEXT;
    }

    // 0x004E51C0
    static int scd_aot_set(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdAotSet*>(sce->data);
        set_aot_entry(opcode->Id, &opcode->Aot);
        sce->data += sizeof(ScdAotSet);
        return SCD_RESULT_NEXT;
    }

    // 0x004E5250
    static int scd_door_aot_se(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdSceAotDoor*>(sce->data);
        set_aot_entry(opcode->Id, &opcode->Data.Aot);
        sce->data += sizeof(ScdSceAotDoor);
        return SCD_RESULT_NEXT;
    }

    // 0x004E8290
    static int scd_sce_bgm_control(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdSceBgmControl*>(sce->data);

        auto arg = (opcode->var_05) | (opcode->var_04 << 8) | (opcode->var_03 << 16) | (opcode->var_02 << 24)
            | (opcode->var_01 << 28);
        bgm_set_control(arg);

        sce->data += sizeof(ScdSceBgmControl);
        return SCD_RESULT_NEXT;
    }

    // 0x004E82E0
    static int scd_sce_bgmtbl_set(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdSceBgmTblSet*>(sce->data);
        bgm_set_entry((opcode->roomstage << 16) | opcode->var_06 | opcode->var_04);
        sce->data += sizeof(ScdSceBgmTblSet);
        return SCD_RESULT_NEXT;
    }

    // 0x004E5120
    static int scd_cut_be_set(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdCutBeSet*>(sce->data);
        auto vcut = rdt_get_offset<VCut>(RdtOffsetKind::RVD);
        while (vcut->fCut != opcode->Id)
        {
            vcut++;
        }
        vcut[opcode->Value].be_flg = opcode->OnOff;
        sce->data += 4;
        return SCD_RESULT_NEXT;
    }

    // 0x004E5200
    static int scd_aot_set_4p(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdAotSet4p*>(sce->data);
        set_aot_entry(opcode->Id, &opcode->Aot);
        opcode->Aot.Sat |= SAT_4P;
        sce->data += sizeof(ScdAotSet4p);
        return SCD_RESULT_NEXT;
    }

    // 0x004E5E90
    static int scd_work_set(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdAotSet4p*>(sce->data);
        auto wkKind = sce->data[1];
        auto wkIndex = sce->data[2];

        std::memset(sce->spd, 0, sizeof(sce->spd));
        std::memset(sce->dspd, 0, sizeof(sce->dspd));
        std::memset(sce->aspd, 0, sizeof(sce->aspd));
        std::memset(sce->adspd, 0, sizeof(sce->adspd));

        sce->data += 3;
        switch (wkKind)
        {
        case WK_PLAYER: sce->work = GetPlayerEntity(); break;
        case WK_SPLAYER: sce->work = GetPartnerEntity(); break;
        case WK_ENEMY: sce->work = GetEnemyEntity(wkIndex); break;
        case WK_OBJECT: sce->work = GetObjectEntity(wkIndex); break;
        case WK_DOOR: sce->work = GetDoorEntity(wkIndex); break;
        }
        return SCD_RESULT_NEXT;
    }

    // 0x004E4420
    static int scd_evt_next(SceTask* sce)
    {
        sce->data++;
        return SCD_RESULT_NEXT_TICK;
    }

    // 0x004E4440
    static int scd_evt_chain(SceTask* sce)
    {
        scd_event_init(sce, *(sce->data + 3));
        return SCD_RESULT_NEXT;
    }

    // 0x004E43D0
    static int scd_evt_end(SceTask* sce)
    {
        auto subroutineDepth = sce->sub_ctr;
        if (subroutineDepth == 0)
        {
            sce->status = SCD_STATUS_EMPTY;
            return SCD_RESULT_NEXT_TICK;
        }

        auto stackOffset = *(&sce->task_level + subroutineDepth);
        auto callerIndex = subroutineDepth - 1;
        sce->data = reinterpret_cast<uint8_t*>(sce->ret_addr[callerIndex]);
        sce->sub_ctr = callerIndex;
        sce->sp = reinterpret_cast<uint8_t**>(&(sce->stack[callerIndex + (stackOffset + 1)]));
        return SCD_RESULT_NEXT;
    }

    // 0x004E8FB0
    static int scd_heal(SceTask* sce)
    {
        sce->data++;
        gPlayerEntity.life = gPlayerEntity.max_life;
        gPoisonTimer = 0;
        gPoisonStatus = 0;
        return SCD_RESULT_NEXT;
    }

    // 0x004E90C0
    static int scd_poison_ck(SceTask* sce)
    {
        sce->data++;
        return gPoisonStatus != 0 ? SCD_RESULT_NEXT : SCD_RESULT_FALSE;
    }

    // 0x004E90E0
    static int scd_poison_clr(SceTask* sce)
    {
        sce->data++;
        gPoisonTimer = 0;
        gPoisonStatus = 0;
        gPlayerEntity.routine_0 = 1;
        return SCD_RESULT_NEXT;
    }

    // 0x004E72D0
    static int scd_plc_motion(SceTask* sce)
    {
        auto group = sce->data[1];
        auto animation = sce->data[2];
        auto flags = sce->data[3];
        auto entity = reinterpret_cast<PlayerEntity*>(sce->work);

        entity->routine_0 = 4;
        entity->routine_1 = group;
        entity->routine_2 = 0;
        entity->routine_3 = 0;
        entity->move_no = animation;
        entity->move_cnt = 0;
        entity->sce_flg = flags;
        entity->sce_free0 = 0;
        entity->sce_free1 = 0;

        sce->data += 4;
        return SCD_RESULT_NEXT;
    }

    // 0x004E44C0
    static int scd_ifel_ck(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdIfelCk*>(sce->data);
        auto blockSize = opcode->BlockSize;
        sce->data += 4;
        sce->ifel_ctr[sce->sub_ctr]++;
        *sce->sp++ = sce->data + blockSize;
        return SCD_RESULT_NEXT;
    }

    // 0x004E4550
    static int scd_end_if(SceTask* sce)
    {
        sce->sp--;
        sce->ifel_ctr[sce->sub_ctr]--;
        sce->data += 2;
        return SCD_RESULT_NEXT;
    }

    // 0x004E4F60
    static int scd_sce_rnd(SceTask* sce)
    {
        sce->data += 2;
        sce_rnd_set();
        return SCD_RESULT_NEXT;
    }

    // 0x004E8230
    static int scd_sce_key_ck(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdSceKeyCk*>(sce->data);
        sce->data += 4;
        if ((gGameTable.g_key & opcode->key) == 0)
        {
            return opcode->flag ^ 1;
        }
        return opcode->flag;
    }

    // 0x004E4F80
    static int scd_cut_ch(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdCutCh*>(sce->data);
        set_flag(FlagGroup::Status, FG_STATUS_CAMERA_LOCKED, true);
        sub_4E5020(opcode->Id & 0x7F);
        set_flag(FlagGroup::Status, FG_STATUS_11, true);
        if (opcode->Id & 0x80)
        {
            gGameTable.byte_98F07B = 0;
        }
        sce->data += 2;
        return SCD_RESULT_NEXT;
    }

    // 0x004E4FE0
    static int scd_cut_old(SceTask* sce)
    {
        sub_4E5020(gGameTable.cut_old);
        set_flag(FlagGroup::Status, FG_STATUS_CAMERA_LOCKED, false);
        set_flag(FlagGroup::Status, FG_STATUS_11, true);
        sce->data++;
        return SCD_RESULT_NEXT;
    }

    // 0x004E5170
    static int scd_message_on(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdMessageOn*>(sce->data);
        auto a3 = opcode->var_04 << 16;
        show_message(0, opcode->var_03 + 768, opcode->var_02, a3);
        gGameTable.fg_stop |= a3;
        sce->data += 6;
        return SCD_RESULT_NEXT;
    }

    // 0x004E5050
    static int scd_cut_auto(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdCutAuto*>(sce->data);
        set_flag(FlagGroup::Status, FG_STATUS_CAMERA_LOCKED, !opcode->on);
        sce->data += 2;
        return SCD_RESULT_NEXT;
    }

    // 0x004E5090
    static int scd_cut_replace(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdCutReplace*>(sce->data);
        auto rvd = rdt_get_offset<uintptr_t>(RdtOffsetKind::RVD);
        auto vCuts = reinterpret_cast<VCut*>((uint32_t)rvd + 2);

        if (vCuts->be_flg != -1)
        {
            uint8_t nextBeFlg = 0;
            do
            {
                if (vCuts->be_flg == opcode->Id)
                {
                    vCuts->be_flg = opcode->value;
                }
                else if (vCuts->be_flg == opcode->value)
                {
                    vCuts->be_flg = opcode->Id;
                }

                if (vCuts->nFloor == opcode->Id)
                {
                    vCuts->nFloor = opcode->value;
                }
                else if (vCuts->nFloor == opcode->value)
                {
                    vCuts->nFloor = opcode->Id;
                }
                nextBeFlg = vCuts[1].be_flg;
                ++vCuts;

            } while (nextBeFlg != 0xFF);
        }

        if (gGameTable.vcut_data[1]->fCut == opcode->Id)
        {
            cut_change(opcode->value);
        }
        sce->data += 3;
        return SCD_RESULT_NEXT;
    }

    // 0x004E52E0
    static int scd_item_aot_set(SceTask* sce)
    {
        auto opcode = reinterpret_cast<SceItemAotSet*>(sce->data);
        sce->data += sizeof(SceItemAotSet);
        if (!gGameTable.aot_table[opcode->aot_id])
        {
            gGameTable.aot_count++;
        }
        gGameTable.aot_table[opcode->aot_id] = &opcode->sce;
        auto flagGroup = gGameTable.current_stage < 4 ? FlagGroup::Item : FlagGroup::Item2;
        // Item already picked up
        if (check_flag(flagGroup, opcode->flag))
        {
            auto aot = (uint32_t*)(gGameTable.aot_table[opcode->aot_id]);
            *aot &= ~0xFF;
            if (opcode->md1 < 32)
            {
                gGameTable.pOm[opcode->md1].be_flg = 0x80000000;
                gGameTable.pOm[opcode->md1].free0 = 0;
            }
            return SCD_RESULT_NEXT;
        }
        if (opcode->md1 >= 32)
        {
            return SCD_RESULT_NEXT;
        }

        gGameTable.pOm[opcode->md1].free0 = opcode->action;
        if (opcode->action & 2)
        {
            gGameTable.pOm[opcode->md1].be_flg = 0x80000000;
        }
        return SCD_RESULT_NEXT;
    }

    static bool is_enemy_dead(uint8_t globalId)
    {
        auto fgEnemy = FlagGroup::Enemy2;
        if (gGameTable.current_stage < 3 && !check_flag(FlagGroup::Status, FG_STATUS_BONUS))
        {
            fgEnemy = FlagGroup::Enemy;
        }
        return globalId != 0xFF && check_flag(fgEnemy, globalId);
    }

    static void* psx_alloc(size_t len)
    {
        auto mem = gGameTable.mem_top;
        gGameTable.mem_top = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mem) + len);
#ifdef DEBUG
        // std::memset(mem, 0xCD, len);
#endif
        return mem;
    }

    template<typename T> static T* psx_alloc()
    {
        return reinterpret_cast<T*>(psx_alloc(sizeof(T)));
    }

    // 0x004E77D0
    static int sce_em_set(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdSceEmSet*>(sce->data);
        sce->data += sizeof(ScdSceEmSet);

        if (is_enemy_dead(opcode->globalId))
        {
            return SCD_RESULT_NEXT;
        }

        EnemySpawnInfo spawnInfo;
        spawnInfo.Animation = opcode->animation;
        spawnInfo.Id = opcode->id;
        spawnInfo.Type = opcode->type;
        spawnInfo.Pose = opcode->pose;
        spawnInfo.Behaviour = opcode->behaviour;
        spawnInfo.Floor = opcode->floor;
        spawnInfo.SoundBank = opcode->soundBank;
        spawnInfo.Texture = opcode->texture;
        spawnInfo.GlobalId = opcode->globalId;
        spawnInfo.Position.x = opcode->x;
        spawnInfo.Position.y = opcode->y;
        spawnInfo.Position.z = opcode->z;
        spawnInfo.Position.d = opcode->d;
        spawnInfo.Animation = opcode->animation;
        spawnInfo.Unknown = opcode->var_14;
        spawn_enemy(spawnInfo);

        return SCD_RESULT_NEXT;
    }

    // 0x004B8470
    static int esp_call(int a0, int a1, Mat16& matrix, Vec16p& vec)
    {
        using sig = int (*)(int, int, Mat16&, Vec16p&);
        auto p = (sig)0x004B8470;
        return p(a0, a1, matrix, vec);
    }

    // 0x004E6E30
    static int scd_sce_espr_on(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdSceEsprOn*>(sce->data);
        sce->data += sizeof(ScdSceEsprOn);
        uint32_t esp_id = opcode->esp_id;

        Mat16& matrix = get_matrix(opcode->work_kind, opcode->work_no);
        Vec16p vec16p{ opcode->svec_x, opcode->svec_y, opcode->svec_z };

        if ((gGameTable.current_stage == 4 && gGameTable.current_room != 0) || gGameTable.current_stage != 3
            || gGameTable.current_room != 10)
        {
            esp_call(opcode->espmv | (((esp_id << 16) | (esp_id & 0xFFFFFF00)) << 8), opcode->dir_y, matrix, vec16p);
            return SCD_RESULT_NEXT;
        }

        if (opcode->work_kind)
        {
            esp_call(opcode->espmv | (((esp_id << 16) | esp_id & 0xFFFFFF00) << 8), opcode->dir_y, matrix, vec16p);
            return SCD_RESULT_NEXT;
        }

        auto v5 = (gGameTable.blood_censor | (esp_id << 8)) << 8;
        esp_call(opcode->espmv | ((esp_id | v5) << 8), opcode->dir_y, matrix, vec16p);
        return SCD_RESULT_NEXT;
    }

    // 0x004E8D70
    static int scd_sce_heal_partner(SceTask* sce)
    {
        gGameTable.byte_98EE7B = 0;
        gGameTable.saved_splayer_health = 200;
        gGameTable.splayer_work->life = 200;
        gGameTable.splayer_work->max_life = 200;
        sce->data++;
        return SCD_RESULT_NEXT;
    }

    static void set_scd_hook(ScdOpcode opcode, ScdOpcodeImpl impl)
    {
        gScdImplTable[opcode] = impl;
    }

    void scd_init_hooks()
    {
        interop::writeJmp(0x004E39E0, &scd_init);
        interop::writeJmp(0x004E3F60, &scd_event_init);
        interop::writeJmp(0x004E4310, &sce_scheduler_main);

        set_scd_hook(SCD_NOP, &scd_nop);
        set_scd_hook(SCD_EVT_NEXT, &scd_evt_next);
        set_scd_hook(SCD_EVT_CHAIN, &scd_evt_chain);
        set_scd_hook(SCD_EVT_END, &scd_evt_end);
        set_scd_hook(SCD_EVT_EXEC, &scd_evt_exec);
        set_scd_hook(SCD_EVT_KILL, &scd_evt_kill);
        set_scd_hook(SCD_IFEL_CK, &scd_ifel_ck);
        set_scd_hook(SCD_END_IF, &scd_end_if);
        set_scd_hook(SCD_SCE_RND, &scd_sce_rnd);
        set_scd_hook(SCD_CUT_CH, &scd_cut_ch);
        set_scd_hook(SCD_CUT_OLD, &scd_cut_old);
        set_scd_hook(SCD_MESSAGE_ON, &scd_message_on);
        set_scd_hook(SCD_AOT_SET, &scd_aot_set);
        set_scd_hook(SCD_WORK_SET, &scd_work_set);
        set_scd_hook(SCD_SCE_ESPR_ON, &scd_sce_espr_on);
        set_scd_hook(SCD_DOOR_AOT_SE, &scd_door_aot_se);
        set_scd_hook(SCE_CUT_AUTO, &scd_cut_auto);
        set_scd_hook(SCD_PLC_MOTION, &scd_plc_motion);
        set_scd_hook(SCD_CUT_REPLACE, &scd_cut_replace);
        set_scd_hook(SCD_ITEM_AOT_SET, &scd_item_aot_set);
        set_scd_hook(SCD_SCE_KEY_CK, &scd_sce_key_ck);
        set_scd_hook(SCD_SCE_EM_SET, &sce_em_set);
        set_scd_hook(SCD_SCE_BGM_CONTROL, &scd_sce_bgm_control);
        set_scd_hook(SCD_SCE_BGMTBL_SET, &scd_sce_bgmtbl_set);
        set_scd_hook(SCD_CUT_BE_SET, &scd_cut_be_set);
        set_scd_hook(SCD_AOT_SET_4P, &scd_aot_set_4p);
        set_scd_hook(SCD_HEAL, &scd_heal);
        set_scd_hook(SCD_POISON_CK, &scd_poison_ck);
        set_scd_hook(SCD_POISON_CLR, &scd_poison_clr);
        set_scd_hook(SCD_SCE_HEAL_PARTNER, &scd_sce_heal_partner);
    }
}