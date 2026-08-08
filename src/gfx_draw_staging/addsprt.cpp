#include "gfx_draw.h"
#include "marni.h"
#include "openre.h"
#include "re2.h"

namespace openre::gfx_draw
{
    // Type modifier table (immutable data from the original binary). Mirrors
    // the shared table in gfx_draw.cpp; ORed into the primitive type to select
    // the MARNI prim variant.
    constexpr uint32_t s_type_mod_524E24[8] = {
        0x100000, 0x200000, 0x100000, 0x100000,
        0x400000, 0x100000, 0x100000, 0x100000,
    };

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

    // 0x00440280
    int add_sprt(Sprt* p, uint32_t page, int z, int add_back)
    {
        MarniPrim* prim = scratch_ptr();

        if (page >= 0x29)
            return 0;
        if (gGameTable.texture_pages[page].handle == 0)
            return 0;
        if (gGameTable.texture_pages[page].var_08 == 1)
            return 0;
        if ((uintptr_t)((uint8_t*)prim + 0x20) > (uintptr_t)scratch_end())
            return 0;
        if (p->clut >= gGameTable.texture_pages[page].var_04)
            p->clut = 0;

        if (p->r == 0x80 && p->g == 0x80 && p->b == 0x80)
        {
            prim->type = 36;
            if ((p->code & 2) != 0)
                prim->type = s_type_mod_524E24[p->tag & 3] | 0x24;
            prim->x0 = p->x0;
            prim->y0 = p->y0;
            prim->x1 = p->x0 + p->w - 1;
            prim->y1 = p->y0 + p->h - 1;
            prim->u0 = p->u0;
            prim->v0 = p->v0;
            prim->u1 = (uint8_t)(p->u0 + (uint8_t)p->w - 1);
            prim->v1 = (uint8_t)(p->v0 + (uint8_t)p->h - 1);
            prim->texture = gGameTable.texture_pages[page].handle;
            prim->var_0C = p->clut;
        }
        else
        {
            prim->type = 37;
            prim[1].pNext = (Prim*)(uintptr_t)((p->r << 16) | (p->g << 8) | p->b);
            if ((p->code & 2) != 0)
                prim->type = s_type_mod_524E24[p->tag & 3] | 0x25;
            prim->x0 = p->x0;
            prim->y0 = p->y0;
            prim->x1 = p->x0 + p->w - 1;
            prim->y1 = p->y0 + p->h - 1;
            prim->u0 = p->u0;
            prim->v0 = p->v0;
            prim->u1 = (uint8_t)(p->u0 + (uint8_t)p->w - 1);
            prim->v1 = (uint8_t)(p->v0 + (uint8_t)p->h - 1);
            prim->texture = gGameTable.texture_pages[page].handle;
            prim->var_0C = p->clut;
        }

        if (add_back)
            marni::add_primitive_back(gGameTable.pMarni, (Prim*)prim, z);
        else
            marni::add_primitive_front(gGameTable.pMarni, (Prim*)prim, z);

        ++gGameTable.dword_67C9CC;
        gGameTable.off_524E1C = (uint32_t)(uintptr_t)((uint8_t*)prim + 0x20);
        return 1;
    }
}
