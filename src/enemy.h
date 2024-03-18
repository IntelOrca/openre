#pragma once

#include <cstdint>

struct Entity;

namespace openre::enemy
{
    enum
    {
        ZOMBIE_COP = 16,
        ZOMBIE_BRAD = 17,
        ZOMBIE_GUY1 = 18,
        ZOMBIE_GIRL = 19,
        ZOMBIE_TESTSUBJECT = 21,
        ZOMBIE_SCIENTIST = 22,
        ZOMBIE_NAKED = 23,
        ZOMBIE_GUY2 = 24,
        ZOMBIE_GUY3 = 30,
        ZOMBIE_RANDOM = 31,
        ZOMBIE_DOG = 32,
        CROW = 33,
        LICKER_RED = 34,
        ALLIGATOR = 35,
        LICKER_GREY = 36,
        SPIDER = 37,
        SPIDER_BABY = 38,
        G_EMBRYO = 39,
        G_ADULT = 40,
        COCKROACH = 41,
        TYRANT_1 = 42,
        TYRANT_2 = 43,
        ZOMBIE_ARMS = 45,
        IVY = 46,
        VINES = 47,
        BIRKIN_1 = 48,
        BIRKIN_2 = 49,
        BIRKIN_3 = 50,
        BIRKIN_4 = 51,
        BIRKIN_5 = 52,
        IVY_POISON = 57,
        MOTH = 58,
        MAGGOTS = 59,
        IRONS_1 = 64,
        ADA_1 = 65,
        IRONS_2 = 66,
        ADA_2 = 67,
        BEN_1 = 68,
        SHERRY_PENDANT = 69,
        BEN_2 = 70,
        ANNETTE_1 = 71,
        KENDO = 72,
        ANNETTE_2 = 73,
        MARVIN = 74,
        MAYORSDAUGHTER = 75,
        SHERRY_JACKET = 79,
        LEON_RPD = 80,
        CLAIRE = 81,
        LEON_BANDAGED = 84,
        CLAIRE_NO_JACKET = 85,
        LEON_TANKTOP = 88,
        CLAIRE_COWGIRL = 89,
        LEON_LEATHER = 90,
    };

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
