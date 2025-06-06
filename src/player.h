#pragma once

#include "re2.h"
#include <cstdint>

namespace openre::player
{
    enum
    {
        PLD_LEON_0,
        PLD_CLAIRE_0,
        PLD_LEON_1,
        PLD_CLAIRE_1,
        PLD_LEON_2,
        PLD_CLAIRE_2,
        PLD_LEON_3,
        PLD_CLAIRE_3,
        PLD_LEON_4,
        PLD_CLAIRE_4,
        PLD_LEON_5,
        PLD_CHRIS,
        PLD_HUNK,
        PLD_TOFU,
        PLD_ADA,
        PLD_SHERRY
    };

    enum class Routine
    {
        IDLE,
        FORWARD,
        RUN_FORWARD,
        BACKWARD,
        ROTATE,
        AIM,
        QUICKTURN,
        PUSH_OBJECT,
        PICK_UP_ITEM,
    };

    extern uint8_t& gCurrentWeaponIndex;
    extern uint8_t& gInventorySize;
    extern InventorySlot* gInventory;

    constexpr uint8_t FULL_INVENTORY_SIZE = 11;

    int inventory_find_item(ItemType type);
    int player_check_life();
    void player_set(PlayerEntity* player);
    bool is_aiming();
    void set_routine(Routine routine);
    void player_move(PlayerEntity* player);

    void player_init_hooks();
}
