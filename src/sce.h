#pragma once

#include <cstdint>

#include "re2.h"

namespace openre::sce
{
    enum
    {
        SCE_AUTO,
        SCE_DOOR,
        SCE_ITEM,
        SCE_NORMAL,
        SCE_MESSAGE,
        SCE_EVENT,
        SCE_FLAG_CHG,
        SCE_WATER,
        SCE_MOVE,
        SCE_SAVE,
        SCE_ITEMBOX,
        SCE_DAMAGE,
        SCE_STATUS,
        SCE_HIKIDASHI,
        SCE_WINDOWS,
    };

    constexpr uint32_t GAME_FLAG_15 = (1 << 15);
    constexpr uint32_t GAME_FLAG_EASY = (1 << 26);
    constexpr uint32_t GAME_FLAG_HAS_PARTNER = (1 << 28);
    constexpr uint32_t GAME_FLAG_IS_PLAYER_1 = (1 << 31);

    struct SceAotBase
    {
        uint8_t Sce;
        uint8_t Sat;
        uint8_t nFloor;
        uint8_t Super;
    };

    struct SceAotDoorData
    {
        int16_t TargetX;     // 0x00
        int16_t TargetY;     // 0x02
        int16_t TargetZ;     // 0x04
        int16_t TargetD;     // 0x06
        uint8_t TargetStage; // 0x08
        uint8_t TargetRoom;  // 0x09
        uint8_t TargetCut;   // 0x0A
        uint8_t TargetFloor; // 0x0B
        uint8_t Texture;     // 0x0C
        uint8_t DoorType;    // 0x0D
        uint8_t KnockType;   // 0x0E
        uint8_t LockId;      // 0x0F
        uint8_t KeyType;     // 0x10
        uint8_t Free;        // 0x11
    };

    struct SceAotItemData
    {
        uint8_t type;
        uint8_t pad_01;
        uint16_t amount;
        uint16_t flag;
        uint8_t md1;
        uint8_t action;
    };

    struct SceAotMessageData
    {
        uint16_t var_00;
        uint16_t var_02;
        uint16_t var_04;
    };

    struct SceAotEventData
    {
        uint16_t task_index;
        uint8_t pad_02;
        uint8_t event_index;
    };

    using Action = void (*)();
    using AotId = uint8_t;
    using SceKind = uint8_t;

    PlayerEntity* GetPlayerEntity();
    Entity* GetPartnerEntity();
    Entity* GetEnemyEntity(int index);
    ObjectEntity* GetObjectEntity(int index);
    DoorEntity* GetDoorEntity(int index);

    void sce_init_hooks();
    int bitarray_get(uint32_t* bitArray, int index);
    void bitarray_set(uint32_t* bitArray, int index, bool value);
    void bitarray_set(uint32_t* bitArray, int index);
    void bitarray_clr(uint32_t* bitArray, int index);
    void set_aot_entry(AotId id, SceAotBase* aot);
    void sce_work_clr();
    void sce_work_clr_at();
    void sce_work_clr_set();
    void sce_rnd_set();
    void sce_model_init();
    void sce_scheduler_set();
}
