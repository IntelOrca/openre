#pragma once

#include <cstdint>

namespace openre::sce
{
    PlayerEntity* GetPlayerEntity();
    Entity* GetPartnerEntity();
    Entity* GetEnemyEntity(int index);
    ObjectEntity* GetObjectEntity(int index);
    Entity* GetDoorEntity(int index);

    void sce_init_hooks();
    int bitarray_get(uint32_t* bitArray, int index);
    void bitarray_set(uint32_t* bitArray, int index);
    void bitarray_clr(uint32_t* bitArray, int index);
}
