#include "gfx_draw.h"
#include "marni.h"
#include "openre.h"
#include "re2.h"

namespace openre::gfx_draw
{
    // 0x00440A20
    // AddScaledSprite - draws a scaled sprite into the shared scratch buffer
    // via AddPrimitiveScaler. The descriptor is a POLY_FT4-shaped record: the
    // sprite origin is (x0, y0), the source u/v quad runs from (u0, v0) to
    // (u3, v3), and the texture region size is taken from the (x3, y3) words
    // (each reduced by one). `z` is stored as a float scale over x0/y0 and is
    // also right-shifted by 4 for the OT z-ordering.
    int add_scaled_sprite(int prim, int page, int z)
    {
        PolyFt4* p = (PolyFt4*)prim;
        MarniPrim* out = scratch_ptr();
        uint8_t* tail = (uint8_t*)out + 0x1C;

        if (page >= 41)
            return 0;
        if (gGameTable.texture_pages[page].handle == 0)
            return 0;
        if (gGameTable.texture_pages[page].var_08 == 1)
            return 0;
        // The 0x24-byte scratch record must fit before the end cap
        // (checked at &out[1].texture, i.e. out + 0x24).
        if ((uintptr_t)out + 0x24 > (uintptr_t)scratch_end())
            return 0;

        // Clamp the CLUT to 0 if it is past the texture page's CLUT table.
        if ((uint32_t)(uint16_t)p->clut >= gGameTable.texture_pages[page].var_04)
            p->clut = 0;

        if (p->r0 == 0x80 && p->g0 == 0x80 && p->b0 == 0x80)
        {
            // White (unshaded) sprite: 0x1002C (SPRT-scaler) primitive.
            out->type = 0x1002C;
        }
        else
        {
            // Flat-shaded sprite: 0x1002D (SPRT-scaler) primitive with the
            // packed RGB colour written into the colour dword at scratch + 0x20
            // (the type field of the tail MarniPrim record).
            out->type = 0x1002D;
            *(uint32_t*)&tail[4] = (uint32_t)((p->r0 << 16) | (p->g0 << 8) | p->b0);
        }

        if ((p->code & 2) != 0)
        {
            out->type |= s_type_mod_524E24[((uint8_t)p->tpage >> 5) & 3];
        }

        out->x1 = p->x0;
        out->y1 = p->y0;

        // Texture region size (16-bit words) minus one, packed into the u0/v0
        // and u1/v1 words.
        *(uint16_t*)&out->u0 = (uint16_t)(p->x3 - 1);
        *(uint16_t*)&out->u1 = (uint16_t)(p->y3 - 1);

        // The four bytes past the MarniPrim record (offset 0x1C..0x1F) hold
        // the source u/v quad (u0, v0, u3 - 1, v3 - 1) as bytes.
        tail[0] = p->u0;
        tail[1] = p->v0;
        tail[2] = (uint8_t)(p->u3 - 1);
        tail[3] = (uint8_t)(p->v3 - 1);

        out->texture = gGameTable.texture_pages[page].handle;
        uint16_t clut = (uint16_t)p->clut;

        // Store the z depth as a float scale over the x0/y0 fields.
        *(float*)&out->x0 = (float)z;
        out->var_0C = clut;

        marni::add_primitive_scaler(gGameTable.pMarni, (Prim*)out, z >> 4);

        ++gGameTable.dword_67C9CC;
        gGameTable.off_524E1C = (uint32_t)((uintptr_t)out + 0x24);

        return 1;
    }
}
