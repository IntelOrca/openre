#include "marni.h"
#include "openre.h"
#include "re2.h"

namespace openre::gfx_draw
{
    // Mirrors the shared infrastructure in gfx_draw.cpp: the scratch
    // primitive written into the shared MARNI_PRIM buffer. Laid out
    // identically to PrimSprite (28 bytes) and followed by a per-type
    // colour tail.
    using MarniPrim = PrimSprite;

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

    // 0x00440480
    int add_sprt_v(int x, int y, int w, int h, int u, int v, unsigned int clut, int page, int depth, int is_back)
    {
        MarniPrim* prim = scratch_ptr();

        if (page >= 41
            || gGameTable.texture_pages[page].handle == 0
            || gGameTable.texture_pages[page].var_08 == 1
            || prim + 1 > scratch_end()
            || w <= 0
            || h <= 0
            || w + u - 1 > 255
            || h + v - 1 > 255
            || u < 0
            || v < 0)
        {
            return 0;
        }

        if (clut >= gGameTable.texture_pages[page].var_04)
            clut = 0;

        prim->u0 = (uint8_t)u;
        prim->v0 = (uint8_t)v;
        prim->x0 = (int16_t)x;
        prim->x1 = (int16_t)(w + x - 1);
        prim->type = 36;
        prim->y0 = (int16_t)y;
        prim->y1 = (int16_t)(h + y - 1);
        prim->u1 = (uint8_t)(u + w - 1);
        prim->v1 = (uint8_t)(v + h - 1);
        prim->texture = gGameTable.texture_pages[page].handle;
        prim->var_0C = clut;

        if (is_back)
            marni::add_primitive_back(gGameTable.pMarni, (Prim*)prim, depth);
        else
            marni::add_primitive_front(gGameTable.pMarni, (Prim*)prim, depth);

        ++gGameTable.dword_67C9CC;
        gGameTable.off_524E1C += sizeof(MarniPrim); // advance one MARNI_PRIM (0x1C)
        return 1;
    }
}
