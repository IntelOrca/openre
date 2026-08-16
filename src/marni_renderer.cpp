#include "marni_renderer.h"
#include "marni.h"
#include "openre.h"
#include "re2.h"
#include "sdl_gpu_renderer.h"
#include "system_config.h"
#include "system_gpu.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <vector>

namespace openre::marni
{

    // 0x00440250: ResetGeom
    std::unique_ptr<Renderer> g_renderer;

    void initRenderer()
    {
        // SDL-only prototype: every renderer method is implemented directly on
        // top of the SDL3 GPU API (see sdl_gpu_renderer.cpp).
        g_renderer = std::make_unique<LoggingRenderer>(std::make_unique<SdlGpuRenderer>());
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

    void LoggingRenderer::begin()
    {
        inner->begin();
    }

    void LoggingRenderer::clearOtags()
    {
        inner->clearOtags();
    }

    void LoggingRenderer::clear()
    {
        inner->clear();
    }

    void LoggingRenderer::draw()
    {
        inner->draw();
    }

    void LoggingRenderer::flip()
    {
        inner->flip();
    }

    void LoggingRenderer::end()
    {
        inner->end();
    }

    void LoggingRenderer::setGpuFlag(uint32_t flag, bool value)
    {
        inner->setGpuFlag(flag, value);
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

    void LoggingRenderer::init()
    {
        inner->init();
    }

    void LoggingRenderer::shutdown()
    {
        inner->shutdown();
    }

    bool LoggingRenderer::toggleFullscreen()
    {
        return inner->toggleFullscreen();
    }

    bool LoggingRenderer::changeResolution()
    {
        return inner->changeResolution();
    }

    int LoggingRenderer::requestDisplayModeCount()
    {
        return inner->requestDisplayModeCount();
    }

    void LoggingRenderer::configReadAll()
    {
        inner->configReadAll();
    }

    void LoggingRenderer::configFlushAll()
    {
        inner->configFlushAll();
    }

    void LoggingRenderer::configShutdown()
    {
        inner->configShutdown();
    }

    void LoggingRenderer::configFlipFilter()
    {
        inner->configFlipFilter();
    }

    void LoggingRenderer::setGpuFlag()
    {
        inner->setGpuFlag();
    }

    void LoggingRenderer::fontTrans()
    {
        inner->fontTrans();
    }

    void LoggingRenderer::movieUpdate()
    {
        inner->movieUpdate();
    }

    const DrawStats& LoggingRenderer::drawStats() const
    {
        return stats;
    }
}
