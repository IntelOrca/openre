#include "entity.h"
#include "interop.hpp"
#include "math.h"
#include "openre.h"

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
        int16_t at_d;
        int16_t at_w;

        if (object->cdir.y & 0x400)
        {
            int16_t ofs_x = object->atd[0].ofs.x;
            object->atd[0].atw_x = object->atd[0].ofs.z;
            at_d = object->atd[0].at_d;
            object->atd[0].atw_z = ofs_x;
            at_w = object->atd[0].at_w;
        }
        else
        {
            int16_t ofs_z = object->atd[0].ofs.z;
            object->atd[0].atw_x = object->atd[0].ofs.x;
            at_d = object->atd[0].at_w;
            object->atd[0].atw_z = ofs_z;
            at_w = object->atd[0].at_d;
        }

        object->atd[0].w = at_d;
        object->atd[0].d = at_w;

        int ofs_y = object->atd[0].ofs.y;
        object->atd[0].pos.x = object->m.pos.x + object->atd[0].atw_x;
        int atw_z = object->atd[0].atw_z;
        object->atd[0].pos.y = object->m.pos.y + ofs_y;
        object->atd[0].pos.z = object->m.pos.z + atw_z;
    }

    // 0x004CEEF0
    int omd_in_check(Vec32* vec, ObjectEntity* object, int a2, int a3)
    {
        unsigned w_base = static_cast<unsigned>(a2) + static_cast<uint16_t>(object->atd[0].w);
        unsigned d_base = static_cast<unsigned>(a2) + static_cast<uint16_t>(object->atd[0].d);

        if (w_base + vec->x - object->atd[0].pos.x > 2 * w_base)
            return 0;
        if (d_base + vec->z - object->atd[0].pos.z > 2 * d_base)
            return 0;
        if (!a3)
            return 1;

        int v5 = static_cast<uint16_t>(object->atd[0].at_h) - 1;
        if (v5 < 0)
            return 0;
        return static_cast<unsigned>(v5 + vec->y - object->atd[0].pos.y) <= static_cast<unsigned>(2 * v5 + 1);
    }

    // 0x004B4480
    void rbj_set()
    {
        auto* rdt_ptr = gGameTable.rdt;
        if (!rdt_ptr)
            return;

        auto* v1 = static_cast<uint8_t*>(rdt_ptr->offsets[22]);
        if (!v1)
            return;

        int v8 = v1[4];
        auto* v2 = reinterpret_cast<uint32_t*>(v1 + *reinterpret_cast<uint32_t*>(v1));

        do
        {
            auto* v3 = reinterpret_cast<int*>(v1 + *v2);
            int v4 = *v3;

            if (gGameTable.fg_system & 0x1000000)
            {
                switch (gGameTable.word_98EB20)
                {
                case 0:
                case 3:
                    if ((v4 & 1) != 0)
                    {
                        gGameTable.pl.field_190 = reinterpret_cast<int32_t>(v3 + 1);
                        gGameTable.pl.field_194 = reinterpret_cast<int32_t>(v1 + v2[1]);
                    }
                    break;
                case 1:
                case 2:
                    if ((v4 & 2) != 0)
                    {
                        gGameTable.pl.field_190 = reinterpret_cast<int32_t>(v3 + 1);
                        gGameTable.pl.field_194 = reinterpret_cast<int32_t>(v1 + v2[1]);
                    }
                    break;
                default: break;
                }

                auto v4_shifted = static_cast<unsigned>(v4) >> 1;
                for (unsigned i = v4_shifted >> 1, idx = 1; i; ++idx)
                {
                    if (i & 1)
                    {
                        auto* entity = (&gGameTable.splayer_work)[idx];
                        if (entity && (entity->be_flg & 1))
                        {
                            entity->field_190 = reinterpret_cast<int32_t>(v3 + 1);
                            entity->field_194 = reinterpret_cast<int32_t>(v1 + v2[1]);
                        }
                    }
                    i >>= 1;
                }
            }
            else
            {
                bool assign_pl;
                if (gGameTable.fg_status & 0x2000000)
                    assign_pl = v4 < 0;
                else
                    assign_pl = (v4 & 1) != 0;

                if (assign_pl)
                {
                    gGameTable.pl.field_190 = reinterpret_cast<int32_t>(v3 + 1);
                    gGameTable.pl.field_194 = reinterpret_cast<int32_t>(v1 + v2[1]);
                }

                for (unsigned i = static_cast<unsigned>(v4) >> 1, idx = 0; i; ++idx)
                {
                    if (i & 1)
                    {
                        auto* entity = (&gGameTable.splayer_work)[idx];
                        if (entity && (entity->be_flg & 1))
                        {
                            entity->field_190 = reinterpret_cast<int32_t>(v3 + 1);
                            entity->field_194 = reinterpret_cast<int32_t>(v1 + v2[1]);
                        }
                    }
                    i >>= 1;
                }
            }

            v2 += 2;
            --v8;
        } while (v8 > 0);
    }

    // 0x004B2360
    int direction_check(int16_t a0, int16_t a1, int16_t a2, int16_t a3)
    {
        return interop::call<int, int16_t, int16_t, int16_t, int16_t>(0x004B2360, a0, a1, a2, a3);
    }
}
