#pragma once

#include <cstdint>

namespace openre::debug
{
    // Queues a formatted string for the top-most GDI text layer (drawn by
    // save_print_flush just before the frame is presented). No-op when the
    // overlay is disabled.
    void print(int x, int y, uint32_t color, const char* fmt, ...);

    // Renders the debug overlay (FPS counter and game stats panel). Called each
    // frame right before save_print_flush so the text is drawn on top of
    // everything else.
    void draw();

    // Toggles the overlay on/off.
    void toggle();

    // Whether the overlay is currently shown.
    bool enabled();
}
