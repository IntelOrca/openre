#pragma once

#include "re2.h"

namespace openre
{
    Kage* kage_work_set(Kage** pK, uint32_t offset, uint32_t half, uint32_t color, const Vec32* pPos);
    void kage_work_init();
    void kage_work9_init();
    int joint_move(Entity* player, Emr* pKanPtr, Edd* pSeqPtr, int lateFlag);
    int32_t goto00_ck(Entity* entity, int32_t x, int32_t z, int32_t dist);
}
