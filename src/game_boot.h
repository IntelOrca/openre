#pragma once

#include <cstdint>

namespace openre::game_boot
{
    /// Loads background image data to the display surface
    /// @param data Pointer to the ADT/TIM background buffer
    // 0x0043F5A0
    void bg_to_surface(uint8_t* data);

    /// Initialises a new game or loads a save before entering game_loop
    /// (0x00506750). Module-only: every start is latched by the Lua menu
    /// (core.loadDemo / core.loadSave / core.startRoomWarp /
    /// core.startNewGame) before core.runGame boots, so the state machine
    /// takes only the module branches; the native boot screens are gone.
    // 0x00506750
    void game_boot_init();
}