#include "scd.h"
#include "audio.h"
#include "re2.h"
#include "sce.h"
#include <cstring>

using namespace openre::audio;
using namespace openre::sce;

namespace openre::scd
{
    enum
    {
        SCD_NOP = 0x00,
        SCD_AOT_SET = 0x2C,
        SCD_WORK_SET = 0x2E,
        SCD_DOOR_AOT_SE = 0x3B,
        SCD_ITEM_AOT_SET = 0x4E,
        SCD_SCE_BGM_CONTROL = 0x51,
        SCD_SCE_BGMTBL_SET = 0x57,
        SCD_AOT_SET_4P = 0x67,
        SCD_DOOR_AOT_SET_4P = 0x68,
        SCD_ITEM_AOT_SET_4P = 0x69,
    };

    using ScdOpcodeImpl = int (*)(SCE_TASK*);

    static SceAotBase** gAotTable = (SceAotBase**)0x988850;
    static uint8_t& gAotCount = *((uint8_t*)0x98E528);
    static ScdOpcodeImpl* gScdImplTable = (ScdOpcodeImpl*)0x53AE10;

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
        return 1;
    }

    // 0x004E51C0
    static int scd_aot_set(SCE_TASK* sce)
    {
        auto opcode = reinterpret_cast<ScdAotSet*>(sce->Data);
        set_aot_entry(opcode->Id, &opcode->Aot);
        sce->Data += sizeof(ScdAotSet);
        return 1;
    }

    // 0x004E5250
    static int scd_door_aot_se(SCE_TASK* sce)
    {
        auto opcode = reinterpret_cast<ScdSceAotDoor*>(sce->Data);
        set_aot_entry(opcode->Id, &opcode->Data.Aot);
        sce->Data += sizeof(ScdSceAotDoor);
        return 1;
    }

    // 0x004E8290
    static int scd_sce_bgm_control(SCE_TASK* sce)
    {
        auto opcode = reinterpret_cast<ScdSceBgmControl*>(sce->Data);

        auto arg = (opcode->var_05) | (opcode->var_04 << 8) | (opcode->var_03 << 16) | (opcode->var_02 << 24) | (opcode->var_01 << 28);
        bgm_set_control(arg);

        sce->Data += sizeof(ScdSceBgmControl);
        return 1;
    }

    // 0x004E82E0
    static int scd_sce_bgmtbl_set(SCE_TASK* sce)
    {
        auto opcode = reinterpret_cast<ScdSceBgmTblSet*>(sce->Data);
        bgm_set_entry((opcode->roomstage << 16) | opcode->var_06 | opcode->var_04);
        sce->Data += sizeof(ScdSceBgmTblSet);
        return 1;
    }

    // 0x004E5200
    static int scd_aot_set_4p(SCE_TASK* sce)
    {
        auto opcode = reinterpret_cast<ScdAotSet4p*>(sce->Data);
        set_aot_entry(opcode->Id, &opcode->Aot);
        opcode->Aot.Sat |= SAT_4P;
        sce->Data += sizeof(ScdAotSet4p);
        return 1;
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
        return 1;
    }

    static void set_scd_hook(ScdOpcode opcode, ScdOpcodeImpl impl)
    {
        gScdImplTable[opcode] = impl;
    }

    void scd_init_hooks()
    {
        set_scd_hook(SCD_NOP, &scd_nop);
        set_scd_hook(SCD_AOT_SET, &scd_aot_set);
        set_scd_hook(SCD_WORK_SET, &scd_work_set);
        set_scd_hook(SCD_DOOR_AOT_SE, &scd_door_aot_se);
        set_scd_hook(SCD_SCE_BGM_CONTROL, &scd_sce_bgm_control);
        set_scd_hook(SCD_SCE_BGMTBL_SET, &scd_sce_bgmtbl_set);
        set_scd_hook(SCD_AOT_SET_4P, &scd_aot_set_4p);
    }
}
