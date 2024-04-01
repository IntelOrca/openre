#include "entity.h"
#include "interop.hpp"

namespace openre
{
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
}
