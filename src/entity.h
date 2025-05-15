#pragma once

#include "re2.h"

namespace openre
{
    enum
    {
        ENTITY_STATUS_FLAG_2 = 2
    };

    Kage* kage_work_set(Kage** pK, uint32_t offset, uint32_t half, uint32_t color, const Vec32* pPos);
    void kage_work_init();
    void kage_work9_init();
    int joint_move(Entity* player, Emr* pKanPtr, Edd* pSeqPtr, int lateFlag);
    int32_t goto00_ck(Entity* entity, int32_t x, int32_t z, int32_t dist);
    void add_speed_xz(ActorEntity* entity, int16_t d);
    uint8_t compute_nfloor(int32_t posY);
    void oma_set_ofs(ObjectEntity* object);
    int omd_in_check(Vec32* vec, ObjectEntity* object, int a2, int a3);
    void add_speed_xz(Entity* entity, int16_t d);
    void rbj_set();
    int direction_check(int16_t a0, int16_t a1, int16_t a2, int16_t a3);
}
