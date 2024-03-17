#pragma once

#include <cstdint>

struct Entity;

namespace openre::enemy
{
    bool is_enemy_dead(uint8_t globalId);

    void em_bin_load(uint8_t type);
    uint8_t em_kind_search(uint8_t id);
    void* emd_load(int id, Entity* entity, void* buffer);

    void* partswork_set(Entity* entity, void* parts);
    void* partswork_link(Entity* entity, void* packetTop, void* kan, int mode);
    void* sa_dat_set(Entity* entity, void* arg1);
    void* mirror_model_cp(Entity* entity, void* arg1);
    int* mem_ck_parts_work(int workNo, int id);
}
