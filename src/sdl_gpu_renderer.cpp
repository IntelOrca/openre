// SDL3/GPU-only renderer for the SDL-only build.
//
// Implements the full Renderer interface (marni_renderer.h) directly on top
// of the SDL3 GPU API. No legacy backend replay: the
// only integration points are the raw SDL_GPU accessors in system_gpu.h
// (device/window/guest framebuffer) and the MARNI ordering tables that the
// decompiled game code shares with the original binary.
//
// draw() walks the ordering tables (otag[3] backgrounds, otag[1] objects,
// otag[0] front text) exactly like the original trans_priority_list and
// decodes each primitive into transformed/lighted (TL) vertices using the same math as
// sub_40CFD0/sub_40D300/sub_40D560/... and submits them with SDL_GPU into
// the system_gpu-owned guest framebuffer. flip() presents the framebuffer
// into the swapchain (letterboxed).
//
// The decoders are replicated 1:1 (colour doubling with specular overflow,
// the 0x100000..0x400000 blend-mode folds, the sub_40E6E0 pixel snap, the
// perspective projection math, the trans_object 3D character pipeline with
// per-normal lighting and split-table CLUT sections). Primitive types with
// no decoder (trans_matrix scalers) are logged and skipped - never killing
// is_gpu_active like the original would.

#include "sdl_gpu_renderer.h"

#include "logger.h"
#include "marni.h"
#include "openre.h"
#include "re2.h"
#include "system_config.h"
#include "system_gpu.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace openre::marni
{
namespace
{
    // ── blend-mode constants (marni_renderer.cpp) ────────────────────────
    constexpr uint32_t kBlendAverage = 0x100000;
    constexpr uint32_t kBlendAdd = 0x200000;
    constexpr uint32_t kBlendSubtract = 0x300000;
    constexpr uint32_t kBlendQuarter = 0x400000;

    // Semitransparency modifiers ORed into the primitive type by the Add*
    // methods (s_sprtTypeMods/s_polyTypeMods/s_tileTypeMods in
    // marni_renderer.cpp). Duplicated here because those tables live inside
    // the MarniRenderer.
    const uint32_t s_sprtTypeMods[8] = {
        kBlendAverage, kBlendAdd, kBlendAverage, kBlendAverage,
        kBlendQuarter, kBlendAverage, kBlendAverage, kBlendAverage,
    };
    const uint32_t s_polyTypeMods[4] = { kBlendAverage, kBlendAdd, kBlendAverage, kBlendSubtract };
    const uint32_t s_tileTypeMods[4] = { kBlendAverage, kBlendAdd, kBlendQuarter, kBlendSubtract };

    // ── TL vertex (matches kTLVertexStride in system_gpu.cpp, 32 bytes) ──
    // Position (sx, sy, sz) in pixels with a top-left origin; the vertex
    // shader converts to NDC using the gViewport uniform. The colour dword is
    // packed RGBA color (little-endian byte order B, G, R, A) - UBYTE4_NORM at offset 16.
    struct TlVertex
    {
        float sx;       // 0x00
        float sy;       // 0x04
        float sz;       // 0x08 (z in [0,1], 1 = farthest)
        float rhw;      // 0x0C (unused by the shaders)
        uint8_t b;      // 0x10
        uint8_t g;      // 0x11
        uint8_t r;      // 0x12
        uint8_t a;      // 0x13
        uint8_t pad[4]; // 0x14 (specular - dropped, see open questions)
        float tu;       // 0x18
        float tv;       // 0x1C
    };
    static_assert(sizeof(TlVertex) == 0x20, "TL vertex must be 32 bytes");
    static_assert(offsetof(TlVertex, sx) == 0x00, "position at 0");
    static_assert(offsetof(TlVertex, b) == 0x10, "colour at 16");
    static_assert(offsetof(TlVertex, tu) == 0x18, "texcoord at 24");

    // ── Growing scratch arena for MARNI primitives ───────────────────────
    // Records are carved from fixed-size blocks that are appended (never
    // moved or reallocated), so pointers already handed to marni's ordering
    // tables stay valid even when the arena grows. Reset at frame start.
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

    // ── Concrete MARNI primitive record layouts ──────────────────────────
    // Identical to the records in marni_renderer.cpp (they are guarded out of
    // that file under the SDL-only build guard, so they are duplicated here). Each Add*
    // method carves one of these out of the arena; the head matches the
    // MarniPrim (PrimSprite, 0x1C bytes) layout.
    using MarniPrim = PrimSprite;

    // SPRT-shaped record (addSprt / addPolyFt4, 0x20 bytes).
    struct MarniSprt : MarniPrim
    {
        uint32_t color; // 0x1C flat-shade colour b | (g << 8) | (r << 16)
    };
    static_assert(sizeof(MarniSprt) == 0x20);

    // Mask sprite record (addMask, 0x24 bytes).
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
        uint16_t cornerX; // 0x18 bottom-right x = x0 + w - 1
        uint16_t cornerY; // 0x1A bottom-right y = y0 + h - 1
        uint8_t su0;      // 0x1C source u0
        uint8_t sv0;      // 0x1D source v0
        uint8_t su1;      // 0x1E u0 + w - 1
        uint8_t sv1;      // 0x1F v0 + h - 1
        uint32_t color;   // 0x20 shaded colour tail (0x1002D variant only)
    };
    static_assert(sizeof(MarniMask) == 0x24);

    // Scaled background record (addBgScaled, 0x20 bytes).
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
        int16_t cornerX; // 0x18 bottom-right x = x + w - 1
        int16_t cornerY; // 0x1A bottom-right y = y + h - 1
        uint8_t su;      // 0x1C source u
        uint8_t sv;      // 0x1D source v
        uint8_t su1;     // 0x1E u + w - 1
        uint8_t sv1;     // 0x1F v + h - 1
    };
    static_assert(sizeof(MarniBgScaled) == 0x20);

    // Scaled sprite record (addScaledSprite, 0x24 bytes).
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
        uint16_t sizeX; // 0x18 scaled width = x3 - 1
        uint16_t sizeY; // 0x1A scaled height = y3 - 1
        uint8_t su0;    // 0x1C source u0
        uint8_t sv0;    // 0x1D source v0
        uint8_t su1;    // 0x1E u3 - 1
        uint8_t sv1;    // 0x1F v3 - 1
        uint32_t color; // 0x20 shaded colour tail (0x1002D variant only)
    };
    static_assert(sizeof(MarniScaledSprite) == 0x24);

    // Scaled quad record (addScaledPoly, 0x30 bytes).
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
        int16_t x2;       // 0x18
        int16_t y2;       // 0x1A
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

    // Gouraud quad record (addPolyGt4, 0x38 bytes).
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
        int16_t x2;       // 0x18
        int16_t y2;       // 0x1A
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

    // Front-OT quad record (addPolyFt42, 0x2C bytes).
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
        int16_t x2;       // 0x18
        int16_t y2;       // 0x1A
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

    // Flat untextured quad record (addPolyF4, 0x14 bytes).
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

    // Flat tile record (addTile, 0x14 bytes).
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

    // ── renderer-side state ──────────────────────────────────────────────

    // Texture registry entry: one SDL_GPU texture per CLUT for a MARNI
    // texture handle. Paletted images with palCnt > 1 get one texture per
    // palette (all same width/height, so UVs stay valid); `texture` is the
    // clut-0 variant. Mirrors the texture object model where mode
    // 0x22/0x41/0x42/0xC1/0xC2 allocates pal_count texture nodes.
    struct TextureEntry
    {
        SDL_GPUTexture* texture = nullptr;       // clut-0 texture
        std::vector<SDL_GPUTexture*> clutTextures; // clut-1..N textures (same dims)
        int width = 0;
        int height = 0;
        int clutCount = 1;                       // number of CLUT variants uploaded
        bool hasAlpha = false;    // decoded pixels include transparent (black-keyed) ones -> v40
        bool noAlphaFlag = false; // mode bit 0x4 set -> v41 = 0 -> v32 && v40 blend fallback disabled

        // Selects the texture for a CLUT index, clamping out-of-range indexes
        // back to clut 0 (the Add* methods already clamp to clutCount).
        SDL_GPUTexture* textureForClut(int clut) const
        {
            if (clut <= 0 || clutTextures.empty())
                return texture;
            if (clut >= (int)clutTextures.size() + 1)
                return texture;
            return clutTextures[clut - 1];
        }
    };

    // Blend-mode variants (LABEL_74 in the original: the 0xF00000 type bits
    // select the blend pair; anything else falls through to the
    // v32 && v40 alpha path or no blending at all).
    enum class BlendSel : uint8_t
    {
        None = 0,   // no blending
        Alpha = 1,  // SRCALPHA / INVSRCALPHA
        Add = 2,    // SRCALPHA / ONE
        SrcColor = 3, // SRCCOLOR / SRCCOLOR (0x700000; prototype maps to its own pipeline)
    };

    // One recorded CPU-side draw call: a run of vertices in the per-frame
    // vertex buffer with a pipeline key + optional texture/sampler.
    struct DrawCall
    {
        uint32_t key = 0; // see makePipelineKey()
        SDL_GPUTexture* texture = nullptr; // null for untextured draws
        SDL_GPUSampler* sampler = nullptr;
        uint32_t firstVertex = 0; // in vertices
        uint32_t vertexCount = 0;
    };

    struct ParseStats
    {
        int primsBg = 0;  // prims examined in otag[3]
        int primsObj = 0; // prims examined in otag[1]
        int primsFg = 0;  // prims examined in otag[0]
        int drawn = 0;    // vertices emitted
        int skipped = 0;  // prims skipped (unsupported types / bad texture)
        int lines = 0;
    };

    // ── small helpers ────────────────────────────────────────────────────

    static std::string hexStr(uint32_t v)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%X", v);
        return std::string(buf);
    }

    // Log the first few occurrences, then every `every`th (keeps the log
    // readable for per-frame repeated conditions).
    static bool throttle(uint64_t& counter, uint64_t every = 500)
    {
        counter++;
        return counter <= 5 || (counter % every) == 0;
    }

    static void setColor(TlVertex& v, uint32_t packedColor)
    {
        // packed RGBA little-endian byte order is B, G, R, A.
        v.b = (uint8_t)(packedColor & 0xFF);
        v.g = (uint8_t)((packedColor >> 8) & 0xFF);
        v.r = (uint8_t)((packedColor >> 16) & 0xFF);
        v.a = (uint8_t)((packedColor >> 24) & 0xFF);
    }

    // Doubles the B/G/R channels, clamps at 255 and folds the excess into
    // the specular value. The 0x100000..0x400000 blend-mode bits pick the
    // alpha byte exactly like the original decoders (0x100000/0x200000 ->
    // 0x80, 0x300000 -> 0x40, 0x400000 -> the primitive's own alpha byte,
    // anything else -> 0xFF).
    struct FoldedColor
    {
        uint32_t color;   // packed RGBA (B,G,R,A)
        uint32_t specular; // channel overflow (dropped; no specular shader input)
        bool overflow;
    };

    static FoldedColor foldColor(const Marni* m, uint32_t type, uint8_t bByte, uint8_t gByte, uint8_t rByte, uint8_t aByte)
    {
        uint32_t b = 2u * bByte;
        uint32_t g = 2u * gByte;
        uint32_t r = 2u * rByte;
        uint32_t ovfB = 0, ovfG = 0, ovfR = 0;
        bool overflow = false;
        if (r >= 0x100)
        {
            overflow = true;
            ovfR = r - 0x100;
            r = 0xFF;
        }
        if (g >= 0x100)
        {
            overflow = true;
            ovfG = g - 0x100;
            g = 0xFF;
        }
        if (b >= 0x100)
        {
            overflow = true;
            ovfB = b - 0x100;
            b = 0xFF;
        }

        // Fold the mode bits into the red/alpha dword (bytes 2-3 of the
        // packed RGBA): redAlpha = R | (A << 8).
        uint32_t redAlpha = r;
        const uint32_t mode = type & 0xF00000;
        const bool hasFlag = (m->gpu_flag & 0x4000) != 0;
        if (mode == 0x400000)
        {
            redAlpha |= (uint32_t)aByte << 8;
        }
        else if (mode == 0x100000 || (hasFlag && mode == 0x200000))
        {
            redAlpha |= 0xFFFF8000;
        }
        else if (mode == 0x300000)
        {
            redAlpha |= 0x4000;
        }
        else
        {
            redAlpha |= 0xFFFFFF00;
        }

        const uint32_t color = b | ((g | (redAlpha << 8)) << 8);
        const uint32_t specular = overflow ? (ovfB | ((ovfG | (ovfR << 8)) << 8)) : 0;
        return { color, specular, overflow };
    }

    // Blend selection (LABEL_74): the 0xF00000 type bits select the pair;
    // otherwise fall through to the v32 && v40 surface-alpha path or no
    // blending. v40 mirrors the surface alpha flag (hasAlpha); v32 requires
    // the texture node to carry no alpha flag (mode bit 0x4 clear), which is
    // folded into the v40 argument at the call sites.
    static BlendSel selectBlend(const Marni* m, uint32_t type, bool v40)
    {
        const uint32_t v33 = type & 0xF00000;
        if (v33 > 0x400000)
        {
            if (v33 == 0x600000)
                return BlendSel::Alpha;
            if (v33 == 0x700000)
                return BlendSel::SrcColor;
        }
        else
        {
            if (v33 == 0x400000 || v33 == 0x100000)
                return BlendSel::Alpha;
            if (v33 == 0x200000 || v33 == 0x300000)
                return BlendSel::Add;
        }
        if (v40 && (type & 0x10000000) == 0)
            return BlendSel::Alpha;
        return BlendSel::None;
    }

    // Pipeline key: textured | blend(2) | depthWrite | depthTest.
    static uint32_t makePipelineKey(bool textured, BlendSel blend, bool depthWrite, bool depthTest)
    {
        uint32_t k = 0;
        if (textured)
            k |= 1u;
        k |= ((uint32_t)blend & 3u) << 1;
        if (depthWrite)
            k |= 1u << 3;
        if (depthTest)
            k |= 1u << 4;
        return k;
    }

    // ── per-frame parse state ────────────────────────────────────────────
    struct DecodeEnv
    {
        Marni* marni = nullptr;
        const std::unordered_map<int, TextureEntry>* textures = nullptr;
        std::vector<uint8_t>* vertexData = nullptr;
        std::vector<DrawCall>* calls = nullptr;
        SDL_GPUSampler* sampler = nullptr;
        // Sampler for 3D character models (trans_object): the original uses
        // set_filtering(self, 1) there (LINEAR when bilinear is enabled),
        // while sprite/bg prims always sample NEAREST (filter=0).
        SDL_GPUSampler* charSampler = nullptr;
        ParseStats* stats = nullptr;
        uint64_t* logMissingTexture = nullptr;
        uint64_t* logTextureFallback = nullptr;
        uint64_t* logClut = nullptr;
        uint64_t* logMovieTex = nullptr;
        uint64_t* logNoDecoder = nullptr;
    };

    static TlVertex* allocVertices(DecodeEnv& env, uint32_t count)
    {
        const size_t base = env.vertexData->size();
        env.vertexData->resize(base + (size_t)count * sizeof(TlVertex));
        return (TlVertex*)(env.vertexData->data() + base);
    }

    static void emitQuad(DecodeEnv& env, TlVertex* verts, uint32_t count, bool textured, BlendSel blend, bool depthWrite, SDL_GPUTexture* texture)
    {
        // Copy the (stack-local) decoder vertices into the per-frame arena so
        // the vertex upload has real data and firstVertex is a valid offset.
        const size_t base = env.vertexData->size();
        const size_t byteCount = (size_t)count * sizeof(TlVertex);
        env.vertexData->resize(base + byteCount);
        std::memcpy(env.vertexData->data() + base, verts, byteCount);

        DrawCall dc;
        dc.key = makePipelineKey(textured, blend, depthWrite, true);
        dc.texture = texture;
        dc.sampler = env.sampler;
        dc.firstVertex = (uint32_t)(base / sizeof(TlVertex));
        dc.vertexCount = count;
        env.calls->push_back(dc);
        env.stats->drawn += (int)count;
    }

    // ── decoders (mirror the marni.cpp originals 1:1) ────────────────────

    // sub_40E6E0: pixel-centre snap + texture-coordinate rearrangement.
    static void snapQuad(TlVertex* v)
    {
        const float tu0 = v[0].tu;
        const float tv0 = v[0].tv;
        const float tu3 = v[3].tu;
        v[2].tv = v[3].tv;
        v[0].sx -= 0.5f;
        v[0].sy -= 0.5f;
        v[1].sx -= 0.5f;
        v[1].sy -= 0.5f;
        v[1].tu = tu3;
        v[1].tv = tv0;
        v[2].sx -= 0.5f;
        v[2].sy -= 0.5f;
        v[2].tu = tu0;
        v[3].sx -= 0.5f;
        v[3].sy -= 0.5f;
    }

    // Resolves the SDL_GPU texture for a primitive. Mirrors the original's
    // texture selection in trans_spr_poly: `type & 4` picks prim->texture,
    // otherwise dword_6449BC (the "current" texture). dword_6449BC is never
    // created (it is a 16x16 white texture made in init), so we fall back to
    // prim->texture, which is what the MarniRenderer
    // Add* methods packed from texture_pages[page].handle.
    static const TextureEntry* resolveTexture(DecodeEnv& env, const Prim* prim)
    {
        const uint32_t type = (uint32_t)prim->type;
        // Texture/clut live at 0x08/0x0C for every sprite-family prim (the
        // base Prim struct only carries pNext/type).
        const auto* sprite = (const PrimSprite*)prim;
        int handle = 0;
        if ((type & 4) != 0)
            handle = (int)sprite->texture;
        else if ((env.marni->gpu_flag & GpuFlags::PER_PRIM_TEXTURE) == 0)
            handle = (int)gGameTable.dword_6449BC;

        auto inRegistry = [&](int h) {
            auto it = env.textures->find(h);
            return it != env.textures->end() && it->second.texture != nullptr;
        };

        if (handle == 0 || !inRegistry(handle))
        {
            // Prototype fallback: prim->texture.
            const int alt = (int)sprite->texture;
            if (alt != 0 && inRegistry(alt))
            {
                if (throttle(*env.logTextureFallback, 300))
                    logging::logDebug(
                        "[sdlgpu] prim type 0x{}: handle {} (dword_6449BC={}) not in registry, falling back to prim->texture {}",
                        hexStr(type), handle, (uint32_t)gGameTable.dword_6449BC, alt);
                handle = alt;
            }
            else
            {
                if (throttle(*env.logMissingTexture, 200))
                    logging::logWarning(
                        "[sdlgpu] no GPU texture for handle {} (type 0x{}, prim->texture {}) - skipping prim",
                        handle, hexStr(type), (uint32_t)sprite->texture);
                return nullptr;
            }
        }

        if (handle < 0 || handle >= 256)
            return nullptr;

        const MarniTexture* tex = &env.marni->textures[handle];
        if ((tex->var_00 & 0x4000) != 0)
        {
            // Temp / movie texture (SOFTWARE_GPU path): registered but empty.
            if (throttle(*env.logMovieTex, 300))
                logging::logDebug("[sdlgpu] prim references temp/movie texture {} (var_00=0x{}) - skipping", handle, hexStr(tex->var_00));
            return nullptr;
        }

        if (sprite->clut != 0 && (tex->var_00 & 0x82) != 0)
        {
            // Paletted texture (var_00 low bits 0x82/0x42/0xA2) - check the
            // clut index is within the uploaded CLUT count.
            auto clutIt = env.textures->find(handle);
            const int clutCount = (clutIt != env.textures->end()) ? clutIt->second.clutCount : 1;
            if (sprite->clut >= (uint32_t)clutCount)
            {
                if (throttle(*env.logClut, 300))
                    logging::logWarning("[sdlgpu] prim uses clut {} on handle {} (clutCount {}) - out of range, falling back to clut 0", sprite->clut, handle, clutCount);
            }
        }

        auto it = env.textures->find(handle);
        return it != env.textures->end() ? &it->second : nullptr;
    }

    // Selects the per-CLUT SDL_GPU texture for a sprite-family prim. Every
    // textured MARNI record carries texture at 0x08 and clut at 0x0C, so we
    // can read the CLUT index directly off the prim head.
    static SDL_GPUTexture* spriteClutTexture(const TextureEntry& entry, const Prim* pPrim)
    {
        const auto* spr = (const PrimSprite*)pPrim;
        return entry.textureForClut((int)spr->clut);
    }

    // sub_40CFD0 (0x1002D): float-z shaded sprite quad.
    static void decode0x1002D(DecodeEnv& env, const Prim* pPrim, const TextureEntry& entry)
    {
        const auto* prim = (const uint8_t*)pPrim;
        const int32_t type = *(const int32_t*)(prim + 4);
        const float z = *(const float*)(prim + 16);
        if (z == 0.0f)
            return;
        const float invTexW = 1.0f / (float)entry.width;
        const float invTexH = 1.0f / (float)entry.height;

        const FoldedColor fc = foldColor(env.marni, (uint32_t)type, prim[32], prim[33], prim[34], prim[35]);
        (void)fc.specular; // specular has no TL-shader input (see open questions)

        const int32_t halfW = env.marni->render_w / 2;
        const int32_t halfH = env.marni->render_h / 2;
        const int32_t x0 = env.marni->field_8C7EC4 + *(const int16_t*)(prim + 20) - halfW;
        const int32_t y0 = env.marni->field_8C7EC8 + *(const int16_t*)(prim + 22) - halfH;
        const int32_t x1 = env.marni->field_8C7EC4 + *(const int16_t*)(prim + 24) - halfW;
        const int32_t y1 = env.marni->field_8C7EC8 + *(const int16_t*)(prim + 26) - halfH;

        const float sz = 1.0f - (float)((double)((int32_t)env.marni->field_8C7EDC / 2) / (double)z);
        const float rhw = 1.0f / z;

        TlVertex v[4]{};
        v[0].sx = (float)((double)x0 * (double)env.marni->aspect_x);
        v[0].sy = (float)((double)y0 * (double)env.marni->aspect_y);
        v[0].sz = sz;
        v[0].rhw = rhw;
        setColor(v[0], fc.color);
        v[0].tu = (float)((double)prim[28] * (double)invTexW);
        v[0].tv = (float)((double)prim[29] * (double)invTexH);

        v[1].sx = (float)((double)(x1 + 1) * (double)env.marni->aspect_x);
        v[1].sy = v[0].sy;
        v[1].sz = sz;
        v[1].rhw = rhw;
        setColor(v[1], fc.color);
        v[1].tu = (float)((double)(prim[30] + 1) * (double)invTexW);
        v[1].tv = v[0].tv;

        v[2].sx = v[0].sx;
        v[2].sy = (float)((double)(y1 + 1) * (double)env.marni->aspect_y);
        v[2].sz = sz;
        v[2].rhw = rhw;
        setColor(v[2], fc.color);
        v[2].tu = v[0].tu;
        v[2].tv = (float)((double)(prim[31] + 1) * (double)invTexH);

        v[3].sx = v[1].sx;
        v[3].sy = v[2].sy;
        v[3].sz = sz;
        v[3].rhw = rhw;
        setColor(v[3], fc.color);
        v[3].tu = v[1].tu;
        v[3].tv = v[2].tv;

        snapQuad(v);
        emitQuad(env, v, 4, true, selectBlend(env.marni, (uint32_t)type, entry.hasAlpha && !entry.noAlphaFlag), true, spriteClutTexture(entry, pPrim));
    }

    // sub_40D300 (0x1002C): float-z sprite quad, colour from the blend bits.
    static void decode0x1002C(DecodeEnv& env, const Prim* pPrim, const TextureEntry& entry)
    {
        struct PrimSprQuad : Prim
        {
            uint32_t texture; // 0x0008
            uint32_t var_0C;  // 0x000C
            float z;          // 0x0010
            int16_t x0;       // 0x0014
            int16_t y0;       // 0x0016
            int16_t x1;       // 0x0018
            int16_t y1;       // 0x001A
            uint8_t u0;       // 0x001C
            uint8_t v0;       // 0x001D
            uint8_t u1;       // 0x001E
            uint8_t v1;       // 0x001F
        };
        const auto* q = (const PrimSprQuad*)pPrim;
        if (q->z == 0.0f)
            return;
        const float invTexW = 1.0f / (float)entry.width;
        const float invTexH = 1.0f / (float)entry.height;

        uint32_t color;
        const uint32_t mode = (uint32_t)q->type & 0xF00000;
        const bool hasFlag = (env.marni->gpu_flag & 0x4000) != 0;
        if (mode == 0x100000 || (hasFlag && mode == 0x200000))
            color = 0x80FFFFFF;
        else if (mode == 0x300000)
            color = 0x40FFFFFF;
        else
            color = 0xFFFFFFFF;

        const int32_t halfW = env.marni->render_w / 2;
        const int32_t halfH = env.marni->render_h / 2;
        const int32_t x0 = env.marni->field_8C7EC4 + q->x0 - halfW;
        const int32_t y0 = env.marni->field_8C7EC8 + q->y0 - halfH;
        const int32_t x1 = env.marni->field_8C7EC4 + q->x1 - halfW;
        const int32_t y1 = env.marni->field_8C7EC8 + q->y1 - halfH;

        const float sz = 1.0f - (float)((double)((int32_t)env.marni->field_8C7EDC / 2) / (double)q->z);
        const float rhw = 1.0f / q->z;

        TlVertex v[4]{};
        v[0].sx = (float)((double)x0 * (double)env.marni->aspect_x);
        v[0].sy = (float)((double)y0 * (double)env.marni->aspect_y);
        v[0].sz = sz;
        v[0].rhw = rhw;
        setColor(v[0], color);
        v[0].tu = (float)((double)q->u0 * (double)invTexW);
        v[0].tv = (float)((double)q->v0 * (double)invTexH);

        v[1].sx = (float)((double)(x1 + 1) * (double)env.marni->aspect_x);
        v[1].sy = v[0].sy;
        v[1].sz = sz;
        v[1].rhw = rhw;
        setColor(v[1], color);
        v[1].tu = (float)((double)(q->u1 + 1) * (double)invTexW);
        v[1].tv = v[0].tv;

        v[2].sx = v[0].sx;
        v[2].sy = (float)((double)(y1 + 1) * (double)env.marni->aspect_y);
        v[2].sz = sz;
        v[2].rhw = rhw;
        setColor(v[2], color);
        v[2].tu = v[0].tu;
        v[2].tv = (float)((double)(q->v1 + 1) * (double)invTexH);

        v[3].sx = v[1].sx;
        v[3].sy = v[2].sy;
        v[3].sz = sz;
        v[3].rhw = rhw;
        setColor(v[3], color);
        v[3].tu = v[1].tu;
        v[3].tv = v[2].tv;

        snapQuad(v);
        emitQuad(env, v, 4, true, selectBlend(env.marni, (uint32_t)q->type, entry.hasAlpha && !entry.noAlphaFlag), true, spriteClutTexture(entry, pPrim));
    }

    // sub_40D560 (type 45): float-centre projected sprite quad.
    static void decode45(DecodeEnv& env, const Prim* pPrim, const TextureEntry& entry)
    {
        const auto* prim = (const uint8_t*)pPrim;
        const float invTexW = 1.0f / (float)entry.width;
        const float invTexH = 1.0f / (float)entry.height;

        const FoldedColor fc = foldColor(env.marni, (uint32_t)pPrim->type, prim[0x28], prim[0x29], prim[0x2A], prim[0x2B]);
        (void)fc.specular;

        const float z = *(const float*)(prim + 0x10);
        if (z == 0.0f)
            return;

        const int32_t xHalf = (prim[0x26] - prim[0x24]) / 2;
        const int32_t yHalf = (prim[0x27] - prim[0x25]) / 2;
        const float cx = *(const float*)(prim + 0x14);
        const float cy = *(const float*)(prim + 0x18);
        const float invZ = (float)(1.0 / (double)z);
        const float scale = (float)(int32_t)env.marni->field_8C7EDC;
        const float offX = (float)env.marni->field_8C7EC4;
        const float offY = (float)env.marni->field_8C7EC8;

        const double left = (((double)cx - (double)xHalf) * (double)invZ * (double)scale + (double)offX) * (double)env.marni->aspect_x;
        const double right = (((double)xHalf + (double)cx) * (double)invZ * (double)scale + (double)offX) * (double)env.marni->aspect_x;
        const double top = (((double)cy - (double)yHalf) * (double)invZ * (double)scale + (double)offY) * (double)env.marni->aspect_y;
        const double bottom = (((double)yHalf + (double)cy) * (double)invZ * (double)scale + (double)offY) * (double)env.marni->aspect_y;
        const float sz = 1.0f - (float)((double)((int32_t)env.marni->field_8C7EDC / 2) / (double)z);
        const float rhw = (float)(1.0 / (double)z);

        TlVertex v[4]{};
        v[0].sx = (float)left;
        v[0].sy = (float)top;
        v[0].sz = sz;
        v[0].rhw = rhw;
        setColor(v[0], fc.color);
        v[0].tu = (float)((double)prim[0x24] * (double)invTexW);
        v[0].tv = (float)((double)prim[0x25] * (double)invTexH);

        v[1].sx = (float)right;
        v[1].sy = v[0].sy;
        v[1].sz = sz;
        v[1].rhw = rhw;
        setColor(v[1], fc.color);
        v[1].tu = (float)((double)(prim[0x26] + 1) * (double)invTexW);
        v[1].tv = v[0].tv;

        v[2].sx = v[0].sx;
        v[2].sy = (float)bottom;
        v[2].sz = sz;
        v[2].rhw = rhw;
        setColor(v[2], fc.color);
        v[2].tu = v[0].tu;
        v[2].tv = (float)((double)(prim[0x27] + 1) * (double)invTexH);

        v[3].sx = v[1].sx;
        v[3].sy = v[2].sy;
        v[3].sz = sz;
        v[3].rhw = rhw;
        setColor(v[3], fc.color);
        v[3].tu = v[1].tu;
        v[3].tv = v[2].tv;

        snapQuad(v);
        emitQuad(env, v, 4, true, selectBlend(env.marni, (uint32_t)pPrim->type, entry.hasAlpha && !entry.noAlphaFlag), true, spriteClutTexture(entry, pPrim));
    }

    // sub_40D8D0 (type 37): shaded sprite quad with its own colour dword at
    // 0x1C (B,G,R,A bytes at 0x1C..0x1F).
    static void decode37(DecodeEnv& env, const Prim* pPrim, const TextureEntry& entry)
    {
        const auto* prim = (const uint8_t*)pPrim;
        const float invTexW = 1.0f / (float)entry.width;
        const float invTexH = 1.0f / (float)entry.height;

        const FoldedColor fc = foldColor(env.marni, (uint32_t)pPrim->type, prim[28], prim[29], prim[30], prim[31]);
        (void)fc.specular;

        TlVertex v[4]{};
        v[0].sx = (float)((double)(*(const int16_t*)(prim + 16)) * (double)env.marni->aspect_x);
        v[0].sy = (float)((double)(*(const int16_t*)(prim + 18)) * (double)env.marni->aspect_y);
        v[0].sz = 0.5f;
        v[0].rhw = 2.0f;
        setColor(v[0], fc.color);
        v[0].tu = (float)((double)prim[24] * (double)invTexW);
        v[0].tv = (float)((double)prim[25] * (double)invTexH);

        v[1].sx = (float)((double)(*(const int16_t*)(prim + 20) + 1) * (double)env.marni->aspect_x);
        v[1].sy = v[0].sy;
        v[1].sz = 0.5f;
        v[1].rhw = 2.0f;
        setColor(v[1], fc.color);
        v[1].tu = (float)((double)(prim[26] + 1) * (double)invTexW);
        v[1].tv = v[0].tv;

        v[2].sx = v[0].sx;
        v[2].sy = (float)((double)(*(const int16_t*)(prim + 22) + 1) * (double)env.marni->aspect_y);
        v[2].sz = 0.5f;
        v[2].rhw = 2.0f;
        setColor(v[2], fc.color);
        v[2].tu = v[0].tu;
        v[2].tv = (float)((double)(prim[27] + 1) * (double)invTexH);

        v[3].sx = v[1].sx;
        v[3].sy = v[2].sy;
        v[3].sz = 0.5f;
        v[3].rhw = 2.0f;
        setColor(v[3], fc.color);
        v[3].tu = v[1].tu;
        v[3].tv = v[2].tv;

        snapQuad(v);
        emitQuad(env, v, 4, true, selectBlend(env.marni, (uint32_t)pPrim->type, entry.hasAlpha && !entry.noAlphaFlag), false, spriteClutTexture(entry, pPrim));
    }

    // MarniDrawPolyFT4 (type 36): white quad, colour from the blend bits.
    static void decode36(DecodeEnv& env, const Prim* pPrim, const TextureEntry& entry)
    {
        const auto* p = (const MarniSprt*)pPrim;
        const float invTexW = 1.0f / (float)entry.width;
        const float invTexH = 1.0f / (float)entry.height;

        uint32_t color;
        const uint32_t mode = (uint32_t)p->type & 0xF00000;
        const bool hasFlag = (env.marni->gpu_flag & 0x4000) != 0;
        if (mode == 0x100000 || (hasFlag && mode == 0x200000))
            color = 0x80FFFFFF;
        else if (mode == 0x300000)
            color = 0x40FFFFFF;
        else
            color = 0xFFFFFFFF;

        TlVertex v[4]{};
        v[0].sx = (float)((double)p->x0 * (double)env.marni->aspect_x);
        v[0].sy = (float)((double)p->y0 * (double)env.marni->aspect_y);
        v[0].sz = 0.5f;
        v[0].rhw = 2.0f;
        setColor(v[0], color);
        v[0].tu = (float)((double)p->u0 * (double)invTexW);
        v[0].tv = (float)((double)p->v0 * (double)invTexH);

        v[1].sx = (float)((double)(p->x1 + 1) * (double)env.marni->aspect_x);
        v[1].sy = v[0].sy;
        v[1].sz = 0.5f;
        v[1].rhw = 2.0f;
        setColor(v[1], color);
        v[1].tu = (float)((double)(p->u1 + 1) * (double)invTexW);
        v[1].tv = v[0].tv;

        v[2].sx = v[0].sx;
        v[2].sy = (float)((double)(p->y1 + 1) * (double)env.marni->aspect_y);
        v[2].sz = 0.5f;
        v[2].rhw = 2.0f;
        setColor(v[2], color);
        v[2].tu = v[0].tu;
        v[2].tv = (float)((double)(p->v1 + 1) * (double)invTexH);

        v[3].sx = v[1].sx;
        v[3].sy = v[2].sy;
        v[3].sz = 0.5f;
        v[3].rhw = 2.0f;
        setColor(v[3], color);
        v[3].tu = v[1].tu;
        v[3].tv = v[2].tv;

        snapQuad(v);
        emitQuad(env, v, 4, true, selectBlend(env.marni, (uint32_t)p->type, entry.hasAlpha && !entry.noAlphaFlag), false, spriteClutTexture(entry, pPrim));
    }

    // sub_40DD90 (type 33): flat untextured quad.
    static void decode33(DecodeEnv& env, const Prim* pPrim)
    {
        const auto* line = (const PrimLine*)pPrim;

        // v8 carries the red/alpha bytes folded by the blend mode.
        uint32_t v8;
        const uint32_t primType = (uint32_t)pPrim->type & 0xF00000;
        const bool hasFlag = (env.marni->gpu_flag & 0x4000) != 0;
        if (primType <= 0x300000)
        {
            if (primType != 0x300000)
            {
                if (primType == 0x100000 || (hasFlag && primType == 0x200000))
                    v8 = ((line->color0 >> 16) & 0xFF) | 0xFFFF8000;
                else
                    v8 = ((line->color0 >> 16) & 0xFFFF) | 0xFFFFFF00;
            }
            else
            {
                v8 = ((line->color0 >> 16) & 0xFF) | 0x4000;
            }
        }
        else
        {
            if (primType != 0x400000)
                v8 = ((line->color0 >> 16) & 0xFFFF) | 0xFFFFFF00;
            else
                v8 = (line->color0 >> 16) & 0xFFFF;
        }

        const uint32_t color = (line->color0 & 0xFF) | ((((line->color0 >> 8) & 0xFF) | (v8 << 8)) << 8);

        if (throttle(*env.logClut, 40))
            logging::logDebug("[sdlgpu] tile type 0x{} color0=0x{} foldedColor=0x{}", hexStr((uint32_t)pPrim->type), hexStr(line->color0), hexStr(color));

        TlVertex v[4]{};
        v[0].sx = (float)((double)line->x0 * (double)env.marni->aspect_x);
        v[0].sy = (float)((double)line->y0 * (double)env.marni->aspect_y);
        v[0].sz = 0.5f;
        v[0].rhw = 2.0f;
        setColor(v[0], color);

        v[1].sx = (float)((double)(line->x1 + 1) * (double)env.marni->aspect_x);
        v[1].sy = v[0].sy;
        v[1].sz = 0.5f;
        v[1].rhw = 2.0f;
        setColor(v[1], color);

        v[2].sx = v[0].sx;
        v[2].sy = (float)((double)(line->y1 + 1) * (double)env.marni->aspect_y);
        v[2].sz = 0.5f;
        v[2].rhw = 2.0f;
        setColor(v[2], color);

        v[3].sx = v[1].sx;
        v[3].sy = v[2].sy;
        v[3].sz = 0.5f;
        v[3].rhw = 2.0f;
        setColor(v[3], color);

        snapQuad(v);
        emitQuad(env, v, 4, false, selectBlend(env.marni, (uint32_t)pPrim->type, false), false, nullptr);
    }

    // sub_40A4B0 (type 61): projected gradient triangle.
    static void decode61(DecodeEnv& env, const Prim* pPrim, const TextureEntry& entry)
    {
        struct PrimGradTri : Prim
        {
            uint32_t texture; // 0x0008
            uint32_t var_0C;  // 0x000C
            int16_t x0;       // 0x0010
            int16_t y0;       // 0x0012
            int16_t x1;       // 0x0014
            int16_t y1;       // 0x0016
            int16_t x2;       // 0x0018
            int16_t y2;       // 0x001A
            int16_t z;        // 0x001C
            uint8_t u0;       // 0x001E
            uint8_t v0;       // 0x001F
            uint8_t u1;       // 0x0020
            uint8_t v1;       // 0x0021
            uint8_t u2;       // 0x0022
            uint8_t v2;       // 0x0023
            uint8_t b;        // 0x0024
            uint8_t g;        // 0x0025
            uint8_t r;        // 0x0026
            uint8_t a;        // 0x0027
        };
        const auto* tri = (const PrimGradTri*)pPrim;
        if (tri->z == 0)
            return;
        const float invTexW = 1.0f / (float)entry.width;
        const float invTexH = 1.0f / (float)entry.height;

        const FoldedColor fc = foldColor(env.marni, (uint32_t)tri->type, tri->b, tri->g, tri->r, tri->a);
        (void)fc.specular;

        const float z = (float)tri->z;
        const float sz = (float)(1.0 - (double)((int32_t)env.marni->field_8C7EDC / 2) / (double)z);
        const float rhw = (float)(1.0 / (double)z);
        const double projScale = (double)(int32_t)env.marni->field_8C7EDC;
        const float texOffset = *(const float*)&env.marni->field_8C7020;

        const auto make_sx = [&](int16_t x) {
            return (float)(((double)x * projScale / (double)z + (double)env.marni->field_8C7EC4) * (double)env.marni->aspect_x);
        };
        const auto make_sy = [&](int16_t y) {
            return (float)(((double)y * projScale / (double)z + (double)env.marni->field_8C7EC8) * (double)env.marni->aspect_y);
        };

        TlVertex v[3]{};
        const int16_t xs[3] = { tri->x0, tri->x1, tri->x2 };
        const int16_t ys[3] = { tri->y0, tri->y1, tri->y2 };
        const uint8_t us[3] = { tri->u0, tri->u1, tri->u2 };
        const uint8_t vs[3] = { tri->v0, tri->v1, tri->v2 };
        for (int i = 0; i < 3; i++)
        {
            v[i].sx = make_sx(xs[i]);
            v[i].sy = make_sy(ys[i]);
            v[i].sz = sz;
            v[i].rhw = rhw;
            setColor(v[i], fc.color);
            v[i].tu = (float)((double)us[i] * (double)invTexW + (double)texOffset);
            v[i].tv = (float)((double)vs[i] * (double)invTexH + (double)texOffset);
        }

        emitQuad(env, v, 3, true, selectBlend(env.marni, (uint32_t)tri->type, entry.hasAlpha && !entry.noAlphaFlag), true, spriteClutTexture(entry, pPrim));
    }

    // sub_40A830 (type 69): flat-colour gouraud quad, direct coordinates.
    static void decode69(DecodeEnv& env, const Prim* pPrim, const TextureEntry& entry)
    {
        struct PrimGouraudQuad : Prim
        {
            uint32_t texture; // 0x0008
            uint32_t var_0C;  // 0x000C
            int16_t x0;       // 0x0010
            int16_t y0;       // 0x0012
            int16_t x1;       // 0x0014
            int16_t y1;       // 0x0016
            int16_t x2;       // 0x0018
            int16_t y2;       // 0x001A
            int16_t x3;       // 0x001C
            int16_t y3;       // 0x001E
            uint8_t u0;       // 0x0020
            uint8_t v0;       // 0x0021
            uint8_t u1;       // 0x0022
            uint8_t v1;       // 0x0023
            uint8_t u2;       // 0x0024
            uint8_t v2;       // 0x0025
            uint8_t u3;       // 0x0026
            uint8_t v3;       // 0x0027
            uint8_t b;        // 0x0028
            uint8_t g;        // 0x0029
            uint8_t r;        // 0x002A
            uint8_t a;        // 0x002B
        };
        const auto* q = (const PrimGouraudQuad*)pPrim;
        const float invTexW = 1.0f / (float)entry.width;
        const float invTexH = 1.0f / (float)entry.height;
        const float uvOffset = *(const float*)&env.marni->field_8C7020;

        const FoldedColor fc = foldColor(env.marni, (uint32_t)q->type, q->b, q->g, q->r, q->a);
        (void)fc.specular;

        const int16_t xs[4] = { q->x0, q->x1, q->x2, q->x3 };
        const int16_t ys[4] = { q->y0, q->y1, q->y2, q->y3 };
        const uint8_t us[4] = { q->u0, q->u1, q->u2, q->u3 };
        const uint8_t vs[4] = { q->v0, q->v1, q->v2, q->v3 };

        TlVertex v[4]{};
        for (int i = 0; i < 4; i++)
        {
            v[i].sx = (float)((double)xs[i] * (double)env.marni->aspect_x);
            v[i].sy = (float)((double)ys[i] * (double)env.marni->aspect_y);
            v[i].sz = 0.5f;
            v[i].rhw = 2.0f;
            setColor(v[i], fc.color);
            v[i].tu = (float)((double)us[i] * (double)invTexW + (double)uvOffset);
            v[i].tv = (float)((double)vs[i] * (double)invTexH + (double)uvOffset);
        }

        emitQuad(env, v, 4, true, selectBlend(env.marni, (uint32_t)q->type, entry.hasAlpha && !entry.noAlphaFlag), false, spriteClutTexture(entry, pPrim));
    }

    // sub_40AB60 (type 70): per-vertex-colour gouraud quad.
    static void decode70(DecodeEnv& env, const Prim* pPrim, const TextureEntry& entry)
    {
        const auto* q = (const MarniPolyGt4*)pPrim;
        const float invTexW = 1.0f / (float)entry.width;
        const float invTexH = 1.0f / (float)entry.height;
        const float uvOffset = *(const float*)&env.marni->field_8C7020;

        const int16_t xs[4] = { q->x0, q->x1, q->x2, q->x3 };
        const int16_t ys[4] = { q->y0, q->y1, q->y2, q->y3 };
        const uint8_t us[4] = { q->u0, q->u1, q->u2, q->u3 };
        const uint8_t vs[4] = { q->v0, q->v1, q->v2, q->v3 };
        const uint32_t colors[4] = { q->color0, q->color1, q->color2, q->color3 };

        TlVertex v[4]{};
        for (int i = 0; i < 4; i++)
        {
            const uint8_t bByte = (uint8_t)(colors[i] & 0xFF);
            const uint8_t gByte = (uint8_t)((colors[i] >> 8) & 0xFF);
            const uint8_t rByte = (uint8_t)((colors[i] >> 16) & 0xFF);
            const uint8_t aByte = (uint8_t)((colors[i] >> 24) & 0xFF);
            const FoldedColor fc = foldColor(env.marni, (uint32_t)q->type, bByte, gByte, rByte, aByte);
            v[i].sx = (float)((double)xs[i] * (double)env.marni->aspect_x);
            v[i].sy = (float)((double)ys[i] * (double)env.marni->aspect_y);
            v[i].sz = 0.5f;
            v[i].rhw = 2.0f;
            setColor(v[i], fc.color);
            v[i].tu = (float)((double)us[i] * (double)invTexW + (double)uvOffset);
            v[i].tv = (float)((double)vs[i] * (double)invTexH + (double)uvOffset);
        }

        emitQuad(env, v, 4, true, selectBlend(env.marni, (uint32_t)q->type, entry.hasAlpha && !entry.noAlphaFlag), false, spriteClutTexture(entry, pPrim));
    }

    // sub_40B260 (type 73): projected sprite, corners at prim+8..+22, colour
    // bytes at 0x1C..0x1F, shared divisor at prim+24.
    static void decode73(DecodeEnv& env, const Prim* pPrim, const TextureEntry& entry)
    {
        const auto* prim = (const uint8_t*)pPrim;
        const int32_t type = *(const int32_t*)(prim + 4);

        uint32_t v7;
        const uint32_t mode = (uint32_t)type & 0xF00000;
        const bool hasFlag = (env.marni->gpu_flag & 0x4000) != 0;
        if (mode == 0x400000)
        {
            v7 = prim[30] | ((uint32_t)prim[31] << 8);
        }
        else if (mode == 0x300000)
        {
            v7 = prim[30] | (0x40u << 8);
        }
        else if (mode == 0x100000 || (hasFlag && mode == 0x200000))
        {
            v7 = prim[30] | 0xFFFF8000;
        }
        else
        {
            v7 = *(const uint16_t*)(prim + 30) | 0xFFFFFF00;
        }
        const uint32_t color = prim[28] | (((uint32_t)prim[29] | (v7 << 8)) << 8);

        const int16_t primW = *(const int16_t*)(prim + 24);
        if (primW == 0)
            return;
        const int32_t prj = (int32_t)env.marni->field_8C7EDC;
        const float sz = (float)(1.0 - (double)(prj / 2) / (double)primW);
        const float rhw = (float)(1.0 / (double)primW);

        const auto make_sx = [&](int16_t x) {
            return (float)(((double)x * (double)prj / (double)primW + (double)env.marni->field_8C7EC4) * (double)env.marni->aspect_x);
        };
        const auto make_sy = [&](int16_t y) {
            return (float)(((double)y * (double)prj / (double)primW + (double)env.marni->field_8C7EC8) * (double)env.marni->aspect_y);
        };

        TlVertex v[4]{};
        for (int i = 0; i < 4; i++)
        {
            v[i].sx = make_sx(*(const int16_t*)(prim + 8 + 4 * i));
            v[i].sy = make_sy(*(const int16_t*)(prim + 10 + 4 * i));
            v[i].sz = sz;
            v[i].rhw = rhw;
            setColor(v[i], color);
        }

        emitQuad(env, v, 4, true, selectBlend(env.marni, (uint32_t)type, entry.hasAlpha && !entry.noAlphaFlag), true, spriteClutTexture(entry, pPrim));
    }

    // sub_40B560 (type 76): projected flat quad, colour from blend bits.
    static void decode76(DecodeEnv& env, const Prim* pPrim, const TextureEntry& entry)
    {
        struct PrimPolyFT4 : Prim
        {
            uint32_t texture; // 0x0008
            uint32_t var_0C;  // 0x000C
            int16_t x0;       // 0x0010
            int16_t y0;       // 0x0012
            int16_t x1;       // 0x0014
            int16_t y1;       // 0x0016
            int16_t x2;       // 0x0018
            int16_t y2;       // 0x001A
            int16_t x3;       // 0x001C
            int16_t y3;       // 0x001E
            int16_t z;        // 0x0020
            uint8_t u0;       // 0x0022
            uint8_t v0;       // 0x0023
            uint8_t u1;       // 0x0024
            uint8_t v1;       // 0x0025
            uint8_t u2;       // 0x0026
            uint8_t v2;       // 0x0027
            uint8_t u3;       // 0x0028
            uint8_t v3;       // 0x0029
        };
        const auto* q = (const PrimPolyFT4*)pPrim;
        const float invTexW = 1.0f / (float)entry.width;
        const float invTexH = 1.0f / (float)entry.height;

        uint32_t color;
        const uint32_t mode = (uint32_t)q->type & 0xF00000;
        const bool hasFlag = (env.marni->gpu_flag & 0x4000) != 0;
        if (mode == 0x100000 || (hasFlag && mode == 0x200000))
            color = 0x80FFFFFF;
        else if (mode == 0x300000)
            color = 0x40FFFFFF;
        else
            color = 0xFFFFFFFF;

        const int16_t primW = q->z;
        if (primW == 0)
            return;
        const int32_t prj = (int32_t)env.marni->field_8C7EDC;
        const float sz = (float)(1.0 - (double)(prj / 2) / (double)primW);
        const float rhw = (float)(1.0 / (double)primW);
        const float adjustV = *(const float*)&env.marni->field_8C7020;

        const auto make_sx = [&](int16_t x) {
            return (float)(((double)x * (double)prj / (double)primW + (double)env.marni->field_8C7EC4) * (double)env.marni->aspect_x);
        };
        const auto make_sy = [&](int16_t y) {
            return (float)(((double)y * (double)prj / (double)primW + (double)env.marni->field_8C7EC8) * (double)env.marni->aspect_y);
        };

        const int16_t xs[4] = { q->x0, q->x1, q->x2, q->x3 };
        const int16_t ys[4] = { q->y0, q->y1, q->y2, q->y3 };
        const uint8_t us[4] = { q->u0, q->u1, q->u2, q->u3 };
        const uint8_t vs[4] = { q->v0, q->v1, q->v2, q->v3 };

        TlVertex v[4]{};
        for (int i = 0; i < 4; i++)
        {
            v[i].sx = make_sx(xs[i]);
            v[i].sy = make_sy(ys[i]);
            v[i].sz = sz;
            v[i].rhw = rhw;
            setColor(v[i], color);
            v[i].tu = (float)((double)us[i] * (double)invTexW + (double)adjustV);
            v[i].tv = (float)((double)vs[i] * (double)invTexH + (double)adjustV);
        }

        emitQuad(env, v, 4, true, selectBlend(env.marni, (uint32_t)q->type, entry.hasAlpha && !entry.noAlphaFlag), true, spriteClutTexture(entry, pPrim));
    }

    // sub_40B8D0 (type 77): projected flat quad with colour bytes at 0x2C..0x2F.
    static void decode77(DecodeEnv& env, const Prim* pPrim, const TextureEntry& entry)
    {
        const auto* prim = (const uint8_t*)pPrim;
        const float invTexW = 1.0f / (float)entry.width;
        const float invTexH = 1.0f / (float)entry.height;

        const FoldedColor fc = foldColor(env.marni, (uint32_t)pPrim->type, prim[44], prim[45], prim[46], prim[47]);
        (void)fc.specular;

        const int16_t primW = *(const int16_t*)(prim + 32);
        if (primW == 0)
            return;
        const int32_t prj = (int32_t)env.marni->field_8C7EDC;
        const float sz = (float)(1.0 - (double)(prj / 2) / (double)primW);
        const float rhw = (float)(1.0 / (double)primW);
        const float adjustV = *(const float*)&env.marni->field_8C7020;

        const auto make_sx = [&](int16_t x) {
            return (float)(((double)x * (double)prj / (double)primW + (double)env.marni->field_8C7EC4) * (double)env.marni->aspect_x);
        };
        const auto make_sy = [&](int16_t y) {
            return (float)(((double)y * (double)prj / (double)primW + (double)env.marni->field_8C7EC8) * (double)env.marni->aspect_y);
        };

        TlVertex v[4]{};
        for (int i = 0; i < 4; i++)
        {
            v[i].sx = make_sx(*(const int16_t*)(prim + 16 + 4 * i));
            v[i].sy = make_sy(*(const int16_t*)(prim + 18 + 4 * i));
            v[i].sz = sz;
            v[i].rhw = rhw;
            setColor(v[i], fc.color);
            v[i].tu = (float)((double)prim[34 + 2 * i] * (double)invTexW + (double)adjustV);
            v[i].tv = (float)((double)prim[35 + 2 * i] * (double)invTexH + (double)adjustV);
        }

        emitQuad(env, v, 4, true, selectBlend(env.marni, (uint32_t)pPrim->type, entry.hasAlpha && !entry.noAlphaFlag), true, spriteClutTexture(entry, pPrim));
    }

    // sub_40BCF0 (0x1004D): projected flat quad with colour bytes at 0x2C..0x2F
    // and centre-relative coordinates.
    static void decode0x1004D(DecodeEnv& env, const Prim* pPrim, const TextureEntry& entry)
    {
        const auto* prim = (const uint8_t*)pPrim;
        const float invTexW = 1.0f / (float)entry.width;
        const float invTexH = 1.0f / (float)entry.height;

        const FoldedColor fc = foldColor(env.marni, (uint32_t)pPrim->type, prim[44], prim[45], prim[46], prim[47]);
        (void)fc.specular;

        const int16_t primW = *(const int16_t*)(prim + 32);
        if (primW == 0)
            return;
        const int32_t prj = (int32_t)env.marni->field_8C7EDC;
        const float sz = (float)(1.0 - (double)(prj / 2) / (double)primW);
        const float rhw = (float)(1.0 / (double)primW);
        const float adjustU = env.marni->field_8C701C;
        const float adjustV = *(const float*)&env.marni->field_8C7020;

        const int halfW = env.marni->render_w / 2;
        const int halfH = env.marni->render_h / 2;
        const int centreX = env.marni->field_8C7EC4;
        const int centreY = env.marni->field_8C7EC8;

        TlVertex v[4]{};
        for (int i = 0; i < 4; i++)
        {
            const int xt = centreX + *(const int16_t*)(prim + 16 + 4 * i) - halfW;
            const int yt = centreY + *(const int16_t*)(prim + 18 + 4 * i) - halfH;
            v[i].sx = (float)((double)xt * (double)env.marni->aspect_x + (double)adjustU);
            v[i].sy = (float)((double)yt * (double)env.marni->aspect_y + (double)adjustU);
            v[i].sz = sz;
            v[i].rhw = rhw;
            setColor(v[i], fc.color);
            v[i].tu = (float)((double)prim[34 + 2 * i] * (double)invTexW + (double)adjustV);
            v[i].tv = (float)((double)prim[35 + 2 * i] * (double)invTexH + (double)adjustV);
        }

        emitQuad(env, v, 4, true, selectBlend(env.marni, (uint32_t)pPrim->type, entry.hasAlpha && !entry.noAlphaFlag), true, entry.texture);
    }

    // sub_40C100 (0x1004C): centre-relative projected flat quad, colour from
    // the blend bits.
    static void decode0x1004C(DecodeEnv& env, const Prim* pPrim, const TextureEntry& entry)
    {
        struct PrimPolyFT4 : Prim
        {
            uint32_t texture; // 0x0008
            uint32_t var_0C;  // 0x000C
            int16_t x0;       // 0x0010
            int16_t y0;       // 0x0012
            int16_t x1;       // 0x0014
            int16_t y1;       // 0x0016
            int16_t x2;       // 0x0018
            int16_t y2;       // 0x001A
            int16_t x3;       // 0x001C
            int16_t y3;       // 0x001E
            int16_t z;        // 0x0020
            uint8_t u0;       // 0x0022
            uint8_t v0;       // 0x0023
            uint8_t u1;       // 0x0024
            uint8_t v1;       // 0x0025
            uint8_t u2;       // 0x0026
            uint8_t v2;       // 0x0027
            uint8_t u3;       // 0x0028
            uint8_t v3;       // 0x0029
        };
        const auto* q = (const PrimPolyFT4*)pPrim;
        const float invTexW = 1.0f / (float)entry.width;
        const float invTexH = 1.0f / (float)entry.height;

        uint32_t color;
        const uint32_t mode = (uint32_t)q->type & 0xF00000;
        const bool hasFlag = (env.marni->gpu_flag & 0x4000) != 0;
        if (mode == 0x400000)
            color = 0xFFFFFF;
        else if (mode == 0x300000)
            color = 0x40FFFFFF;
        else if (mode == 0x100000 || (hasFlag && mode == 0x200000))
            color = 0x80FFFFFF;
        else
            color = 0xFFFFFFFF;

        if (q->z == 0)
            return;
        const int halfW = env.marni->render_w / 2;
        const int halfH = env.marni->render_h / 2;
        const int centreX = env.marni->field_8C7EC4;
        const int centreY = env.marni->field_8C7EC8;

        const float adjustU = env.marni->field_8C701C;
        const float adjustV = *(const float*)&env.marni->field_8C7020;
        const float sz = 1.0f - (float)((double)((int32_t)env.marni->field_8C7EDC / 2) / (double)q->z);
        const float rhw = 1.0f / (float)q->z;

        const int16_t xs[4] = { q->x0, q->x1, q->x2, q->x3 };
        const int16_t ys[4] = { q->y0, q->y1, q->y2, q->y3 };
        const uint8_t us[4] = { q->u0, q->u1, q->u2, q->u3 };
        const uint8_t vs[4] = { q->v0, q->v1, q->v2, q->v3 };

        TlVertex v[4]{};
        for (int i = 0; i < 4; i++)
        {
            const int xt = centreX + xs[i] - halfW;
            const int yt = centreY + ys[i] - halfH;
            v[i].sx = (float)((double)xt * (double)env.marni->aspect_x + (double)adjustU);
            v[i].sy = (float)((double)yt * (double)env.marni->aspect_y + (double)adjustU);
            v[i].sz = sz;
            v[i].rhw = rhw;
            setColor(v[i], color);
            v[i].tu = (float)((double)us[i] * (double)invTexW + (double)adjustV);
            v[i].tv = (float)((double)vs[i] * (double)invTexH + (double)adjustV);
        }

        emitQuad(env, v, 4, true, selectBlend(env.marni, (uint32_t)q->type, entry.hasAlpha && !entry.noAlphaFlag), true, spriteClutTexture(entry, pPrim));
    }

    // sub_40C470 (0x10049): sprite with direct corners (prim+8..+22) and the
    // divisor at prim+24.
    static void decode0x10049(DecodeEnv& env, const Prim* pPrim, const TextureEntry& entry)
    {
        const auto* prim = (const uint8_t*)pPrim;
        const int32_t type = *(const int32_t*)(prim + 4);

        uint32_t v7;
        const uint32_t mode = (uint32_t)type & 0xF00000;
        const bool hasFlag = (env.marni->gpu_flag & 0x4000) != 0;
        if (mode == 0x400000)
        {
            v7 = prim[30] | ((uint32_t)prim[31] << 8);
        }
        else if (mode == 0x300000)
        {
            v7 = prim[30] | (0x40u << 8);
        }
        else if (mode == 0x100000 || (hasFlag && mode == 0x200000))
        {
            v7 = prim[30] | 0xFFFF8000;
        }
        else
        {
            v7 = *(const uint16_t*)(prim + 30) | 0xFFFFFF00;
        }
        const uint32_t color = prim[28] | (((uint32_t)prim[29] | (v7 << 8)) << 8);

        const int16_t primW = *(const int16_t*)(prim + 24);
        if (primW == 0)
            return;
        const float sz = (float)(1.0 - (double)(env.marni->resolutions[0].height / 2) / (double)primW);
        const float rhw = (float)(1.0 / (double)primW);

        const auto make_sx = [&](int16_t x) {
            return (float)((double)x * (double)env.marni->aspect_x + (double)env.marni->field_8C701C);
        };
        const auto make_sy = [&](int16_t y) {
            return (float)((double)y * (double)env.marni->aspect_y + (double)env.marni->field_8C701C);
        };

        TlVertex v[4]{};
        for (int i = 0; i < 4; i++)
        {
            v[i].sx = make_sx(*(const int16_t*)(prim + 8 + 4 * i));
            v[i].sy = make_sy(*(const int16_t*)(prim + 10 + 4 * i));
            v[i].sz = sz;
            v[i].rhw = rhw;
            setColor(v[i], color);
        }

        emitQuad(env, v, 4, true, selectBlend(env.marni, (uint32_t)type, entry.hasAlpha && !entry.noAlphaFlag), true, spriteClutTexture(entry, pPrim));
    }

    // draw_line_flat / draw_line_gourad / draw_line_gpu (types 17/18).
    static void decodeLine(DecodeEnv& env, const Prim* pPrim, bool gourad)
    {
        const auto* line = (const PrimLine2*)pPrim;

        // The original computes `type` from xsize/line->pNext (garbage-derived
        // when xsize != 640); the prototype derives it from the prim's own
        // type bits, which matches the xsize == 640 behaviour (doubled, plus
        // additive when bit 0x200000 is set).
        int type = 1;
        if (line->type & 0x200000)
            type |= 2;
        const bool doubled = (type & 1) != 0;
        const bool additive = (type & 2) != 0;

        const float sx0 = doubled ? 2.0f * line->x0 : (float)line->x0;
        const float sy0 = doubled ? 2.0f * line->y0 : (float)line->y0;
        const float sx1 = doubled ? 2.0f * line->x1 + 2.0f : (float)(line->x1 + 1);
        const float sy1 = doubled ? 2.0f * line->y1 + 2.0f : (float)(line->y1 + 1);

        const int adx = line->x1 > line->x0 ? line->x1 - line->x0 : line->x0 - line->x1;
        const int ady = line->y1 > line->y0 ? line->y1 - line->y0 : line->y0 - line->y1;

        const uint32_t c0 = 0xFF000000u | line->color0;
        const uint32_t c1 = gourad ? (0xFF000000u | line->color1) : c0;

        TlVertex v[4]{};
        for (auto& vert : v)
        {
            vert.sz = 0.0f;
            vert.rhw = 1.0f;
        }
        if (adx >= ady)
        {
            v[0].sx = sx0; v[0].sy = sy0; setColor(v[0], c0);
            v[1].sx = sx0; v[1].sy = sy1; setColor(v[1], c0);
            v[2].sx = sx1; v[2].sy = sy0; setColor(v[2], c1);
            v[3].sx = sx1; v[3].sy = sy1; setColor(v[3], c1);
        }
        else
        {
            v[0].sx = sx0; v[0].sy = sy0; setColor(v[0], c0);
            v[1].sx = sx1; v[1].sy = sy0; setColor(v[1], c0);
            v[2].sx = sx0; v[2].sy = sy1; setColor(v[2], c1);
            v[3].sx = sx1; v[3].sy = sy1; setColor(v[3], c1);
        }

        // Lines are drawn without depth interaction.
        DrawCall dc;
        dc.key = makePipelineKey(false, additive ? BlendSel::Add : BlendSel::None, false, false);
        const size_t base = (size_t)allocVertices(env, 4) - (size_t)env.vertexData->data();
        std::memcpy(env.vertexData->data() + base, v, sizeof(v));
        dc.firstVertex = (uint32_t)(base / sizeof(TlVertex));
        dc.vertexCount = 4;
        env.calls->push_back(dc);
        env.stats->drawn += 4;
        env.stats->lines++;
    }

    // trans_matrix equivalent (marni.cpp 0x004074C0): applies a scaler
    // primitive to the projection state. Pure data - safe to replicate.
    static void applyTransMatrix(Marni* self, const Prim* pPrim)
    {
        const auto* pScaler = (const PrimScaler*)pPrim;
        if ((pScaler->type & 0x2000) != 0)
        {
            self->aspect_x = pScaler->rate_x;
            self->aspect_y = pScaler->rate_y;
        }
        if ((pScaler->type & 0x4000) != 0)
        {
            self->xsize = (int)((float)(int32_t)pScaler->var_2C * self->aspect_x);
            self->ysize = (int)((float)(int32_t)pScaler->var_30 * self->aspect_y);
        }
        if ((pScaler->type & 0x800) != 0)
            self->field_8C7EDC = pScaler->prj;
        if ((pScaler->type & 0x400) != 0)
        {
            self->field_8C7EC4 = (int32_t)pScaler->c_x;
            self->field_8C7EC8 = (int32_t)pScaler->c_y;
            *(int32_t*)&self->pad_8C7ECC[0] = (int32_t)pScaler->c_x - self->render_w / 2;
            *(int32_t*)&self->pad_8C7ECC[4] = (int32_t)pScaler->c_y - self->render_h / 2;
        }
        if ((pScaler->type & 0x200) != 0)
            self->field_8C7E90 = pScaler->rgb0;
        if ((pScaler->type & 0x1000) != 0)
            *(uint32_t*)&self->ambient_b = pScaler->rgb1;
    }

    // trans_spr_poly dispatch (marni.cpp 0x0040DF70): decodes one textured
    // primitive into vertices + a draw call.
    static void decodeSprPoly(DecodeEnv& env, const Prim* prim)
    {
        const uint32_t type = (uint32_t)prim->type;
        const int v5 = (int)(type & 0xFFFFF);

        const TextureEntry* entry = nullptr;
        if (v5 != 33) // tiles are untextured
        {
            entry = resolveTexture(env, prim);
            if (entry == nullptr)
            {
                env.stats->skipped++;
                return;
            }
            if (throttle(*env.logClut, 60))
            {
                const auto* spr = (const PrimSprite*)prim;
                logging::logDebug("[sdlgpu] spr type 0x{} tex {} texw {} texh {} uv({},{})-({},{})",
                    hexStr(type), spr->texture, entry->width, entry->height,
                    (unsigned)spr->u0, (unsigned)spr->v0, (unsigned)spr->u1, (unsigned)spr->v1);
            }
        }

        if (v5 > 0x49)
        {
            if (v5 > 0x1002D)
            {
                const int v30 = v5 - 65609; // 0x10049
                if (v30 == 0)
                {
                    decode0x10049(env, prim, *entry);
                    return;
                }
                const int v31 = v30 - 3; // 0x1004C
                if (v31 == 0)
                {
                    decode0x1004C(env, prim, *entry);
                    return;
                }
                if (v31 == 1) // 0x1004D
                {
                    decode0x1004D(env, prim, *entry);
                    return;
                }
            }
            else
            {
                if (v5 == 65581) // 0x1002D
                {
                    decode0x1002D(env, prim, *entry);
                    return;
                }
                const int v28 = v5 - 76;
                if (v28 == 0)
                {
                    decode76(env, prim, *entry);
                    return;
                }
                const int v29 = v28 - 1;
                if (v29 == 0)
                {
                    decode77(env, prim, *entry);
                    return;
                }
                if (v29 == 65503) // 0x1002C
                {
                    decode0x1002C(env, prim, *entry);
                    return;
                }
            }
            if (throttle(*env.logNoDecoder))
                logging::logDebug("[sdlgpu] type 0x{}: no decoder (original: LABEL_26 no-op) - skipping", hexStr(type));
            env.stats->skipped++;
            return;
        }

        if (v5 == 73)
        {
            decode73(env, prim, *entry);
            return;
        }
        switch (v5)
        {
        case 33: decode33(env, prim); return;
        case 36: decode36(env, prim, *entry); return;
        case 37: decode37(env, prim, *entry); return;
        case 44: // sub_40DF60 no-op in the original
            return;
        case 45: decode45(env, prim, *entry); return;
        case 61: decode61(env, prim, *entry); return;
        case 69: decode69(env, prim, *entry); return;
        case 70: decode70(env, prim, *entry); return;
        default:
            if (throttle(*env.logNoDecoder))
                logging::logDebug("[sdlgpu] type 0x{}: no decoder (original: LABEL_26 no-op) - skipping", hexStr(type));
            env.stats->skipped++;
            return;
        }
    }

    // ── 3D character models (trans_object, types 88 / 0x188 / 256) ───────

    // Port of marni.cpp apply_matrix_float (0x00411630): 3-vector by the
    // upper-left 3x3 of a row-major 4x4 matrix (no translation).
    static void applyMatrixFloat(float* vec, const float* mat)
    {
        const float v0 = vec[0];
        const float v1 = vec[1];
        const float v2 = vec[2];
        vec[0] = v0 * mat[0] + v1 * mat[1] + v2 * mat[2];
        vec[1] = v0 * mat[4] + v1 * mat[5] + v2 * mat[6];
        vec[2] = v0 * mat[8] + v1 * mat[9] + v2 * mat[10];
    }

    // Raw accessors for the MARNI polygon object. re2.h only types the
    // vtable of PolygonObject; the payload matches marni.cpp's
    // MarniPolyObject overlay:
    //   +0x04 vertices ptr, +0x08 normals ptr, +0x0C primitives ptr,
    //   +0x10 9-dword header ([2]=vertex count [4]=normal count [6]=prim
    //   count [8]=primitive type), +0x30 type, +0x34 flags (bit 0 = valid).

    // Port of marni.cpp refer_vertex (0x00415E80): resolves a vertex index
    // into world-space float coords.
    static bool referVertex(const PolygonObject* obj, int index, float* dst)
    {
        const auto* s = (const uint8_t*)obj;
        if ((s[0x34] & 1) == 0 || index >= (int)*(const uint32_t*)(s + 0x18))
            return false;

        const uint32_t type = *(const uint32_t*)(s + 0x30) & 0xFF801FFF;
        switch (type)
        {
        case 0x800400:
        case 0x402:
        case 0x404:
        case 0x1442:
        case 0x1800400:
        case 0x1800401:
        {
            const float* v = (const float*)(*(const uint8_t**)(s + 0x04) + 12 * index);
            dst[0] = v[0];
            dst[1] = v[1];
            dst[2] = v[2];
            return true;
        }
        case 0x10014C0:
        {
            const float* v = (const float*)(*(const uint8_t**)(s + 0x04) + 32 * index);
            dst[0] = v[0];
            dst[1] = v[1];
            dst[2] = v[2];
            return true;
        }
        case 0x1800080:
        case 0x1800081:
        {
            const double scale = (double)(1 << ((*(const uint32_t*)(s + 0x30) >> 13) & 0x1F));
            const int16_t* v = (const int16_t*)(*(const uint8_t**)(s + 0x04) + 6 * index);
            dst[0] = (float)((double)v[0] / scale);
            dst[1] = (float)((double)v[1] / scale);
            dst[2] = (float)((double)v[2] / scale);
            return true;
        }
        default: return false;
        }
    }

    // Port of marni.cpp refer_normal (0x00415AE0): resolves a normal index
    // into light-space float coords.
    static bool referNormal(const PolygonObject* obj, int index, float* dst)
    {
        const auto* s = (const uint8_t*)obj;
        if ((s[0x34] & 1) == 0 || index >= (int)*(const uint32_t*)(s + 0x20))
            return false;

        const uint32_t type = *(const uint32_t*)(s + 0x30) & 0xFF801FFF;
        switch (type)
        {
        case 0x402:
        case 0x404:
        case 0x800400:
        case 0x1800400:
        case 0x1800401:
            return true; // the original leaves dst untouched for these types
        case 0x1442:
        {
            const float* n = (const float*)(*(const uint8_t**)(s + 0x08) + 12 * index);
            dst[0] = n[0];
            dst[1] = n[1];
            dst[2] = n[2];
            return true;
        }
        case 0x10014C0:
        {
            const float* n = (const float*)(*(const uint8_t**)(s + 0x04) + 32 * index + 12);
            dst[0] = n[0];
            dst[1] = n[1];
            dst[2] = n[2];
            return true;
        }
        case 0x1800080:
        case 0x1800081:
        {
            const double scale = (double)(1 << ((*(const uint32_t*)(s + 0x30) >> 18) & 0x1F));
            const int16_t* n = (const int16_t*)(*(const uint8_t**)(s + 0x08) + 6 * index);
            dst[0] = (float)((double)n[0] / scale);
            dst[1] = (float)((double)n[1] / scale);
            dst[2] = (float)((double)n[2] / scale);
            return true;
        }
        default: return false;
        }
    }

    // Port of marni.cpp modify_primitive (0x004156E0): copies the
    // per-primitive record. For 0x1800081 this is 18 bytes: 3x uint16 vertex
    // indices, 3x uint16 colour (normal) indices and 6 packed U/V bytes.
    static bool modifyPrimitive(const PolygonObject* obj, int index, uint8_t* dst)
    {
        const auto* s = (const uint8_t*)obj;
        if ((s[0x34] & 1) == 0 || index >= (int)*(const uint32_t*)(s + 0x28))
            return false;

        const uint32_t type = *(const uint32_t*)(s + 0x30) & 0xFF801FFF;
        const uint8_t* prims = *(const uint8_t**)(s + 0x0C);
        switch (type)
        {
        case 0x402:
        case 0x1800401: memcpy(dst, prims + 12 * index, 12); return true;
        case 0x404: memcpy(dst, prims + 24 * index, 0x18); return true;
        case 0x1442: memcpy(dst, prims + 14 * index, 14); return true;
        case 0x10014C0: memcpy(dst, prims + 8 * index, 8); return true;
        case 0x800400: memcpy(dst, prims + 40 * index, 0x28); return true;
        case 0x1800400: memcpy(dst, prims + 16 * index, 16); return true;
        case 0x1800080: memcpy(dst, prims + 24 * index, 0x18); return true;
        case 0x1800081: memcpy(dst, prims + 18 * index, 18); return true;
        default: return false;
        }
    }

    // Port of marni.cpp sub_407480 (prim type 256): copies the two 4x4 light
    // matrices from the primitive into the MARNI state consumed by the next
    // trans_object primitives.
    static void applyMatrixCopy256(Marni* self, const Prim* pPrim)
    {
        const auto* prim = (const uint8_t*)pPrim;
        memcpy(&self->field_8C7E10, prim + 8, 0x40);
        memcpy(&self->field_8C7E50, prim + 72, 0x40);
    }

    // Port of marni.cpp trans_object_ngtin3_vinsnins (0x004157D0): the real
    // 3D model renderer for 0x1800081 polygon objects. Looks up the base
    // texture (prim+0x08; up to 4 CLUT sections gated by the split table at
    // prim+0x58), lights each normal through the two light matrices, projects
    // the vertices and expands every primitive into three TL vertices.
    // Returns true when at least one draw call was emitted.
    static bool decodeTransObjectNgtin3(DecodeEnv& env, const Prim* pPrim)
    {
        const auto* prim = (const uint8_t*)pPrim;
        const uint32_t textureHandle = *(const uint32_t*)(prim + 8);
        if (textureHandle == 0)
            return false;

        auto baseIt = env.textures->find((int)textureHandle);
        if (baseIt == env.textures->end() || baseIt->second.texture == nullptr)
        {
            if (throttle(*env.logMissingTexture, 300))
                logging::logWarning("[sdlgpu] trans_object: no GPU texture for handle {} - skipping model", textureHandle);
            env.stats->skipped++;
            return false;
        }
        const TextureEntry* baseTex = &baseIt->second;

        // Each split-table section samples the CLUT at prim+0x54+i (matching
        // the original search_texture_object_0_from_1_in_condition call per
        // section); sections beyond the uploaded clut count fall back to
        // clut 0 via textureForClut.
        for (int i = 1; i < 4; i++)
        {
            if (*(const uint16_t*)(prim + 0x58 + 2 * (i - 1)) != 0)
            {
                const uint8_t clut = *(const uint8_t*)(prim + 0x54 + i);
                if (clut >= baseTex->clutCount && throttle(*env.logClut, 300))
                    logging::logWarning("[sdlgpu] trans_object: section {} uses clut {} (clutCount {}) - out of range, falling back to clut 0", i, clut, baseTex->clutCount);
            }
        }

        // The polygon object is held in the 2K buffer; when flagged, copy its
        // header (vertex/normal/primitive counts) from offset 0x10.
        auto* pObject = env.marni->polygons[*(const uint32_t*)(prim + 0x4C)];
        uint32_t header[9] = { 0 };
        if (pObject != nullptr && (((const uint8_t*)pObject)[0x34] & 1) != 0)
            memcpy(header, (const uint8_t*)pObject + 0x10, sizeof(header));

        // Primitive colour: B/G/R are doubled (modulated by the per-normal
        // light), A is used raw; each channel is clamped to 8-bit range.
        const int32_t primType = pPrim->type;
        const uint8_t alpha = *(const uint8_t*)(prim + 0x53);
        int32_t primB = 2 * *(const uint8_t*)(prim + 0x52);
        int32_t primG = 2 * *(const uint8_t*)(prim + 0x51);
        int32_t primR = 2 * *(const uint8_t*)(prim + 0x50);
        if (primB >= 256)
            primB = 255;
        if (primG >= 256)
            primG = 255;
        if (primR >= 256)
            primR = 255;

        const bool flatShade = (int32_t)primType < 0 || (env.marni->gpu_flag & GpuFlags::FILTER_BIT_0) != 0;
        uint32_t fallbackColor = 0;
        std::vector<uint32_t> colors(0x400, 0);
        if (flatShade)
        {
            // Flat shading: build a single colour from the primitive colour
            // and the alpha byte picked by the 0x100000..0x400000 mode bits.
            uint32_t base;
            switch (primType & 0xF00000)
            {
            case 0x100000: base = (uint32_t)primB | 0xFFFF8000; break;
            case 0x300000: base = (uint32_t)primB | 0x4000; break;
            case 0x400000: base = (uint32_t)primB | ((uint32_t)alpha << 8); break;
            default: base = (uint32_t)primB | 0xFFFFFF00; break;
            }
            fallbackColor = (uint32_t)primR | (((uint32_t)primG | (base << 8)) << 8);
        }
        else
        {
            // Per-normal lighting: transform each normal through the two light
            // matrices, add the ambient colour and modulate by the primitive
            // colour. Components equal to zero (or unordered/NaN) are clamped.
            const float* lightMatrix1 = (const float*)&env.marni->field_8C7E10;
            const float* lightMatrix2 = &env.marni->field_8C7E50;
            const auto* ambient = (const uint8_t*)&env.marni->field_8C7E90;
            for (uint32_t n = 0; n < header[4]; n++)
            {
                float normal[3] = { 0.0f, 0.0f, 0.0f };
                referNormal(pObject, (int)n, normal);

                applyMatrixFloat(normal, lightMatrix1);
                if (!(normal[0] >= 0.0f))
                    normal[0] = 0.0f;
                if (!(normal[1] >= 0.0f))
                    normal[1] = 0.0f;
                if (!(normal[2] >= 0.0f))
                    normal[2] = 0.0f;

                applyMatrixFloat(normal, lightMatrix2);
                if (!(normal[0] >= 0.0f))
                    normal[0] = 0.0f;
                if (!(normal[1] >= 0.0f))
                    normal[1] = 0.0f;
                if (!(normal[2] >= 0.0f))
                    normal[2] = 0.0f;

                // The ambient word at field_8C7E90 is packed B,G,R (bytes 0,1,2).
                normal[0] += (float)ambient[2];
                normal[1] += (float)ambient[1];
                normal[2] += (float)ambient[0];

                if (normal[0] >= 255.0f)
                    normal[0] = 255.0f;
                if (normal[1] >= 255.0f)
                    normal[1] = 255.0f;
                if (normal[2] >= 255.0f)
                    normal[2] = 255.0f;

                const int nB = (int)normal[0];
                const int nG = (int)normal[1];
                const int nR = (int)normal[2];
                const int cR = (primR * nR) / 255;
                const int cG = (primG * nG) / 255;
                const int cB = (primB * nB) / 255;

                switch (primType & 0xF00000)
                {
                case 0x100000: colors[n] = 0x80000000 | (cB << 16) | (cG << 8) | cR; break;
                case 0x300000: colors[n] = 0x40000000 | (cB << 16) | (cG << 8) | cR; break;
                case 0x400000: colors[n] = ((uint32_t)alpha << 24) | (cB << 16) | (cG << 8) | cR; break;
                default: colors[n] = 0xFF000000 | (cB << 16) | (cG << 8) | cR; break;
                }
            }
        }

        // Transform the object's vertices: refer, negate Y, apply the object
        // matrix and translation, then project into screen space. Vertices
        // behind the near plane are flattened to the origin.
        const int32_t prj = (int32_t)env.marni->field_8C7EDC;
        const double projScale = (double)prj;
        const double halfPrj = (double)(prj / 2);
        std::vector<float> verts(0x800 * 3, 0.0f);
        for (uint32_t i = 0; i < header[2]; i++)
        {
            float a1[3] = { 0.0f, 0.0f, 0.0f };
            referVertex(pObject, (int)i, a1);
            a1[1] = -a1[1];
            applyMatrixFloat(a1, (const float*)(prim + 0xC));
            a1[0] += *(const float*)(prim + 0x18);
            a1[1] += *(const float*)(prim + 0x28);
            a1[2] += *(const float*)(prim + 0x38);

            // Only an unordered (NaN) depth is flattened to the origin; any
            // ordered value (including negative) is projected and left to the
            // w-clip.
            if (a1[2] != a1[2])
            {
                a1[0] = 0.0f;
                a1[1] = 0.0f;
            }
            else
            {
                a1[0] = (float)(((double)a1[0] * projScale / (double)a1[2] + (double)env.marni->field_8C7EC4) * (double)env.marni->aspect_x);
                a1[1] = (float)(((double)a1[1] * projScale / (double)a1[2] + (double)env.marni->field_8C7EC8) * (double)env.marni->aspect_y);
            }
            verts[3 * i + 0] = a1[0];
            verts[3 * i + 1] = a1[1];
            verts[3 * i + 2] = a1[2];
        }

        // Texture coordinate scale factors (width/height minus one) and the
        // shared U/V adjust value.
        const int texW = baseTex->width - 1;
        const int texH = baseTex->height - 1;
        const float texOffset = *reinterpret_cast<const float*>(&env.marni->field_8C7020);

        // The primitive records (from ModifyPrimitive) reference vertex
        // indices, colour (normal) indices and packed U/V bytes. The six U/V
        // bytes map to TU/TV as uN / (width-1), vN / (height-1).
        struct TransPrimRecord
        {
            uint16_t vtx0;
            uint16_t vtx1;
            uint16_t vtx2;
            uint16_t color0;
            uint16_t color1;
            uint16_t color2;
            uint8_t u0;
            uint8_t v0;
            uint8_t u1;
            uint8_t v1;
            uint8_t u2;
            uint8_t v2;
        };
        static_assert(sizeof(TransPrimRecord) == 18, "prim record is 18 bytes");

        // Expand each primitive into three TL vertices and record section
        // splits from the split table at primitive offset 0x58.
        std::vector<uint32_t> chunkStarts;
        uint32_t threshold = *(const uint16_t*)(prim + 0x58);
        const uint16_t* splitTable = (const uint16_t*)(prim + 0x58);
        std::vector<TlVertex> packedVertices(0x800 * 3);
        int primIdx;
        for (primIdx = 0; primIdx < (int)header[6]; primIdx++)
        {
            if (*(const uint16_t*)(prim + 0x58) != 0 && primIdx == (int)threshold)
            {
                chunkStarts.push_back((uint32_t)primIdx);
                splitTable++;
                threshold += *splitTable;
            }

            TransPrimRecord record{};
            modifyPrimitive(pObject, primIdx, (uint8_t*)&record);

            auto* vout = &packedVertices[3 * primIdx];
            const float* v0 = &verts[3 * record.vtx0];
            const float* v1 = &verts[3 * record.vtx1];
            const float* v2 = &verts[3 * record.vtx2];

            const double invW0 = 1.0 / (double)v0[2];
            vout[0].sx = v0[0];
            vout[0].sy = v0[1];
            vout[0].sz = (float)(1.0 - halfPrj * invW0);
            vout[0].rhw = (float)invW0;
            setColor(vout[0], colors[record.color0]);
            vout[0].tu = (float)((double)record.u0 / (double)texW + (double)texOffset);
            vout[0].tv = (float)((double)record.v0 / (double)texH + (double)texOffset);

            const double invW1 = 1.0 / (double)v1[2];
            vout[1].sx = v1[0];
            vout[1].sy = v1[1];
            vout[1].sz = (float)(1.0 - halfPrj * invW1);
            vout[1].rhw = (float)invW1;
            setColor(vout[1], colors[record.color1]);
            vout[1].tu = (float)((double)record.u1 / (double)texW + (double)texOffset);
            vout[1].tv = (float)((double)record.v1 / (double)texH + (double)texOffset);

            const double invW2 = 1.0 / (double)v2[2];
            vout[2].sx = v2[0];
            vout[2].sy = v2[1];
            vout[2].sz = (float)(1.0 - halfPrj * invW2);
            vout[2].rhw = (float)invW2;
            setColor(vout[2], colors[record.color2]);
            vout[2].tu = (float)((double)record.u2 / (double)texW + (double)texOffset);
            vout[2].tv = (float)((double)record.v2 / (double)texH + (double)texOffset);

            // Software lighting path: override with the flat colour.
            if (flatShade)
            {
                setColor(vout[0], fallbackColor);
                setColor(vout[1], fallbackColor);
                setColor(vout[2], fallbackColor);
            }
        }
        chunkStarts.push_back((uint32_t)primIdx);

        // Draw each section (texture) of the primitive list. The SDL
        // prototype pipeline is a triangle strip, so each triangle is
        // submitted as its own 3-vertex call (a 3N-vertex batch would stitch
        // the triangles together into a single strip).
        const BlendSel blend = selectBlend(env.marni, (uint32_t)primType, false);
        const int splitCount = (int)chunkStarts.size();
        bool drew = false;
        // trans_object uses the bilinear-aware sampler (original filter=1);
        // the other decoders keep NEAREST via env.sampler.
        SDL_GPUSampler* prevSampler = env.sampler;
        if (env.charSampler)
            env.sampler = env.charSampler;
        for (int i = 0; i < splitCount; i++)
        {
            int start;
            int end;
            if (*(const uint16_t*)(prim + 0x58) != 0 && i != 0)
            {
                // Section i covers primitives [chunkStarts[i-1], chunkStarts[i]).
                start = (int)chunkStarts[i - 1];
                end = (int)chunkStarts[i];
            }
            else
            {
                start = 0;
                end = (int)chunkStarts[0];
            }
            // Section i uses the CLUT at prim+0x54+i (the base section is
            // prim+0x54), matching trans_object_ngtin3_vinsnins.
            const uint8_t sectionClut = *(const uint8_t*)(prim + 0x54 + i);
            SDL_GPUTexture* sectionTex = baseTex->textureForClut(sectionClut);
            for (int p = start; p < end; p++)
            {
                emitQuad(env, &packedVertices[3 * p], 3, true, blend, true, sectionTex);
                drew = true;
            }
        }
        env.sampler = prevSampler;
        return drew;
    }

    // Port of marni.cpp trans_object (0x00408140): the 3D character
    // dispatch. Reads the polygon object index at prim+0x4C, validates the
    // object, copies the two light matrices when the 0x100 type bit is set,
    // then dispatches on the polygon object's primitive type - only 0x1800081
    // actually renders. Returns true when at least one draw call was emitted.
    static bool decodeTransObject(DecodeEnv& env, const Prim* pPrim)
    {
        const auto* prim = (const uint8_t*)pPrim;
        const uint32_t objectIndex = *(const uint32_t*)(prim + 0x4C);
        auto* pObject = env.marni->polygons[objectIndex];
        if (pObject == nullptr || (((const uint8_t*)pObject)[0x34] & 1) == 0)
        {
            env.stats->skipped++;
            return false;
        }

        // Copy the polygon object header (obj+0x10, 9 dwords). Fields of
        // interest: [2]=vertices count [4]=normals count [6]=primitives
        // count [8]=primitive type.
        uint32_t header[9];
        memcpy(header, (const uint8_t*)pObject + 0x10, sizeof(header));
        if (header[4] > 0x400u || header[2] > 0x800u || header[6] > 0x800u)
        {
            env.stats->skipped++;
            return false;
        }

        // 0x100 type bit: copy the two 4x4 light matrices into the system.
        if ((pPrim->type & 0x100) != 0)
        {
            memcpy(&env.marni->field_8C7E10, prim + 0x60, 0x40);
            memcpy(&env.marni->field_8C7E50, prim + 0xA0, 0x40);
        }

        // Dispatch on the masked polygon object primitive type.
        if ((header[8] & 0xFF801FFF) != 0x1800081)
        {
            env.stats->skipped++;
            return false;
        }
        return decodeTransObjectNgtin3(env, pPrim);
    }

    // Walks one ordering table exactly like the original trans_priority_list.
    static void parseOrderingTable(DecodeEnv& env, MarniOt* ot, int otIndex, int& primCount, uint64_t& skipTypeLogCounter, uint64_t& transObjectLogCounter, uint64_t& transMatrixLogCounter)
    {
        if (ot == nullptr || !ot->is_valid || ot->pHead == nullptr || ot->zdepth <= 0)
            return;

        // The whole OT is one flat linked chain: ot_clear relinks each bucket
        // node's pNext to the next bucket (and the last bucket's pNext to the
        // first real prim), so the bucket sentinel nodes sit *inside* the
        // chain. Skip them by address range - their type field is not
        // meaningful. This mirrors what the binary's ot_get_primitive
        // (0x004164D0) does structurally.
        const Prim* const bucketEnd = ot->pHead + ot->zdepth;
        for (Prim* prim = ot->pHead->pNext; prim != nullptr; prim = prim->pNext)
        {
            if (prim >= ot->pHead && prim < bucketEnd)
                continue; // bucket sentinel node (not a primitive)
            const uint32_t type = (uint32_t)prim->type;
            if (type == 0)
                continue; // type 0 is a no-op in the original switch
            primCount++;

            if ((type & 0xFE00) != 0)
            {
                // trans_matrix (scaler). The scaler itself is a state update;
                // there is no draw.
                applyTransMatrix(env.marni, prim);
                if (throttle(transMatrixLogCounter, 1000))
                    logging::logDebug("[sdlgpu] ot[{}]: trans_matrix prim type 0x{} - state applied (aspect {}x{} prj {} centre {}x{})",
                        otIndex, hexStr(type), env.marni->aspect_x, env.marni->aspect_y, env.marni->field_8C7EDC,
                        env.marni->field_8C7EC4, env.marni->field_8C7EC8);
                continue;
            }

            switch (type & 0xFFFFF)
            {
            case 0: break;
            case 17: decodeLine(env, prim, false); break;
            case 18: decodeLine(env, prim, true); break;
            case 33:
            case 36:
            case 37:
            case 38:
            case 44:
            case 45:
            case 46:
            case 61:
            case 69:
            case 70:
            case 73:
            case 76:
            case 77: decodeSprPoly(env, prim); break;
            case 88:
            case 0x100 | 88:
            {
                const bool drew = decodeTransObject(env, prim);
                if (throttle(transObjectLogCounter, 600))
                    logging::logDebug("[sdlgpu] ot[{}]: trans_object (3D character) type 0x{} - {}", otIndex, hexStr(type), drew ? "rendered" : "no draw");
                break;
            }
            case 256:
                applyMatrixCopy256(env.marni, prim);
                if (throttle(transMatrixLogCounter, 1000))
                    logging::logDebug("[sdlgpu] ot[{}]: type 256 matrix copy - applied", otIndex);
                break;
            case 0x10000 | 44:
            case 0x10000 | 45:
            case 0x10000 | 73:
            case 0x10000 | 76:
            case 0x10000 | 77: decodeSprPoly(env, prim); break;
            default:
                if (throttle(skipTypeLogCounter, 300))
                    logging::logWarning("[sdlgpu] ot[{}]: invalid primitive header type 0x{} - SKIPPED (original would kill the GPU)", otIndex, hexStr(type));
                env.stats->skipped++;
                break;
            }
        }
    }

    // ── ordering-table helpers (reimplemented locally; the marni.cpp
    //    versions route through the original rendering path) ─────────────

    static int otAddPrimitiveAsZ(MarniOt* ot, Prim* prim, int z)
    {
        if (!ot->is_valid || ot->zdepth <= 0 || ot->pHead == nullptr)
            return 0;
        const int n = std::clamp(z, 0, ot->zdepth - 1);
        auto last = ot->pHead + (ot->zdepth - n) - 1;
        prim->pNext = last->pNext;
        last->pNext = prim;
        return 1;
    }

    static int addPrimitiveFront(Marni* self, Prim* prim, int z)
    {
        if (!self->is_gpu_active)
            return 0;
        if ((prim->type & 8) != 0)
        {
            logging::logWarning("[sdlgpu] add_primitive_front rejected ZCAL prim type 0x{}", hexStr((uint32_t)prim->type));
            return 0;
        }
        return otAddPrimitiveAsZ(&self->otag[0], prim, z);
    }

    static int addPrimitiveBack(Marni* self, Prim* prim, int z)
    {
        if (!self->is_gpu_active)
            return 0;
        if ((prim->type & 8) != 0)
        {
            logging::logWarning("[sdlgpu] add_primitive_back rejected ZCAL prim type 0x{}", hexStr((uint32_t)prim->type));
            return 0;
        }
        return otAddPrimitiveAsZ(&self->otag[3], prim, z);
    }

    static int addPrimitiveScaler(Marni* self, Prim* prim, int z)
    {
        if (!self->is_gpu_active)
            return 0;
        return otAddPrimitiveAsZ(&self->otag[1], prim, z);
    }

    static void otClear(MarniOt* ot)
    {
        if (!ot->is_valid || ot->zdepth <= 0 || ot->pHead == nullptr)
            return;
        for (int i = 0; i < ot->zdepth - 1; i++)
            ot->pHead[i].pNext = &ot->pHead[i + 1];
        auto& last = ot->pHead[ot->zdepth - 1];
        last.pNext = nullptr;
        last.type = 0;
        ot->pCurrent = ot->pHead;
    }

    // ── BMP dump ─────────────────────────────────────────────────────────
    // Writes a 24-bit BGR bottom-up BMP with a DIB header.
    static void writeBmp(const char* filename, const uint8_t* rgba, int w, int h)
    {
        const int rowBytes24 = (w * 3 + 3) & ~3;
        std::vector<uint8_t> bmp;
        const size_t pixelBytes = (size_t)rowBytes24 * h;
        const size_t fileSize = 14 + 40 + pixelBytes;
        bmp.resize(fileSize);
        uint8_t* p = bmp.data();

        // BITMAPFILEHEADER
        p[0] = 'B'; p[1] = 'M';
        *(uint32_t*)(p + 2) = (uint32_t)fileSize;
        *(uint32_t*)(p + 10) = 14 + 40;
        // BITMAPINFOHEADER
        *(uint32_t*)(p + 14) = 40;
        *(int32_t*)(p + 18) = w;
        *(int32_t*)(p + 22) = h; // positive: bottom-up
        *(uint16_t*)(p + 26) = 1;
        *(uint16_t*)(p + 28) = 24;
        *(uint32_t*)(p + 30) = 0; // BI_RGB
        *(uint32_t*)(p + 34) = (uint32_t)pixelBytes;

        uint8_t* out = p + 54;
        for (int y = 0; y < h; y++)
        {
            // Bottom-up: output row h-1-y first.
            const uint8_t* src = rgba + (size_t)(h - 1 - y) * w * 4;
            uint8_t* dst = out + (size_t)y * rowBytes24;
            for (int x = 0; x < w; x++)
            {
                dst[x * 3 + 0] = src[x * 4 + 2]; // B
                dst[x * 3 + 1] = src[x * 4 + 1]; // G
                dst[x * 3 + 2] = src[x * 4 + 0]; // R
            }
        }

        std::ofstream f(filename, std::ios::binary);
        if (f)
            f.write((const char*)bmp.data(), (std::streamsize)bmp.size());
    }

    // Downloads the guest framebuffer and writes gpu_dump_XXXXX.bmp. Fires at
    // frame 30 and then every `interval` frames (OPENRE_SDLGPU_DUMP env var;
    // 0 or unset disables; default 0).
    static void maybeDumpSceneTexture(SDL_GPUDevice* dev, SDL_GPUTexture* fb, int w, int h)
    {
        static uint64_t dumpCounter = 0;
        static const int interval = []() {
            const char* e = std::getenv("OPENRE_SDLGPU_DUMP");
            return e ? std::atoi(e) : 0;
        }();
        if (interval <= 0)
            return;
        dumpCounter++;
        if (dumpCounter != 30 && (dumpCounter < 30 || (dumpCounter - 30) % (uint64_t)interval != 0))
            return;

        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        tbci.size = (Uint32)w * (Uint32)h * 4;
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tbci);
        if (!tb)
        {
            logging::logError("[sdlgpu] dump: SDL_CreateGPUTransferBuffer failed: {}", SDL_GetError());
            return;
        }
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
        if (!cmd)
        {
            SDL_ReleaseGPUTransferBuffer(dev, tb);
            return;
        }
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        if (cp)
        {
            SDL_GPUTextureRegion src{};
            src.texture = fb;
            src.w = (Uint32)w;
            src.h = (Uint32)h;
            src.d = 1;
            SDL_GPUTextureTransferInfo dst{};
            dst.transfer_buffer = tb;
            dst.pixels_per_row = (Uint32)w;
            dst.rows_per_layer = (Uint32)h;
            SDL_DownloadFromGPUTexture(cp, &src, &dst);
            SDL_EndGPUCopyPass(cp);
        }
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_WaitForGPUIdle(dev);

        void* data = SDL_MapGPUTransferBuffer(dev, tb, false);
        if (data)
        {
            char filename[64];
            std::snprintf(filename, sizeof(filename), "gpu_dump_%05llu.bmp", (unsigned long long)dumpCounter);
            writeBmp(filename, (const uint8_t*)data, w, h);
            logging::logDebug("[sdlgpu] dumped guest framebuffer {}x{} to {}", w, h, filename);
        }
        else
        {
            logging::logError("[sdlgpu] dump: SDL_MapGPUTransferBuffer failed: {}", SDL_GetError());
        }
        SDL_UnmapGPUTransferBuffer(dev, tb);
        SDL_ReleaseGPUTransferBuffer(dev, tb);
    }

    // Diagnostic stage logging + optional full GPU serialization.
    // OPENRE_SDLGPU_STAGE=1 logs every submit boundary; OPENRE_SDLGPU_SYNC=1
    // additionally calls SDL_WaitForGPUIdle after every submit so the device
    // hang can be bisected to a single submission.
    static void submitDiag(SDL_GPUDevice* dev, const char* stage)
    {
        static const bool stageOn = []() {
            const char* e = std::getenv("OPENRE_SDLGPU_STAGE");
            return e && e[0] && e[0] != '0';
        }();
        static const bool sync = []() {
            const char* e = std::getenv("OPENRE_SDLGPU_SYNC");
            return e && e[0] && e[0] != '0';
        }();
        if (stageOn)
            logging::logInfo("[sdlgpu][stage] submit: {}", stage);
        if (sync)
        {
            SDL_WaitForGPUIdle(dev);
            logging::logInfo("[sdlgpu][sync] GPU idle after {}", stage);
        }
    }

    static bool stageDiag()
    {
        static const bool stage = []() {
            const char* e = std::getenv("OPENRE_SDLGPU_STAGE");
            return e && e[0] && e[0] != '0';
        }();
        return stage;
    }

    // ── texture decoding (Image -> RGBA8) ────────────────────────────────
    static bool decodeToRgba(const Image& img, std::vector<uint8_t>& rgba, int clut = 0)
    {
        rgba.assign((size_t)img.width * img.height * 4, 0);

        if (img.depth == 16)
        {
            const size_t needed = (size_t)img.width * img.height * 2;
            if (img.pixels.size() < needed)
            {
                logging::logWarning("[sdlgpu] loadTexture: 16bpp image pixels too small ({} < {})", img.pixels.size(), needed);
                return false;
            }
            const auto* src = (const uint16_t*)img.pixels.data();

            for (int y = 0; y < img.height; y++)
            {
                const uint16_t* row = src + (size_t)y * img.width;
                uint8_t* dst = rgba.data() + (size_t)y * img.width * 4;
                for (int x = 0; x < img.width; x++)
                {
                    const uint16_t v = row[x];
                    // RE2 PC transparency: the original applied a black color key
                    // (DDCOLORKEY {0,0}) to texture surfaces and the software
                    // surface reads collapse black pixels (RGB == 0, e.g. 0x0000
                    // or 0x8000) to fully transparent. Bit 15 is otherwise not
                    // used by the game data (16bpp textures are effectively
                    // X1R5G5B5), so only the colour channels decide alpha.
                    const bool transparent = (v & 0x7FFF) == 0;
                    if (img.psxFormat)
                    {
                        // PSX 555: red low, no alpha.
                        dst[0] = (uint8_t)((v & 0x1F) << 3);
                        dst[1] = (uint8_t)(((v >> 5) & 0x1F) << 3);
                        dst[2] = (uint8_t)(((v >> 10) & 0x1F) << 3);
                        dst[3] = transparent ? 0x00 : 0xFF;
                    }
                    else
                    {
                        // 555 layout: blue low, alpha in bit 15.
                        dst[0] = (uint8_t)(((v >> 10) & 0x1F) << 3);
                        dst[1] = (uint8_t)(((v >> 5) & 0x1F) << 3);
                        dst[2] = (uint8_t)((v & 0x1F) << 3);
                        dst[3] = transparent ? 0x00 : 0xFF;
                    }
                    dst += 4;
                }
            }
            return true;
        }

        if (img.depth == 32)
        {
            const size_t needed = (size_t)img.width * img.height * 4;
            if (img.pixels.size() < needed)
            {
                logging::logWarning("[sdlgpu] loadTexture: 32bpp image pixels too small ({} < {})", img.pixels.size(), needed);
                return false;
            }
            const auto* src = (const uint32_t*)img.pixels.data();
            for (int y = 0; y < img.height; y++)
            {
                const uint32_t* row = src + (size_t)y * img.width;
                uint8_t* dst = rgba.data() + (size_t)y * img.width * 4;
                for (int x = 0; x < img.width; x++)
                {
                    const uint32_t v = row[x];
                    // Marni 32bpp work surfaces are A8R8G8B8 (desc a_shift=24,
                    // r_shift=16, g_shift=8, b_shift=0). The kage shadow texture
                    // is built this way: each pixel is a packed 0xAARRGGBB where
                    // 0 is fully transparent and non-zero pixels are semi-opaque
                    // dark grey.
                    dst[0] = (uint8_t)((v >> 16) & 0xFF);
                    dst[1] = (uint8_t)((v >> 8) & 0xFF);
                    dst[2] = (uint8_t)(v & 0xFF);
                    dst[3] = (uint8_t)((v >> 24) & 0xFF);
                    dst += 4;
                }
            }
            return true;
        }

        if (img.depth == 4 || img.depth == 8)
        {
            if (img.palBpp != 16 || img.palCnt <= 0)
            {
                logging::logWarning("[sdlgpu] loadTexture: paletted image without 16bpp palette (palBpp={} palCnt={})", img.palBpp, img.palCnt);
                return false;
            }
            const int entriesPerPal = img.depth == 4 ? 16 : 256;
            const size_t palNeeded = (size_t)entriesPerPal * img.palCnt * 2;
            if (img.palette.size() < palNeeded)
            {
                logging::logWarning("[sdlgpu] loadTexture: palette too small ({} < {})", img.palette.size(), palNeeded);
                return false;
            }

            if (clut < 0 || clut >= img.palCnt)
            {
                logging::logWarning("[sdlgpu] loadTexture: clut {} out of range (palCnt {})", clut, img.palCnt);
                return false;
            }
            const auto* pal = (const uint16_t*)img.palette.data() + (size_t)clut * entriesPerPal;

            // Track whether the palette contains any usable colour at all. The
            // kage shadow texture (mode 0x21) has every entry cleared to
            // 0x0000 by Kage_set, so the usual "zero entry is transparent"
            // rule would decode the whole texture as empty. Like the D3D
            // color-key path, only index 0 is the transparent key there; any
            // other index is opaque so the pixel indices define the shadow
            // shape and the vertex colour supplies the alpha/opacity.
            bool hasPalColor = false;
            uint8_t lut[256 * 4];
            for (int i = 0; i < entriesPerPal; i++)
            {
                const uint16_t e = pal[i];
                // Same black color-key semantics as the 16bpp path: palette
                // entries with no colour (0x0000/0x8000) are transparent.
                lut[i * 4 + 0] = (uint8_t)((e & 0x1F) << 3);
                lut[i * 4 + 1] = (uint8_t)(((e >> 5) & 0x1F) << 3);
                lut[i * 4 + 2] = (uint8_t)(((e >> 10) & 0x1F) << 3);
                if ((e & 0x7FFF) == 0)
                    lut[i * 4 + 3] = 0x00;
                else
                {
                    lut[i * 4 + 3] = 0xFF;
                    hasPalColor = i != 0;
                }
            }
            if (!hasPalColor)
            {
                // All-zero palette: honour the D3D colour key (index 0) only.
                for (int i = 1; i < entriesPerPal; i++)
                    lut[i * 4 + 3] = 0xFF;
            }

            if (img.depth == 4)
            {
                const size_t rowBytes = ((size_t)img.width + 1) / 2;
                if (img.pixels.size() < rowBytes * (size_t)img.height)
                {
                    logging::logWarning("[sdlgpu] loadTexture: 4bpp image pixels too small");
                    return false;
                }
                for (int y = 0; y < img.height; y++)
                {
                    const uint8_t* src = img.pixels.data() + (size_t)y * rowBytes;
                    uint8_t* dst = rgba.data() + (size_t)y * img.width * 4;
                    for (int x = 0; x < img.width; x++)
                    {
                        // PSX TIM 4bpp layout: in each byte the leftmost (even x)
                        // texel is in the low nibble (bits 0-3), the next in the
                        // high nibble. See psx-spx GPU texture bitmaps.
                        const uint8_t nib = (x & 1) ? (src[x / 2] >> 4) : (src[x / 2] & 0xF);
                        std::memcpy(dst + x * 4, lut + (size_t)nib * 4, 4);
                    }
                }
            }
            else
            {
                const size_t rowBytes = (size_t)img.width;
                if (img.pixels.size() < rowBytes * (size_t)img.height)
                {
                    logging::logWarning("[sdlgpu] loadTexture: 8bpp image pixels too small");
                    return false;
                }
                for (int y = 0; y < img.height; y++)
                {
                    const uint8_t* src = img.pixels.data() + (size_t)y * rowBytes;
                    uint8_t* dst = rgba.data() + (size_t)y * img.width * 4;
                    for (int x = 0; x < img.width; x++)
                    {
                        const uint8_t idx = src[x];
                        std::memcpy(dst + x * 4, lut + (size_t)idx * 4, 4);
                    }
                }
            }
            return true;
        }

        logging::logWarning("[sdlgpu] loadTexture: unsupported depth {}", img.depth);
        return false;
    }

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// SdlGpuRenderer::Impl - all SDL3/GPU state
// ─────────────────────────────────────────────────────────────────────────
struct SdlGpuRenderer::Impl
{
    // Device/window/framebuffer are fetched fresh from system::gpu each call
    // so they survive re-creation (resolution changes, guest framebuffer
    // rebuilds).

    // Shaders + samplers (created lazily on first draw).
    SDL_GPUShader* vertexShader = nullptr;
    SDL_GPUShader* texturedFrag = nullptr;
    SDL_GPUShader* untexturedFrag = nullptr;
    SDL_GPUSampler* samplerLinear = nullptr;
    SDL_GPUSampler* samplerNearest = nullptr;

    // Scene pipelines keyed by makePipelineKey(). Created lazily.
    std::unordered_map<uint32_t, SDL_GPUGraphicsPipeline*> pipelines;

    // Present (letterbox blit) pipeline for the swapchain format.
    SDL_GPUGraphicsPipeline* presentPipeline = nullptr;
    SDL_GPUTextureFormat presentFormat = SDL_GPU_TEXTUREFORMAT_INVALID;

    // Depth target for the scene pass, recreated when the framebuffer size
    // changes.
    SDL_GPUTexture* depthTexture = nullptr;
    int depthW = 0;
    int depthH = 0;

    // Per-frame vertex pool (grows on demand; re-uploaded every draw()).
    SDL_GPUBuffer* vertexBuffer = nullptr;
    SDL_GPUTransferBuffer* vertexTransfer = nullptr;
    uint32_t vertexCapacity = 0;

    // Small dedicated buffer for the flip() letterbox quad.
    SDL_GPUBuffer* presentVertexBuffer = nullptr;
    SDL_GPUTransferBuffer* presentTransfer = nullptr;

    // Movie overlay: the movie player captures decoded DirectShow frames
    // (top-down RGB24) and forwards them via system::gpu::set_movie_frame;
    // flip() uploads each new frame into movieTexture and composites it into
    // the guest framebuffer right after the scene pass, so cutscenes render
    // into the framebuffer instead of a child video window.
    SDL_GPUTexture* movieTexture = nullptr;
    SDL_GPUTransferBuffer* movieUpload = nullptr;
    Uint32 movieTexW = 0;
    Uint32 movieTexH = 0;
    SDL_GPUGraphicsPipeline* moviePipeline = nullptr;
    SDL_GPUBuffer* movieVertexBuffer = nullptr;
    SDL_GPUTransferBuffer* movieVertexTransfer = nullptr;

    // Texture registry keyed by the MARNI texture handle.
    std::unordered_map<int, TextureEntry> textures;
    int nextHandle = 1;

    // Arena + per-frame vertex/draw-call scratch.
    PrimitiveArena arena;
    std::vector<uint8_t> frameVertices;
    std::vector<DrawCall> frameCalls;
    ParseStats stats;
    SDL_GPUSampler* frameSampler = nullptr;

    // Pending-clear state from clear() / CLEAR_TARGET.
    bool pendingClearTarget = false;
    bool pendingClearDepth = false;
    // The target clear colour is the material ambient colour (set via
    // marni's scaler rgb1 -> ambient_b/g/r -> material). Capture it at
    // clear() time so draw() can clear the framebuffer to it instead of black.
    SDL_FColor pendingClearColor{ 0.0f, 0.0f, 0.0f, 1.0f };

    // Throttled-log counters.
    uint64_t drawCount = 0;
    uint64_t logMissingTexture = 0;
    uint64_t logTextureFallback = 0;
    uint64_t logClut = 0;
    uint64_t logClutRaw = 0;
    uint64_t logMovieTex = 0;
    uint64_t logNoDecoder = 0;
    uint64_t logSkipType = 0;
    uint64_t logTransObject = 0;
    uint64_t logTransMatrix = 0;

    bool ensureShadersAndSamplers(SDL_GPUDevice* dev)
    {
        if (vertexShader && texturedFrag && untexturedFrag && samplerLinear && samplerNearest)
            return true;

        if (!vertexShader || !texturedFrag || !untexturedFrag)
        {
            const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(dev);
            SDL_GPUShaderFormat format;
            const uint8_t* vsCode;
            const uint8_t* texCode;
            const uint8_t* untexCode;
            uint32_t vsSize, texSize, untexSize;
            if ((formats & SDL_GPU_SHADERFORMAT_DXIL) != 0)
            {
                format = SDL_GPU_SHADERFORMAT_DXIL;
                vsCode = gfx::gTLVertexDxil;
                vsSize = gfx::gTLVertexDxilSize;
                texCode = gfx::gTLTexturedFragDxil;
                texSize = gfx::gTLTexturedFragDxilSize;
                untexCode = gfx::gTLUntexturedFragDxil;
                untexSize = gfx::gTLUntexturedFragDxilSize;
                logging::logInfo("[sdlgpu] device shader format: DXIL");
            }
            else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0)
            {
                format = SDL_GPU_SHADERFORMAT_SPIRV;
                vsCode = gfx::gTLVertexSpirv;
                vsSize = gfx::gTLVertexSpirvSize;
                texCode = gfx::gTLTexturedFragSpirv;
                texSize = gfx::gTLTexturedFragSpirvSize;
                untexCode = gfx::gTLUntexturedFragSpirv;
                untexSize = gfx::gTLUntexturedFragSpirvSize;
                logging::logInfo("[sdlgpu] device shader format: SPIRV");
            }
            else
            {
                logging::logError("[sdlgpu] no supported shader format (got 0x{}); device cannot render", hexStr((uint32_t)formats));
                return false;
            }

            SDL_GPUShaderCreateInfo vsInfo{};
            vsInfo.code_size = vsSize;
            vsInfo.code = vsCode;
            vsInfo.entrypoint = "main";
            vsInfo.format = format;
            vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
            vsInfo.num_samplers = 0;
            vsInfo.num_uniform_buffers = 1;
            if (!vertexShader)
            {
                vertexShader = SDL_CreateGPUShader(dev, &vsInfo);
                if (!vertexShader)
                {
                    logging::logError("[sdlgpu] SDL_CreateGPUShader(vertex) failed: {}", SDL_GetError());
                    return false;
                }
            }

            SDL_GPUShaderCreateInfo texInfo{};
            texInfo.code_size = texSize;
            texInfo.code = texCode;
            texInfo.entrypoint = "main";
            texInfo.format = format;
            texInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
            texInfo.num_samplers = 1;
            if (!texturedFrag)
            {
                texturedFrag = SDL_CreateGPUShader(dev, &texInfo);
                if (!texturedFrag)
                {
                    logging::logError("[sdlgpu] SDL_CreateGPUShader(textured frag) failed: {}", SDL_GetError());
                    return false;
                }
            }

            SDL_GPUShaderCreateInfo untexInfo{};
            untexInfo.code_size = untexSize;
            untexInfo.code = untexCode;
            untexInfo.entrypoint = "main";
            untexInfo.format = format;
            untexInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
            untexInfo.num_samplers = 0;
            if (!untexturedFrag)
            {
                untexturedFrag = SDL_CreateGPUShader(dev, &untexInfo);
                if (!untexturedFrag)
                {
                    logging::logError("[sdlgpu] SDL_CreateGPUShader(untextured frag) failed: {}", SDL_GetError());
                    return false;
                }
            }
        }

        if (!samplerLinear || !samplerNearest)
        {
            SDL_GPUSamplerCreateInfo sci{};
            sci.min_filter = SDL_GPU_FILTER_NEAREST;
            sci.mag_filter = SDL_GPU_FILTER_NEAREST;
            sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            sci.mip_lod_bias = 0.0f;
            sci.max_anisotropy = 1.0f;
            sci.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
            sci.min_lod = 0.0f;
            sci.max_lod = 0.0f;
            sci.enable_anisotropy = false;
            sci.enable_compare = false;
            if (!samplerNearest)
            {
                samplerNearest = SDL_CreateGPUSampler(dev, &sci);
                if (!samplerNearest)
                {
                    logging::logError("[sdlgpu] SDL_CreateGPUSampler(nearest) failed: {}", SDL_GetError());
                    return false;
                }
            }
            sci.min_filter = SDL_GPU_FILTER_LINEAR;
            sci.mag_filter = SDL_GPU_FILTER_LINEAR;
            if (!samplerLinear)
            {
                samplerLinear = SDL_CreateGPUSampler(dev, &sci);
                if (!samplerLinear)
                {
                    logging::logError("[sdlgpu] SDL_CreateGPUSampler(linear) failed: {}", SDL_GetError());
                    return false;
                }
            }
        }
        return true;
    }

    SDL_GPUGraphicsPipeline* getScenePipeline(SDL_GPUDevice* dev, uint32_t key)
    {
        if (!ensureShadersAndSamplers(dev))
            return nullptr;
        auto it = pipelines.find(key);
        if (it != pipelines.end())
            return it->second;

        const bool textured = (key & 1u) != 0;
        const BlendSel blend = (BlendSel)((key >> 1) & 3u);
        const bool depthWrite = (key & (1u << 3)) != 0;
        const bool depthTest = (key & (1u << 4)) != 0;

        SDL_GPUGraphicsPipeline* pipe = createPipeline(dev, textured ? texturedFrag : untexturedFrag, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, blend, depthWrite, depthTest, true);
        if (pipe)
            pipelines[key] = pipe;
        return pipe;
    }

    SDL_GPUGraphicsPipeline* createPipeline(SDL_GPUDevice* dev, SDL_GPUShader* frag, SDL_GPUTextureFormat colorFormat, BlendSel blend, bool depthWrite, bool depthTest, bool hasDepthTarget)
    {
        SDL_GPUVertexBufferDescription vbDesc{};
        vbDesc.slot = 0;
        vbDesc.pitch = sizeof(TlVertex);
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute attrs[3]{};
        attrs[0].location = 0;
        attrs[0].buffer_slot = 0;
        attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attrs[0].offset = 0;
        attrs[1].location = 1;
        attrs[1].buffer_slot = 0;
        attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
        attrs[1].offset = 16;
        attrs[2].location = 2;
        attrs[2].buffer_slot = 0;
        attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[2].offset = 24;

        SDL_GPUVertexInputState inputState{};
        inputState.vertex_buffer_descriptions = &vbDesc;
        inputState.num_vertex_buffers = 1;
        inputState.vertex_attributes = attrs;
        inputState.num_vertex_attributes = 3;

        SDL_GPURasterizerState raster{};
        raster.fill_mode = SDL_GPU_FILLMODE_FILL;
        raster.cull_mode = SDL_GPU_CULLMODE_NONE;
        raster.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        raster.enable_depth_clip = true;

        SDL_GPUMultisampleState ms{};
        ms.sample_count = SDL_GPU_SAMPLECOUNT_1;

        SDL_GPUDepthStencilState depthStencil{};
        depthStencil.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        depthStencil.enable_depth_test = depthTest;
        depthStencil.enable_depth_write = depthWrite;
        depthStencil.enable_stencil_test = false;

        SDL_GPUColorTargetBlendState bs{};
        bs.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        bs.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        bs.color_blend_op = SDL_GPU_BLENDOP_ADD;
        bs.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        bs.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        bs.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        bs.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
        switch (blend)
        {
        case BlendSel::None:
            bs.enable_blend = false;
            break;
        case BlendSel::Alpha:
            bs.enable_blend = true;
            break;
        case BlendSel::Add:
            bs.enable_blend = true;
            bs.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            bs.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            break;
        case BlendSel::SrcColor:
            bs.enable_blend = true;
            bs.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_COLOR;
            bs.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_COLOR;
            break;
        }

        SDL_GPUColorTargetDescription target{};
        target.format = colorFormat;
        target.blend_state = bs;

        SDL_GPUGraphicsPipelineTargetInfo targetInfo{};
        targetInfo.color_target_descriptions = &target;
        targetInfo.num_color_targets = 1;
        targetInfo.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
        targetInfo.has_depth_stencil_target = hasDepthTarget;

        SDL_GPUGraphicsPipelineCreateInfo ci{};
        ci.vertex_shader = vertexShader;
        ci.fragment_shader = frag;
        ci.vertex_input_state = inputState;
        ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
        ci.rasterizer_state = raster;
        ci.multisample_state = ms;
        ci.depth_stencil_state = depthStencil;
        ci.target_info = targetInfo;

        SDL_GPUGraphicsPipeline* pipe = SDL_CreateGPUGraphicsPipeline(dev, &ci);
        if (!pipe)
            logging::logError("[sdlgpu] SDL_CreateGPUGraphicsPipeline failed (blend={} depthWrite={} depthTest={}): {}", (int)blend, depthWrite, depthTest, SDL_GetError());
        return pipe;
    }

    SDL_GPUGraphicsPipeline* getPresentPipeline(SDL_GPUDevice* dev, SDL_GPUTextureFormat swapFormat)
    {
        if (!ensureShadersAndSamplers(dev))
            return nullptr;
        if (presentPipeline && presentFormat == swapFormat)
            return presentPipeline;
        if (presentPipeline)
        {
            SDL_WaitForGPUIdle(dev);
            SDL_ReleaseGPUGraphicsPipeline(dev, presentPipeline);
            presentPipeline = nullptr;
        }
        presentPipeline = createPipeline(dev, texturedFrag, swapFormat, BlendSel::None, false, false, false);
        presentFormat = swapFormat;
        logging::logInfo("[sdlgpu] present pipeline created for swapchain format {}", (int)swapFormat);
        return presentPipeline;
    }

    bool ensureDepthTexture(SDL_GPUDevice* dev, int w, int h)
    {
        if (depthTexture && depthW == w && depthH == h)
            return true;
        if (depthTexture)
        {
            SDL_WaitForGPUIdle(dev);
            SDL_ReleaseGPUTexture(dev, depthTexture);
            depthTexture = nullptr;
        }
        SDL_GPUTextureCreateInfo tci{};
        tci.type = SDL_GPU_TEXTURETYPE_2D;
        tci.format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
        tci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        tci.width = (Uint32)w;
        tci.height = (Uint32)h;
        tci.layer_count_or_depth = 1;
        tci.num_levels = 1;
        tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
        depthTexture = SDL_CreateGPUTexture(dev, &tci);
        if (!depthTexture)
        {
            logging::logError("[sdlgpu] SDL_CreateGPUTexture(depth) failed: {}", SDL_GetError());
            return false;
        }
        depthW = w;
        depthH = h;
        logging::logInfo("[sdlgpu] depth texture created {}x{}", w, h);
        return true;
    }

    bool ensureVertexBuffer(SDL_GPUDevice* dev, uint32_t byteCount)
    {
        if (vertexBuffer && vertexCapacity >= byteCount)
            return true;

        // Growing requires releasing the old buffers, which is only safe once
        // the GPU is idle.
        SDL_WaitForGPUIdle(dev);
        if (vertexBuffer)
        {
            SDL_ReleaseGPUBuffer(dev, vertexBuffer);
            vertexBuffer = nullptr;
        }
        if (vertexTransfer)
        {
            SDL_ReleaseGPUTransferBuffer(dev, vertexTransfer);
            vertexTransfer = nullptr;
        }
        vertexCapacity = std::max(byteCount, 64u * 1024u);

        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bci.size = vertexCapacity;
        vertexBuffer = SDL_CreateGPUBuffer(dev, &bci);
        if (!vertexBuffer)
        {
            logging::logError("[sdlgpu] SDL_CreateGPUBuffer(vertex) failed: {}", SDL_GetError());
            return false;
        }
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size = vertexCapacity;
        vertexTransfer = SDL_CreateGPUTransferBuffer(dev, &tbci);
        if (!vertexTransfer)
        {
            logging::logError("[sdlgpu] SDL_CreateGPUTransferBuffer(vertex) failed: {}", SDL_GetError());
            return false;
        }
        logging::logInfo("[sdlgpu] vertex pool grown to {} bytes", vertexCapacity);
        return true;
    }

    bool ensurePresentBuffers(SDL_GPUDevice* dev)
    {
        if (presentVertexBuffer)
            return true;
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bci.size = 4 * sizeof(TlVertex);
        presentVertexBuffer = SDL_CreateGPUBuffer(dev, &bci);
        if (!presentVertexBuffer)
        {
            logging::logError("[sdlgpu] SDL_CreateGPUBuffer(present) failed: {}", SDL_GetError());
            return false;
        }
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size = 4 * sizeof(TlVertex);
        presentTransfer = SDL_CreateGPUTransferBuffer(dev, &tbci);
        if (!presentTransfer)
        {
            logging::logError("[sdlgpu] SDL_CreateGPUTransferBuffer(present) failed: {}", SDL_GetError());
            return false;
        }
        return true;
    }

    // Movie overlay: uploads the latest captured DirectShow
    // frame (top-down RGB24) into movieTexture and composites it into the
    // guest framebuffer, so cutscenes render into the framebuffer instead of a
    // child video window. Mirrors the reference uploadMovieFrame +
    // blitOverlayTexture. Runs on the main thread during flip().
    void uploadAndCompositeMovie(SDL_GPUDevice* dev, SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* fb, Uint32 fbW, Uint32 fbH)
    {
        if (!dev || !cmd || !fb || fbW == 0 || fbH == 0)
            return;

        // Pull the latest captured frame (top-down RGB24). movie_frame() also
        // clears the "new frame" flag; before the first capture the flag is
        // false and we do nothing (the scene shows), and while no new frame has
        // arrived the last texture is re-blitted each flip.
        int frameW = 0;
        int frameH = 0;
        int framePitch = 0;
        const void* pixels = nullptr;
        if (system::gpu::movie_frame_new())
            pixels = system::gpu::movie_frame(frameW, frameH, framePitch);

        if (pixels != nullptr && frameW > 0 && frameH > 0 && framePitch > 0)
        {
            // (Re)create the movie texture/upload when the frame size changes
            // (a different movie / re-opened file).
            if (movieTexture == nullptr || movieTexW != (Uint32)frameW || movieTexH != (Uint32)frameH)
            {
                SDL_WaitForGPUIdle(dev);
                if (movieTexture)
                {
                    SDL_ReleaseGPUTexture(dev, movieTexture);
                    movieTexture = nullptr;
                }
                if (movieUpload)
                {
                    SDL_ReleaseGPUTransferBuffer(dev, movieUpload);
                    movieUpload = nullptr;
                }

                SDL_GPUTextureCreateInfo info{};
                info.type = SDL_GPU_TEXTURETYPE_2D;
                info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
                info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
                info.width = (Uint32)frameW;
                info.height = (Uint32)frameH;
                info.layer_count_or_depth = 1;
                info.num_levels = 1;
                info.sample_count = SDL_GPU_SAMPLECOUNT_1;
                movieTexture = SDL_CreateGPUTexture(dev, &info);

                SDL_GPUTransferBufferCreateInfo transferInfo{};
                transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                transferInfo.size = (Uint32)frameW * (Uint32)frameH * 4;
                movieUpload = SDL_CreateGPUTransferBuffer(dev, &transferInfo);

                if (movieTexture == nullptr || movieUpload == nullptr)
                {
                    logging::logError("[sdlgpu] movie texture/upload creation failed: {}", SDL_GetError());
                    if (movieTexture)
                    {
                        SDL_ReleaseGPUTexture(dev, movieTexture);
                        movieTexture = nullptr;
                    }
                    if (movieUpload)
                    {
                        SDL_ReleaseGPUTransferBuffer(dev, movieUpload);
                        movieUpload = nullptr;
                    }
                    movieTexW = 0;
                    movieTexH = 0;
                    return;
                }
                movieTexW = (Uint32)frameW;
                movieTexH = (Uint32)frameH;
                logging::logInfo("[sdlgpu] movie texture created ({}x{})", frameW, frameH);
            }

            // Expand the RGB24 rows into the texture's RGBA8 layout (same
            // row-wise conversion as the reference uploadMovieFrame).
            void* mapped = SDL_MapGPUTransferBuffer(dev, movieUpload, false);
            if (mapped == nullptr)
            {
                logging::logError("[sdlgpu] movie frame upload map failed: {}", SDL_GetError());
                return;
            }
            auto* dst = static_cast<uint8_t*>(mapped);
            for (int row = 0; row < frameH; row++)
            {
                const auto* src = static_cast<const uint8_t*>(pixels) + static_cast<size_t>(row) * framePitch;
                auto* dstRow = dst + static_cast<size_t>(row) * frameW * 4;
                for (int x = 0; x < frameW; x++)
                {
                    dstRow[x * 4 + 0] = src[x * 3 + 2];
                    dstRow[x * 4 + 1] = src[x * 3 + 1];
                    dstRow[x * 4 + 2] = src[x * 3 + 0];
                    dstRow[x * 4 + 3] = 0xFF;
                }
            }
            SDL_UnmapGPUTransferBuffer(dev, movieUpload);

            SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
            if (copyPass)
            {
                SDL_GPUTextureTransferInfo source{};
                source.transfer_buffer = movieUpload;
                source.pixels_per_row = (Uint32)frameW;
                source.rows_per_layer = (Uint32)frameH;
                SDL_GPUTextureRegion destination{};
                destination.texture = movieTexture;
                destination.w = (Uint32)frameW;
                destination.h = (Uint32)frameH;
                destination.d = 1;
                SDL_UploadToGPUTexture(copyPass, &source, &destination, false);
                SDL_EndGPUCopyPass(copyPass);
            }
        }

        // Composite the (latest) movie texture over the guest framebuffer: a
        // full-framebuffer opaque blit through the movie pipeline (plain
        // textured blit into R8G8B8A8_UNORM, no depth target). Only drawn once
        // a frame has been captured.
        if (movieTexture == nullptr)
            return;

        if (!moviePipeline)
        {
            moviePipeline = createPipeline(dev, texturedFrag, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, BlendSel::None, false, false, false);
            if (!moviePipeline)
            {
                logging::logError("[sdlgpu] movie pipeline creation failed: {}", SDL_GetError());
                return;
            }
        }

        // Full-framebuffer quad (guest framebuffer pixel space).
        if (!movieVertexBuffer)
        {
            SDL_GPUBufferCreateInfo bci{};
            bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            bci.size = 4 * sizeof(TlVertex);
            movieVertexBuffer = SDL_CreateGPUBuffer(dev, &bci);
            SDL_GPUTransferBufferCreateInfo tbci{};
            tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            tbci.size = 4 * sizeof(TlVertex);
            movieVertexTransfer = SDL_CreateGPUTransferBuffer(dev, &tbci);
            if (!movieVertexBuffer || !movieVertexTransfer)
            {
                logging::logError("[sdlgpu] movie vertex buffer creation failed: {}", SDL_GetError());
                if (movieVertexBuffer)
                {
                    SDL_ReleaseGPUBuffer(dev, movieVertexBuffer);
                    movieVertexBuffer = nullptr;
                }
                if (movieVertexTransfer)
                {
                    SDL_ReleaseGPUTransferBuffer(dev, movieVertexTransfer);
                    movieVertexTransfer = nullptr;
                }
                return;
            }
        }

        TlVertex verts[4]{};
        verts[0].sx = 0.0f; verts[0].sy = 0.0f; verts[0].tu = 0.0f; verts[0].tv = 0.0f;
        verts[1].sx = (float)fbW; verts[1].sy = 0.0f; verts[1].tu = 1.0f; verts[1].tv = 0.0f;
        verts[2].sx = 0.0f; verts[2].sy = (float)fbH; verts[2].tu = 0.0f; verts[2].tv = 1.0f;
        verts[3].sx = (float)fbW; verts[3].sy = (float)fbH; verts[3].tu = 1.0f; verts[3].tv = 1.0f;
        for (auto& vert : verts)
        {
            vert.sz = 0.5f;
            vert.rhw = 1.0f;
            setColor(vert, 0xFFFFFFFF);
        }

        void* mapped = SDL_MapGPUTransferBuffer(dev, movieVertexTransfer, true);
        if (!mapped)
        {
            logging::logError("[sdlgpu] SDL_MapGPUTransferBuffer(movie) failed: {}", SDL_GetError());
            return;
        }
        std::memcpy(mapped, verts, sizeof(verts));
        SDL_UnmapGPUTransferBuffer(dev, movieVertexTransfer);

        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        if (cp)
        {
            SDL_GPUTransferBufferLocation src{};
            src.transfer_buffer = movieVertexTransfer;
            SDL_GPUBufferRegion dst{};
            dst.buffer = movieVertexBuffer;
            dst.size = sizeof(verts);
            SDL_UploadToGPUBuffer(cp, &src, &dst, true);
            SDL_EndGPUCopyPass(cp);
        }

        // Composite into the guest framebuffer (LOAD, preserving anything the
        // scene pass drew; the movie quad covers the whole framebuffer).
        SDL_GPUColorTargetInfo target{};
        target.texture = fb;
        target.mip_level = 0;
        target.layer_or_depth_plane = 0;
        target.load_op = SDL_GPU_LOADOP_LOAD;
        target.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
        if (!pass)
        {
            logging::logError("[sdlgpu] movie composite: SDL_BeginGPURenderPass failed: {}", SDL_GetError());
            return;
        }
        SDL_GPUViewport vp{};
        vp.x = 0.0f;
        vp.y = 0.0f;
        vp.w = (float)fbW;
        vp.h = (float)fbH;
        vp.min_depth = 0.0f;
        vp.max_depth = 1.0f;
        SDL_SetGPUViewport(pass, &vp);

        const float viewportData[4] = { 0.0f, 0.0f, (float)fbW, (float)fbH };
        SDL_PushGPUVertexUniformData(cmd, 0, viewportData, sizeof(viewportData));

        SDL_BindGPUGraphicsPipeline(pass, moviePipeline);

        SDL_GPUTextureSamplerBinding binding{};
        binding.texture = movieTexture;
        binding.sampler = samplerLinear ? samplerLinear : samplerNearest;
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);

        SDL_GPUBufferBinding vbBind{};
        vbBind.buffer = movieVertexBuffer;
        vbBind.offset = 0;
        SDL_BindGPUVertexBuffers(pass, 0, &vbBind, 1);

        SDL_DrawGPUPrimitives(pass, 4, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
    }

    void releaseAll(SDL_GPUDevice* dev)
    {
        if (!dev)
            return;
        SDL_WaitForGPUIdle(dev);
        for (auto& [handle, entry] : textures)
        {
            (void)handle;
            if (entry.texture)
                SDL_ReleaseGPUTexture(dev, entry.texture);
            for (SDL_GPUTexture* t : entry.clutTextures)
            {
                if (t)
                    SDL_ReleaseGPUTexture(dev, t);
            }
        }
        textures.clear();
        for (auto& [key, pipe] : pipelines)
        {
            (void)key;
            if (pipe)
                SDL_ReleaseGPUGraphicsPipeline(dev, pipe);
        }
        pipelines.clear();
        if (presentPipeline)
        {
            SDL_ReleaseGPUGraphicsPipeline(dev, presentPipeline);
            presentPipeline = nullptr;
        }
        if (depthTexture)
        {
            SDL_ReleaseGPUTexture(dev, depthTexture);
            depthTexture = nullptr;
        }
        if (vertexBuffer)
        {
            SDL_ReleaseGPUBuffer(dev, vertexBuffer);
            vertexBuffer = nullptr;
        }
        if (vertexTransfer)
        {
            SDL_ReleaseGPUTransferBuffer(dev, vertexTransfer);
            vertexTransfer = nullptr;
        }
        if (presentVertexBuffer)
        {
            SDL_ReleaseGPUBuffer(dev, presentVertexBuffer);
            presentVertexBuffer = nullptr;
        }
        if (presentTransfer)
        {
            SDL_ReleaseGPUTransferBuffer(dev, presentTransfer);
            presentTransfer = nullptr;
        }
        if (movieTexture)
        {
            SDL_ReleaseGPUTexture(dev, movieTexture);
            movieTexture = nullptr;
        }
        if (movieUpload)
        {
            SDL_ReleaseGPUTransferBuffer(dev, movieUpload);
            movieUpload = nullptr;
        }
        if (moviePipeline)
        {
            SDL_ReleaseGPUGraphicsPipeline(dev, moviePipeline);
            moviePipeline = nullptr;
        }
        if (movieVertexBuffer)
        {
            SDL_ReleaseGPUBuffer(dev, movieVertexBuffer);
            movieVertexBuffer = nullptr;
        }
        if (movieVertexTransfer)
        {
            SDL_ReleaseGPUTransferBuffer(dev, movieVertexTransfer);
            movieVertexTransfer = nullptr;
        }
        movieTexW = 0;
        movieTexH = 0;
        if (vertexShader)
        {
            SDL_ReleaseGPUShader(dev, vertexShader);
            vertexShader = nullptr;
        }
        if (texturedFrag)
        {
            SDL_ReleaseGPUShader(dev, texturedFrag);
            texturedFrag = nullptr;
        }
        if (untexturedFrag)
        {
            SDL_ReleaseGPUShader(dev, untexturedFrag);
            untexturedFrag = nullptr;
        }
        if (samplerLinear)
        {
            SDL_ReleaseGPUSampler(dev, samplerLinear);
            samplerLinear = nullptr;
        }
        if (samplerNearest)
        {
            SDL_ReleaseGPUSampler(dev, samplerNearest);
            samplerNearest = nullptr;
        }
        vertexCapacity = 0;
        depthW = depthH = 0;
    }
};

SdlGpuRenderer::SdlGpuRenderer()
    : impl(std::make_unique<Impl>())
{
    logging::logDebug("[sdlgpu] SdlGpuRenderer constructed");
}

SdlGpuRenderer::~SdlGpuRenderer() = default;

void SdlGpuRenderer::reset()
{
    logging::logDebug("[sdlgpu] reset()");
    impl->arena.reset();
    gGameTable.global_cx = 0;
    gGameTable.global_cy = 0;

    // Lazily ensure the guest framebuffer exists at the configured render
    // resolution (system_gpu re-creates it when the size changes).
    const auto renderRes = system::config::get_render_resolution();
    system::gpu::create_guest_framebuffer(renderRes.width, renderRes.height);
}

void SdlGpuRenderer::begin()
{
    logging::logDebug("[sdlgpu] begin()");
    clearOtags();
    reset();
}

void SdlGpuRenderer::clearOtags()
{
    logging::logDebug("[sdlgpu] clearOtags()");
    Marni* m = gGameTable.pMarni;
    if (!m)
        return;
    // Only otag[0]/otag[1]/otag[3] are allocated by marni::init; the rest are
    // guarded by is_valid in otClear.
    for (auto& ot : m->otag)
        otClear(&ot);
    gGameTable.dword_543A14 = &gGameTable.unk_544148;
}

void SdlGpuRenderer::clear()
{
    logging::logDebug("[sdlgpu] clear()");
    Marni* m = gGameTable.pMarni;
    if (!m)
        return;
    // Match the original marni::clear (0x00404D20): the colour target is only
    // cleared when CLEAR_TARGET is set, and the clear colour is the material
    // ambient colour (ambient_r/g/b, set by addScaler from rgb1). We capture
    // that here so the next draw() clears the framebuffer to the ambient
    // colour (e.g. the dark navy room backdrop) rather than black.
    impl->pendingClearTarget = (m->gpu_flag & GpuFlags::CLEAR_TARGET) != 0;
    impl->pendingClearDepth = true;
    impl->pendingClearColor = {
        (float)m->ambient_r / 255.0f,
        (float)m->ambient_g / 255.0f,
        (float)m->ambient_b / 255.0f,
        1.0f
    };
    logging::logDebug("[sdlgpu] clear() pendingClearTarget={} pendingClearDepth=1 ambient=({},{},{})",
        impl->pendingClearTarget, (unsigned)m->ambient_r, (unsigned)m->ambient_g, (unsigned)m->ambient_b);
}

void SdlGpuRenderer::draw()
{
    Marni* m = gGameTable.pMarni;
    if (!m)
        return;
    SDL_GPUDevice* dev = (SDL_GPUDevice*)system::gpu::device();
    if (!dev)
    {
        logging::logError("[sdlgpu] draw(): no SDL_GPU device (system_gpu not initialized)");
        return;
    }
    SDL_GPUTexture* fb = (SDL_GPUTexture*)system::gpu::guest_framebuffer();
    if (!fb)
    {
        logging::logWarning("[sdlgpu] draw(): no guest framebuffer - nothing to draw into");
        return;
    }
    const int fbW = system::gpu::framebuffer_width();
    const int fbH = system::gpu::framebuffer_height();
    if (fbW <= 0 || fbH <= 0)
    {
        logging::logWarning("[sdlgpu] draw(): invalid framebuffer size {}x{}", fbW, fbH);
        return;
    }
    impl->drawCount++;

    // ---- Parse the ordering tables into CPU-side vertices + draw calls ----
    impl->frameVertices.clear();
    impl->frameCalls.clear();
    impl->stats = ParseStats{};
    // The original always draws sprite/bg prims with NEAREST filtering
    // (set_filtering(self, 0) in trans_spr_poly, filter=0 in every
    // tessellate_insert_draw_op call). LINEAR (filter=1) is used only by the
    // 3D-character path (trans_object_ngtin3_vinsnins). Sprite UVs land on
    // exact texel boundaries, so a LINEAR sampler blends adjacent texels into
    // a per-2-pixel stripe on effect sprites like fire; keep those decoders
    // on NEAREST and let only trans_object use frameSampler (charSampler).
    impl->frameSampler = (m->gpu_flag & (GpuFlags::FILTER_BIT_0 | GpuFlags::FILTER_BIT_1)) == (GpuFlags::FILTER_BIT_0 | GpuFlags::FILTER_BIT_1) ? impl->samplerLinear : impl->samplerNearest;
    if (!impl->frameSampler && !impl->ensureShadersAndSamplers(dev))
        return;
    if (!impl->frameSampler)
        impl->frameSampler = impl->samplerNearest;

    DecodeEnv env;
    env.marni = m;
    env.textures = &impl->textures;
    env.vertexData = &impl->frameVertices;
    env.calls = &impl->frameCalls;
    env.sampler = impl->samplerNearest;
    env.charSampler = impl->frameSampler;
    env.stats = &impl->stats;
    env.logMissingTexture = &impl->logMissingTexture;
    env.logTextureFallback = &impl->logTextureFallback;
    env.logClut = &impl->logClut;
    env.logMovieTex = &impl->logMovieTex;
    env.logNoDecoder = &impl->logNoDecoder;

    // The original draw order: otag[3] (backgrounds), otag[1] (objects),
    // otag[0] (front text).
    parseOrderingTable(env, &m->otag[3], 3, impl->stats.primsBg, impl->logSkipType, impl->logTransObject, impl->logTransMatrix);
    parseOrderingTable(env, &m->otag[1], 1, impl->stats.primsObj, impl->logSkipType, impl->logTransObject, impl->logTransMatrix);
    parseOrderingTable(env, &m->otag[0], 0, impl->stats.primsFg, impl->logSkipType, impl->logTransObject, impl->logTransMatrix);

    const int totalPrims = impl->stats.primsBg + impl->stats.primsObj + impl->stats.primsFg;
    if ((impl->drawCount % 300) == 1 || impl->frameCalls.empty())
    {
        logging::logDebug(
            "[sdlgpu] draw #{}: {} prims (bg {} / obj {} / fg {}) -> {} draw calls, {} vertices, {} lines, {} skipped",
            impl->drawCount, totalPrims, impl->stats.primsBg, impl->stats.primsObj, impl->stats.primsFg,
            impl->frameCalls.size(), impl->stats.drawn, impl->stats.lines, impl->stats.skipped);
    }

    // Nothing to draw and nothing pending: skip the GPU entirely (keeps the
    // log readable for idle frames).
    if (impl->frameCalls.empty() && !impl->pendingClearTarget)
    {
        logging::logDebug("[sdlgpu] draw #{}: nothing to draw, skipping GPU work", impl->drawCount);
        return;
    }

    if (!impl->ensureShadersAndSamplers(dev))
        return;
    if (!impl->ensureDepthTexture(dev, fbW, fbH))
        return;
    if (!impl->ensureVertexBuffer(dev, (uint32_t)impl->frameVertices.size()))
        return;

    if (stageDiag())
        logging::logInfo("[sdlgpu][stage] draw #{}: acquiring cmd ({} verts, {} calls)", impl->drawCount, impl->frameVertices.size(), impl->frameCalls.size());
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd)
    {
        logging::logError("[sdlgpu] SDL_AcquireGPUCommandBuffer failed: {}", SDL_GetError());
        return;
    }

    // ---- Upload the frame's vertices ----
    if (!impl->frameVertices.empty())
    {
        void* mapped = SDL_MapGPUTransferBuffer(dev, impl->vertexTransfer, true);
        if (!mapped)
        {
            logging::logError("[sdlgpu] SDL_MapGPUTransferBuffer(vertex) failed: {}", SDL_GetError());
            SDL_CancelGPUCommandBuffer(cmd);
            return;
        }
        std::memcpy(mapped, impl->frameVertices.data(), impl->frameVertices.size());
        SDL_UnmapGPUTransferBuffer(dev, impl->vertexTransfer);

        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        if (cp)
        {
            SDL_GPUTransferBufferLocation src{};
            src.transfer_buffer = impl->vertexTransfer;
            SDL_GPUBufferRegion dst{};
            dst.buffer = impl->vertexBuffer;
            dst.size = (Uint32)impl->frameVertices.size();
            SDL_UploadToGPUBuffer(cp, &src, &dst, true);
            SDL_EndGPUCopyPass(cp);
        }
    }

    if (stageDiag())
        logging::logInfo("[sdlgpu][stage] draw #{}: vertex copy done, starting scene pass", impl->drawCount);

    // ---- Scene render pass into the guest framebuffer ----
    SDL_GPUColorTargetInfo colorTarget{};
    colorTarget.texture = fb;
    colorTarget.mip_level = 0;
    colorTarget.layer_or_depth_plane = 0;
    // Clear to the ambient colour (the room's backdrop tint, e.g. dark
    // navy) captured at clear() time, not plain black. When there is no
    // pending clear the load_op is LOAD and clear_color is ignored.
    colorTarget.clear_color = impl->pendingClearColor;
    colorTarget.load_op = impl->pendingClearTarget ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depthTarget{};
    depthTarget.texture = impl->depthTexture;
    depthTarget.clear_depth = 1.0f;
    depthTarget.load_op = impl->pendingClearDepth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    depthTarget.store_op = SDL_GPU_STOREOP_STORE;
    depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    impl->pendingClearTarget = false;
    impl->pendingClearDepth = false;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, &depthTarget);
    if (!pass)
    {
        logging::logError("[sdlgpu] SDL_BeginGPURenderPass failed: {}", SDL_GetError());
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }

    SDL_GPUViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.w = (float)fbW;
    vp.h = (float)fbH;
    vp.min_depth = 0.0f;
    vp.max_depth = 1.0f;
    SDL_SetGPUViewport(pass, &vp);

    // gViewport for the TL vertex shader (top-left origin in pixels).
    const float viewportData[4] = { 0.0f, 0.0f, (float)fbW, (float)fbH };
    SDL_PushGPUVertexUniformData(cmd, 0, viewportData, sizeof(viewportData));

    for (const DrawCall& dc : impl->frameCalls)
    {
        SDL_GPUGraphicsPipeline* pipe = impl->getScenePipeline(dev, dc.key);
        if (!pipe)
            continue;
        if (stageDiag())
            logging::logInfo("[sdlgpu][stage]   call: tex={} sampler={} firstVtx={} vtxCount={} key=0x{}",
                (void*)dc.texture, (void*)dc.sampler, dc.firstVertex, dc.vertexCount, hexStr(dc.key));
        SDL_BindGPUGraphicsPipeline(pass, pipe);
        if (dc.texture)
        {
            SDL_GPUTextureSamplerBinding binding{};
            binding.texture = dc.texture;
            binding.sampler = dc.sampler ? dc.sampler : impl->samplerNearest;
            SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
        }
        SDL_GPUBufferBinding vbBind{};
        vbBind.buffer = impl->vertexBuffer;
        vbBind.offset = dc.firstVertex * sizeof(TlVertex);
        SDL_BindGPUVertexBuffers(pass, 0, &vbBind, 1);
        SDL_DrawGPUPrimitives(pass, dc.vertexCount, 1, 0, 0);
    }
    SDL_EndGPURenderPass(pass);

    SDL_SubmitGPUCommandBuffer(cmd);
    submitDiag(dev, "draw-scene");

    // Screenshot harness: dump the guest framebuffer periodically.
    maybeDumpSceneTexture(dev, fb, fbW, fbH);
}

void SdlGpuRenderer::flip()
{
    Marni* m = gGameTable.pMarni;
    if (!m)
        return;
    SDL_GPUDevice* dev = (SDL_GPUDevice*)system::gpu::device();
    if (!dev)
    {
        logging::logError("[sdlgpu] flip(): no SDL_GPU device");
        return;
    }
    SDL_Window* win = (SDL_Window*)system::gpu::window();
    if (!win)
    {
        logging::logError("[sdlgpu] flip(): no window");
        return;
    }
    SDL_GPUTexture* fb = (SDL_GPUTexture*)system::gpu::guest_framebuffer();
    if (!fb)
    {
        logging::logWarning("[sdlgpu] flip(): no guest framebuffer");
        return;
    }
    const int fbW = system::gpu::framebuffer_width();
    const int fbH = system::gpu::framebuffer_height();
    if (fbW <= 0 || fbH <= 0)
        return;

    const SDL_GPUTextureFormat swapFormat = (SDL_GPUTextureFormat)system::gpu::swapchain_format();
    SDL_GPUGraphicsPipeline* presentPipe = impl->getPresentPipeline(dev, swapFormat);
    if (!presentPipe)
        return;
    if (!impl->ensurePresentBuffers(dev))
        return;

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd)
    {
        logging::logError("[sdlgpu] flip(): SDL_AcquireGPUCommandBuffer failed: {}", SDL_GetError());
        return;
    }

    SDL_GPUTexture* swapTex = nullptr;
    Uint32 winW = 0;
    Uint32 winH = 0;
    if (!SDL_AcquireGPUSwapchainTexture(cmd, win, &swapTex, &winW, &winH) || !swapTex)
    {
        // Minimized or too many frames in flight.
        SDL_CancelGPUCommandBuffer(cmd);
        logging::logDebug("[sdlgpu] flip(): swapchain texture unavailable (minimized?) - skipping present");
        return;
    }
    if (winW == 0 || winH == 0)
    {
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }

    // Movie composite: cutscenes render into the guest
    // framebuffer (captured DirectShow frames handed over by the movie player;
    // no child video window). Composited before the letterbox present below so
    // the swapchain blit shows the movie on top of the scene.
    if (system::gpu::movie_frame_valid())
        impl->uploadAndCompositeMovie(dev, cmd, fb, fbW, fbH);

    // Letterbox the guest framebuffer into the swapchain (aspect preserved,
    // black bars).
    const float scale = std::min((float)winW / (float)fbW, (float)winH / (float)fbH);
    const float outW = (float)fbW * scale;
    const float outH = (float)fbH * scale;
    const float x0 = ((float)winW - outW) * 0.5f;
    const float y0 = ((float)winH - outH) * 0.5f;

    TlVertex verts[4]{};
    verts[0].sx = x0; verts[0].sy = y0; verts[0].tu = 0.0f; verts[0].tv = 0.0f;
    verts[1].sx = x0 + outW; verts[1].sy = y0; verts[1].tu = 1.0f; verts[1].tv = 0.0f;
    verts[2].sx = x0; verts[2].sy = y0 + outH; verts[2].tu = 0.0f; verts[2].tv = 1.0f;
    verts[3].sx = x0 + outW; verts[3].sy = y0 + outH; verts[3].tu = 1.0f; verts[3].tv = 1.0f;
    for (auto& vert : verts)
    {
        vert.sz = 0.5f;
        vert.rhw = 1.0f;
        setColor(vert, 0xFFFFFFFF);
    }

    void* mapped = SDL_MapGPUTransferBuffer(dev, impl->presentTransfer, true);
    if (!mapped)
    {
        logging::logError("[sdlgpu] flip(): SDL_MapGPUTransferBuffer(present) failed: {}", SDL_GetError());
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }
    std::memcpy(mapped, verts, sizeof(verts));
    SDL_UnmapGPUTransferBuffer(dev, impl->presentTransfer);

    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    if (cp)
    {
        SDL_GPUTransferBufferLocation src{};
        src.transfer_buffer = impl->presentTransfer;
        SDL_GPUBufferRegion dst{};
        dst.buffer = impl->presentVertexBuffer;
        dst.size = sizeof(verts);
        SDL_UploadToGPUBuffer(cp, &src, &dst, true);
        SDL_EndGPUCopyPass(cp);
    }

    SDL_GPUColorTargetInfo colorTarget{};
    colorTarget.texture = swapTex;
    colorTarget.mip_level = 0;
    colorTarget.layer_or_depth_plane = 0;
    colorTarget.clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &colorTarget, 1, nullptr);
    if (!pass)
    {
        logging::logError("[sdlgpu] flip(): SDL_BeginGPURenderPass failed: {}", SDL_GetError());
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }

    SDL_GPUViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.w = (float)winW;
    vp.h = (float)winH;
    vp.min_depth = 0.0f;
    vp.max_depth = 1.0f;
    SDL_SetGPUViewport(pass, &vp);

    const float viewportData[4] = { 0.0f, 0.0f, (float)winW, (float)winH };
    SDL_PushGPUVertexUniformData(cmd, 0, viewportData, sizeof(viewportData));

    SDL_BindGPUGraphicsPipeline(pass, presentPipe);

    SDL_GPUTextureSamplerBinding binding{};
    binding.texture = fb;
    binding.sampler = impl->samplerLinear ? impl->samplerLinear : impl->samplerNearest;
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);

    SDL_GPUBufferBinding vbBind{};
    vbBind.buffer = impl->presentVertexBuffer;
    vbBind.offset = 0;
    SDL_BindGPUVertexBuffers(pass, 0, &vbBind, 1);

    SDL_DrawGPUPrimitives(pass, 4, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    submitDiag(dev, "flip-present");

    logging::logDebug("[sdlgpu] flip(): presented {}x{} framebuffer into {}x{} window", fbW, fbH, winW, winH);
}

void SdlGpuRenderer::end()
{
    logging::logDebug("[sdlgpu] end() = draw() + flip()");
    draw();
    flip();
}

void SdlGpuRenderer::setGpuFlag(uint32_t flag, bool value)
{
    if (value)
        gGameTable.pMarni->gpu_flag |= flag;
    else
        gGameTable.pMarni->gpu_flag &= ~flag;
    logging::logDebug("[sdlgpu] setGpuFlag(0x{}, {}) -> gpu_flag 0x{}", hexStr(flag), value, hexStr(gGameTable.pMarni->gpu_flag));
}

// 0x00440280
int SdlGpuRenderer::addSprt(const Sprt* p, uint32_t page, int z, int add_back)
{
    Marni* m = gGameTable.pMarni;
    if (m == nullptr)
        return 0;
    if (page >= 0x29)
        return 0;
    if (gGameTable.texture_pages[page].handle == 0)
        return 0;
    if (gGameTable.texture_pages[page].suspended == 1)
        return 0;

    uint16_t clut = p->clut;
    if (throttle(impl->logClutRaw, 200))
        logging::logDebug("[sdlgpu] addSprt RAW clut {} (clutCount {}) page {}", clut, gGameTable.texture_pages[page].clutCount, page);
    if (clut >= gGameTable.texture_pages[page].clutCount)
        clut = 0;

    auto* prim = (MarniSprt*)impl->arena.alloc(sizeof(MarniSprt));
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
        addPrimitiveBack(m, (Prim*)prim, z);
    else
        addPrimitiveFront(m, (Prim*)prim, z);
    logging::logDebug("[sdlgpu] addSprt page {} clut {} type 0x{} ({},{})-({},{}) uv({},{})-({},{}) -> {}",
        page, clut, hexStr((uint32_t)prim->type), prim->x0, prim->y0, prim->x1, prim->y1,
        (unsigned)prim->u0, (unsigned)prim->v0, (unsigned)prim->u1, (unsigned)prim->v1, add_back ? "back" : "front");
    return 1;
}

// 0x00440600
void SdlGpuRenderer::addPolyFt4(const PolyFt4* p, int page, int z, int add_back)
{
    Marni* m = gGameTable.pMarni;
    if (m == nullptr)
        return;
    if (page >= 41 || gGameTable.texture_pages[page].handle == 0 || gGameTable.texture_pages[page].suspended == 1)
        return;

    uint16_t clut = (uint16_t)p->clut;
    if (clut >= gGameTable.texture_pages[page].clutCount)
        clut = 0;

    auto* prim = (MarniSprt*)impl->arena.alloc(sizeof(MarniSprt));
    if (p->r0 == 0x80 && p->g0 == 0x80 && p->b0 == 0x80)
    {
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
        addPrimitiveBack(m, (Prim*)prim, z);
    else
        addPrimitiveFront(m, (Prim*)prim, z);
    logging::logDebug("[sdlgpu] addPolyFt4 page {} type 0x{}", page, hexStr((uint32_t)prim->type));
}

// 0x004407F0
int SdlGpuRenderer::addMask(const Sprt* p, int page, int z)
{
    Marni* m = gGameTable.pMarni;
    if (m == nullptr)
        return 0;
    if (page >= 41)
        return 0;
    if (gGameTable.texture_pages[page].handle == 0)
        return 0;
    if (gGameTable.texture_pages[page].suspended == 1)
        return 0;

    uint16_t clut = p->clut;
    if (clut >= gGameTable.texture_pages[page].clutCount)
        clut = 0;

    auto* prim = (MarniMask*)impl->arena.alloc(sizeof(MarniMask));

    prim->type = 0x1002C;
    if (p->code & 2)
    {
        prim->type = 0x1002D;
        prim->type = (int32_t)(s_sprtTypeMods[p->tag & 3] | 0x1002D);
        prim->color = (uint32_t)p->b | ((uint32_t)p->g << 8) | ((uint32_t)p->r << 16);
    }

    prim->x1 = p->x0;
    prim->y1 = p->y0;
    prim->cornerX = (uint16_t)(p->x0 + p->w - 1);
    prim->cornerY = (uint16_t)(p->y0 + p->h - 1);
    prim->su0 = p->u0;
    prim->sv0 = p->v0;
    prim->su1 = (uint8_t)(p->u0 + (uint8_t)p->w - 1);
    prim->sv1 = (uint8_t)(p->v0 + (uint8_t)p->h - 1);
    prim->texture = gGameTable.texture_pages[page].handle;
    prim->clut = clut;

    if (z >= (int)gGameTable.global_prj / 2)
        prim->scale = (float)z;
    else
        prim->scale = (float)(gGameTable.global_prj / 2);

    addPrimitiveScaler(m, (Prim*)prim, z >> 4);
    logging::logDebug("[sdlgpu] addMask page {} type 0x{} z {} scale {}", page, hexStr((uint32_t)prim->type), z, prim->scale);
    return 1;
}

// 0x00440950
int SdlGpuRenderer::addBgScaled(const BgScaledDesc* bg, int z)
{
    Marni* m = gGameTable.pMarni;
    if (m == nullptr)
        return 0;
    if (gGameTable.bg_tex0 == 0)
        return 0;

    auto* prim = (MarniBgScaled*)impl->arena.alloc(sizeof(MarniBgScaled));
    prim->type = 0x1002C;
    prim->x1 = bg->x;
    prim->y1 = bg->y;
    prim->cornerX = bg->x + bg->w - 1;
    prim->cornerY = bg->y + bg->h - 1;
    prim->su = bg->u;
    prim->sv = bg->v;
    prim->su1 = bg->u + bg->w - 1;
    prim->sv1 = bg->v + bg->h - 1;
    prim->scale = (float)z;
    prim->texture = gGameTable.bg_tex0;

    addPrimitiveScaler(m, (Prim*)prim, z >> 4);
    logging::logDebug("[sdlgpu] addBgScaled bg_tex0 {} z {} scale {}", gGameTable.bg_tex0, z, prim->scale);
    return 1;
}

void SdlGpuRenderer::addBgPrims(int16_t x_off, int16_t y_off)
{
    Marni* m = gGameTable.pMarni;
    if (m == nullptr)
        return;
    if (gGameTable.bg_tex0 != 0)
    {
        auto* prim = (MarniPrim*)impl->arena.alloc(sizeof(PrimSprite));
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
        addPrimitiveBack(m, (Prim*)prim, 15);
    }

    if (gGameTable.bg_tex1 != 0)
    {
        auto* prim = (MarniPrim*)impl->arena.alloc(sizeof(PrimSprite));
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
        addPrimitiveBack(m, (Prim*)prim, 15);

        auto* prim2 = (MarniPrim*)impl->arena.alloc(sizeof(PrimSprite));
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
        addPrimitiveBack(m, (Prim*)prim2, 15);
    }
}

// 0x0043FB30
void SdlGpuRenderer::addBg()
{
    logging::logDebug("[sdlgpu] addBg() bg_tex0={} bg_tex1={} at global_cx/cy {}/{}",
        (uint32_t)gGameTable.bg_tex0, (uint32_t)gGameTable.bg_tex1, (uint32_t)gGameTable.global_cx, (uint32_t)gGameTable.global_cy);
    addBgPrims((int16_t)gGameTable.global_cx, (int16_t)gGameTable.global_cy);
    gGameTable.bgDrawn = 1;
}

// 0x0043FCB0
void SdlGpuRenderer::addBg2(int16_t scroll_y)
{
    logging::logDebug("[sdlgpu] addBg2() scroll_y={} bgScrollTextures {}/{}", scroll_y, (uint32_t)gGameTable.bgScrollTextures[0], (uint32_t)gGameTable.bgScrollTextures[1]);
    addBgPrims(0, scroll_y);

    if (gGameTable.bgScrollTextures[0] != 0)
    {
        auto* prim = (MarniPrim*)impl->arena.alloc(sizeof(PrimSprite));
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
        addPrimitiveBack(gGameTable.pMarni, (Prim*)prim, 15);
    }

    if (gGameTable.bgScrollTextures[1] != 0)
    {
        auto* prim = (MarniPrim*)impl->arena.alloc(sizeof(PrimSprite));
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
        addPrimitiveBack(gGameTable.pMarni, (Prim*)prim, 15);

        auto* prim2 = (MarniPrim*)impl->arena.alloc(sizeof(PrimSprite));
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
        addPrimitiveBack(gGameTable.pMarni, (Prim*)prim2, 15);
    }

    gGameTable.bgDrawn = 1;
}

// 0x00440A20
int SdlGpuRenderer::addScaledSprite(const PolyFt4* prim, int page, int z)
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

    auto* out = (MarniScaledSprite*)impl->arena.alloc(sizeof(MarniScaledSprite));

    if (p->r0 == 0x80 && p->g0 == 0x80 && p->b0 == 0x80)
    {
        out->type = 0x1002C;
    }
    else
    {
        out->type = 0x1002D;
        out->color = (uint32_t)((p->r0 << 16) | (p->g0 << 8) | p->b0);
    }
    if ((p->code & 2) != 0)
        out->type |= s_sprtTypeMods[((uint8_t)p->tpage >> 5) & 3];

    out->x1 = p->x0;
    out->y1 = p->y0;
    out->sizeX = (uint16_t)(p->x3 - 1);
    out->sizeY = (uint16_t)(p->y3 - 1);
    out->su0 = p->u0;
    out->sv0 = p->v0;
    out->su1 = (uint8_t)(p->u3 - 1);
    out->sv1 = (uint8_t)(p->v3 - 1);
    out->texture = gGameTable.texture_pages[page].handle;
    out->scale = (float)z;
    out->clut = clut;

    addPrimitiveScaler(gGameTable.pMarni, (Prim*)out, z >> 4);
    logging::logDebug("[sdlgpu] addScaledSprite page {} type 0x{} z {}", page, hexStr((uint32_t)out->type), z);
    return 1;
}

// 0x00440B70
int SdlGpuRenderer::addScaledPoly(const PolyFt4* prim, int page, int z)
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

    auto* out = (MarniScaledPoly*)impl->arena.alloc(sizeof(MarniScaledPoly));

    if (p->r0 == 0x80 && p->g0 == 0x80 && p->b0 == 0x80)
    {
        out->type = 0x1004C;
        if ((p->code & 2) != 0)
            out->type = (int32_t)(s_polyTypeMods[((uint8_t)p->tpage >> 5) & 3] | 0x1004C);
    }
    else
    {
        out->type = 0x1004D;
        out->color = (uint32_t)((p->r0 << 16) | (p->g0 << 8) | p->b0);
        if ((p->code & 2) != 0)
            out->type = (int32_t)(s_polyTypeMods[((uint8_t)p->tpage >> 5) & 3] | 0x1004D);
    }
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

    addPrimitiveScaler(gGameTable.pMarni, (Prim*)out, z >> 4);
    logging::logDebug("[sdlgpu] addScaledPoly page {} type 0x{} z {}", page, hexStr((uint32_t)out->type), z);
    return 1;
}

// 0x00440DD0
int SdlGpuRenderer::addPolyGt4(const PolyGt4* p, int page, int z)
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

    auto* prim = (MarniPolyGt4*)impl->arena.alloc(sizeof(MarniPolyGt4));
    prim->type = 70;
    prim->color0 = (uint32_t)b0 | ((uint32_t)g0 << 8) | ((uint32_t)r0 << 16);
    prim->color1 = (uint32_t)b1 | ((uint32_t)g1 << 8) | ((uint32_t)r1 << 16);
    prim->color2 = (uint32_t)b2 | ((uint32_t)g2 << 8) | ((uint32_t)r2 << 16);
    prim->color3 = (uint32_t)b3 | ((uint32_t)g3 << 8) | ((uint32_t)r3 << 16);
    if ((p->code & 2) != 0)
        prim->type |= (int32_t)s_polyTypeMods[p->tag & 3];
    prim->x0 = p->x0;
    prim->y0 = p->y0;
    prim->x1 = p->x1;
    prim->y1 = p->y1;
    prim->x2 = p->x2;
    prim->y2 = p->y2;
    prim->x3 = p->x3;
    prim->y3 = p->y3;
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

    addPrimitiveFront(gGameTable.pMarni, (Prim*)prim, z);
    logging::logDebug("[sdlgpu] addPolyGt4 page {} type 0x{} z {}", page, hexStr((uint32_t)prim->type), z);
    return 1;
}

// 0x00440FF0
int SdlGpuRenderer::addPolyFt42(const PolyFt4* p, int page, int z)
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

    auto* prim = (MarniPolyFt42*)impl->arena.alloc(sizeof(MarniPolyFt42));
    prim->type = 69;
    const uint8_t r0 = p->r0 > 0x80 ? 0x80 : p->r0;
    const uint8_t g0 = p->g0 > 0x80 ? 0x80 : p->g0;
    const uint8_t b0 = p->b0 > 0x80 ? 0x80 : p->b0;
    prim->color = ((uint32_t)r0 << 16) | ((uint32_t)g0 << 8) | b0;
    if ((p->code & 2) != 0)
        prim->type |= s_polyTypeMods[p->tag & 3];
    prim->x0 = p->x0;
    prim->y0 = p->y0;
    prim->x1 = p->x1;
    prim->y1 = p->y1;
    prim->x2 = p->x2;
    prim->y2 = p->y2;
    prim->x3 = p->x3;
    prim->y3 = p->y3;
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

    addPrimitiveFront(gGameTable.pMarni, (Prim*)prim, z);
    logging::logDebug("[sdlgpu] addPolyFt42 page {} type 0x{} z {}", page, hexStr((uint32_t)prim->type), z);
    return 1;
}

// 0x00441170
int SdlGpuRenderer::addPolyF4(const Tile* p, int z, int is_back)
{
    auto* prim = (MarniPolyF4*)impl->arena.alloc(sizeof(MarniPolyF4));
    prim->type = 33;
    prim->packedCorner0 = ((uint32_t)(uint16_t)p->y0 << 16) | (uint16_t)p->x0;
    prim->x1 = (int16_t)(((uint16_t)p[1].r | ((uint16_t)p[1].g << 8)) - 1);
    prim->y1 = (int16_t)(((uint16_t)p[1].b | ((uint16_t)p[1].code << 8)) - 1);
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
        addPrimitiveBack(gGameTable.pMarni, (Prim*)prim, z);
    else
        addPrimitiveFront(gGameTable.pMarni, (Prim*)prim, z);
    logging::logDebug("[sdlgpu] addPolyF4 type 0x{} ({},{})-({},{})", hexStr((uint32_t)prim->type), prim->x0, prim->y0, prim->x1, prim->y1);
    return 1;
}

// 0x00441270
int SdlGpuRenderer::addTile(const Tile* p, int z, int is_back)
{
    auto* prim = (MarniTile*)impl->arena.alloc(sizeof(MarniTile));
    prim->type = 0x21;
    prim->x0 = p->x0;
    prim->y0 = p->y0;
    prim->x1 = (int16_t)(p->x0 + p->w - 1);
    prim->y1 = (int16_t)(p->y0 + p->h - 1);
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
        addPrimitiveBack(gGameTable.pMarni, (Prim*)prim, z);
    else
        addPrimitiveFront(gGameTable.pMarni, (Prim*)prim, z);
    logging::logDebug("[sdlgpu] addTile type 0x{} ({},{})-({},{})", hexStr((uint32_t)prim->type), prim->x0, prim->y0, prim->x1, prim->y1);
    return 1;
}

// 0x00402210
int SdlGpuRenderer::addScaler(const PrimScaler* p, int z)
{
    auto* copy = (PrimScaler*)impl->arena.alloc(sizeof(PrimScaler));
    std::memcpy(copy, p, sizeof(PrimScaler));
    const int result = addPrimitiveScaler(gGameTable.pMarni, (Prim*)copy, z);
    logging::logDebug("[sdlgpu] addScaler type 0x{} prj {} rate {}x{} z {} rgb1={}", hexStr((uint32_t)copy->type), copy->prj, copy->rate_x, copy->rate_y, z, (unsigned)copy->rgb1);
    return result;
}

// 0x00441370
int SdlGpuRenderer::addLineF2(const LineF2* p, int z, int is_back)
{
    if (p->x0 < 0)
        return 0;

    auto* prim = (PrimLine*)impl->arena.alloc(sizeof(PrimLine));
    prim->type = 17;
    if ((p->code & 2) != 0 && p->tag == 1)
        prim->type = 0x200011;
    prim->x0 = p->x0;
    prim->y0 = p->y0;
    prim->x1 = p->x1;
    prim->y1 = p->y1;
    prim->color0 = (uint32_t)((p->r0 << 16) | (p->g0 << 8) | p->b0);

    if (is_back)
        addPrimitiveBack(gGameTable.pMarni, (Prim*)prim, z);
    else
        addPrimitiveFront(gGameTable.pMarni, (Prim*)prim, z);
    logging::logDebug("[sdlgpu] addLineF2 type 0x{} ({},{})-({},{}) -> {}", hexStr((uint32_t)prim->type), prim->x0, prim->y0, prim->x1, prim->y1, is_back ? "back" : "front");
    return 1;
}

// Uploads `image` as a new SDL_GPU texture, returning the MARNI texture
// handle (1..255, 0 on failure).
int SdlGpuRenderer::loadTexture(const Image& image, uint32_t mode)
{
    logging::logDebug(
        "[sdlgpu] loadTexture {}x{} depth={} palBpp={} palCnt={} psxFormat={} mode=0x{} pixels={}B palette={}B",
        image.width, image.height, image.depth, image.palBpp, image.palCnt, image.psxFormat, hexStr(mode),
        image.pixels.size(), image.palette.size());

    if (image.width <= 0 || image.height <= 0)
    {
        logging::logWarning("[sdlgpu] loadTexture: invalid size {}x{}", image.width, image.height);
        return 0;
    }
    if ((image.width > 256 || image.height > 256) && (mode & 0x4000) == 0)
    {
        logging::logWarning("[sdlgpu] loadTexture: {}x{} exceeds 256x256 (mode 0x{}) - rejecting like create_texture_handle", image.width, image.height, hexStr(mode));
        return 0;
    }
    SDL_GPUDevice* dev = (SDL_GPUDevice*)system::gpu::device();
    if (!dev)
    {
        logging::logError("[sdlgpu] loadTexture: no SDL_GPU device");
        return 0;
    }

    // Allocate a free handle in 1..255 (index 0 is reserved).
    int handle = 0;
    for (int i = 0; i < 255; i++)
    {
        if (impl->nextHandle < 1 || impl->nextHandle > 255)
            impl->nextHandle = 1;
        const int cand = impl->nextHandle++;
        if (impl->nextHandle > 255)
            impl->nextHandle = 1;
        if (impl->textures.find(cand) == impl->textures.end())
        {
            handle = cand;
            break;
        }
    }
    if (handle == 0)
    {
        logging::logError("[sdlgpu] loadTexture: no free texture slot (255 in use)");
        return 0;
    }

    // Paletted images carry palCnt CLUTs; upload one SDL texture per CLUT so
    // prims can pick the right palette (mirrors the pal_count texture
    // nodes). Each variant is decoded with a different palette but shares the
    // same pixel data, so all textures keep the image's width/height and UVs
    // stay valid.
    const bool tempTexture = (mode & 0x4000) != 0;
    const bool paletted = (image.depth == 4 || image.depth == 8) && image.palBpp == 16 && image.palCnt > 0;
    const int clutCount = (!tempTexture && paletted) ? image.palCnt : 1;

    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = (Uint32)image.width;
    tci.height = (Uint32)image.height;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;

    std::vector<SDL_GPUTexture*> texs;
    bool hasAlpha = false;
    for (int clut = 0; clut < clutCount; clut++)
    {
        std::vector<uint8_t> rgba;
        if (!tempTexture)
        {
            if (!decodeToRgba(image, rgba, clut))
            {
                for (SDL_GPUTexture* t : texs)
                    SDL_ReleaseGPUTexture(dev, t);
                return 0;
            }
            // The original marks a surface as alpha-bearing (var_2C / v40)
            // when any pixel's alpha bits are not fully set; here that is any
            // decoded pixel with alpha < 0xFF (the black-keyed transparent
            // pixels). Any clut variant carrying transparency sets the flag.
            for (size_t i = 3; i < rgba.size(); i += 4)
            {
                if (rgba[i] != 0xFF)
                {
                    hasAlpha = true;
                    break;
                }
            }
        }

        SDL_GPUTexture* tex = SDL_CreateGPUTexture(dev, &tci);
        if (!tex)
        {
            logging::logError("[sdlgpu] loadTexture: SDL_CreateGPUTexture failed: {}", SDL_GetError());
            for (SDL_GPUTexture* t : texs)
                SDL_ReleaseGPUTexture(dev, t);
            return 0;
        }

        if (!tempTexture)
        {
            SDL_GPUTransferBufferCreateInfo tbci{};
            tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            tbci.size = (Uint32)rgba.size();
            SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tbci);
            if (!tb)
            {
                logging::logError("[sdlgpu] loadTexture: SDL_CreateGPUTransferBuffer failed: {}", SDL_GetError());
                SDL_ReleaseGPUTexture(dev, tex);
                for (SDL_GPUTexture* t : texs)
                    SDL_ReleaseGPUTexture(dev, t);
                return 0;
            }
            void* mapped = SDL_MapGPUTransferBuffer(dev, tb, false);
            if (!mapped)
            {
                logging::logError("[sdlgpu] loadTexture: SDL_MapGPUTransferBuffer failed: {}", SDL_GetError());
                SDL_ReleaseGPUTransferBuffer(dev, tb);
                SDL_ReleaseGPUTexture(dev, tex);
                for (SDL_GPUTexture* t : texs)
                    SDL_ReleaseGPUTexture(dev, t);
                return 0;
            }
            std::memcpy(mapped, rgba.data(), rgba.size());
            SDL_UnmapGPUTransferBuffer(dev, tb);

            SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
            if (!cmd)
            {
                logging::logError("[sdlgpu] loadTexture: SDL_AcquireGPUCommandBuffer failed: {}", SDL_GetError());
                SDL_ReleaseGPUTransferBuffer(dev, tb);
                SDL_ReleaseGPUTexture(dev, tex);
                for (SDL_GPUTexture* t : texs)
                    SDL_ReleaseGPUTexture(dev, t);
                return 0;
            }
            SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
            if (cp)
            {
                SDL_GPUTextureTransferInfo src{};
                src.transfer_buffer = tb;
                src.pixels_per_row = (Uint32)image.width;
                src.rows_per_layer = (Uint32)image.height;
                SDL_GPUTextureRegion dst{};
                dst.texture = tex;
                dst.w = (Uint32)image.width;
                dst.h = (Uint32)image.height;
                dst.d = 1;
                SDL_UploadToGPUTexture(cp, &src, &dst, false);
                SDL_EndGPUCopyPass(cp);
            }
            SDL_SubmitGPUCommandBuffer(cmd);
            submitDiag(dev, "loadTexture-upload");
            SDL_ReleaseGPUTransferBuffer(dev, tb); // deferred: safe after submit
        }
        else
        {
            logging::logInfo("[sdlgpu] loadTexture: temp texture (mode 0x4000) - registered without GPU upload");
        }
        texs.push_back(tex);
    }

    TextureEntry entry;
    entry.texture = texs[0];
    if (texs.size() > 1)
        entry.clutTextures.assign(texs.begin() + 1, texs.end());
    entry.width = image.width;
    entry.height = image.height;
    entry.clutCount = clutCount;
    entry.hasAlpha = hasAlpha;
    entry.noAlphaFlag = (mode & 4) != 0;
    impl->textures[handle] = entry;
    Marni* m = gGameTable.pMarni;
    if (m)
        m->textures[handle].var_00 = mode;
    logging::logInfo("[sdlgpu] loadTexture -> handle {} ({} clut(s), SDL texture {})", handle, clutCount, (void*)entry.texture);
    return handle;
}

// 0x00404CE0
void SdlGpuRenderer::unloadTexture(int handle)
{
    logging::logInfo("[sdlgpu] unloadTexture({})", handle);
    SDL_GPUDevice* dev = (SDL_GPUDevice*)system::gpu::device();
    auto it = impl->textures.find(handle);
    if (it == impl->textures.end())
    {
        logging::logWarning("[sdlgpu] unloadTexture({}): not in registry", handle);
        return;
    }
    if (dev && it->second.texture)
    {
        SDL_WaitForGPUIdle(dev);
        SDL_ReleaseGPUTexture(dev, it->second.texture);
    }
    if (dev)
    {
        for (SDL_GPUTexture* t : it->second.clutTextures)
        {
            if (t)
                SDL_ReleaseGPUTexture(dev, t);
        }
    }
    impl->textures.erase(it);
    if (gGameTable.pMarni)
        gGameTable.pMarni->textures[handle].var_00 = 0;
}

// 0x00441520
void SdlGpuRenderer::unloadAllTextures()
{
    logging::logInfo("[sdlgpu] unloadAllTextures() ({} entries)", impl->textures.size());
    // Mirror the reference result_unload_textures(): only the room
    // texture pages (0-7, 16-33) are unloaded. The persistent pages (8-9
    // fonts, 10-15 espcore effect sprites) keep their handles so effects
    // like the room-100 smoke (page 10) still have a valid texture.
    marni::result_unload_textures();
}

// 0x00405320
void SdlGpuRenderer::init()
{
    logging::logInfo("[sdlgpu] init(): allocating Marni + marni::init (otag[0]=16, otag[1]=4096, otag[3]=16)");
    auto marniPtr = (Marni*)operator_new(sizeof(Marni));
    gGameTable.pMarni = marni::init(marniPtr, gGameTable.hwnd, 320, 240);
    if (gGameTable.pMarni == nullptr)
    {
        logging::logError("[sdlgpu] init(): marni::init failed");
        return;
    }
    Marni* m = gGameTable.pMarni;
    logging::logInfo(
        "[sdlgpu] init(): gpu_flag=0x{} is_gpu_active={} render={}x{} xsize={}x{} aspect={}x{} prj={} centre={}x{} otag depths {}/{}/{}/{}/{}",
        hexStr(m->gpu_flag), m->is_gpu_active, m->render_w, m->render_h, m->xsize, m->ysize, m->aspect_x, m->aspect_y,
        m->field_8C7EDC, m->field_8C7EC4, m->field_8C7EC8, m->otag[0].zdepth, m->otag[1].zdepth, m->otag[2].zdepth,
        m->otag[3].zdepth, m->otag[4].zdepth);

    // marni::init calls system::gpu::init(); ensure the guest framebuffer
    // exists at the configured render resolution.
    const auto renderRes = system::config::get_render_resolution();
    logging::logInfo("[sdlgpu] init(): creating guest framebuffer at {}x{} (device={})", renderRes.width, renderRes.height, system::gpu::device() != nullptr);
    system::gpu::create_guest_framebuffer(renderRes.width, renderRes.height);
}

// 0x004419A0
void SdlGpuRenderer::shutdown()
{
    logging::logInfo("[sdlgpu] shutdown()");
    SDL_GPUDevice* dev = (SDL_GPUDevice*)system::gpu::device();
    if (dev)
        impl->releaseAll(dev);
    marni::kill();
    logging::logInfo("[sdlgpu] shutdown() complete (device left to system_gpu)");
}

// ALT+ENTER
bool SdlGpuRenderer::toggleFullscreen()
{
    logging::logInfo("[sdlgpu] toggleFullscreen()");
    return marni::toggle_fullscreen(gGameTable.pMarni);
}

// 0x00402500
bool SdlGpuRenderer::changeResolution()
{
    logging::logInfo("[sdlgpu] changeResolution()");
    return marni::change_resolution(gGameTable.pMarni);
}

// 0x00402530
int SdlGpuRenderer::requestDisplayModeCount()
{
    const int count = marni::request_display_mode_count(gGameTable.pMarni);
    logging::logInfo("[sdlgpu] requestDisplayModeCount() -> {}", count);
    return count;
}

// 0x0050ACB0
void SdlGpuRenderer::configReadAll()
{
    logging::logInfo("[sdlgpu] configReadAll()");
    marni::config_read_all(&gGameTable.marni_config);
}

// 0x0050B020
void SdlGpuRenderer::configFlushAll()
{
    logging::logInfo("[sdlgpu] configFlushAll()");
    marni::config_flush_all(&gGameTable.marni_config);
}

// 0x0050B900
void SdlGpuRenderer::configShutdown()
{
    logging::logInfo("[sdlgpu] configShutdown()");
    marni::config_shutdown();
}

// 0x0050B220
void SdlGpuRenderer::configFlipFilter()
{
    logging::logInfo("[sdlgpu] configFlipFilter()");
    marni::config_flip_filter(&gGameTable.marni_config);
}

// 0x00442CB0
void SdlGpuRenderer::setGpuFlag()
{
    logging::logDebug("[sdlgpu] setGpuFlag()");
    marni::set_gpu_flag();
}

// 0x00411360 - the GDI font path is tied to the removed backend; skipped for the prototype.
void SdlGpuRenderer::fontTrans()
{
    logging::logDebug("[sdlgpu] fontTrans(): GDI font path is tied to the removed backend - SKIPPED (prototype)");
}

// 0x00401F70
void SdlGpuRenderer::movieUpdate()
{
    logging::logDebug("[sdlgpu] movieUpdate()");
    marni::marni_movie_update(gGameTable.pMarni);
}

} // namespace openre::marni
