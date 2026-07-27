#pragma once

#include <cstdint>

namespace openre::title
{
    /// Loads background image data to the display surface
    /// @param data Pointer to the ADT/TIM background buffer
    // 0x0043F5A0
    void bg_to_surface(uint8_t* data);

    /// Main title screen task (0x005035B0)
    /// Handles title screen display, logo, and demo mode transitions
    // 0x005035B0
    void title();

    /// Initialises a new game or loads a save before entering game_loop (0x00506750)
    // 0x00506750
    void title_game_init();

    void title_init_hooks();
}
