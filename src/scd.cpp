#include "scd.h"
#include "re2.h"

namespace openre::scd
{
    enum {
        SCD_NOP = 0x00,
        SCD_AOT_SET = 0x2C,
        SCD_DOOR_AOT_SE = 0x3B,
        SCD_AOT_SET_4P = 0x67,
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

    static int scd_nop(SCE_TASK* sce)
    {
        sce->Data++;
        return 1;
    }

    static int scd_aot_set(SCE_TASK* sce)
    {
        auto opcode = reinterpret_cast<ScdAotSet*>(sce->Data);
        set_aot_entry(opcode->Id, &opcode->Aot);
        sce->Data += sizeof(ScdAotSet);
        return 1;
    }

    static int scd_door_aot_se(SCE_TASK* sce)
    {
        auto opcode = reinterpret_cast<ScdSceAotDoor*>(sce->Data);
        set_aot_entry(opcode->Id, &opcode->Data.Aot);
        sce->Data += sizeof(ScdSceAotDoor);
        return 1;
    }

    static int scd_aot_set_4p(SCE_TASK* sce)
    {
        auto opcode = reinterpret_cast<ScdAotSet4p*>(sce->Data);
        set_aot_entry(opcode->Id, &opcode->Aot);
        opcode->Aot.Sat |= SAT_4P;
        sce->Data += sizeof(ScdAotSet4p);
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
        set_scd_hook(SCD_DOOR_AOT_SE, &scd_door_aot_se);
        set_scd_hook(SCD_AOT_SET_4P, &scd_aot_set_4p);
    }
}
