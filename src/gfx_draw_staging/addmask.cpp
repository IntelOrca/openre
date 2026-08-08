#include "gfx_draw.h"
#include "marni.h"
#include "openre.h"
#include "re2.h"

namespace openre::gfx_draw
{
    // 0x004407F0
    // AddMask - adds a textured sprite into the scratch buffer and hands it
    // to MARNI's scaler OT insertion. This is the stencil/mask sprite path.
    int add_mask(Sprt* p, int page, int z)
    {
        MarniPrim* prim = scratch_ptr();

        // Bounds checks: page must be valid and its texture loaded.
        if (page >= 41)
            return 0;
        if (texture_pages()[page].handle == 0)
            return 0;
        if (texture_pages()[page].var_08 == 1)
            return 0;

        // Bounds check: the 0x24-byte record must fit before the end cap
        // (checked at prim + 0x24).
        if ((uint8_t*)prim + 0x24 > (uint8_t*)scratch_end())
            return 0;

        // Clamp the CLUT against the palette count for this page.
        if (p->clut >= texture_pages()[page].var_04)
            p->clut = 0;

        // Textured sprite primitive (PSX SPRT).
        prim->type = 0x1002C; // 65580

        if (p->code & 2)
        {
            prim->type = 0x1002D; // 65581
            prim->type = (int32_t)(s_type_mod_524E24[p->tag & 3] | 0x1002D);
            // Colour packed as b | (g << 8) | (r << 16) into the tail dword.
            *(uint32_t*)((uint8_t*)prim + 0x20) = (uint32_t)p->b | ((uint32_t)p->g << 8) | ((uint32_t)p->r << 16);
        }

        prim->x1 = p->x0;
        prim->y1 = p->y0;
        // Bottom-right corner packed into the u0/v0 and u1/v1 words.
        *(uint16_t*)&prim->u0 = (uint16_t)(p->x0 + p->w - 1);
        *(uint16_t*)&prim->u1 = (uint16_t)(p->y0 + p->h - 1);

        // Second UV pair in the tail bytes at 0x1C..0x1F.
        *(uint8_t*)((uint8_t*)prim + 0x1C) = p->u0;
        *(uint8_t*)((uint8_t*)prim + 0x1D) = p->v0;
        *(uint8_t*)((uint8_t*)prim + 0x1E) = (uint8_t)(p->u0 + (uint8_t)p->w - 1);
        *(uint8_t*)((uint8_t*)prim + 0x1F) = (uint8_t)(p->v0 + (uint8_t)p->h - 1);

        prim->texture = texture_pages()[page].handle;
        prim->var_0C = p->clut;

        // The z value is clamped to the projection plane before being stored.
        if (z >= gGameTable.global_prj / 2)
            *(float*)&prim->x0 = (float)z;
        else
            *(float*)&prim->x0 = (float)(gGameTable.global_prj / 2);

        marni::add_primitive_scaler(gGameTable.pMarni, (Prim*)prim, z >> 4);

        ++gGameTable.dword_67C9CC;
        gGameTable.off_524E1C = (uintptr_t)prim + 0x24;

        return 1;
    }
}
