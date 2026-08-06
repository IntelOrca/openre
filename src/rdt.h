#pragma once

#include "re2.h"

namespace openre::rdt
{
    enum class RdtOffsetKind
    {
        EDT,
        VH,
        VB,
        VH_TRIAL,
        VB_TRIAL,
        OVA,
        SCA,
        RID,
        RVD,
        LIT,
        MODELS,
        FLR,
        BLK,
        MSG_JA,
        MSG_EN,
        SCROLL,
        SCD_INIT,
        SCD_MAIN,
        ESP_IDS,
        ESP_EFF_TABLE,
        EFF,
        MODEL_TEXTURES,
        RBJ,
    };

    template<typename T> T* rdt_get_offset(RdtOffsetKind kind)
    {
        return static_cast<T*>(rdt_get_offset<void>(kind));
    }

    template<> void* rdt_get_offset(RdtOffsetKind kind);

    // A spawn point used for command-line room warps (-p/-r). Positions and
    // cut numbers are taken from the game's new game and demo definitions.
    struct RoomSpawnPoint
    {
        uint8_t stage;
        uint8_t room;
        int16_t x;
        int16_t y;
        int16_t z;
        int16_t cdir;
        uint8_t cut;
    };

    // Returns the spawn point for a room, or nullptr if none is known.
    const RoomSpawnPoint* rdt_get_spawn_point(int stage, int room);
}
