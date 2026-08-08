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
        // TODO(decompiler): implement
        return 0;
    }

    // 0x00440480
    int add_sprt_v(int x, int y, int w, int h, int u, int v, uint16_t clut, uint32_t page, int z, int is_back)
    {
        // TODO(decompiler): implement
        return 0;
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

    // 0x00440950
    int sub_440950(int bg, int z)
    {
        // TODO(decompiler): implement
        return 0;
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
        // TODO(decompiler): implement
        return 0;
    }

    // 0x00441370
    int add_line_f2(LineF2* p, int z, int is_back)
    {
        // TODO(decompiler): implement
        return 0;
    }

    void init_hooks()
    {
        interop::writeJmp(0x00440250, &reset_geom);
        interop::writeJmp(0x00441170, &add_poly_f4);
    }
}
