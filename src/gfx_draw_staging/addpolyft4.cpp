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

    // 0x00440600
    void add_poly_ft4(PolyFt4* p, int page, int z, int add_back)
    {
        MarniPrim* v4 = scratch_ptr();
        MarniPrim* v5 = scratch_ptr();

        if (page < 41
            && gGameTable.texture_pages[page].handle != 0
            && gGameTable.texture_pages[page].var_08 != 1
            && (uintptr_t)scratch_ptr() + 0x20 <= (uintptr_t)scratch_end())
        {
            // Clamp the CLUT to 0 if it is past the texture page's CLUT table.
            if ((uint32_t)(uint16_t)p->clut >= gGameTable.texture_pages[page].var_04)
                p->clut = 0;

            if (p->r0 == 0x80 && p->g0 == 0x80 && p->b0 == 0x80)
            {
                // White quad: no per-vertex colour, 0x24 (POLY_FT4) primitive.
                v4->type = 36;
                if ((p->code & 2) != 0)
                    v4->type = (int32_t)(s_type_mod_524E24[p->tag & 3] | 0x24);
                v4->x0 = p->x0;
                v4->y0 = p->y0;
                v4->x1 = p->x3;
                v4->y1 = p->y3;
                v4->u0 = p->u0;
                v4->v0 = p->v0;
                v4->u1 = p->u3;
                v4->v1 = p->v3;
                v4->texture = gGameTable.texture_pages[page].handle;
                v4->var_0C = (uint16_t)p->clut;
            }
            else
            {
                // Flat-shaded quad: 0x25 (POLY_GT4) primitive with the packed
                // colour written into the colour tail at scratch + 0x1C.
                v5->type = 37;
                uint16_t v6 = (uint16_t)(p->g0 | (p->r0 << 8));
                v5[1].pNext = (Prim*)(uintptr_t)((uint8_t)p->b0 | (v6 << 8));
                if ((p->code & 2) != 0)
                {
                    v5->type = (int32_t)(s_type_mod_524E24[p->tag & 7] | 0x25);
                    if ((p->tag & 7) == 4)
                    {
                        int v7 = 2 * (uint8_t)p->r0 - 1;
                        if (v7 < 0)
                            v7 = 0;
                        v5[1].pNext = (Prim*)(uintptr_t)(((uint32_t)v7 << 24) | 0x808080);
                    }
                }
                v5->x0 = p->x0;
                v5->y0 = p->y0;
                v5->x1 = p->x3;
                v5->y1 = p->y3;
                v5->u0 = p->u0;
                v5->v0 = p->v0;
                v5->u1 = p->u3;
                v5->v1 = p->v3;
                v5->texture = gGameTable.texture_pages[page].handle;
                v5->var_0C = (uint16_t)p->clut;
            }

            if (add_back)
                marni::add_primitive_back(gGameTable.pMarni, (Prim*)scratch_ptr(), z);
            else
                marni::add_primitive_front(gGameTable.pMarni, (Prim*)scratch_ptr(), z);

            ++gGameTable.dword_67C9CC;
            gGameTable.off_524E1C = (uint32_t)((uintptr_t)scratch_ptr() + 0x20);
        }
    }
}
