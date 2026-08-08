#include "gfx_draw.h"
#include "marni.h"
#include "openre.h"
#include "re2.h"

namespace openre::gfx_draw
{
    namespace
    {
        // Returns the current scratch write pointer.
        static MarniPrim* scratch_ptr()
        {
            return (MarniPrim*)(uintptr_t)gGameTable.off_524E1C;
        }

        // Returns the end cap of the scratch region.
        static MarniPrim* scratch_end()
        {
            return (MarniPrim*)(uintptr_t)gGameTable.off_524E20;
        }
    }

    // 0x00441370
    int add_line_f2(LineF2* p, int z, int is_back)
    {
        // The line is written into the shared scratch buffer as a flat
        // 2-vertex line primitive (20 bytes, see PrimLine in re2.h).
        PrimLine* prim = (PrimLine*)scratch_ptr();

        if (p->x0 < 0)
            return 0;
        if ((uintptr_t)(prim + 1) > (uintptr_t)scratch_end())
            return 0;

        prim->type = 17;
        if ((p->code & 2) != 0 && p->tag == 1)
            prim->type = 0x200011;
        prim->x0 = p->x0;
        prim->y0 = p->y0;
        prim->x1 = p->x1;
        prim->y1 = p->y1;
        prim->color0 = (uint32_t)((p->r0 << 16) | (p->g0 << 8) | p->b0);

        if (is_back)
            marni::add_primitive_back(gGameTable.pMarni, (Prim*)prim, z);
        else
            marni::add_primitive_front(gGameTable.pMarni, (Prim*)prim, z);

        ++gGameTable.dword_67C9CC;
        gGameTable.off_524E1C = (uint32_t)(uintptr_t)(prim + 1);
        return 1;
    }
}
