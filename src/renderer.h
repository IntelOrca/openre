#pragma once

#include "gfx_draw.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace openre::gfx_draw
{
    // Raw decoded texture image. Callers decode TIM/ADT data into an Image and
    // hand it to the renderer; the renderer is responsible for uploading it to
    // the GPU. The pixel buffer is stored as the marni surface bitmap (pitch *
    // height bytes) and the palette buffer as the raw palette entries.
    class Image
    {
    public:
        int width = 0; // surface width in pixels
        int height = 0;
        int depth = 0;         // pixel bit depth: 4, 8 or 16
        int palBpp = 0;        // palette entry bit depth (16); 0 = not paletted
        int palCnt = 0;        // number of palettes; 0 = not paletted
        bool psxFormat = true; // PSX 555 layout (red low, no alpha); false = D3D 555 (blue low, alpha in bit 15)
        std::vector<uint8_t> pixels;
        std::vector<uint8_t> palette;
    };

    // Abstract renderer interface. One method per hooked Add* function in
    // gfx_draw.h; the hooks are thin wrappers that delegate to the global
    // renderer. Each method mirrors the original binary function (address in
    // the implementation).
    class Renderer
    {
    public:
        virtual ~Renderer() = default;

        virtual void resetGeom() = 0;
        virtual int addSprt(const Sprt* p, uint32_t page, int z, int add_back) = 0;
        virtual int addSprtV(int x, int y, int w, int h, int u, int v, unsigned int clut, int page, int depth, int is_back) = 0;
        virtual void addPolyFt4(const PolyFt4* p, int page, int z, int add_back) = 0;
        virtual int addMask(const Sprt* p, int page, int z) = 0;
        virtual int addBgScaled(int bg, int z) = 0;
        virtual void addBg() = 0;
        virtual void addBg2(int16_t scroll_y) = 0;
        virtual int addScaledSprite(int prim, int page, int z) = 0;
        virtual int addScaledPoly(int prim, int page, int z) = 0;
        virtual int addPolyGt4(const PolyGt4* p, int page, int z) = 0;
        virtual int addPolyFt42(const PolyFt4* p, int page, int z) = 0;
        virtual int addPolyF4(const Tile* p, int z, int is_back) = 0;
        virtual int addTile(const Tile* p, int z, int is_back) = 0;
        virtual int addLineF2(const LineF2* p, int z, int is_back) = 0;

        // Uploads `image` as a new texture and returns its handle (0 on
        // failure).
        virtual int loadTexture(const Image& image, uint32_t mode) = 0;
        // Unloads a texture handle previously returned by loadTexture().
        virtual void unloadTexture(int handle) = 0;
        // Unloads all textures (title / logo sequence).
        virtual void unloadAllTextures() = 0;
    };

    // Decorator that records every draw call to the per-frame DrawStats while
    // delegating the actual drawing to an inner renderer. Records even when the
    // inner renderer rejects a submission.
    class LoggingRenderer final : public Renderer
    {
    public:
        explicit LoggingRenderer(std::unique_ptr<Renderer> inner);

        void resetGeom() override;
        int addSprt(const Sprt* p, uint32_t page, int z, int add_back) override;
        int addSprtV(int x, int y, int w, int h, int u, int v, unsigned int clut, int page, int depth, int is_back) override;
        void addPolyFt4(const PolyFt4* p, int page, int z, int add_back) override;
        int addMask(const Sprt* p, int page, int z) override;
        int addBgScaled(int bg, int z) override;
        void addBg() override;
        void addBg2(int16_t scroll_y) override;
        int addScaledSprite(int prim, int page, int z) override;
        int addScaledPoly(int prim, int page, int z) override;
        int addPolyGt4(const PolyGt4* p, int page, int z) override;
        int addPolyFt42(const PolyFt4* p, int page, int z) override;
        int addPolyF4(const Tile* p, int z, int is_back) override;
        int addTile(const Tile* p, int z, int is_back) override;
        int addLineF2(const LineF2* p, int z, int is_back) override;

        int loadTexture(const Image& image, uint32_t mode) override;
        void unloadTexture(int handle) override;
        void unloadAllTextures() override;

        // Per-frame draw-call statistics accumulated by this logger.
        const DrawStats& drawStats() const;

    private:
        void record(DrawKind kind, int z, int page, int16_t x0, int16_t y0, int16_t x1, int16_t y1);

        std::unique_ptr<Renderer> inner;
        DrawStats stats;
    };

    // The global renderer used by the gfx_draw hooks.
    extern std::unique_ptr<Renderer> g_renderer;

    // Constructs the global renderer (a LoggingRenderer wrapping the real
    // renderer). Called from gfx_draw::init_hooks().
    void initRenderer();
}
