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
        int16_t TargetX;
        int16_t TargetY;
        int16_t TargetZ;
        int16_t TargetD;
        uint8_t TargetStage;
        uint8_t TargetRoom;
        uint8_t TargetCut;
        uint8_t TargetFloor;
        uint8_t Texture;
        uint8_t DoorType;
        uint8_t KnockType;
        uint8_t LockId;
        uint8_t KeyType;
        uint8_t Free;
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

    using Action = void (*)();
    using SceKind = uint8_t;

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
