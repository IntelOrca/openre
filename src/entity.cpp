#include "entity.h"
#include "interop.hpp"
#include "math.h"

using namespace openre::math;

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

    // 0x004B21D0
    void add_speed_xz(ActorEntity* entity, int16_t d)
    {
        Mat16 rotMatrix;
        Vec16p vec{ 0, entity->cdir.y + d, 0 };
        rotate_matrix(vec, rotMatrix);
        apply_matrixsv(rotMatrix, entity->spd, vec);
        entity->m.pos.x += vec.x;
        entity->m.pos.z += vec.z;
    }

    uint8_t compute_nfloor(int32_t posY)
    {
        auto mul = static_cast<int64_t>(0x6E5D4C3B) * posY;
        auto hi32 = static_cast<int32_t>(mul >> 32);
        auto diff = hi32 - posY;
        auto floorDiv = diff >> 10;
        auto correction = (diff >> 31) & 1;
        return floorDiv + correction;
    }

    // 0x004CD610
    void oma_set_ofs(ObjectEntity* object)
    {
        interop::call<void, ObjectEntity*>(0x004CD610, object);
    }

    // 0x004CEEF0
    int omd_in_check(Vec32* vec, ObjectEntity* object, int a2, int a3)
    {
        return interop::call<int, Vec32*, ObjectEntity*, int, int>(0x004CEEF0, vec, object, a2, a3);
    }
}
