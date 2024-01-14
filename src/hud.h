#pragma once

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

    void hud_init_hooks();
}
