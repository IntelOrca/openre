#pragma once

#include "re2.h"

namespace openre
{
    int joint_move(Entity* player, int pKanPtr, int pSeqPtr, int lateFlag);
    int32_t goto00_ck(Entity* entity, int32_t x, int32_t z, int32_t dist);
}
