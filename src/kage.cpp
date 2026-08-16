#include "kage.h"

namespace openre
{
    // 0x004B34A0
    // Sets the shadow polygon colours (Prim[0].r0 / Prim[1].r0) preserving the
    // alpha channel already stored in the upper byte.
    void kage_work_color_set(Kage* pKage, int32_t col)
    {
        pKage->prim0_rgb = col | (pKage->prim0_rgb & 0xFF000000);
        pKage->prim1_rgb = col | (pKage->prim1_rgb & 0xFF000000);
    }
}
