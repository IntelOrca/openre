#include "interop.hpp"
#include "openre.h"
#include "re2.h"

namespace openre::gfx_draw
{
    // Scratch primitive buffer. Base 0x674E68, grows upward through the
    // shared MARNI_PRIM region and is bounded by off_524E20.
    constexpr uint32_t SCRATCH_BASE = 0x674E68;

    // 0x004416F0
    static void set_geom_offset(int cx, int cy)
    {
        interop::call<void, int, int>(0x004416F0, cx, cy);
    }

    // 0x00440250
    void reset_geom()
    {
        // Reset the primitive counter for the current frame.
        gGameTable.dword_67C9CC = 0;
        // Reset the scratch write pointer to the base of the scratch region.
        gGameTable.off_524E1C = SCRATCH_BASE;
        set_geom_offset(0, 0);
    }
}
