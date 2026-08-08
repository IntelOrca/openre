#include "gfx_draw.h"
#include "interop.hpp"
#include "marni.h"
#include "openre.h"
#include "re2.h"

namespace openre::gfx_draw
{
    // 0x00441270
    int add_tile(Tile* p, int z, int is_back)
    {
        MarniPrim* prim = scratch_ptr();

        // Bounds check: the 0x14-byte record must fit before the end cap
        // (checked at &prim->x1, i.e. prim + 0x14).
        if (&prim->x1 > (int16_t*)scratch_end())
            return 0;

        prim->type = 0x21; // 33: flat untextured tile

        // Pack the top-left corner into the texture field (LOWORD/HIWORD).
        *(uint16_t*)&prim->texture = (uint16_t)p->x0;
        *((uint16_t*)&prim->texture + 1) = (uint16_t)p->y0;

        // Pack the bottom-right corner (x0+w-1, y0+h-1) into field_C.
        *(uint16_t*)&prim->var_0C = (uint16_t)(p->x0 + p->w - 1);
        *((uint16_t*)&prim->var_0C + 1) = (uint16_t)(p->y0 + p->h - 1);

        // Colour packed as b | (g << 8) | (r << 16) over the x0/y0/x1/y1 fields.
        *(uint32_t*)&prim->x0 = (uint32_t)p->b | ((uint32_t)p->g << 8) | ((uint32_t)p->r << 16);

        if (p->code & 2)
        {
            prim->type = (int32_t)(s_type_mod_524E5C[p->tag & 3] | 0x21);
            if (p->tag == 2)
            {
                int r = p->r;
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
        gGameTable.off_524E1C = (uint32_t)((char*)prim + 0x14);

        return 1;
    }
}
