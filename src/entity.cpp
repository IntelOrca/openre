#include "entity.h"
#include "interop.hpp"

namespace openre
{
    // 0x004B2CE0
    Kage* kage_work_set(Kage** pK, uint32_t offset, uint32_t half, uint32_t color, const Vec32* pPos)
    {
        return interop::call<Kage*, Kage**, uint32_t, uint32_t, uint32_t, const Vec32*>(
            0x004B2CE0, pK, offset, half, color, pPos);
    }

    // 0x004B2C00
    void kage_work_init()
    {
        interop::call(0x004B2C00);
    }

    // 0x004B2CC0
    void kage_work9_init()
    {
        interop::call(0x004B2CC0);
    }

    // 0x004C1C30
    int joint_move(Entity* entity, Emr* pKanPtr, Edd* pSeqPtr, int lateFlag)
    {
        return interop::call<int, Entity*, Emr*, Edd*, int>(0x004C1C30, entity, pKanPtr, pSeqPtr, lateFlag);
    }

    // 0x004B2440
    int32_t goto00_ck(Entity* entity, int32_t x, int32_t z, int32_t dist)
    {
        return interop::call<int32_t, Entity*, int32_t, int32_t, int32_t>(0x004B2440, entity, x, z, dist);
    }

    // 0x004B4480
    void rbj_set()
    {
        interop::call(0x004B4480);
    }
}
