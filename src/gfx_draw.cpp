#include "gfx_draw.h"
#include "interop.hpp"
#include "openre.h"
#include "renderer.h"

namespace openre::gfx_draw
{
    // The hooked Add* family (originally all __cdecl in the 0x00440xxx range)
    // is now a set of thin wrappers over the global renderer, which owns all
    // primitive memory. init_hooks() routes all callers to these wrappers.

    // 0x00440250
    void reset_geom()
    {
        g_renderer->reset();
    }

    // 0x00440280
    int add_sprt(const Sprt* p, uint32_t page, int z, int add_back)
    {
        return g_renderer->addSprt(p, page, z, add_back);
    }

    // 0x00440480
    int add_sprt_v(int x, int y, int w, int h, int u, int v, unsigned int clut, int page, int depth, int is_back)
    {
        return g_renderer->addSprtV(x, y, w, h, u, v, clut, page, depth, is_back);
    }

    // 0x00440600
    void add_poly_ft4(const PolyFt4* p, int page, int z, int add_back)
    {
        g_renderer->addPolyFt4(p, page, z, add_back);
    }

    // 0x004407F0
    int add_mask(const Sprt* p, int page, int z)
    {
        return g_renderer->addMask(p, page, z);
    }

    // 0x00440950
    int add_bg_scaled(const BgScaledDesc* bg, int z)
    {
        return g_renderer->addBgScaled(bg, z);
    }

    // 0x0043FB30
    void add_bg()
    {
        g_renderer->addBg();
    }

    // 0x0043FCB0
    void add_bg_2(int16_t scroll_y)
    {
        g_renderer->addBg2(scroll_y);
    }

    // 0x00440A20
    int add_scaled_sprite(const PolyFt4* prim, int page, int z)
    {
        return g_renderer->addScaledSprite(prim, page, z);
    }

    // 0x00440B70
    int add_scaled_poly(const PolyFt4* prim, int page, int z)
    {
        return g_renderer->addScaledPoly(prim, page, z);
    }

    // 0x00440DD0
    int add_poly_gt4(const PolyGt4* p, int page, int z)
    {
        return g_renderer->addPolyGt4(p, page, z);
    }

    // 0x00440FF0
    int add_poly_ft4_2(const PolyFt4* p, int page, int z)
    {
        return g_renderer->addPolyFt42(p, page, z);
    }

    // 0x00441170
    int add_poly_f4(const Tile* p, int z, int is_back)
    {
        return g_renderer->addPolyF4(p, z, is_back);
    }

    // 0x00441270
    int add_tile(const Tile* p, int z, int is_back)
    {
        return g_renderer->addTile(p, z, is_back);
    }

    // 0x00441370
    int add_line_f2(const LineF2* p, int z, int is_back)
    {
        return g_renderer->addLineF2(p, z, is_back);
    }

    // 0x00402210
    int add_scaler(const PrimScaler* p, int z)
    {
        return g_renderer->addScaler(p, z);
    }

    const DrawStats& draw_stats()
    {
        static const DrawStats empty{};
        // Only the LoggingRenderer accumulates stats; a bare MarniRenderer
        // has none, so fall back to an empty result.
        if (auto* logger = dynamic_cast<LoggingRenderer*>(g_renderer.get()))
            return logger->drawStats();
        return empty;
    }

    // 0x0043FF40 (page-upload part of tim_buffer_to_surface)
    int loadTexturePage(uint32_t page, const Image& image, uint32_t mode)
    {
        if (page >= std::size(gGameTable.texture_pages))
            return 0;
        unloadTexturePage(page);

        int handle = g_renderer->loadTexture(image, mode);
        if (handle == 0)
            return 0;

        auto& tp = gGameTable.texture_pages[page];
        tp.handle = handle;
        tp.clutCount = image.palCnt > 0 ? image.palCnt : 1;
        update_timer();
        return handle;
    }

    // 0x0043F550
    void unloadTexturePage(uint32_t page)
    {
        if (page >= std::size(gGameTable.texture_pages))
            return;
        auto& tp = gGameTable.texture_pages[page];
        if (tp.handle != 0)
            g_renderer->unloadTexture(tp.handle);
        tp.handle = 0;
        tp.clutCount = 0;
        tp.suspended = 0;
        update_timer();
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
        interop::writeJmp(0x004407F0, &add_mask);
        interop::writeJmp(0x00440DD0, &add_poly_gt4);
        interop::writeJmp(0x00440FF0, &add_poly_ft4_2);
        interop::writeJmp(0x00440600, &add_poly_ft4);
        interop::writeJmp(0x00440B70, &add_scaled_poly);
        interop::writeJmp(0x00440A20, &add_scaled_sprite);
        interop::writeJmp(0x0043FB30, &add_bg);
        interop::writeJmp(0x0043FCB0, &add_bg_2);
    }
}