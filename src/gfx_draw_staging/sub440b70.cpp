#include "gfx_draw.h"
#include "marni.h"
#include "openre.h"
#include "re2.h"

namespace openre::gfx_draw
{
    // 0x00440B70
    // AddPrimitiveScaler path for a POLY_FT4 quad. The four vertex x/y
    // positions are packed into the MarniPrim head (x0,y0 = v0; x1,y1 = v1;
    // x2/y2 written over the u0/v0 and u1/v1 words) and the remaining vertex
    // data (x3,y3 + the per-vertex u/v) plus the z depth are packed into the
    // 0x1C-byte tail that follows the MarniPrim record.
    int sub_440B70(int prim, int page, int z)
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
        // The 0x30-byte scratch record must fit before the end cap.
        if ((uintptr_t)out + 0x30 > (uintptr_t)scratch_end())
            return 0;

        // Clamp the CLUT to 0 if it is past the texture page's CLUT table.
        if ((uint32_t)(uint16_t)p->clut >= gGameTable.texture_pages[page].var_04)
            p->clut = 0;

        if (z >> 4 > 4095)
            return 0;

        if (p->r0 == 0x80 && p->g0 == 0x80 && p->b0 == 0x80)
        {
            // White (unshaded) quad: 0x1004C (POLY_FT4-scaler) primitive.
            out->type = 0x1004C;
            if ((p->code & 2) != 0)
                out->type = (int32_t)(s_type_mod_524E44[((uint8_t)p->tpage >> 5) & 3] | 0x1004C);

            out->x0 = p->x0;
            out->y0 = p->y0;
            out->x1 = p->x1;
            out->y1 = p->y1;
            *(int16_t*)&out->u0 = p->x2;
            *(int16_t*)&out->u1 = p->y2;
            *(int16_t*)&tail[0] = p->x3;         // LOWORD(out[1].pNext)
            *(int16_t*)&tail[2] = p->y3;         // HIWORD(out[1].pNext)
            *(uint16_t*)&tail[4] = (uint16_t)z;  // LOWORD(out[1].type)
            tail[6] = p->u0;                     // BYTE2(out[1].type)
            tail[7] = p->v0;                     // HIBYTE(out[1].type)
            tail[8] = p->u1;                     // LOBYTE(out[1].pTexture)
            tail[9] = p->v1;                     // BYTE1(out[1].pTexture)
            tail[10] = p->u2;                    // BYTE2(out[1].pTexture)
            tail[11] = p->v2;                    // HIBYTE(out[1].pTexture)
            tail[12] = p->u3;                    // LOBYTE(out[1].field_C)
            tail[13] = p->v3;                    // BYTE1(out[1].field_C)
            out->texture = gGameTable.texture_pages[page].handle;
            out->var_0C = (uint16_t)p->clut;
        }
        else
        {
            // Flat-shaded quad: 0x1004D (POLY_GT4-scaler) primitive with the
            // packed RGB colour written into the colour tail at scratch + 0x2C.
            out->type = 0x1004D;
            *(uint32_t*)&tail[0x10] = (uint32_t)((p->r0 << 16) | (p->g0 << 8) | p->b0);
            if ((p->code & 2) != 0)
                out->type = (int32_t)(s_type_mod_524E44[((uint8_t)p->tpage >> 5) & 3] | 0x1004D);

            out->x0 = p->x0;
            out->y0 = p->y0;
            out->x1 = p->x1;
            out->y1 = p->y1;
            *(int16_t*)&out->u0 = p->x2;
            *(int16_t*)&out->u1 = p->y2;
            *(int16_t*)&tail[0] = p->x3;         // LOWORD(out[1].pNext)
            *(int16_t*)&tail[2] = p->y3;         // HIWORD(out[1].pNext)
            *(uint16_t*)&tail[4] = (uint16_t)z;  // LOWORD(out[1].type)
            tail[6] = p->u0;                     // BYTE2(out[1].type)
            tail[7] = p->v0;                     // HIBYTE(out[1].type)
            tail[8] = p->u1;                     // LOBYTE(out[1].pTexture)
            tail[9] = p->v1;                     // BYTE1(out[1].pTexture)
            tail[10] = p->u2;                    // BYTE2(out[1].pTexture)
            tail[11] = p->v2;                    // HIBYTE(out[1].pTexture)
            tail[12] = p->u3;                    // LOBYTE(out[1].field_C)
            tail[13] = p->v3;                    // BYTE1(out[1].field_C)
            out->texture = gGameTable.texture_pages[page].handle;
            out->var_0C = (uint16_t)p->clut;
        }

        marni::add_primitive_scaler(gGameTable.pMarni, (Prim*)out, z >> 4);

        ++gGameTable.dword_67C9CC;
        gGameTable.off_524E1C = (uint32_t)((uintptr_t)out + 0x30);

        return 1;
    }
}
