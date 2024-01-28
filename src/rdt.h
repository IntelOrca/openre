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

    template<typename T>
    T* rdt_get_offset(RdtOffsetKind kind)
    {
        return static_cast<T*>(rdt_get_offset<void>(kind));
    }

    template<>
    void* rdt_get_offset(RdtOffsetKind kind);
}
