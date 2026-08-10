#include "renderer.h"
#include "marni.h"
#include "openre.h"
#include "re2.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <vector>

namespace openre::gfx_draw
{
    namespace
    {
        // Semi-transparency blend-mode modifiers, ORed into the primitive
        // type. The primitive's tag (low 2 bits) indexes the table for its
        // prim family; marni's draw-op dispatch reads the resulting bits to
        // pick the PSX semi-transparency blend mode. Immutable data from the
        // original binary.
        constexpr uint32_t kBlendAverage = 0x100000;  // 0.5 x B + 0.5 x F  -> SRCALPHA/INVSRCALPHA
        constexpr uint32_t kBlendAdd = 0x200000;      // B + F              -> SRCALPHA/ONE
        constexpr uint32_t kBlendSubtract = 0x300000; // B - F              -> SRCALPHA/ONE
        constexpr uint32_t kBlendQuarter = 0x400000;  // 0.25 x B + 0.25 x F-> SRCALPHA/INVSRCALPHA

        constexpr uint32_t s_sprtTypeMods[8] = {
            kBlendAverage, kBlendAdd, kBlendAverage, kBlendAverage, kBlendQuarter, kBlendAverage, kBlendAverage, kBlendAverage,
        };
        constexpr uint32_t s_polyTypeMods[4] = {
            kBlendAverage,
            kBlendAdd,
            kBlendAverage,
            kBlendSubtract,
        };
        constexpr uint32_t s_tileTypeMods[4] = {
            kBlendAverage,
            kBlendAdd,
            kBlendQuarter,
            kBlendSubtract,
        };

        // Growing scratch arena for MARNI primitives. Records are carved from
        // fixed-size blocks that are appended (never moved or reallocated), so
        // pointers already handed to marni's ordering tables stay valid even
        // when the arena grows. Reset at the start of each frame; blocks are
        // reused across frames.
        class PrimitiveArena
        {
        public:
            explicit PrimitiveArena(size_t blockSize = 64 * 1024)
                : blockSize(blockSize)
            {
                addBlock();
            }

            void* alloc(size_t size)
            {
                size = (size + 0xF) & ~size_t(0xF);
                if (blocks[currentBlock].used + size > blockSize)
                    addBlock();
                auto& block = blocks[currentBlock];
                void* ptr = block.data.get() + block.used;
                block.used += size;
                return ptr;
            }

            void reset()
            {
                currentBlock = 0;
                for (auto& block : blocks)
                    block.used = 0;
            }

        private:
            struct Block
            {
                std::unique_ptr<uint8_t[]> data;
                size_t used = 0;
            };

            void addBlock()
            {
                blocks.push_back({ std::make_unique<uint8_t[]>(blockSize), 0 });
                currentBlock = blocks.size() - 1;
            }

            std::vector<Block> blocks;
            size_t currentBlock = 0;
            size_t blockSize;
        };

        // Materializes a marni work surface from an Image, matching the layout
        // produced by timobject_in (PSX 555 descriptor, var_2A = 1). The caller
        // must surface2_release() the surface when done.
        static bool materializeSurface(MarniSurface2* surface, const Image& image)
        {
            marni::surface2_ctor(surface);
            if (!marni::surface2_create_work(surface, image.width, image.height, image.depth, image.palBpp, image.palCnt))
            {
                marni::surface2_release(surface);
                return false;
            }

            if (image.psxFormat)
            {
                // PSX 555 pixel format: red in the low bits, no alpha channel.
                surface->desc.r_shift = 0;
                surface->desc.r_mask = 31;
                surface->desc.r_bitcnt = 5;
                surface->desc.g_shift = 5;
                surface->desc.g_mask = 31;
                surface->desc.g_bitcnt = 5;
                surface->desc.b_shift = 10;
                surface->desc.b_mask = 31;
                surface->desc.b_bitcnt = 5;
                surface->desc.a_shift = 0;
                surface->desc.a_mask = 0;
                surface->desc.a_bitcnt = 0;
            }
            surface->var_2A = 1;
            surface->bOpen = 1;

            size_t bitmapSize = (size_t)surface->pitch * surface->height;
            if (!image.pixels.empty() && image.pixels.size() <= bitmapSize)
                std::memcpy(surface->pBitmap, image.pixels.data(), image.pixels.size());

            size_t paletteSize = 0;
            if (image.palBpp > 0 && image.palCnt > 0)
            {
                int entriesPerPal = surface->bpp == 4 ? 16 : 256;
                paletteSize = (size_t)entriesPerPal * image.palCnt * (image.palBpp / 8);
                if (image.palette.size() <= paletteSize)
                    std::memcpy(surface->pPalette, image.palette.data(), image.palette.size());
            }
            return true;
        }

        // ── Concrete MARNI primitive record layouts ──────────────────────
        // Each Add* method carves a fixed-size record out of the arena whose
        // head matches the MarniPrim (PrimSprite, 0x1C bytes, see re2.h)
        // layout and whose tail carries the per-type colour/UV data that the
        // original code packed via raw byte offsets. The structs below lay
        // those records out explicitly; the static asserts pin each one to the
        // exact allocation size of its Add* method. Records are passed to
        // marni as Prim* via a reinterpret cast; the head fields must line up
        // with Prim exactly.

        // SPRT-shaped record (addSprt and addPolyFt4, 0x20 bytes): MarniPrim
        // head plus a colour tail dword used only by the shaded (type 37)
        // variant.
        struct MarniSprt : MarniPrim
        {
            uint32_t color; // 0x1C flat-shade colour b | (g << 8) | (r << 16)
        };
        static_assert(sizeof(MarniSprt) == 0x20);

        // Mask sprite record (addMask, 0x24 bytes). The PSX header's u0/v0 and
        // u1/v1 byte pairs are repurposed as the 16-bit bottom-right corner.
        struct MarniMask
        {
            Prim* pNext;      // 0x00
            int32_t type;     // 0x04
            uint32_t texture; // 0x08
            uint32_t clut;    // 0x0C
            union
            {
                struct
                {
                    int16_t x0; // 0x10 (not used as coordinates; see scale)
                    int16_t y0; // 0x12
                    int16_t x1; // 0x14 on-screen origin x
                    int16_t y1; // 0x16 on-screen origin y
                };
                float scale; // 0x10 z depth stored as a float over x0/y0
            };
            uint16_t cornerX; // 0x18 bottom-right x = x0 + w - 1 (aliases the PSX u0/v0 bytes)
            uint16_t cornerY; // 0x1A bottom-right y = y0 + h - 1 (aliases the PSX u1/v1 bytes)
            uint8_t su0;      // 0x1C source u0
            uint8_t sv0;      // 0x1D source v0
            uint8_t su1;      // 0x1E u0 + w - 1
            uint8_t sv1;      // 0x1F v0 + h - 1
            uint32_t color;   // 0x20 shaded colour tail (0x1002D variant only)
        };
        static_assert(sizeof(MarniMask) == 0x24);

        // Scaled background record (addBgScaled, 0x20 bytes): like the mask
        // record but without the colour tail; clut is unused.
        struct MarniBgScaled
        {
            Prim* pNext;      // 0x00
            int32_t type;     // 0x04
            uint32_t texture; // 0x08
            uint32_t clut;    // 0x0C (unused)
            union
            {
                struct
                {
                    int16_t x0; // 0x10 (not used as coordinates; see scale)
                    int16_t y0; // 0x12
                    int16_t x1; // 0x14 on-screen origin x
                    int16_t y1; // 0x16 on-screen origin y
                };
                float scale; // 0x10 z depth stored as a float over x0/y0
            };
            int16_t cornerX; // 0x18 bottom-right x = x + w - 1 (aliases the PSX u0/v0 bytes)
            int16_t cornerY; // 0x1A bottom-right y = y + h - 1 (aliases the PSX u1/v1 bytes)
            uint8_t su;      // 0x1C source u
            uint8_t sv;      // 0x1D source v
            uint8_t su1;     // 0x1E u + w - 1
            uint8_t sv1;     // 0x1F v + h - 1
        };
        static_assert(sizeof(MarniBgScaled) == 0x20);

        // Scaled sprite record (addScaledSprite, 0x24 bytes): like the mask
        // record; sizeX/sizeY hold the scaled draw size (x3 - 1, y3 - 1).
        struct MarniScaledSprite
        {
            Prim* pNext;      // 0x00
            int32_t type;     // 0x04
            uint32_t texture; // 0x08
            uint32_t clut;    // 0x0C
            union
            {
                struct
                {
                    int16_t x0; // 0x10 (not used as coordinates; see scale)
                    int16_t y0; // 0x12
                    int16_t x1; // 0x14 sprite origin x
                    int16_t y1; // 0x16 sprite origin y
                };
                float scale; // 0x10 z depth stored as a float over x0/y0
            };
            uint16_t sizeX; // 0x18 scaled width = x3 - 1 (aliases the PSX u0/v0 bytes)
            uint16_t sizeY; // 0x1A scaled height = y3 - 1 (aliases the PSX u1/v1 bytes)
            uint8_t su0;    // 0x1C source u0
            uint8_t sv0;    // 0x1D source v0
            uint8_t su1;    // 0x1E u3 - 1
            uint8_t sv1;    // 0x1F v3 - 1
            uint32_t color; // 0x20 shaded colour tail (0x1002D variant only)
        };
        static_assert(sizeof(MarniScaledSprite) == 0x24);

        // Scaled quad record (addScaledPoly, 0x30 bytes): the four vertex
        // positions (x2/y2 packed over the PSX u0/v0, u1/v1 byte slots), the z
        // depth, the per-vertex UV quad and the shaded colour tail.
        struct MarniScaledPoly
        {
            Prim* pNext;      // 0x00
            int32_t type;     // 0x04
            uint32_t texture; // 0x08
            uint32_t clut;    // 0x0C
            int16_t x0;       // 0x10
            int16_t y0;       // 0x12
            int16_t x1;       // 0x14
            int16_t y1;       // 0x16
            int16_t x2;       // 0x18 (repurposed from the PSX header u0/v0 bytes)
            int16_t y2;       // 0x1A (repurposed from the PSX header u1/v1 bytes)
            int16_t x3;       // 0x1C
            int16_t y3;       // 0x1E
            uint16_t z;       // 0x20 draw depth
            uint8_t u0;       // 0x22
            uint8_t v0;       // 0x23
            uint8_t u1;       // 0x24
            uint8_t v1;       // 0x25
            uint8_t u2;       // 0x26
            uint8_t v2;       // 0x27
            uint8_t u3;       // 0x28
            uint8_t v3;       // 0x29
            uint8_t pad[2];   // 0x2A (unused)
            uint32_t color;   // 0x2C shaded colour tail (0x1004D variant only)
        };
        static_assert(sizeof(MarniScaledPoly) == 0x30);

        // Gouraud quad record (addPolyGt4, 0x38 bytes): the four vertex
        // positions and the per-vertex UV quad followed by the four per-vertex
        // colour dwords.
        struct MarniPolyGt4
        {
            Prim* pNext;      // 0x00
            int32_t type;     // 0x04
            uint32_t texture; // 0x08
            uint32_t clut;    // 0x0C
            int16_t x0;       // 0x10
            int16_t y0;       // 0x12
            int16_t x1;       // 0x14
            int16_t y1;       // 0x16
            int16_t x2;       // 0x18 (repurposed from the PSX header u0/v0 bytes)
            int16_t y2;       // 0x1A (repurposed from the PSX header u1/v1 bytes)
            int16_t x3;       // 0x1C
            int16_t y3;       // 0x1E
            uint8_t u0;       // 0x20
            uint8_t v0;       // 0x21
            uint8_t u1;       // 0x22
            uint8_t v1;       // 0x23
            uint8_t u2;       // 0x24
            uint8_t v2;       // 0x25
            uint8_t u3;       // 0x26
            uint8_t v3;       // 0x27
            uint32_t color0;  // 0x28 vertex 0 colour b | (g << 8) | (r << 16)
            uint32_t color1;  // 0x2C vertex 1 colour
            uint32_t color2;  // 0x30 vertex 2 colour
            uint32_t color3;  // 0x34 vertex 3 colour
        };
        static_assert(sizeof(MarniPolyGt4) == 0x38);

        // Front-OT quad record (addPolyFt42, 0x2C bytes): same vertex packing
        // as the gouraud record but with a single flat colour tail.
        struct MarniPolyFt42
        {
            Prim* pNext;      // 0x00
            int32_t type;     // 0x04
            uint32_t texture; // 0x08
            uint32_t clut;    // 0x0C
            int16_t x0;       // 0x10
            int16_t y0;       // 0x12
            int16_t x1;       // 0x14
            int16_t y1;       // 0x16
            int16_t x2;       // 0x18 (repurposed from the PSX header u0/v0 bytes)
            int16_t y2;       // 0x1A (repurposed from the PSX header u1/v1 bytes)
            int16_t x3;       // 0x1C
            int16_t y3;       // 0x1E
            uint8_t u0;       // 0x20
            uint8_t v0;       // 0x21
            uint8_t u1;       // 0x22
            uint8_t v1;       // 0x23
            uint8_t u2;       // 0x24
            uint8_t v2;       // 0x25
            uint8_t u3;       // 0x26
            uint8_t v3;       // 0x27
            uint32_t color;   // 0x28 flat colour b | (g << 8) | (r << 16)
        };
        static_assert(sizeof(MarniPolyFt42) == 0x2C);

        // Flat untextured quad record (addPolyF4, 0x14 bytes). The PSX
        // header's texture/clut/x0 fields are repurposed as the packed
        // corners and the colour dword.
        struct MarniPolyF4
        {
            Prim* pNext;  // 0x00
            int32_t type; // 0x04
            union
            {
                struct
                {
                    int16_t x0; // 0x08 top-left corner x
                    int16_t y0; // 0x0A top-left corner y
                };
                uint32_t packedCorner0; // 0x08 y0 << 16 | x0
            };
            union
            {
                struct
                {
                    int16_t x1; // 0x0C bottom-right corner x
                    int16_t y1; // 0x0E bottom-right corner y
                };
                uint32_t packedCorner1; // 0x0C y1 << 16 | x1
            };
            uint32_t color; // 0x10 packed colour b | (g << 8) | (r << 16)
        };
        static_assert(sizeof(MarniPolyF4) == 0x14);

        // Flat tile record (addTile, 0x14 bytes): same layout as MarniPolyF4,
        // with the corner coordinates stored directly.
        struct MarniTile
        {
            Prim* pNext;    // 0x00
            int32_t type;   // 0x04
            int16_t x0;     // 0x08 top-left corner x
            int16_t y0;     // 0x0A top-left corner y
            int16_t x1;     // 0x0C bottom-right corner x = x0 + w - 1
            int16_t y1;     // 0x0E bottom-right corner y = y0 + h - 1
            uint32_t color; // 0x10 packed colour b | (g << 8) | (r << 16)
        };
        static_assert(sizeof(MarniTile) == 0x14);

        // Concrete renderer that builds MARNI primitives in its own growing
        // arena and submits them to marni.
        class MarniRenderer final : public Renderer
        {
        public:
            void reset() override
            {
                arena.reset();
                setGeomOffset(0, 0);
            }

            // 0x00440280
            int addSprt(const Sprt* p, uint32_t page, int z, int add_back) override
            {
                if (page >= 0x29)
                    return 0;
                if (gGameTable.texture_pages[page].handle == 0)
                    return 0;
                if (gGameTable.texture_pages[page].suspended == 1)
                    return 0;

                uint16_t clut = p->clut;
                if (clut >= gGameTable.texture_pages[page].clutCount)
                    clut = 0;

                auto* prim = (MarniSprt*)arena.alloc(sizeof(MarniSprt));
                if (p->r == 0x80 && p->g == 0x80 && p->b == 0x80)
                {
                    prim->type = 36;
                    if ((p->code & 2) != 0)
                        prim->type = (int32_t)(s_sprtTypeMods[p->tag & 3] | 0x24);
                    prim->x0 = p->x0;
                    prim->y0 = p->y0;
                    prim->x1 = p->x0 + p->w - 1;
                    prim->y1 = p->y0 + p->h - 1;
                    prim->u0 = p->u0;
                    prim->v0 = p->v0;
                    prim->u1 = (uint8_t)(p->u0 + (uint8_t)p->w - 1);
                    prim->v1 = (uint8_t)(p->v0 + (uint8_t)p->h - 1);
                    prim->texture = gGameTable.texture_pages[page].handle;
                    prim->clut = clut;
                }
                else
                {
                    prim->type = 37;
                    prim->color = (uint32_t)((p->r << 16) | (p->g << 8) | p->b);
                    if ((p->code & 2) != 0)
                        prim->type = (int32_t)(s_sprtTypeMods[p->tag & 3] | 0x25);
                    prim->x0 = p->x0;
                    prim->y0 = p->y0;
                    prim->x1 = p->x0 + p->w - 1;
                    prim->y1 = p->y0 + p->h - 1;
                    prim->u0 = p->u0;
                    prim->v0 = p->v0;
                    prim->u1 = (uint8_t)(p->u0 + (uint8_t)p->w - 1);
                    prim->v1 = (uint8_t)(p->v0 + (uint8_t)p->h - 1);
                    prim->texture = gGameTable.texture_pages[page].handle;
                    prim->clut = clut;
                }

                if (add_back)
                    marni::add_primitive_back(gGameTable.pMarni, (Prim*)prim, z);
                else
                    marni::add_primitive_front(gGameTable.pMarni, (Prim*)prim, z);
                return 1;
            }

            // 0x00440600
            void addPolyFt4(const PolyFt4* p, int page, int z, int add_back) override
            {
                if (page < 41 && gGameTable.texture_pages[page].handle != 0 && gGameTable.texture_pages[page].suspended != 1)
                {
                    uint16_t clut = (uint16_t)p->clut;
                    if (clut >= gGameTable.texture_pages[page].clutCount)
                        clut = 0;

                    auto* prim = (MarniSprt*)arena.alloc(sizeof(MarniSprt));
                    if (p->r0 == 0x80 && p->g0 == 0x80 && p->b0 == 0x80)
                    {
                        // White quad: no per-vertex colour, 0x24 (POLY_FT4) primitive.
                        prim->type = 36;
                        if ((p->code & 2) != 0)
                            prim->type = (int32_t)(s_sprtTypeMods[p->tag & 3] | 0x24);
                        prim->x0 = p->x0;
                        prim->y0 = p->y0;
                        prim->x1 = p->x3;
                        prim->y1 = p->y3;
                        prim->u0 = p->u0;
                        prim->v0 = p->v0;
                        prim->u1 = p->u3;
                        prim->v1 = p->v3;
                        prim->texture = gGameTable.texture_pages[page].handle;
                        prim->clut = clut;
                    }
                    else
                    {
                        // Flat-shaded quad: 0x25 (POLY_GT4) primitive with the
                        // packed colour written into the colour tail.
                        prim->type = 37;
                        uint16_t v6 = (uint16_t)(p->g0 | (p->r0 << 8));
                        prim->color = (uint32_t)((uint8_t)p->b0 | (v6 << 8));
                        if ((p->code & 2) != 0)
                        {
                            prim->type = (int32_t)(s_sprtTypeMods[p->tag & 7] | 0x25);
                            if ((p->tag & 7) == 4)
                            {
                                int v7 = 2 * (uint8_t)p->r0 - 1;
                                if (v7 < 0)
                                    v7 = 0;
                                prim->color = (uint32_t)(((uint32_t)v7 << 24) | 0x808080);
                            }
                        }
                        prim->x0 = p->x0;
                        prim->y0 = p->y0;
                        prim->x1 = p->x3;
                        prim->y1 = p->y3;
                        prim->u0 = p->u0;
                        prim->v0 = p->v0;
                        prim->u1 = p->u3;
                        prim->v1 = p->v3;
                        prim->texture = gGameTable.texture_pages[page].handle;
                        prim->clut = clut;
                    }

                    if (add_back)
                        marni::add_primitive_back(gGameTable.pMarni, (Prim*)prim, z);
                    else
                        marni::add_primitive_front(gGameTable.pMarni, (Prim*)prim, z);
                }
            }

            // 0x004407F0
            // AddMask - adds a textured sprite into the scratch buffer and hands
            // it to MARNI's scaler OT insertion. This is the stencil/mask sprite
            // path.
            int addMask(const Sprt* p, int page, int z) override
            {
                if (page >= 41)
                    return 0;
                if (gGameTable.texture_pages[page].handle == 0)
                    return 0;
                if (gGameTable.texture_pages[page].suspended == 1)
                    return 0;

                uint16_t clut = p->clut;
                if (clut >= gGameTable.texture_pages[page].clutCount)
                    clut = 0;

                auto* prim = (MarniMask*)arena.alloc(sizeof(MarniMask));

                // Textured sprite primitive (PSX SPRT).
                prim->type = 0x1002C; // 65580

                if (p->code & 2)
                {
                    prim->type = 0x1002D; // 65581
                    prim->type = (int32_t)(s_sprtTypeMods[p->tag & 3] | 0x1002D);
                    // Colour packed as b | (g << 8) | (r << 16) into the tail dword.
                    prim->color = (uint32_t)p->b | ((uint32_t)p->g << 8) | ((uint32_t)p->r << 16);
                }

                prim->x1 = p->x0;
                prim->y1 = p->y0;
                // Bottom-right corner packed into the u0/v0 and u1/v1 words.
                prim->cornerX = (uint16_t)(p->x0 + p->w - 1);
                prim->cornerY = (uint16_t)(p->y0 + p->h - 1);

                // Source UV quad in the tail bytes at 0x1C..0x1F.
                prim->su0 = p->u0;
                prim->sv0 = p->v0;
                prim->su1 = (uint8_t)(p->u0 + (uint8_t)p->w - 1);
                prim->sv1 = (uint8_t)(p->v0 + (uint8_t)p->h - 1);

                prim->texture = gGameTable.texture_pages[page].handle;
                prim->clut = clut;

                // The z value is clamped to the projection plane before being stored.
                if (z >= gGameTable.global_prj / 2)
                    prim->scale = (float)z;
                else
                    prim->scale = (float)(gGameTable.global_prj / 2);

                marni::add_primitive_scaler(gGameTable.pMarni, (Prim*)prim, z >> 4);
                return 1;
            }

            // 0x00440950
            // AddBgScaled - draws a full-screen scaled background quad (bg0)
            // using the shared scratch buffer. `bg` points at a descriptor whose
            // interesting fields are x@+8 (i16), y@+0A (i16), u@+0C (u8), v@+0D
            // (u8), w@+10 (i16), h@+12 (i16). `z` is the draw depth; it is stored
            // as a float scale and is also right-shifted by 4 for the OT z-order.
            int addBgScaled(const BgScaledDesc* bg, int z) override
            {
                if (gGameTable.bg_tex0 == 0)
                    return 0;

                auto* prim = (MarniBgScaled*)arena.alloc(sizeof(MarniBgScaled));

                prim->type = 0x1002C; // 65580: scaled full-screen background sprite

                prim->x1 = bg->x;
                prim->y1 = bg->y;

                // Pack (x + w - 1) into the u0/v0 word and (y + h - 1) into the
                // u1/v1 word (16-bit arithmetic, matching the binary).
                prim->cornerX = bg->x + bg->w - 1;
                prim->cornerY = bg->y + bg->h - 1;

                // The four bytes past the MarniPrim record (offset 0x1C..0x1F)
                // hold the source u/v quad (u, v, u + w - 1, v + h - 1) as bytes.
                prim->su = bg->u;
                prim->sv = bg->v;
                prim->su1 = bg->u + bg->w - 1;
                prim->sv1 = bg->v + bg->h - 1;

                // Store the z depth as a float scale over the x0/y0 fields.
                prim->scale = (float)z;
                prim->texture = gGameTable.bg_tex0;

                marni::add_primitive_scaler(gGameTable.pMarni, (Prim*)prim, z >> 4);
                return 1;
            }

            // Draws the shared full-screen background (bg_tex0) and right edge
            // strip (bg_tex1) quads at the given screen-space origin. Used by
            // both addBg and addBg2.
            void addBgPrims(int16_t x_off, int16_t y_off)
            {
                if (gGameTable.bg_tex0 != 0)
                {
                    auto* prim = (MarniPrim*)arena.alloc(sizeof(PrimSprite));
                    prim->type = 36;
                    prim->texture = gGameTable.bg_tex0;
                    prim->x0 = x_off;
                    prim->y0 = y_off;
                    prim->x1 = x_off + 255;
                    prim->y1 = y_off + 239;
                    prim->u0 = 0;
                    prim->v0 = 0;
                    prim->u1 = 255;
                    prim->v1 = 239;
                    marni::add_primitive_back(gGameTable.pMarni, (Prim*)prim, 15);
                }

                if (gGameTable.bg_tex1 != 0)
                {
                    auto* prim = (MarniPrim*)arena.alloc(sizeof(PrimSprite));
                    prim->type = 36;
                    prim->texture = gGameTable.bg_tex1;
                    prim->x0 = x_off + 0x100;
                    prim->y0 = y_off;
                    prim->x1 = x_off + 0x13F;
                    prim->y1 = y_off + 0x7F;
                    prim->u0 = 0;
                    prim->v0 = 0;
                    prim->u1 = 0x3F;
                    prim->v1 = 0x7F;
                    marni::add_primitive_back(gGameTable.pMarni, (Prim*)prim, 15);

                    auto* prim2 = (MarniPrim*)arena.alloc(sizeof(PrimSprite));
                    prim2->type = 36;
                    prim2->texture = gGameTable.bg_tex1;
                    prim2->x0 = x_off + 0x100;
                    prim2->y0 = y_off + 0x80;
                    prim2->x1 = x_off + 0x13F;
                    prim2->y1 = y_off + 0xEF;
                    prim2->u0 = 0x40;
                    prim2->v0 = 0;
                    prim2->u1 = 0x7F;
                    prim2->v1 = 0x6F;
                    marni::add_primitive_back(gGameTable.pMarni, (Prim*)prim2, 15);
                }
            }

            // 0x0043FB30
            // AddBg - draws the full-screen background (bg_tex0) followed by the
            // right edge strip (bg_tex1) at the current global camera offset.
            void addBg() override
            {
                addBgPrims((int16_t)gGameTable.global_cx, (int16_t)gGameTable.global_cy);
                gGameTable.bgDrawn = 1;
            }

            // 0x0043FCB0
            // AddBg2 - same as AddBg but the background is scrolled by
            // `scroll_y`, and an extra pair of strips (from bgScrollTextures) is
            // drawn below the visible frame to fill in during a vertical scroll
            // transition.
            void addBg2(int16_t scroll_y) override
            {
                addBgPrims(0, scroll_y);

                if (gGameTable.bgScrollTextures[0] != 0)
                {
                    auto* prim = (MarniPrim*)arena.alloc(sizeof(PrimSprite));
                    prim->type = 36;
                    prim->texture = gGameTable.bgScrollTextures[0];
                    prim->x0 = 0;
                    prim->y0 = scroll_y - 0xF0;
                    prim->x1 = 255;
                    prim->y1 = scroll_y - 1;
                    prim->u0 = 0;
                    prim->v0 = 0;
                    prim->u1 = 255;
                    prim->v1 = 239;
                    marni::add_primitive_back(gGameTable.pMarni, (Prim*)prim, 15);
                }

                if (gGameTable.bgScrollTextures[1] != 0)
                {
                    auto* prim = (MarniPrim*)arena.alloc(sizeof(PrimSprite));
                    prim->type = 36;
                    prim->texture = gGameTable.bgScrollTextures[1];
                    prim->x0 = 0x100;
                    prim->y0 = scroll_y - 0xF0;
                    prim->x1 = 0x13F;
                    prim->y1 = scroll_y - 0x71;
                    prim->u0 = 0;
                    prim->v0 = 0;
                    prim->u1 = 0x3F;
                    prim->v1 = 0x7F;
                    marni::add_primitive_back(gGameTable.pMarni, (Prim*)prim, 15);

                    auto* prim2 = (MarniPrim*)arena.alloc(sizeof(PrimSprite));
                    prim2->type = 36;
                    prim2->texture = gGameTable.bgScrollTextures[1];
                    prim2->x0 = 0x100;
                    prim2->y0 = scroll_y - 0x70;
                    prim2->x1 = 0x13F;
                    prim2->y1 = scroll_y - 1;
                    prim2->u0 = 0x40;
                    prim2->v0 = 0;
                    prim2->u1 = 0x7F;
                    prim2->v1 = 0x6F;
                    marni::add_primitive_back(gGameTable.pMarni, (Prim*)prim2, 15);
                }

                gGameTable.bgDrawn = 1;
            }

            // 0x00440A20
            // AddScaledSprite - draws a scaled sprite via AddPrimitiveScaler.
            // The descriptor is a POLY_FT4-shaped record: the sprite origin is
            // (x0, y0), the source u/v quad runs from (u0, v0) to (u3, v3), and
            // the texture region size is taken from the (x3, y3) words (each
            // reduced by one). `z` is stored as a float scale over x0/y0 and is
            // also right-shifted by 4 for the OT z-ordering.
            int addScaledSprite(const PolyFt4* prim, int page, int z) override
            {
                const PolyFt4* p = prim;

                if (page >= 41)
                    return 0;
                if (gGameTable.texture_pages[page].handle == 0)
                    return 0;
                if (gGameTable.texture_pages[page].suspended == 1)
                    return 0;

                uint16_t clut = (uint16_t)p->clut;
                if (clut >= gGameTable.texture_pages[page].clutCount)
                    clut = 0;

                auto* out = (MarniScaledSprite*)arena.alloc(sizeof(MarniScaledSprite));

                if (p->r0 == 0x80 && p->g0 == 0x80 && p->b0 == 0x80)
                {
                    // White (unshaded) sprite: 0x1002C (SPRT-scaler) primitive.
                    out->type = 0x1002C;
                }
                else
                {
                    // Flat-shaded sprite: 0x1002D (SPRT-scaler) primitive with
                    // the packed RGB colour written into the colour dword at
                    // scratch + 0x20.
                    out->type = 0x1002D;
                    out->color = (uint32_t)((p->r0 << 16) | (p->g0 << 8) | p->b0);
                }

                if ((p->code & 2) != 0)
                {
                    out->type |= s_sprtTypeMods[((uint8_t)p->tpage >> 5) & 3];
                }

                out->x1 = p->x0;
                out->y1 = p->y0;

                // Texture region size (16-bit words) minus one, packed into the
                // u0/v0 and u1/v1 words.
                out->sizeX = (uint16_t)(p->x3 - 1);
                out->sizeY = (uint16_t)(p->y3 - 1);

                // The four bytes past the MarniPrim record (offset 0x1C..0x1F)
                // hold the source u/v quad (u0, v0, u3 - 1, v3 - 1) as bytes.
                out->su0 = p->u0;
                out->sv0 = p->v0;
                out->su1 = (uint8_t)(p->u3 - 1);
                out->sv1 = (uint8_t)(p->v3 - 1);

                out->texture = gGameTable.texture_pages[page].handle;

                // Store the z depth as a float scale over the x0/y0 fields.
                out->scale = (float)z;
                out->clut = clut;

                marni::add_primitive_scaler(gGameTable.pMarni, (Prim*)out, z >> 4);
                return 1;
            }

            // 0x00440B70
            // AddPrimitiveScaler path for a POLY_FT4 quad. The four vertex x/y
            // positions are packed into the MarniPrim head and the remaining
            // vertex data (x3,y3 + the per-vertex u/v) plus the z depth are
            // packed into the 0x1C-byte tail that follows the MarniPrim record.
            int addScaledPoly(const PolyFt4* prim, int page, int z) override
            {
                const PolyFt4* p = prim;

                if (page >= 41)
                    return 0;
                if (gGameTable.texture_pages[page].handle == 0)
                    return 0;
                if (gGameTable.texture_pages[page].suspended == 1)
                    return 0;
                if (z >> 4 > 4095)
                    return 0;

                uint16_t clut = (uint16_t)p->clut;
                if (clut >= gGameTable.texture_pages[page].clutCount)
                    clut = 0;

                auto* out = (MarniScaledPoly*)arena.alloc(sizeof(MarniScaledPoly));

                if (p->r0 == 0x80 && p->g0 == 0x80 && p->b0 == 0x80)
                {
                    // White (unshaded) quad: 0x1004C (POLY_FT4-scaler) primitive.
                    out->type = 0x1004C;
                    if ((p->code & 2) != 0)
                        out->type = (int32_t)(s_polyTypeMods[((uint8_t)p->tpage >> 5) & 3] | 0x1004C);

                    out->x0 = p->x0;
                    out->y0 = p->y0;
                    out->x1 = p->x1;
                    out->y1 = p->y1;
                    out->x2 = p->x2;
                    out->y2 = p->y2;
                    out->x3 = p->x3;
                    out->y3 = p->y3;
                    out->z = (uint16_t)z;
                    out->u0 = p->u0;
                    out->v0 = p->v0;
                    out->u1 = p->u1;
                    out->v1 = p->v1;
                    out->u2 = p->u2;
                    out->v2 = p->v2;
                    out->u3 = p->u3;
                    out->v3 = p->v3;
                    out->texture = gGameTable.texture_pages[page].handle;
                    out->clut = clut;
                }
                else
                {
                    // Flat-shaded quad: 0x1004D (POLY_GT4-scaler) primitive with
                    // the packed RGB colour written into the colour tail.
                    out->type = 0x1004D;
                    out->color = (uint32_t)((p->r0 << 16) | (p->g0 << 8) | p->b0);
                    if ((p->code & 2) != 0)
                        out->type = (int32_t)(s_polyTypeMods[((uint8_t)p->tpage >> 5) & 3] | 0x1004D);

                    out->x0 = p->x0;
                    out->y0 = p->y0;
                    out->x1 = p->x1;
                    out->y1 = p->y1;
                    out->x2 = p->x2;
                    out->y2 = p->y2;
                    out->x3 = p->x3;
                    out->y3 = p->y3;
                    out->z = (uint16_t)z;
                    out->u0 = p->u0;
                    out->v0 = p->v0;
                    out->u1 = p->u1;
                    out->v1 = p->v1;
                    out->u2 = p->u2;
                    out->v2 = p->v2;
                    out->u3 = p->u3;
                    out->v3 = p->v3;
                    out->texture = gGameTable.texture_pages[page].handle;
                    out->clut = clut;
                }

                marni::add_primitive_scaler(gGameTable.pMarni, (Prim*)out, z >> 4);
                return 1;
            }

            // 0x00440DD0
            // AddPolyGT4 - adds a gouraud-shaded textured quad. The 0x34-byte
            // POLY_GT4 becomes a 0x38-byte MarniPrim record: the PrimSprite
            // header (0x1C) followed by the four per-vertex colours, one dword
            // per vertex.
            int addPolyGt4(const PolyGt4* p, int page, int z) override
            {
                if (page >= 41)
                    return 0;
                if (gGameTable.texture_pages[page].handle == 0)
                    return 0;
                if (gGameTable.texture_pages[page].suspended == 1)
                    return 0;
                if ((int)(z & 0xFFFFFFF0) > 65520)
                    return 0;

                uint16_t clut = p->clut;
                if (clut >= gGameTable.texture_pages[page].clutCount)
                    clut = 0;

                // MARNI stores half-intensity colours and scales them by two
                // during shading, so each channel is clamped to 0x80.
                const uint8_t r0 = p->r0 > 0x80 ? 0x80 : p->r0;
                const uint8_t g0 = p->g0 > 0x80 ? 0x80 : p->g0;
                const uint8_t b0 = p->b0 > 0x80 ? 0x80 : p->b0;
                const uint8_t r1 = p->r1 > 0x80 ? 0x80 : p->r1;
                const uint8_t g1 = p->g1 > 0x80 ? 0x80 : p->g1;
                const uint8_t b1 = p->b1 > 0x80 ? 0x80 : p->b1;
                const uint8_t r2 = p->r2 > 0x80 ? 0x80 : p->r2;
                const uint8_t g2 = p->g2 > 0x80 ? 0x80 : p->g2;
                const uint8_t b2 = p->b2 > 0x80 ? 0x80 : p->b2;
                const uint8_t r3 = p->r3 > 0x80 ? 0x80 : p->r3;
                const uint8_t g3 = p->g3 > 0x80 ? 0x80 : p->g3;
                const uint8_t b3 = p->b3 > 0x80 ? 0x80 : p->b3;

                auto* prim = (MarniPolyGt4*)arena.alloc(sizeof(MarniPolyGt4));

                prim->type = 70; // 0x46: gouraud textured quad (GT4)

                // Pack the four vertex colours into the colour tail as
                // b | (g << 8) | (r << 16), one dword per vertex.
                prim->color0 = (uint32_t)b0 | ((uint32_t)g0 << 8) | ((uint32_t)r0 << 16);
                prim->color1 = (uint32_t)b1 | ((uint32_t)g1 << 8) | ((uint32_t)r1 << 16);
                prim->color2 = (uint32_t)b2 | ((uint32_t)g2 << 8) | ((uint32_t)r2 << 16);
                prim->color3 = (uint32_t)b3 | ((uint32_t)g3 << 8) | ((uint32_t)r3 << 16);

                if ((p->code & 2) != 0)
                    prim->type |= (int32_t)s_polyTypeMods[p->tag & 3];

                // Vertex coordinates; the second and third vertices (x2/y2 and
                // x3/y3) are packed into the otherwise-unused u/v bytes and the
                // tail's pNext field.
                prim->x0 = p->x0;
                prim->y0 = p->y0;
                prim->x1 = p->x1;
                prim->y1 = p->y1;
                prim->x2 = p->x2;
                prim->y2 = p->y2;
                prim->x3 = p->x3;
                prim->y3 = p->y3;

                // Texture coordinates, two (u,v) pairs packed into the tail header.
                prim->u0 = p->u0;
                prim->v0 = p->v0;
                prim->u1 = p->u1;
                prim->v1 = p->v1;
                prim->u2 = p->u2;
                prim->v2 = p->v2;
                prim->u3 = p->u3;
                prim->v3 = p->v3;

                prim->texture = gGameTable.texture_pages[page].handle;
                prim->clut = clut;

                marni::add_primitive_front(gGameTable.pMarni, (Prim*)prim, z);
                return 1;
            }

            // 0x00440FF0
            // AddPolyFT4_2 - adds a textured quad via the front OT only.
            // Unlike AddPolyFT4, this variant always emits primitive type 69
            // (with the semi-transparency modifier from s_polyTypeMods when
            // the CODE bit 1 flag is set) and has no back-OT variant. The UV
            // coordinates are packed into the second prim's type and texture
            // fields.
            int addPolyFt42(const PolyFt4* p, int page, int z) override
            {
                if (page >= 41)
                    return 0;
                if (gGameTable.texture_pages[page].handle == 0)
                    return 0;
                if (gGameTable.texture_pages[page].suspended == 1)
                    return 0;

                uint16_t clut = (uint16_t)p->clut;
                if (clut >= (uint32_t)gGameTable.texture_pages[page].clutCount)
                    clut = 0;

                auto* prim = (MarniPolyFt42*)arena.alloc(sizeof(MarniPolyFt42));

                prim->type = 69;
                const uint8_t r0 = p->r0 > 0x80 ? 0x80 : p->r0;
                const uint8_t g0 = p->g0 > 0x80 ? 0x80 : p->g0;
                const uint8_t b0 = p->b0 > 0x80 ? 0x80 : p->b0;
                // Colour packed into the tail: b | (g << 8) | (r << 16).
                prim->color = ((uint32_t)r0 << 16) | ((uint32_t)g0 << 8) | b0;
                if ((p->code & 2) != 0)
                    prim->type |= s_polyTypeMods[p->tag & 3];

                // Geometry: x1/y1 pair and x2/y2 (packed over the u0/v0 and
                // u1/v1 byte slots of the first prim) plus the x3/y3 pair
                // (packed into the second prim's pNext word slots).
                prim->x0 = p->x0;
                prim->y0 = p->y0;
                prim->x1 = p->x1;
                prim->y1 = p->y1;
                prim->x2 = p->x2;
                prim->y2 = p->y2;
                prim->x3 = p->x3;
                prim->y3 = p->y3;
                // UV coordinates are packed into the tail header.
                prim->u0 = p->u0;
                prim->v0 = p->v0;
                prim->u1 = p->u1;
                prim->v1 = p->v1;
                prim->u2 = p->u2;
                prim->v2 = p->v2;
                prim->u3 = p->u3;
                prim->v3 = p->v3;

                prim->texture = gGameTable.texture_pages[page].handle;
                prim->clut = clut;

                marni::add_primitive_front(gGameTable.pMarni, (Prim*)prim, z);
                return 1;
            }

            // 0x00441170
            // AddPolyF4 - adds a flat (untextured) quad primitive (front or
            // back OT insertion).
            int addPolyF4(const Tile* p, int z, int is_back) override
            {
                auto* prim = (MarniPolyF4*)arena.alloc(sizeof(MarniPolyF4));

                prim->type = 33;
                // x0/y0 packed into the texture field (PSX-style POLY_F4).
                prim->packedCorner0 = ((uint32_t)(uint16_t)p->y0 << 16) | (uint16_t)p->x0;
                // x1/y1 come from the word following the tile (r|g and b|code of
                // the next Tile at +0x14/+0x16), each decremented by one.
                prim->x1 = (int16_t)(((uint16_t)p[1].r | ((uint16_t)p[1].g << 8)) - 1);
                prim->y1 = (int16_t)(((uint16_t)p[1].b | ((uint16_t)p[1].code << 8)) - 1);
                // Colour: b | (g << 8) | (r << 16).
                prim->color = (uint32_t)p->b | ((uint32_t)p->g << 8) | ((uint32_t)p->r << 16);

                if ((p->code & 2) != 0)
                {
                    prim->type = s_tileTypeMods[p->tag & 3] | 0x21;
                    if ((p->tag & 2) != 0)
                    {
                        int r = (uint8_t)p->r;
                        if (r == 255)
                        {
                            prim->type = 0x21;
                            prim->color = 0;
                        }
                        else
                        {
                            prim->color = (uint32_t)r << 24;
                        }
                    }
                }

                if (is_back)
                    marni::add_primitive_back(gGameTable.pMarni, (Prim*)prim, z);
                else
                    marni::add_primitive_front(gGameTable.pMarni, (Prim*)prim, z);
                return 1;
            }

            // 0x00441270
            int addTile(const Tile* p, int z, int is_back) override
            {
                auto* prim = (MarniTile*)arena.alloc(sizeof(MarniTile));

                prim->type = 0x21; // 33: flat untextured tile

                // Pack the top-left corner into the texture field (LOWORD/HIWORD).
                prim->x0 = p->x0;
                prim->y0 = p->y0;

                // Pack the bottom-right corner (x0+w-1, y0+h-1) into the second
                // dword (the CLUT slot, repurposed as scratch space here).
                prim->x1 = (int16_t)(p->x0 + p->w - 1);
                prim->y1 = (int16_t)(p->y0 + p->h - 1);

                // Colour packed as b | (g << 8) | (r << 16) over the x0/y0/x1/y1 fields.
                prim->color = (uint32_t)p->b | ((uint32_t)p->g << 8) | ((uint32_t)p->r << 16);

                if (p->code & 2)
                {
                    prim->type = (int32_t)(s_tileTypeMods[p->tag & 3] | 0x21);
                    if (p->tag == 2)
                    {
                        int r = p->r;
                        if (r == 255)
                        {
                            prim->type = 0x21;
                            prim->color = 0;
                        }
                        else
                        {
                            prim->color = (uint32_t)r << 24;
                        }
                    }
                }

                if (is_back)
                    marni::add_primitive_back(gGameTable.pMarni, (Prim*)prim, z);
                else
                    marni::add_primitive_front(gGameTable.pMarni, (Prim*)prim, z);
                return 1;
            }

            // 0x00402210
            int addScaler(const PrimScaler* p, int z) override
            {
                auto* copy = (PrimScaler*)arena.alloc(sizeof(PrimScaler));
                std::memcpy(copy, p, sizeof(PrimScaler));
                return marni::add_primitive_scaler(gGameTable.pMarni, (Prim*)copy, z);
            }

            // 0x00441370
            int addLineF2(const LineF2* p, int z, int is_back) override
            {
                if (p->x0 < 0)
                    return 0;

                // The line is a flat 2-vertex line primitive (20 bytes, see
                // PrimLine in re2.h).
                auto* prim = (PrimLine*)arena.alloc(sizeof(PrimLine));

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
                return 1;
            }

            // Uploads `image` as a new texture, returning the handle (0 on
            // failure).
            int loadTexture(const Image& image, uint32_t mode) override
            {
                MarniSurface2 surface;
                if (!materializeSurface(&surface, image))
                    return 0;

                int handle = marni::create_texture_handle(gGameTable.pMarni, &surface, mode);
                marni::surface2_release(&surface);
                return handle;
            }

            // 0x00404CE0
            void unloadTexture(int handle) override
            {
                marni::unload_texture(gGameTable.pMarni, handle);
            }

            // 0x00441520
            void unloadAllTextures() override
            {
                marni::result_unload_textures();
            }

        private:
            static void setGeomOffset(int cx, int cy)
            {
                gGameTable.global_cx = (uint32_t)cx;
                gGameTable.global_cy = (uint32_t)cy;
            }

            PrimitiveArena arena;
        };
    }

    // 0x00440250: ResetGeom
    std::unique_ptr<Renderer> g_renderer;

    void initRenderer()
    {
        g_renderer = std::make_unique<LoggingRenderer>(std::make_unique<MarniRenderer>());
    }

    LoggingRenderer::LoggingRenderer(std::unique_ptr<Renderer> inner)
        : inner(std::move(inner))
    {
    }

    void LoggingRenderer::record(DrawKind kind, int z, int page, int16_t x0, int16_t y0, int16_t x1, int16_t y1)
    {
        stats.counts[static_cast<int>(kind)]++;
        auto& slot = stats.log[stats.log_count % DRAW_CALL_LOG_SIZE];
        slot.kind = kind;
        slot.z = z;
        slot.page = page < 0 ? 0xFFFF : (uint16_t)page;
        slot.x0 = x0;
        slot.y0 = y0;
        slot.x1 = x1;
        slot.y1 = y1;
        stats.log_count++;
    }

    void LoggingRenderer::reset()
    {
        stats = {};
        inner->reset();
    }

    int LoggingRenderer::addSprt(const Sprt* p, uint32_t page, int z, int add_back)
    {
        int result = inner->addSprt(p, page, z, add_back);
        record(DrawKind::Sprt, z, (int)page, p->x0, p->y0, (int16_t)(p->x0 + p->w - 1), (int16_t)(p->y0 + p->h - 1));
        return result;
    }

    void LoggingRenderer::addPolyFt4(const PolyFt4* p, int page, int z, int add_back)
    {
        inner->addPolyFt4(p, page, z, add_back);
        record(DrawKind::PolyFt4, z, page, p->x0, p->y0, p->x3, p->y3);
    }

    int LoggingRenderer::addMask(const Sprt* p, int page, int z)
    {
        int result = inner->addMask(p, page, z);
        record(DrawKind::Mask, z, page, p->x0, p->y0, (int16_t)(p->x0 + p->w - 1), (int16_t)(p->y0 + p->h - 1));
        return result;
    }

    int LoggingRenderer::addBgScaled(const BgScaledDesc* bg, int z)
    {
        int result = inner->addBgScaled(bg, z);
        record(DrawKind::BgScaled, z, -1, bg->x, bg->y, (int16_t)(bg->x + bg->w - 1), (int16_t)(bg->y + bg->h - 1));
        return result;
    }

    void LoggingRenderer::addBg()
    {
        inner->addBg();
    }

    void LoggingRenderer::addBg2(int16_t scroll_y)
    {
        inner->addBg2(scroll_y);
    }

    int LoggingRenderer::addScaledSprite(const PolyFt4* prim, int page, int z)
    {
        int result = inner->addScaledSprite(prim, page, z);
        record(DrawKind::ScaledSprite, z, page, prim->x0, prim->y0, (int16_t)(prim->x3 - 1), (int16_t)(prim->y3 - 1));
        return result;
    }

    int LoggingRenderer::addScaledPoly(const PolyFt4* prim, int page, int z)
    {
        int result = inner->addScaledPoly(prim, page, z);
        record(DrawKind::ScaledPoly, z, page, prim->x0, prim->y0, prim->x3, prim->y3);
        return result;
    }

    int LoggingRenderer::addPolyGt4(const PolyGt4* p, int page, int z)
    {
        int result = inner->addPolyGt4(p, page, z);
        record(DrawKind::PolyGt4, z, page, p->x0, p->y0, p->x3, p->y3);
        return result;
    }

    int LoggingRenderer::addPolyFt42(const PolyFt4* p, int page, int z)
    {
        int result = inner->addPolyFt42(p, page, z);
        record(DrawKind::PolyFt4_2, z, page, p->x0, p->y0, p->x3, p->y3);
        return result;
    }

    int LoggingRenderer::addPolyF4(const Tile* p, int z, int is_back)
    {
        int result = inner->addPolyF4(p, z, is_back);
        record(
            DrawKind::PolyF4,
            z,
            -1,
            (int16_t)p->x0,
            (int16_t)p->y0,
            (int16_t)(((uint16_t)p[1].r | ((uint16_t)p[1].g << 8)) - 1),
            (int16_t)(((uint16_t)p[1].b | ((uint16_t)p[1].code << 8)) - 1));
        return result;
    }

    int LoggingRenderer::addTile(const Tile* p, int z, int is_back)
    {
        int result = inner->addTile(p, z, is_back);
        record(DrawKind::Tile, z, -1, p->x0, p->y0, (int16_t)(p->x0 + p->w - 1), (int16_t)(p->y0 + p->h - 1));
        return result;
    }

    int LoggingRenderer::addLineF2(const LineF2* p, int z, int is_back)
    {
        int result = inner->addLineF2(p, z, is_back);
        record(DrawKind::LineF2, z, -1, p->x0, p->y0, p->x1, p->y1);
        return result;
    }

    int LoggingRenderer::addScaler(const PrimScaler* p, int z)
    {
        int result = inner->addScaler(p, z);
        record(DrawKind::Scaler, z, -1, 0, 0, 0, 0);
        return result;
    }

    int LoggingRenderer::loadTexture(const Image& image, uint32_t mode)
    {
        return inner->loadTexture(image, mode);
    }

    void LoggingRenderer::unloadTexture(int handle)
    {
        inner->unloadTexture(handle);
    }

    void LoggingRenderer::unloadAllTextures()
    {
        inner->unloadAllTextures();
    }

    const DrawStats& LoggingRenderer::drawStats() const
    {
        return stats;
    }
}
