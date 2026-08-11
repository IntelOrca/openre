#pragma once

#include "marni_renderer.h"

#include <cstdint>
#include <memory>

namespace openre::marni
{
    // SDL3/GPU-only renderer. Implements the full Renderer interface from
    // scratch on top of the SDL3 GPU API only: no DirectDraw, no D3D, no
    // GfxBackend replay. The guest framebuffer, device and window are owned by
    // system_gpu (system_gpu.cpp); this renderer drives them directly via the
    // raw accessors in system_gpu.h.
    //
    // The draw path parses the MARNI ordering tables (gGameTable.pMarni->otag,
    // five tables) the same way the original binary's trans_priority_list does
    // and emits SDL_GPU draws into the guest framebuffer: otag[3] backgrounds,
    // otag[1] objects/scaler, otag[0] front text. flip() presents by
    // letterboxing the guest framebuffer into the swapchain. Every method logs
    // its activity: the renderer doubles as a diagnostic harness proving the
    // no-D3D goal is reachable.
    //
    // Supported primitive families (matching trans_priority_list):
    //   - lines (17/18)        -> untextured quads (flat/gouraud)
    //   - sprites/quads/tiles  -> textured/untextured quads via the
    //     sub_40CFD0/sub_40D300/... decoder math (types 33,36,37,44,45,61,69,
    //     70,73,76,77 and the scaler variants 0x1002C/0x1002D/0x10049/0x1004C/
    //     0x1004D; 0x10000|44/45/73/76/77)
    //   - 3D characters (88/0x188), the type-256 matrix copy and all
    //     trans_matrix prims (type & 0xFE00) are logged and skipped.
    class SdlGpuRenderer final : public Renderer
    {
    public:
        SdlGpuRenderer();
        ~SdlGpuRenderer() override;

        void reset() override;
        void begin() override;
        void clearOtags() override;
        void clear() override;
        void draw() override;
        void flip() override;
        void end() override;
        void setGpuFlag(uint32_t flag, bool value) override;

        int addSprt(const Sprt* p, uint32_t page, int z, int add_back) override;
        void addPolyFt4(const PolyFt4* p, int page, int z, int add_back) override;
        int addMask(const Sprt* p, int page, int z) override;
        int addBgScaled(const BgScaledDesc* bg, int z) override;
        void addBg() override;
        void addBg2(int16_t scroll_y) override;
        int addScaledSprite(const PolyFt4* prim, int page, int z) override;
        int addScaledPoly(const PolyFt4* prim, int page, int z) override;
        int addPolyGt4(const PolyGt4* p, int page, int z) override;
        int addPolyFt42(const PolyFt4* p, int page, int z) override;
        int addPolyF4(const Tile* p, int z, int is_back) override;
        int addTile(const Tile* p, int z, int is_back) override;
        int addLineF2(const LineF2* p, int z, int is_back) override;
        int addScaler(const PrimScaler* p, int z) override;

        int loadTexture(const Image& image, uint32_t mode) override;
        void unloadTexture(int handle) override;
        void unloadAllTextures() override;

        void init() override;
        void shutdown() override;
        bool toggleFullscreen() override;
        bool changeResolution() override;
        int requestDisplayModeCount() override;
        void configReadAll() override;
        void configFlushAll() override;
        void configShutdown() override;
        void configFlipFilter() override;
        void setGpuFlag() override;
        void fontTrans() override;
        void movieUpdate() override;

    private:
        // Pimpl: the SDL3/GPU resources (device-owned pipelines, command
        // buffers, vertex buffer, textures) live in the .cpp.
        struct Impl;
        std::unique_ptr<Impl> impl;

        // Inserts the full-screen background prims (bg_tex0 and the bg_tex1
        // right-edge strips) into the back OT at the given camera offset.
        void addBgPrims(int16_t x_off, int16_t y_off);
    };
}
