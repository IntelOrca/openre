#include "gfx_draw.h"
#include "interop.hpp"
#include "marni.h"
#include "openre.h"
#include "re2.h"

namespace openre::gfx_draw
{
    // Scratch primitive buffer. Base 0x674E68, grows upward through the
    // shared MARNI_PRIM region and is bounded by off_524E20.
    constexpr uint32_t SCRATCH_BASE = 0x674E68;

    // Type modifier tables (immutable data from the original binary).
    // These are ORed into the primitive type to select the MARNI prim
    // variant (see marni.cpp draw-op dispatch).
    constexpr uint32_t s_type_mod_524E24[8] = {
        0x100000, 0x200000, 0x100000, 0x100000,
        0x400000, 0x100000, 0x100000, 0x100000,
    };
    constexpr uint32_t s_type_mod_524E44[4] = {
        0x100000, 0x200000, 0x100000, 0x300000,
    };
    constexpr uint32_t s_type_mod_524E5C[4] = {
        0x100000, 0x200000, 0x400000, 0x300000,
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

        // The texture page descriptor array used by the Add* functions.
        static const auto& texture_pages()
        {
            return gGameTable.texture_pages;
        }
    }

    // 0x004416F0
    static void set_geom_offset(int cx, int cy)
    {
        interop::call<void, int, int>(0x004416F0, cx, cy);
    }

    // 0x00440250
    void reset_geom()
    {
        // Reset the primitive counter for the current frame.
        gGameTable.dword_67C9CC = 0;
        // Reset the scratch write pointer to the base of the scratch region.
        gGameTable.off_524E1C = SCRATCH_BASE;
        set_geom_offset(0, 0);
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

    // 0x00440600
    void add_poly_ft4(PolyFt4* p, int page, int z, int add_back)
    {
        // TODO(decompiler): implement
    }

    // 0x004407F0
    int add_mask(Sprt* p, int page, int z)
    {
        // TODO(decompiler): implement
        return 0;
    }

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

    // 0x00440A20
    int sub_440A20(int prim, int page, int z)
    {
        // TODO(decompiler): implement
        return 0;
    }

    // 0x00440B70
    int sub_440B70(int prim, int page, int z)
    {
        // TODO(decompiler): implement
        return 0;
    }

    // 0x00440DD0
    int add_poly_gt4(PolyGt4* p, int page, int z)
    {
        // TODO(decompiler): implement
        return 0;
    }

    // 0x00440FF0
    int add_poly_ft4_2(PolyFt4* p, int page, int z)
    {
        // TODO(decompiler): implement
        return 0;
    }

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

    // 0x00441370
    int add_line_f2(LineF2* p, int z, int is_back)
    {
        // The line is written into the shared scratch buffer as a flat
        // 2-vertex line primitive (20 bytes, see PrimLine in re2.h).
        PrimLine* prim = (PrimLine*)scratch_ptr();

        if (p->x0 < 0)
            return 0;
        if ((uintptr_t)(prim + 1) > (uintptr_t)scratch_end())
            return 0;

        prim->type = 17;
        if ((p->code & 2) != 0 && p->tag == 1)
            prim->type = 0x200011;
        prim->x0 = p->x0;
        prim->y0 = p->y0;
        prim->x1 = p->x1;
        prim->y1 = p->y1;
        prim->color0 = (uint32_t)((p->r0 << 16) | (p->g0 << 8) | p->b0);

        if (is_back)
            marni::add_primitive_back(gGameTable.pMarni, (Prim*)prim, z);
        else
            marni::add_primitive_front(gGameTable.pMarni, (Prim*)prim, z);

        ++gGameTable.dword_67C9CC;
        gGameTable.off_524E1C = (uint32_t)(uintptr_t)(prim + 1);
        return 1;
    }

    void init_hooks()
    {
        interop::writeJmp(0x00440250, &reset_geom);
        interop::writeJmp(0x00441170, &add_poly_f4);
        interop::writeJmp(0x00440950, &add_bg_scaled);
        interop::writeJmp(0x00440280, &add_sprt);
        interop::writeJmp(0x00441270, &add_tile);
        interop::writeJmp(0x00440480, &add_sprt_v);
        interop::writeJmp(0x00441370, &add_line_f2);
    }
}
