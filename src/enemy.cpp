#include "enemy.h"
#include "interop.hpp"
#include "openre.h"

namespace openre::enemy
{
    bool is_enemy_dead(uint8_t globalId)
    {
        auto fgEnemy = FlagGroup::Enemy2;
        if (gGameTable.current_stage < 3 && !check_flag(FlagGroup::Status, FG_STATUS_BONUS))
        {
            fgEnemy = FlagGroup::Enemy;
        }
        return globalId != 0xFF && check_flag(fgEnemy, globalId);
    }

    // 0x004B20B0
    void em_bin_load_old(uint8_t type)
    {
        using sig = void (*)(uint8_t);
        auto p = (sig)0x004B20B0;
        p(type);
    }

    using EnemyInitFunc = void (*)(EnemyEntity*);

    static EnemyInitFunc get_enemy_init_func(uint8_t type, int slot)
    {
        auto index = (slot * 96) + type;
        return reinterpret_cast<EnemyInitFunc>(gGameTable.enemy_init_table[index]);
    }

    static bool is_zombie(uint8_t type)
    {
        return type >= ZOMBIE_COP && type <= ZOMBIE_RANDOM;
    }

    // 0x004B20B0
    void em_bin_load(uint8_t type)
    {
        auto func = get_enemy_init_func(type, 0);
        if (func == nullptr)
            return;

        // Share same init function for all zombies
        auto sharedType = is_zombie(type) ? ZOMBIE_COP : type;

        auto entries = gGameTable.enemy_init_entries;
        int slot;
        for (slot = 0; slot < 2; slot++)
        {
            if (entries[slot].type == sharedType || entries[slot].enabled == 0)
                break;
        }

        entries[slot].type = sharedType;
        entries[slot].enabled = 1;
        gGameTable.enemy_init_map[type] = get_enemy_init_func(type, slot);
    }

    // 0x004E3AB0
    uint8_t em_kind_search(uint8_t id)
    {
        using sig = uint8_t (*)(uint8_t);
        auto p = (sig)0x004E3AB0;
        return p(id);
    }

    // 0x004B1660
    void* emd_load(int id, Entity* entity, void* buffer)
    {
        using sig = void* (*)(int, Entity*, void*);
        auto p = (sig)0x004B1660;
        return p(id, entity, buffer);
    }

    // 0x004C1270
    void* partswork_set(Entity* entity, void* parts)
    {
        using sig = void* (*)(Entity*, void*);
        auto p = (sig)0x004C1270;
        return p(entity, parts);
    }

    // 0x004C12B0
    void* partswork_link(Entity* entity, void* packetTop, void* kan, int mode)
    {
        using sig = void* (*)(Entity*, void*, void*, int);
        auto p = (sig)0x004C12B0;
        return p(entity, packetTop, kan, mode);
    }

    // 0x004DFD20
    void* sa_dat_set(Entity* entity, void* arg1)
    {
        using sig = void* (*)(Entity*, void*);
        auto p = (sig)0x004DFD20;
        return p(entity, arg1);
    }

    // 0x004C52A0
    void* mirror_model_cp(Entity* entity, void* arg1)
    {
        using sig = void* (*)(Entity*, void*);
        auto p = (sig)0x004C52A0;
        return p(entity, arg1);
    }

    // 0x00443F40
    int* mem_ck_parts_work(int workNo, int id)
    {
        using sig = int* (*)(int, int);
        auto p = (sig)0x00443F40;
        return p(workNo, id);
    }
}
