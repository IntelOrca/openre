#pragma once

#include "re2.h"
#include <cstdint>

namespace openre::hud
{
    enum
    {
        HUD_MODE_INVENTORY,
        HUD_MODE_ITEM_BOX,
        HUD_MODE_PICKUP_ITEM,
        HUD_MODE_MAP_1,
        HUD_MODE_MAP_2,
    };

    void hud_fade_set(short a0, short add, char mask, char pri);
    void hud_fade_adjust(int no, int16_t kido, uint32_t rgb, PsxRect* rect);
    bool hud_fade_status(int no);
    void hud_fade_off(int no);

    void hud_init_hooks();
}