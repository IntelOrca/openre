#include "gfx_draw.h"
#include "marni.h"
#include "openre.h"
#include "re2.h"

namespace openre::gfx_draw
{
    // 0x00440FF0
    // AddPolyFT4_2 - adds a textured quad into the scratch buffer and hands it
    // to MARNI via the front OT only. Unlike AddPolyFT4, this variant always
    // emits primitive type 69 (with the semi-transparency modifier from
    // s_type_mod_524E44 when the CODE bit 1 flag is set) and has no back-OT
    // variant. The UV coordinates are packed into the second prim's type and
    // texture fields.
    int add_poly_ft4_2(PolyFt4* p, int page, int z)
    {
        MarniPrim* prim = scratch_ptr();
        if (page >= 41)
            return 0;
        if (gGameTable.texture_pages[page].handle == 0)
            return 0;
        if (gGameTable.texture_pages[page].var_08 == 1)
            return 0;
        if (&prim[1].x1 > (int16_t*)scratch_end())
            return 0;

        // The texture page's CLUT size clamps the primitive's CLUT index.
        if ((uint16_t)p->clut >= (uint32_t)gGameTable.texture_pages[page].var_04)
            p->clut = 0;

        prim->type = 69;
        if (p->r0 > 0x80)
            p->r0 = 0x80;
        if (p->g0 > 0x80)
            p->g0 = 0x80;
        if (p->b0 > 0x80)
            p->b0 = 0x80;
        // Colour packed into the second prim's var_0C: r | (g << 8) | (b << 16).
        prim[1].var_0C = ((uint32_t)p->r0 << 16) | ((uint32_t)p->g0 << 8) | p->b0;
        if ((p->code & 2) != 0)
            prim->type |= s_type_mod_524E44[p->tag & 3];

        // Geometry: x1/y1 pair and x2/y2 (packed over the u0/v0 and u1/v1
        // byte slots of the first prim) plus the x3/y3 pair (packed into the
        // second prim's pNext word slots).
        prim->x0 = p->x0;
        prim->x1 = p->x1;
        *(uint16_t*)&prim->u0 = p->x2;
        *(uint16_t*)&prim[1].pNext = p->x3;
        prim->y0 = p->y0;
        prim->y1 = p->y1;
        *(uint16_t*)&prim->u1 = p->y2;
        *((uint16_t*)&prim[1].pNext + 1) = p->y3;
        // UV coordinates are packed into the second prim's type/texture fields.
        prim[1].type = (uint32_t)p->u0 | ((uint32_t)p->v0 << 8) | ((uint32_t)p->u1 << 16) | ((uint32_t)p->v1 << 24);
        prim[1].texture = (uint32_t)p->u2 | ((uint32_t)p->v2 << 8) | ((uint32_t)p->u3 << 16) | ((uint32_t)p->v3 << 24);

        prim->texture = gGameTable.texture_pages[page].handle;
        prim->var_0C = (uint16_t)p->clut;

        marni::add_primitive_front(gGameTable.pMarni, (Prim*)prim, z);

        ++gGameTable.dword_67C9CC;
        gGameTable.off_524E1C = (uintptr_t)prim + 0x2C;
        return 1;
    }
}
