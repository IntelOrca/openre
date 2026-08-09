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

                auto* prim = (MarniPrim*)arena.alloc(0x20);
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
                    prim[1].pNext = (Prim*)(uintptr_t)((p->r << 16) | (p->g << 8) | p->b);
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

                    auto* prim = (MarniPrim*)arena.alloc(0x20);
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
                        prim[1].pNext = (Prim*)(uintptr_t)((uint8_t)p->b0 | (v6 << 8));
                        if ((p->code & 2) != 0)
                        {
                            prim->type = (int32_t)(s_sprtTypeMods[p->tag & 7] | 0x25);
                            if ((p->tag & 7) == 4)
                            {
                                int v7 = 2 * (uint8_t)p->r0 - 1;
                                if (v7 < 0)
                                    v7 = 0;
                                prim[1].pNext = (Prim*)(uintptr_t)(((uint32_t)v7 << 24) | 0x808080);
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

                auto* prim = (MarniPrim*)arena.alloc(0x24);

                // Textured sprite primitive (PSX SPRT).
                prim->type = 0x1002C; // 65580

                if (p->code & 2)
                {
                    prim->type = 0x1002D; // 65581
                    prim->type = (int32_t)(s_sprtTypeMods[p->tag & 3] | 0x1002D);
                    // Colour packed as b | (g << 8) | (r << 16) into the tail dword.
                    *(uint32_t*)((uint8_t*)prim + 0x20) = (uint32_t)p->b | ((uint32_t)p->g << 8) | ((uint32_t)p->r << 16);
                }

                prim->x1 = p->x0;
                prim->y1 = p->y0;
                // Bottom-right corner packed into the u0/v0 and u1/v1 words.
                *(uint16_t*)&prim->u0 = (uint16_t)(p->x0 + p->w - 1);
                *(uint16_t*)&prim->u1 = (uint16_t)(p->y0 + p->h - 1);

                // Second UV pair in the tail bytes at 0x1C..0x1F.
                *(uint8_t*)((uint8_t*)prim + 0x1C) = p->u0;
                *(uint8_t*)((uint8_t*)prim + 0x1D) = p->v0;
                *(uint8_t*)((uint8_t*)prim + 0x1E) = (uint8_t)(p->u0 + (uint8_t)p->w - 1);
                *(uint8_t*)((uint8_t*)prim + 0x1F) = (uint8_t)(p->v0 + (uint8_t)p->h - 1);

                prim->texture = gGameTable.texture_pages[page].handle;
                prim->clut = clut;

                // The z value is clamped to the projection plane before being stored.
                if (z >= gGameTable.global_prj / 2)
                    *(float*)&prim->x0 = (float)z;
                else
                    *(float*)&prim->x0 = (float)(gGameTable.global_prj / 2);

                marni::add_primitive_scaler(gGameTable.pMarni, (Prim*)prim, z >> 4);
                return 1;
            }

            // 0x00440950
            // AddBgScaled - draws a full-screen scaled background quad (bg0)
            // using the shared scratch buffer. `bg` points at a descriptor whose
            // interesting fields are x@+8 (i16), y@+0A (i16), u@+0C (u8), v@+0D
            // (u8), w@+10 (i16), h@+12 (i16). `z` is the draw depth; it is stored
            // as a float scale and is also right-shifted by 4 for the OT z-order.
            int addBgScaled(int bg, int z) override
            {
                if (gGameTable.bg_tex0 == 0)
                    return 0;

                auto* prim = (MarniPrim*)arena.alloc(0x20);

                prim->type = 0x1002C; // 65580: scaled full-screen background sprite

                prim->x1 = *(int16_t*)(bg + 8);
                prim->y1 = *(int16_t*)(bg + 0x0A);

                // Pack (x + w - 1) into the u0/v0 word and (y + h - 1) into the
                // u1/v1 word (16-bit arithmetic, matching the binary).
                *(int16_t*)&prim->u0 = *(int16_t*)(bg + 8) + *(int16_t*)(bg + 16) - 1;
                *(int16_t*)&prim->u1 = *(int16_t*)(bg + 0x0A) + *(int16_t*)(bg + 18) - 1;

                // The remaining four bytes past the MarniPrim record (offset
                // 0x1C..0x1F) hold the source u/v quad (u, v, u + w - 1, v + h - 1)
                // as bytes.
                uint8_t* tail = (uint8_t*)prim + 0x1C;
                tail[0] = *(uint8_t*)(bg + 12);
                tail[1] = *(uint8_t*)(bg + 13);
                tail[2] = *(uint8_t*)(bg + 12) + *(uint8_t*)(bg + 16) - 1;
                tail[3] = *(uint8_t*)(bg + 13) + *(uint8_t*)(bg + 18) - 1;

                // Store the z depth as a float scale over the x0/y0 fields.
                *(float*)&prim->x0 = (float)z;
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
            int addScaledSprite(int prim, int page, int z) override
            {
                const PolyFt4* p = (const PolyFt4*)prim;

                if (page >= 41)
                    return 0;
                if (gGameTable.texture_pages[page].handle == 0)
                    return 0;
                if (gGameTable.texture_pages[page].suspended == 1)
                    return 0;

                uint16_t clut = (uint16_t)p->clut;
                if (clut >= gGameTable.texture_pages[page].clutCount)
                    clut = 0;

                auto* out = (MarniPrim*)arena.alloc(0x24);
                uint8_t* tail = (uint8_t*)out + 0x1C;

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
                    *(uint32_t*)&tail[4] = (uint32_t)((p->r0 << 16) | (p->g0 << 8) | p->b0);
                }

                if ((p->code & 2) != 0)
                {
                    out->type |= s_sprtTypeMods[((uint8_t)p->tpage >> 5) & 3];
                }

                out->x1 = p->x0;
                out->y1 = p->y0;

                // Texture region size (16-bit words) minus one, packed into the
                // u0/v0 and u1/v1 words.
                *(uint16_t*)&out->u0 = (uint16_t)(p->x3 - 1);
                *(uint16_t*)&out->u1 = (uint16_t)(p->y3 - 1);

                // The four bytes past the MarniPrim record (offset 0x1C..0x1F)
                // hold the source u/v quad (u0, v0, u3 - 1, v3 - 1) as bytes.
                tail[0] = p->u0;
                tail[1] = p->v0;
                tail[2] = (uint8_t)(p->u3 - 1);
                tail[3] = (uint8_t)(p->v3 - 1);

                out->texture = gGameTable.texture_pages[page].handle;

                // Store the z depth as a float scale over the x0/y0 fields.
                *(float*)&out->x0 = (float)z;
                out->clut = clut;

                marni::add_primitive_scaler(gGameTable.pMarni, (Prim*)out, z >> 4);
                return 1;
            }

            // 0x00440B70
            // AddPrimitiveScaler path for a POLY_FT4 quad. The four vertex x/y
            // positions are packed into the MarniPrim head and the remaining
            // vertex data (x3,y3 + the per-vertex u/v) plus the z depth are
            // packed into the 0x1C-byte tail that follows the MarniPrim record.
            int addScaledPoly(int prim, int page, int z) override
            {
                const PolyFt4* p = (const PolyFt4*)prim;

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

                auto* out = (MarniPrim*)arena.alloc(0x30);
                uint8_t* tail = (uint8_t*)out + 0x1C;

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
                    *(int16_t*)&out->u0 = p->x2;
                    *(int16_t*)&out->u1 = p->y2;
                    *(int16_t*)&tail[0] = p->x3;        // LOWORD(out[1].pNext)
                    *(int16_t*)&tail[2] = p->y3;        // HIWORD(out[1].pNext)
                    *(uint16_t*)&tail[4] = (uint16_t)z; // LOWORD(out[1].type)
                    tail[6] = p->u0;                    // BYTE2(out[1].type)
                    tail[7] = p->v0;                    // HIBYTE(out[1].type)
                    tail[8] = p->u1;                    // LOBYTE(out[1].pTexture)
                    tail[9] = p->v1;                    // BYTE1(out[1].pTexture)
                    tail[10] = p->u2;                   // BYTE2(out[1].pTexture)
                    tail[11] = p->v2;                   // HIBYTE(out[1].pTexture)
                    tail[12] = p->u3;                   // LOBYTE(out[1].clut)
                    tail[13] = p->v3;                   // BYTE1(out[1].clut)
                    out->texture = gGameTable.texture_pages[page].handle;
                    out->clut = clut;
                }
                else
                {
                    // Flat-shaded quad: 0x1004D (POLY_GT4-scaler) primitive with
                    // the packed RGB colour written into the colour tail.
                    out->type = 0x1004D;
                    *(uint32_t*)&tail[0x10] = (uint32_t)((p->r0 << 16) | (p->g0 << 8) | p->b0);
                    if ((p->code & 2) != 0)
                        out->type = (int32_t)(s_polyTypeMods[((uint8_t)p->tpage >> 5) & 3] | 0x1004D);

                    out->x0 = p->x0;
                    out->y0 = p->y0;
                    out->x1 = p->x1;
                    out->y1 = p->y1;
                    *(int16_t*)&out->u0 = p->x2;
                    *(int16_t*)&out->u1 = p->y2;
                    *(int16_t*)&tail[0] = p->x3;        // LOWORD(out[1].pNext)
                    *(int16_t*)&tail[2] = p->y3;        // HIWORD(out[1].pNext)
                    *(uint16_t*)&tail[4] = (uint16_t)z; // LOWORD(out[1].type)
                    tail[6] = p->u0;                    // BYTE2(out[1].type)
                    tail[7] = p->v0;                    // HIBYTE(out[1].type)
                    tail[8] = p->u1;                    // LOBYTE(out[1].pTexture)
                    tail[9] = p->v1;                    // BYTE1(out[1].pTexture)
                    tail[10] = p->u2;                   // BYTE2(out[1].pTexture)
                    tail[11] = p->v2;                   // HIBYTE(out[1].pTexture)
                    tail[12] = p->u3;                   // LOBYTE(out[1].clut)
                    tail[13] = p->v3;                   // BYTE1(out[1].clut)
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

                auto* prim = (MarniPrim*)arena.alloc(0x38);

                prim->type = 70; // 0x46: gouraud textured quad (GT4)

                // Pack the four vertex colours into the colour tail as
                // b | (g << 8) | (r << 16), one dword per vertex.
                *(uint32_t*)&prim[1].clut = (uint32_t)b0 | ((uint32_t)g0 << 8) | ((uint32_t)r0 << 16);
                *(uint32_t*)&prim[1].x0 = (uint32_t)b1 | ((uint32_t)g1 << 8) | ((uint32_t)r1 << 16);
                *(uint32_t*)&prim[1].x1 = (uint32_t)b2 | ((uint32_t)g2 << 8) | ((uint32_t)r2 << 16);
                *(uint32_t*)&prim[1].u0 = (uint32_t)b3 | ((uint32_t)g3 << 8) | ((uint32_t)r3 << 16);

                if ((p->code & 2) != 0)
                    prim->type |= (int32_t)s_polyTypeMods[p->tag & 3];

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

                auto* prim = (MarniPrim*)arena.alloc(0x2C);

                prim->type = 69;
                const uint8_t r0 = p->r0 > 0x80 ? 0x80 : p->r0;
                const uint8_t g0 = p->g0 > 0x80 ? 0x80 : p->g0;
                const uint8_t b0 = p->b0 > 0x80 ? 0x80 : p->b0;
                // Colour packed into the second prim's clut: b | (g << 8) | (r << 16).
                prim[1].clut = ((uint32_t)r0 << 16) | ((uint32_t)g0 << 8) | b0;
                if ((p->code & 2) != 0)
                    prim->type |= s_polyTypeMods[p->tag & 3];

                // Geometry: x1/y1 pair and x2/y2 (packed over the u0/v0 and
                // u1/v1 byte slots of the first prim) plus the x3/y3 pair
                // (packed into the second prim's pNext word slots).
                prim->x0 = p->x0;
                prim->x1 = p->x1;
                *(uint16_t*)&prim->u0 = p->x2;
                *(uint16_t*)&prim[1].pNext = p->x3;
                prim->y0 = p->y0;
                prim->y1 = p->y1;
                *(uint16_t*)&prim->u1 = p->y2;
                *((uint16_t*)&prim[1].pNext + 1) = p->y3;
                // UV coordinates are packed into the second prim's type/texture fields.
                prim[1].type = (uint32_t)p->u0 | ((uint32_t)p->v0 << 8) | ((uint32_t)p->u1 << 16) | ((uint32_t)p->v1 << 24);
                prim[1].texture = (uint32_t)p->u2 | ((uint32_t)p->v2 << 8) | ((uint32_t)p->u3 << 16) | ((uint32_t)p->v3 << 24);

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
                auto* prim = (MarniPrim*)arena.alloc(0x14);

                prim->type = 33;
                // x0/y0 packed into the texture field (PSX-style POLY_F4).
                prim->texture = ((uint32_t)(uint16_t)p->y0 << 16) | (uint16_t)p->x0;
                // x1/y1 come from the word following the tile (r|g and b|code of
                // the next Tile at +0x14/+0x16), each decremented by one.
                *(uint16_t*)&prim->clut = *(uint16_t*)&p[1].r - 1;
                *((uint16_t*)&prim->clut + 1) = *(uint16_t*)&p[1].b - 1;
                // Colour: b | (g << 8) | (r << 16).
                *(uint32_t*)&prim->x0 = (uint8_t)p->b | ((uint8_t)p->g << 8) | ((uint8_t)p->r << 16);

                if ((p->code & 2) != 0)
                {
                    prim->type = s_tileTypeMods[p->tag & 3] | 0x21;
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
                return 1;
            }

            // 0x00441270
            int addTile(const Tile* p, int z, int is_back) override
            {
                auto* prim = (MarniPrim*)arena.alloc(0x14);

                prim->type = 0x21; // 33: flat untextured tile

                // Pack the top-left corner into the texture field (LOWORD/HIWORD).
                *(uint16_t*)&prim->texture = (uint16_t)p->x0;
                *((uint16_t*)&prim->texture + 1) = (uint16_t)p->y0;

                // Pack the bottom-right corner (x0+w-1, y0+h-1) into the second
                // dword (the CLUT slot, repurposed as scratch space here).
                *(uint16_t*)&prim->clut = (uint16_t)(p->x0 + p->w - 1);
                *((uint16_t*)&prim->clut + 1) = (uint16_t)(p->y0 + p->h - 1);

                // Colour packed as b | (g << 8) | (r << 16) over the x0/y0/x1/y1 fields.
                *(uint32_t*)&prim->x0 = (uint32_t)p->b | ((uint32_t)p->g << 8) | ((uint32_t)p->r << 16);

                if (p->code & 2)
                {
                    prim->type = (int32_t)(s_tileTypeMods[p->tag & 3] | 0x21);
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
                return 1;
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

    int LoggingRenderer::addBgScaled(int bg, int z)
    {
        int result = inner->addBgScaled(bg, z);
        record(
            DrawKind::BgScaled,
            z,
            -1,
            *(int16_t*)(bg + 8),
            *(int16_t*)(bg + 0x0A),
            (int16_t)(*(int16_t*)(bg + 8) + *(int16_t*)(bg + 16) - 1),
            (int16_t)(*(int16_t*)(bg + 0x0A) + *(int16_t*)(bg + 18) - 1));
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

    int LoggingRenderer::addScaledSprite(int prim, int page, int z)
    {
        int result = inner->addScaledSprite(prim, page, z);
        const PolyFt4* p = (const PolyFt4*)prim;
        record(DrawKind::ScaledSprite, z, page, p->x0, p->y0, (int16_t)(p->x3 - 1), (int16_t)(p->y3 - 1));
        return result;
    }

    int LoggingRenderer::addScaledPoly(int prim, int page, int z)
    {
        int result = inner->addScaledPoly(prim, page, z);
        const PolyFt4* p = (const PolyFt4*)prim;
        record(DrawKind::ScaledPoly, z, page, p->x0, p->y0, p->x3, p->y3);
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
            (int16_t)(*(uint16_t*)&p[1].r - 1),
            (int16_t)(*(uint16_t*)&p[1].b - 1));
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
