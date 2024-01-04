#include "scd.h"
#include "audio.h"
#include "interop.hpp"
#include "openre.h"
#include "re2.h"
#include "sce.h"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace openre::audio;
using namespace openre::sce;

namespace openre::scd
{
    enum
    {
        SCD_NOP = 0x00,
        SCD_EVT_END = 0x01,
        SCD_EVT_NEXT = 0x02,
        SCD_EVT_KILL = 0x05,
        SCD_AOT_SET = 0x2C,
        SCD_WORK_SET = 0x2E,
        SCD_DOOR_AOT_SE = 0x3B,
        SCD_ITEM_AOT_SET = 0x4E,
        SCD_SCE_BGM_CONTROL = 0x51,
        SCD_SCE_BGMTBL_SET = 0x57,
        SCD_AOT_SET_4P = 0x67,
        SCD_DOOR_AOT_SET_4P = 0x68,
        SCD_ITEM_AOT_SET_4P = 0x69,
        SCD_HEAL = 0x83,
        SCD_POISON_CK = 0x86,
        SCD_POISON_CLR = 0x87,
    };

    enum
    {
        SCD_RESULT_FALSE,
        SCD_RESULT_NEXT,
        SCD_RESULT_NEXT_TICK,
    };

    constexpr uint8_t SCD_STATUS_EMPTY = 0;

    using ScdOpcodeImpl = int (*)(SCE_TASK*);

    static SceAotBase** gAotTable = (SceAotBase**)0x988850;
    static uint8_t& gAotCount = *((uint8_t*)0x98E528);
    static ScdOpcodeImpl* gScdImplTable = (ScdOpcodeImpl*)0x53AE10;
    static uint32_t& gRandomBase = *((uint32_t*)0x695E58);
    static int16_t& word_989EEE = *((int16_t*)0x989EEE);
    static int16_t& word_98EB26 = *((int16_t*)0x98EB26);
    static int16_t& word_98EB28 = *((int16_t*)0x98EB28);

    static int get_max_tasks()
    {
        return 14;
    }

    static SCE_TASK* get_task(SceTaskId index)
    {
        assert(index < get_max_tasks());
        return &((SCE_TASK*)0x00694A00)[index];
    }

    // 0x004E39E0
    static void scd_init()
    {
        auto maxTasks = get_max_tasks();
        for (auto i = 0; i < maxTasks; i++)
        {
            auto task = get_task(i);
            task->Status = SCD_STATUS_EMPTY;
            task->Task_level = maxTasks - i - 1;
            task->Sub_ctr = 0;
            task->Ifel_ctr[0] = 0xFF;
            task->Loop_ctr[0] = 0xFF;
        }
        gRandomBase = 0x138201C3;
    }

    // 0x004E3DA0
    static void sce_work_clr()
    {
        interop::call(0x004E3DA0);
    }

    // 0x004E3F40
    static void sce_aot_init()
    {
        interop::call(0x004E3F40);
    }

    // 0x004E3DE0
    static void sce_work_clr_at()
    {
        interop::call(0x004E3DE0);
    }

    static int scd_execute_opcode(SCE_TASK* task, ScdOpcode instruction)
    {
        return gScdImplTable[instruction](task);
    }

    // 0x004E4310
    static void sce_scheduler_main()
    {
        for (auto i = 0; i < 10; i++)
        {
            auto task = get_task(i);
            if (task->Status != SCD_STATUS_EMPTY)
            {
                while (true)
                {
                    auto opcode = *task->Data;
                    auto result = scd_execute_opcode(task, opcode);
                    if (dword_68A204->var_13 != 0)
                        return;
                    if (result == SCD_RESULT_NEXT)
                        continue;
                    if (result == SCD_RESULT_NEXT_TICK)
                        break;
                    auto eax = task->Sub_ctr;
                    auto cl = task->Ifel_ctr[eax];
                    if (cl & 0x80)
                        break;
                    task->pS_SP--;
                    task->Data = *task->pS_SP;
                    task->Ifel_ctr[eax]--;
                }
            }
        }
        sce_work_clr();
        sce_work_clr_at();
    }

    static void set_aot_entry(AotId id, SceAotBase* aot)
    {
        auto& entry = gAotTable[id];
        if (entry == nullptr)
        {
            gAotCount++;
        }
        entry = aot;
    }

    // 0x004E43B0
    static int scd_nop(SCE_TASK* sce)
    {
        sce->Data++;
        return SCD_RESULT_NEXT;
    }

    // 0x004E4490
    static int scd_evt_kill(SCE_TASK* sce)
    {
        sce->Data++;
        auto taskId = *sce->Data++;
        auto taskToKill = get_task(taskId);
        taskToKill->Status = SCD_STATUS_EMPTY;
        return SCD_RESULT_NEXT;
    }

    // 0x004E51C0
    static int scd_aot_set(SCE_TASK* sce)
    {
        auto opcode = reinterpret_cast<ScdAotSet*>(sce->Data);
        set_aot_entry(opcode->Id, &opcode->Aot);
        sce->Data += sizeof(ScdAotSet);
        return SCD_RESULT_NEXT;
    }

    // 0x004E5250
    static int scd_door_aot_se(SCE_TASK* sce)
    {
        auto opcode = reinterpret_cast<ScdSceAotDoor*>(sce->Data);
        set_aot_entry(opcode->Id, &opcode->Data.Aot);
        sce->Data += sizeof(ScdSceAotDoor);
        return SCD_RESULT_NEXT;
    }

    // 0x004E8290
    static int scd_sce_bgm_control(SCE_TASK* sce)
    {
        auto opcode = reinterpret_cast<ScdSceBgmControl*>(sce->Data);

        auto arg = (opcode->var_05) | (opcode->var_04 << 8) | (opcode->var_03 << 16) | (opcode->var_02 << 24) | (opcode->var_01 << 28);
        bgm_set_control(arg);

        sce->Data += sizeof(ScdSceBgmControl);
        return SCD_RESULT_NEXT;
    }

    // 0x004E82E0
    static int scd_sce_bgmtbl_set(SCE_TASK* sce)
    {
        auto opcode = reinterpret_cast<ScdSceBgmTblSet*>(sce->Data);
        bgm_set_entry((opcode->roomstage << 16) | opcode->var_06 | opcode->var_04);
        sce->Data += sizeof(ScdSceBgmTblSet);
        return SCD_RESULT_NEXT;
    }

    // 0x004E5200
    static int scd_aot_set_4p(SCE_TASK* sce)
    {
        auto opcode = reinterpret_cast<ScdAotSet4p*>(sce->Data);
        set_aot_entry(opcode->Id, &opcode->Aot);
        opcode->Aot.Sat |= SAT_4P;
        sce->Data += sizeof(ScdAotSet4p);
        return SCD_RESULT_NEXT;
    }

    // 0x004E5E90
    static int scd_work_set(SCE_TASK* sce)
    {
        auto opcode = reinterpret_cast<ScdAotSet4p*>(sce->Data);
        auto wkKind = sce->Data[1];
        auto wkIndex = sce->Data[2];

        std::memset(sce->Spd, 0, sizeof(sce->Spd));
        std::memset(sce->Dspd, 0, sizeof(sce->Dspd));
        std::memset(sce->Aspd, 0, sizeof(sce->Aspd));
        std::memset(sce->Adspd, 0, sizeof(sce->Adspd));

        sce->Data += 3;
        switch (wkKind)
        {
        case WK_PLAYER:
            sce->pWork = GetPlayerEntity();
            break;
        case WK_SPLAYER:
            sce->pWork = GetPartnerEntity();
            break;
        case WK_ENEMY:
            sce->pWork = GetEnemyEntity(wkIndex);
            break;
        case WK_OBJECT:
            sce->pWork = GetObjectEntity(wkIndex);
            break;
        case WK_DOOR:
            sce->pWork = GetDoorEntity(wkIndex);
            break;
        }
        return SCD_RESULT_NEXT;
    }

    // 0x004E4420
    static int scd_evt_next(SCE_TASK* sce)
    {
        sce->Data++;
        return SCD_RESULT_NEXT_TICK;
    }

    // 0x004E43D0
    static int scd_evt_end(SCE_TASK* sce)
    {
        auto subroutineDepth = sce->Sub_ctr;
        if (subroutineDepth == 0)
        {
            sce->Status = SCD_STATUS_EMPTY;
            return SCD_RESULT_NEXT_TICK;
        }

        auto stackOffset = *(&sce->Task_level + subroutineDepth);
        auto callerIndex = subroutineDepth - 1;
        sce->Data = reinterpret_cast<uint8_t*>(sce->Ret_addr[callerIndex]);
        sce->Sub_ctr = callerIndex;
        sce->pS_SP = reinterpret_cast<uint8_t**>(&(sce->Stack[callerIndex + (stackOffset + 1)]));
        return SCD_RESULT_NEXT;
    }

    // 0x004E8FB0
    static int scd_heal(SCE_TASK* sce)
    {
        sce->Data++;
        gPlayerEntity.Life = gPlayerEntity.Max_life;
        gPoisonTimer = 0;
        gPoisonStatus = 0;
        return SCD_RESULT_NEXT;
    }

    // 0x004E90C0
    static int scd_poison_ck(SCE_TASK* sce)
    {
        sce->Data++;
        return gPoisonStatus != 0 ? SCD_RESULT_NEXT : SCD_RESULT_FALSE;
    }

    // 0x004E90E0
    static int scd_poison_clr(SCE_TASK* sce)
    {
        sce->Data++;
        gPoisonTimer = 0;
        gPoisonStatus = 0;
        gPlayerEntity.Routine_0 = 1;
        return SCD_RESULT_NEXT;
    }

    static void set_scd_hook(ScdOpcode opcode, ScdOpcodeImpl impl)
    {
        gScdImplTable[opcode] = impl;
    }

    void scd_init_hooks()
    {
        interop::writeJmp(0x004E39E0, &scd_init);
        interop::writeJmp(0x004E4310, &sce_scheduler_main);

        set_scd_hook(SCD_NOP, &scd_nop);
        set_scd_hook(SCD_EVT_NEXT, &scd_evt_next);
        set_scd_hook(SCD_EVT_END, &scd_evt_end);
        set_scd_hook(SCD_EVT_KILL, &scd_evt_kill);
        set_scd_hook(SCD_AOT_SET, &scd_aot_set);
        set_scd_hook(SCD_WORK_SET, &scd_work_set);
        set_scd_hook(SCD_DOOR_AOT_SE, &scd_door_aot_se);
        set_scd_hook(SCD_SCE_BGM_CONTROL, &scd_sce_bgm_control);
        set_scd_hook(SCD_SCE_BGMTBL_SET, &scd_sce_bgmtbl_set);
        set_scd_hook(SCD_AOT_SET_4P, &scd_aot_set_4p);
        set_scd_hook(SCD_HEAL, &scd_heal);
        set_scd_hook(SCD_POISON_CK, &scd_poison_ck);
        set_scd_hook(SCD_POISON_CLR, &scd_poison_clr);
    }
}
