#pragma once

#include <cstdint>
#include "re2.h"

namespace openre::player
{
    extern uint8_t& gCurrentWeaponIndex;
    extern uint8_t& gInventorySize;
    extern InventorySlot* gInventory;

    int inventory_find_item(ItemType type);
    void player_init_hooks();
}
