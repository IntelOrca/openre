#include "gfx_draw.h"
#include "marni.h"
#include "openre.h"
#include "re2.h"

namespace openre::gfx_draw
{
    // Type modifier table (immutable data from the original binary). Mirrors
    // the shared table in gfx_draw.cpp; ORed into the primitive type to select
    // the MARNI prim variant.
    constexpr uint32_t s_type_mod_524E44[4] = {
        0x100000, 0x200000, 0x100000, 0x300000,
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

    // 0x00440DD0
    // AddPolyGT4 - adds a gouraud-shaded textured quad into the scratch
    // buffer and hands it to MARNI. The 0x34-byte POLY_GT4 becomes a 0x38-byte
    // MarniPrim record: the PrimSprite header (0x1C) followed by the four
    // per-vertex colours (0x28/0x2C/0x30/0x34), one dword per vertex.
    int add_poly_gt4(PolyGt4* p, int page, int z)
    {
        MarniPrim* prim = scratch_ptr();

        if (page >= 41)
            return 0;
        if (gGameTable.texture_pages[page].handle == 0)
            return 0;
        if (gGameTable.texture_pages[page].var_08 == 1)
            return 0;
        // The 0x38-byte record must fit before the end cap.
        if ((uintptr_t)((uint8_t*)prim + 0x38) > (uintptr_t)scratch_end())
            return 0;
        if (p->clut >= gGameTable.texture_pages[page].var_04)
            p->clut = 0;
        if ((int)(z & 0xFFFFFFF0) > 65520)
            return 0;

        prim->type = 70; // 0x46: gouraud textured quad (GT4)

        // Clamp each colour channel to 0x80 (MARNI stores half-intensity
        // colours and scales them by two during shading).
        if (p->r0 > 0x80)
            p->r0 = 0x80;
        if (p->r1 > 0x80)
            p->r1 = 0x80;
        if (p->r2 > 0x80)
            p->r2 = 0x80;
        if (p->r3 > 0x80)
            p->r3 = 0x80;
        if (p->g0 > 0x80)
            p->g0 = 0x80;
        if (p->g1 > 0x80)
            p->g1 = 0x80;
        if (p->g2 > 0x80)
            p->g2 = 0x80;
        if (p->g3 > 0x80)
            p->g3 = 0x80;
        if (p->b0 > 0x80)
            p->b0 = 0x80;
        if (p->b1 > 0x80)
            p->b1 = 0x80;
        if (p->b2 > 0x80)
            p->b2 = 0x80;
        if (p->b3 > 0x80)
            p->b3 = 0x80;

        // Pack the four vertex colours into the colour tail as
        // b | (g << 8) | (r << 16), one dword per vertex.
        *(uint32_t*)&prim[1].var_0C = (uint32_t)p->b0 | ((uint32_t)p->g0 << 8) | ((uint32_t)p->r0 << 16);
        *(uint32_t*)&prim[1].x0 = (uint32_t)p->b1 | ((uint32_t)p->g1 << 8) | ((uint32_t)p->r1 << 16);
        *(uint32_t*)&prim[1].x1 = (uint32_t)p->b2 | ((uint32_t)p->g2 << 8) | ((uint32_t)p->r2 << 16);
        *(uint32_t*)&prim[1].u0 = (uint32_t)p->b3 | ((uint32_t)p->g3 << 8) | ((uint32_t)p->r3 << 16);

        if ((p->code & 2) != 0)
            prim->type |= (int32_t)s_type_mod_524E44[p->tag & 3];

        // Vertex coordinates; the second and third vertices (x2/y2 and
        // x3/y3) are packed into the otherwise-unused u/v bytes and the
        // tail's pNext field.
        prim->x0 = p->x0;
        prim->y0 = p->y0;
        prim->x1 = p->x1;
        prim->y1 = p->y1;
        *(uint16_t*)&prim->u0 = (uint16_t)p->x2;
        *(uint16_t*)&prim->u1 = (uint16_t)p->y2;
        *(uint16_t*)&prim[1].pNext = (uint16_t)p->x3;
        *((uint16_t*)&prim[1].pNext + 1) = (uint16_t)p->y3;

        // Texture coordinates, two (u,v) pairs packed into the tail header.
        prim[1].type = p->u0 | ((int32_t)p->v0 << 8) | ((int32_t)p->u1 << 16) | ((int32_t)p->v1 << 24);
        prim[1].texture = p->u2 | ((int32_t)p->v2 << 8) | ((int32_t)p->u3 << 16) | ((int32_t)p->v3 << 24);

        prim->texture = gGameTable.texture_pages[page].handle;
        prim->var_0C = p->clut;

        marni::add_primitive_front(gGameTable.pMarni, (Prim*)prim, z);

        ++gGameTable.dword_67C9CC;
        gGameTable.off_524E1C = (uint32_t)((uint8_t*)prim + 0x38);
        return 1;
    }
}
