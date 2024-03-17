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
    void em_bin_load(uint8_t type)
    {
        using sig = void (*)(uint8_t);
        auto p = (sig)0x004B20B0;
        p(type);
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
