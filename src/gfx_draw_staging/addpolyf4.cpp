#include "gfx_draw.h"
#include "marni.h"
#include "re2.h"

namespace openre::gfx_draw
{
    // 0x00441170
    // AddPolyF4 - adds a flat (untextured) quad primitive into the scratch
    // buffer and hands it to MARNI (front or back OT insertion).
    int add_poly_f4(Tile* p, int z, int is_back)
    {
        MarniPrim* prim = scratch_ptr();
        if ((int16_t*)&prim->x1 > (int16_t*)scratch_end())
            return 0;

        prim->type = 33;
        // x0/y0 packed into the texture field (PSX-style POLY_F4).
        prim->texture = ((uint32_t)(uint16_t)p->y0 << 16) | (uint16_t)p->x0;
        // x1/y1 come from the word following the tile (r|g and b|code of the
        // next Tile at +0x14/+0x16), each decremented by one.
        *(uint16_t*)&prim->var_0C = *(uint16_t*)&p[1].r - 1;
        *((uint16_t*)&prim->var_0C + 1) = *(uint16_t*)&p[1].b - 1;
        // Colour: b | (g << 8) | (r << 16).
        *(uint32_t*)&prim->x0 = (uint8_t)p->b | ((uint8_t)p->g << 8) | ((uint8_t)p->r << 16);

        if ((p->code & 2) != 0)
        {
            prim->type = s_type_mod_524E5C[p->tag & 3] | 0x21;
            if ((p->tag & 2) != 0)
            {
                int r = (uint8_t)p->r;
                if (r == 255)
                {
                    prim->type = 0x21;
                    *(uint32_t*)&prim->x0 = 0;
                }
                else
                {
                    *(uint32_t*)&prim->x0 = (uint32_t)r << 24;
                }
            }
        }

        if (is_back)
            marni::add_primitive_back(gGameTable.pMarni, (Prim*)prim, z);
        else
            marni::add_primitive_front(gGameTable.pMarni, (Prim*)prim, z);

        ++gGameTable.dword_67C9CC;
        gGameTable.off_524E1C = (uintptr_t)prim + 0x14;
        return 1;
    }
}
