#pragma once

#include "re2.h"
#include <cstdint>

struct Entity;

namespace openre::enemy
{
    enum
    {
        EM_ZOMBIE_COP = 16,
        EM_ZOMBIE_BRAD = 17,
        EM_ZOMBIE_GUY1 = 18,
        EM_ZOMBIE_GIRL = 19,
        EM_ZOMBIE_14,
        EM_ZOMBIE_TESTSUBJECT = 21,
        EM_ZOMBIE_SCIENTIST = 22,
        EM_ZOMBIE_NAKED = 23,
        EM_ZOMBIE_GUY2 = 24,
        EM_ZOMBIE_19,
        EM_ZOMBIE_1A,
        EM_ZOMBIE_1B,
        EM_ZOMBIE_1C,
        EM_ZOMBIE_1D,
        EM_ZOMBIE_GUY3 = 30,
        EM_ZOMBIE_RANDOM = 31,
        EM_ZOMBIE_DOG = 32,
        EM_CROW = 33,
        EM_LICKER_RED = 34,
        EM_ALLIGATOR = 35,
        EM_LICKER_GREY = 36,
        EM_SPIDER = 37,
        EM_SPIDER_BABY = 38,
        EM_G_EMBRYO = 39,
        EM_G_ADULT = 40,
        EM_COCKROACH = 41,
        EM_TYRANT_1 = 42,
        EM_TYRANT_2 = 43,
        EM_2C,
        EM_ZOMBIE_ARMS = 45,
        EM_IVY = 46,
        EM_VINES = 47,
        EM_BIRKIN_1 = 48,
        EM_BIRKIN_2 = 49,
        EM_BIRKIN_3 = 50,
        EM_BIRKIN_4 = 51,
        EM_BIRKIN_5 = 52,
        EM_36,
        EM_37,
        EM_38,
        EM_IVY_POISON = 57,
        EM_MOTH = 58,
        EM_MAGGOTS = 59,
        EM_3E,
        EM_3F,
        EM_IRONS_1 = 64,
        EM_ADA_1 = 65,
        EM_IRONS_2 = 66,
        EM_ADA_2 = 67,
        EM_BEN_1 = 68,
        EM_SHERRY_PENDANT = 69,
        EM_BEN_2 = 70,
        EM_ANNETTE_1 = 71,
        EM_KENDO = 72,
        EM_ANNETTE_2 = 73,
        EM_MARVIN = 74,
        EM_MAYORSDAUGHTER = 75,
        EM_SHERRY_JACKET = 79,
        EM_LEON_RPD = 80,
        EM_CLAIRE = 81,
        EM_LEON_BANDAGED = 84,
        EM_CLAIRE_NO_JACKET = 85,
        EM_LEON_TANKTOP = 88,
        EM_CLAIRE_COWGIRL = 89,
        EM_LEON_LEATHER = 90,
        EM_NONE = 0xFF,
    };

    struct EnemySpawnInfo
    {
        uint8_t Id{};
        uint8_t Type{};
        uint8_t Pose{};
        uint8_t Behaviour{};
        uint8_t Floor{};
        uint8_t SoundBank{};
        uint8_t Texture{};
        uint8_t GlobalId{};
        Vec32d Position{};
        int16_t Animation{};
        int16_t Unknown{};
    };

    using EnemyFunc = void (*)(EnemyEntity*);
    using EnemyRoutineFunc = void (*)(EnemyEntity*, Emr*, Edd*);

    bool is_enemy_dead(uint8_t globalId);

    void* partswork_set(Entity* entity, void* parts);
    void* partswork_link(Entity* entity, void* packetTop, void* kan, int mode);
    void* sa_dat_set(Entity* entity, void* arg1);
    void* mirror_model_cp(Entity* entity, void* arg1);
    int* mem_ck_parts_work(int workNo, int id);
    void oba_ck_em(EnemyEntity* enemy);
    void sca_ck_em(EnemyEntity* enemy, int a1);
    void add_speed_xz(Entity* entity, int16_t d);
    int root_ck(Entity* entity, int a1, int a2, int a3);
    void goto00(Entity* entity, int x, int z, int t);
    void rot_neck_em(Entity* entity, int d);
    void snd_se_enem(uint8_t id, EnemyEntity* enemy);

    bool spawn_enemy(const EnemySpawnInfo& info);

    void em_dog(EnemyEntity* enemy);

    void enemy_init_hooks();
}
