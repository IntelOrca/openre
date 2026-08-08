#pragma once

#include "re2.h"

#include <cstdint>

namespace openre::gfx_draw
{
    // PSX-style primitive structures passed by the game code to the Add*
    // family. These mirror the original RE2 (PSX libgpu) prim structures used
    // by the binary. Layouts verified against IDA (SPRT = 0x14 bytes, TILE =
    // 0x10, POLY_FT4 = 0x28, POLY_GT4 = 0x34, LINE_F2 = 0x10).

    struct Sprt
    {
        uint32_t tag;    // 0x00
        uint8_t r;       // 0x04
        uint8_t g;       // 0x05
        uint8_t b;       // 0x06
        uint8_t code;    // 0x07
        int16_t x0;      // 0x08
        int16_t y0;      // 0x0A
        uint8_t u0;      // 0x0C
        uint8_t v0;      // 0x0D
        uint16_t clut;   // 0x0E
        uint16_t w;      // 0x10
        uint16_t h;      // 0x12
    };
    static_assert(sizeof(Sprt) == 0x14);

    struct Tile
    {
        uint32_t tag;    // 0x00
        uint8_t r;       // 0x04
        uint8_t g;       // 0x05
        uint8_t b;       // 0x06
        uint8_t code;    // 0x07
        int16_t x0;      // 0x08
        int16_t y0;      // 0x0A
        int16_t w;       // 0x0C
        int16_t h;       // 0x0E
    };
    static_assert(sizeof(Tile) == 0x10);

    struct PolyFt4
    {
        uint32_t tag;     // 0x00
        uint8_t r0;       // 0x04
        uint8_t g0;       // 0x05
        uint8_t b0;       // 0x06
        uint8_t code;     // 0x07
        int16_t x0;       // 0x08
        int16_t y0;       // 0x0A
        uint8_t u0;       // 0x0C
        uint8_t v0;       // 0x0D
        int16_t clut;     // 0x0E
        int16_t x1;       // 0x10
        int16_t y1;       // 0x12
        uint8_t u1;       // 0x14
        uint8_t v1;       // 0x15
        int16_t tpage;    // 0x16
        int16_t x2;       // 0x18
        int16_t y2;       // 0x1A
        uint8_t u2;       // 0x1C
        uint8_t v2;       // 0x1D
        uint8_t pad1;     // 0x1E
        int16_t x3;       // 0x20
        int16_t y3;       // 0x22
        uint8_t u3;       // 0x24
        uint8_t v3;       // 0x25
        uint8_t pad2;     // 0x26
    };
    static_assert(sizeof(PolyFt4) == 0x28);

    struct PolyGt4
    {
        uint32_t tag;     // 0x00
        uint8_t r0;       // 0x04
        uint8_t g0;       // 0x05
        uint8_t b0;       // 0x06
        uint8_t code;     // 0x07
        int16_t x0;       // 0x08
        int16_t y0;       // 0x0A
        uint8_t u0;       // 0x0C
        uint8_t v0;       // 0x0D
        uint16_t clut;    // 0x0E
        uint8_t r1;       // 0x10
        uint8_t g1;       // 0x11
        uint8_t b1;       // 0x12
        uint8_t p1;       // 0x13
        int16_t x1;       // 0x14
        int16_t y1;       // 0x16
        uint8_t u1;       // 0x18
        uint8_t v1;       // 0x19
        int16_t tpage;    // 0x1A
        uint8_t r2;       // 0x1C
        uint8_t g2;       // 0x1D
        uint8_t b2;       // 0x1E
        uint8_t p2;       // 0x1F
        int16_t x2;       // 0x20
        int16_t y2;       // 0x22
        uint8_t u2;       // 0x24
        uint8_t v2;       // 0x25
        uint8_t pad2;     // 0x26
        uint8_t r3;       // 0x28
        uint8_t g3;       // 0x29
        uint8_t b3;       // 0x2A
        uint8_t p3;       // 0x2B
        int16_t x3;       // 0x2C
        int16_t y3;       // 0x2E
        uint8_t u3;       // 0x30
        uint8_t v3;       // 0x31
        uint8_t pad3;     // 0x32
    };
    static_assert(sizeof(PolyGt4) == 0x34);

    struct LineF2
    {
        uint32_t tag;     // 0x00
        uint8_t r0;       // 0x04
        uint8_t g0;       // 0x05
        uint8_t b0;       // 0x06
        uint8_t code;     // 0x07
        int16_t x0;       // 0x08
        int16_t y0;       // 0x0A
        int16_t x1;       // 0x0C
        int16_t y1;       // 0x0E
    };
    static_assert(sizeof(LineF2) == 0x10);

    // The scratch primitive written into the shared MARNI_PRIM buffer. It is
    // laid out identically to PrimSprite (see re2.h) and is followed by a
    // per-type colour tail.
    using MarniPrim = PrimSprite;

    // Decompiled Add* family (originally all __cdecl in the 0x00440xxx range).
    // These replace the original binary functions; init_hooks() routes all
    // callers to our implementations.

    void reset_geom();
    int add_sprt(Sprt* p, uint32_t page, int z, int add_back);
    int add_sprt_v(int x, int y, int w, int h, int u, int v, uint16_t clut, uint32_t page, int z, int is_back);
    void add_poly_ft4(PolyFt4* p, int page, int z, int add_back);
    int add_mask(Sprt* p, int page, int z);
    int add_bg_scaled(int bg, int z);
    int sub_440A20(int prim, int page, int z);
    int sub_440B70(int prim, int page, int z);
    int add_poly_gt4(PolyGt4* p, int page, int z);
    int add_poly_ft4_2(PolyFt4* p, int page, int z);
    int add_poly_f4(Tile* p, int z, int is_back);
    int add_tile(Tile* p, int z, int is_back);
    int add_line_f2(LineF2* p, int z, int is_back);

    // Registers hooks for the Add* functions and ResetGeom.
    void init_hooks();
}
