#pragma once

#include "gfx_draw.h"

#include <memory>

namespace openre::gfx_draw
{
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
