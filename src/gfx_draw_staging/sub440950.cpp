#include "gfx_draw.h"
#include "marni.h"
#include "re2.h"

namespace openre::gfx_draw
{
    // Full-screen background (bg0) texture handle. Not present in GameTable,
    // so it is exposed here as a direct reference to the fixed address.
    static uint32_t& th_bg0 = *reinterpret_cast<uint32_t*>(0x0067C728);

    // 0x00440950
    // AddBgScaled - draws a full-screen scaled background quad (bg0) using the
    // shared scratch buffer. `bg` points at a descriptor whose interesting
    // fields are x@+8 (i16), y@+0A (i16), u@+0C (u8), v@+0D (u8), w@+10 (i16),
    // h@+12 (i16). `z` is the draw depth; it is stored as a float scale and is
    // also right-shifted by 4 for the OT z-ordering.
    int add_bg_scaled(int bg, int z)
    {
        MarniPrim* prim = scratch_ptr();

        if (th_bg0 == 0)
            return 0;

        // Bounds check: the 0x20-byte record must fit before the end cap
        // (checked at &prim[1].type, i.e. prim + 0x20).
        if (&prim[1].type > (int32_t*)scratch_end())
            return 0;

        prim->type = 0x1002C; // 65580: scaled full-screen background sprite

        prim->x1 = *(int16_t*)(bg + 8);
        prim->y1 = *(int16_t*)(bg + 0x0A);

        // Pack (x + w - 1) into the u0/v0 word and (y + h - 1) into the
        // u1/v1 word (16-bit arithmetic, matching the binary).
        *(int16_t*)&prim->u0 = *(int16_t*)(bg + 8) + *(int16_t*)(bg + 16) - 1;
        *(int16_t*)&prim->u1 = *(int16_t*)(bg + 0x0A) + *(int16_t*)(bg + 18) - 1;

        // The remaining four bytes past the MarniPrim record (offset 0x1C..0x1F)
        // hold the source u/v quad (u, v, u + w - 1, v + h - 1) as bytes.
        uint8_t* tail = (uint8_t*)prim + 0x1C;
        tail[0] = *(uint8_t*)(bg + 12);
        tail[1] = *(uint8_t*)(bg + 13);
        tail[2] = *(uint8_t*)(bg + 12) + *(uint8_t*)(bg + 16) - 1;
        tail[3] = *(uint8_t*)(bg + 13) + *(uint8_t*)(bg + 18) - 1;

        // Store the z depth as a float scale over the x0/y0 fields.
        *(float*)&prim->x0 = (float)z;
        prim->texture = th_bg0;

        marni::add_primitive_scaler(gGameTable.pMarni, (Prim*)prim, z >> 4);

        ++gGameTable.dword_67C9CC;
        gGameTable.off_524E1C = (uintptr_t)prim + 0x20;

        return 1;
    }
}
