#pragma once

#include "marni_draw.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace openre::marni
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
    // marni_draw.h; the hooks are thin wrappers that delegate to the global
    // renderer. Each method mirrors the original binary function (address in
    // the implementation).
    class Renderer
    {
    public:
        virtual ~Renderer() = default;

        /**
         * Begins a new frame of drawing. Discards all primitives queued since
         * the previous frame and re-zeros the global geometry offset used by
         * addBg(). The game calls this once per frame before submitting any
         * draw calls.
         */
        virtual void reset() = 0;

        /**
         * Begins a new frame of drawing. Clears the ordering tables and resets
         * the geometry/arena state (equivalent to the original
         * `clear_otags + reset_geom` prologue of the frame loop). The back
         * buffer is NOT cleared here: the game calls clear() separately, after
         * any GPU_3 flag changes, because that flag controls what clear() does.
         */
        virtual void begin() = 0;

        /**
         * Clears only the ordering tables, leaving geometry/arena state intact.
         *
         * Used by the mid-frame second draw pass in render_frame, which resets
         * the ordering tables without touching geometry or the back buffer.
         */
        virtual void clearOtags() = 0;

        /**
         * Clears the back buffer (and Z buffer, controlled by the GPU_3 flag).
         */
        virtual void clear() = 0;

        /**
         * Renders the current ordering-table contents to the back buffer.
         */
        virtual void draw() = 0;

        /**
         * Presents the back buffer to the screen.
         */
        virtual void flip() = 0;

        /**
         * Convenience for paths that draw and present together: draw() + flip().
         */
        virtual void end() = 0;

        /**
         * Sets or clears a single flag bit in the renderer's GPU state
         * (pMarni->gpu_flag).
         *
         * @param flag The bit to modify (a marni::GpuFlags value).
         * @param value true to set the bit, false to clear it.
         */
        virtual void setGpuFlag(uint32_t flag, bool value) = 0;

        /**
         * Draws an axis-aligned textured sprite (PSX SPRT) quad.
         *
         * The sprite occupies the screen rectangle (x0, y0) to
         * (x0 + w - 1, y0 + h - 1) and samples a w by h region of the
         * texture page starting at pixel (u0, v0), tinted with the CLUT
         * colour p->clut. When p->r/g/b is 0x808080 the sprite is drawn
         * unshaded; otherwise it is flat-shaded with that colour.
         *
         * @param p The sprite descriptor (position, size, UV origin, CLUT index).
         * @param page The texture page index whose texture is sampled.
         * @param z The depth used to sort the primitive into the ordering table.
         * @param add_back Whether to insert into the back OT rather than the front OT.
         * @return 1 on success, 0 if the page is invalid/unloaded/suspended.
         */
        virtual int addSprt(const Sprt* p, uint32_t page, int z, int add_back) = 0;

        /**
         * Draws an axis-aligned textured sprite quad (PSX SPRT), taking its
         * parameters as plain values instead of a Sprt descriptor.
         *
         * Convenience overload of addSprt(): builds a Sprt descriptor with a
         * white tint (unshaded) and delegates to addSprt(). Unlike addSprt(),
         * the sampled texture region (u, v, u + w, v + h) is validated to fit
         * within a 256-pixel page.
         *
         * @param x Left edge of the sprite on screen.
         * @param y Top edge of the sprite on screen.
         * @param w Sprite width in pixels.
         * @param h Sprite height in pixels.
         * @param u Texture x origin of the sampled region.
         * @param v Texture y origin of the sampled region.
         * @param clut CLUT (palette) index to tint the sprite with.
         * @param page The texture page index whose texture is sampled.
         * @param depth The depth used to sort the primitive into the ordering table.
         * @param is_back Whether to insert into the back OT rather than the front OT.
         * @return 1 on success, 0 if the region is out of bounds or the page is invalid/unloaded/suspended.
         */
        int addSprtV(int x, int y, int w, int h, int u, int v, unsigned int clut, int page, int depth, int is_back)
        {
            if (w <= 0 || h <= 0 || u < 0 || v < 0 || u + w - 1 > 255 || v + h - 1 > 255)
                return 0;

            Sprt s = {};
            s.x0 = (int16_t)x;
            s.y0 = (int16_t)y;
            s.w = (uint16_t)w;
            s.h = (uint16_t)h;
            s.u0 = (uint8_t)u;
            s.v0 = (uint8_t)v;
            s.clut = clut;
            s.r = s.g = s.b = 0x80; // white tint → unshaded
            return addSprt(&s, page, depth, is_back);
        }

        /**
         * Draws a textured four-vertex quad (PSX POLY_FT4).
         *
         * Unlike a sprite, the quad corners need not form an axis-aligned
         * rectangle (e.g. a perspective-tilted or trapezoid wall); the four
         * corners are (x0,y0), (x1,y1), (x2,y2) and (x3,y3). The texture is
         * sampled between (u0,v0) and (u3,v3) across the quad. When the
         * descriptor colour is 0x808080 the quad is drawn unshaded, otherwise
         * it is flat-shaded with that single colour (no per-vertex gradients).
         *
         * @param p The quad descriptor (4 corners, 2 UV corners, CLUT, colour).
         * @param page The texture page index whose texture is sampled.
         * @param z The depth used to sort the primitive into the ordering table.
         * @param add_back Whether to insert into the back OT rather than the front OT.
         */
        virtual void addPolyFt4(const PolyFt4* p, int page, int z, int add_back) = 0;

        /**
         * Draws a stencil/mask sprite.
         *
         * Used for masking effects. The sprite is written through the scaler
         * OT with its z clamped to the projection plane (global_prj / 2), so
         * it always sits in front of ordinary geometry. It always emits a
         * scaler primitive and never uses the back OT.
         *
         * @param p The sprite descriptor (position, size, UV origin, CLUT index).
         * @param page The texture page index whose texture is sampled.
         * @param z The requested depth; clamped to the projection plane when too deep.
         * @return 1 on success, 0 if the page is invalid/unloaded/suspended.
         */
        virtual int addMask(const Sprt* p, int page, int z) = 0;

        /**
         * Draws a full-screen scaled background quad.
         *
         * Uploads the shared background texture (bg_tex0) as a quad scaled to
         * the screen. The `bg` descriptor supplies the on-screen position and
         * the source UV region of the texture. The z depth is stored as a
         * float scale and also shifted right by 4 for OT z-ordering.
         *
         * @param bg Pointer to a BgScaledDesc; fields are x@+8 (i16),
         *     y@+0A (i16), u@+0C (u8), v@+0D (u8), w@+10 (i16), h@+12 (i16).
         * @param z The draw depth; used as a float scale and OT z-order (z >> 4).
         * @return 1 on success, 0 if no background texture is loaded.
         */
        virtual int addBgScaled(const BgScaledDesc* bg, int z) = 0;

        /**
         * Draws the current room background at the camera offset.
         *
         * Draws the full-screen background (bg_tex0) plus the right-edge strip
         * (bg_tex1) at the current global camera position (global_cx,
         * global_cy) and marks the background as drawn for this frame.
         */
        virtual void addBg() = 0;

        /**
         * Draws the room background scrolled vertically.
         *
         * Like addBg() but the background is positioned by `scroll_y` instead
         * of the camera offset, and any scroll-strip textures are drawn below
         * the visible frame so the screen stays filled during a vertical
         * scroll transition.
         *
         * @param scroll_y Vertical scroll offset for the background.
         */
        virtual void addBg2(int16_t scroll_y) = 0;

        /**
         * Draws a sprite scaled to a non-standard size via the scaler OT.
         *
         * The descriptor is shaped like a POLY_FT4 quad: the on-screen origin
         * is (x0, y0), the source UV region is bounded by (u0,v0) and (u3,v3),
         * and the drawn size comes from the (x3, y3) words (each reduced by
         * one). The z depth is stored as a float scale and shifted right by 4
         * for OT z-ordering.
         *
         * @param prim Pointer to a PolyFt4 descriptor.
         * @param page The texture page index whose texture is sampled.
         * @param z The draw depth; used as a float scale and OT z-order (z >> 4).
         * @return 1 on success, 0 if the page is invalid/unloaded/suspended.
         */
        virtual int addScaledSprite(const PolyFt4* prim, int page, int z) = 0;

        /**
         * Draws a textured quad scaled to a non-standard size via the scaler OT.
         *
         * Like addScaledSprite() but for full four-vertex quads (POLY_FT4):
         * all four corners and all four UV coordinates are used. When the
         * descriptor colour is 0x808080 the quad is unshaded, otherwise it is
         * flat-shaded. The z depth is shifted right by 4 and must fit within
         * the 4095-entry scaler OT.
         *
         * @param prim Pointer to a PolyFt4 descriptor.
         * @param page The texture page index whose texture is sampled.
         * @param z The draw depth; OT z-order is z >> 4 (rejected if > 4095).
         * @return 1 on success, 0 if the page is invalid/unloaded/suspended or z overflows.
         */
        virtual int addScaledPoly(const PolyFt4* prim, int page, int z) = 0;

        /**
         * Draws a gouraud-shaded textured quad (PSX POLY_GT4).
         *
         * Use this when each of the four quad corners has its own colour
         * (e.g. lighting gradients on walls/characters). The colour is
         * interpolated smoothly across the quad from the four per-vertex RGB
         * values. MARNI stores half-intensity colours and doubles them during
         * shading, so each channel is clamped to 0x80. Drawn on the front OT
         * only.
         *
         * @param p The quad descriptor (4 corners, 4 UVs, 4 vertex colours, CLUT).
         * @param page The texture page index whose texture is sampled.
         * @param z The depth used to sort the primitive into the ordering table.
         * @return 1 on success, 0 if the page is invalid/unloaded/suspended or z overflows.
         */
        virtual int addPolyGt4(const PolyGt4* p, int page, int z) = 0;

        /**
         * Draws a flat-shaded textured quad (PSX POLY_FT4) on the front OT.
         *
         * A second variant of addPolyFt4() with a fixed primitive type and no
         * back-OT insertion. Use it when a flat-colour textured quad must be
         * drawn on the front OT; the single colour comes from the descriptor's
         * (r0, g0, b0). All four corners and four UV coordinates are used.
         *
         * @param p The quad descriptor (4 corners, 4 UVs, flat colour, CLUT).
         * @param page The texture page index whose texture is sampled.
         * @param z The depth used to sort the primitive into the ordering table.
         * @return 1 on success, 0 if the page is invalid/unloaded/suspended.
         */
        virtual int addPolyFt42(const PolyFt4* p, int page, int z) = 0;

        /**
         * Draws a flat, untextured quad (PSX POLY_F4) filled with a solid colour.
         *
         * No texture is sampled; the quad is a solid-colour rectangle
         * described by a Tile-like record. Note the bottom-right corner is
         * recovered from the word following the descriptor, so the descriptor
         * must be followed by another Tile in memory.
         *
         * @param p The tile descriptor (origin x0/y0, colour r/g/b); the corner
         *     (x1,y1) is read from the next Tile in memory.
         * @param z The depth used to sort the primitive into the ordering table.
         * @param is_back Whether to insert into the back OT rather than the front OT.
         * @return 1 on success.
         */
        virtual int addPolyF4(const Tile* p, int z, int is_back) = 0;

        /**
         * Draws a flat, untextured rectangle (PSX TILE) filled with a solid colour.
         *
         * Simpler than addPolyF4(): the rectangle is fully described by the
         * origin (x0, y0) and explicit size (w, h) in the descriptor, with no
         * reliance on adjacent memory. Semi-transparency is supported via the
         * code/tag flags.
         *
         * @param p The tile descriptor (origin, size, colour).
         * @param z The depth used to sort the primitive into the ordering table.
         * @param is_back Whether to insert into the back OT rather than the front OT.
         * @return 1 on success.
         */
        virtual int addTile(const Tile* p, int z, int is_back) = 0;

        /**
         * Draws a flat 2-vertex line (PSX LINE_F2) in a solid colour.
         *
         * @param p The line descriptor (endpoints x0/y0 to x1/y1, colour).
         * @param z The depth used to sort the primitive into the ordering table.
         * @param is_back Whether to insert into the back OT rather than the front OT.
         * @return 1 on success, 0 if the line starts off-screen (x0 < 0).
         */
        virtual int addLineF2(const LineF2* p, int z, int is_back) = 0;

        /**
         * Draws a scaling/transform primitive (PSX-type 0x3DE0).
         *
         * The scaler record carries a 3x3 transform matrix plus per-primitive
         * fields used by the full-screen/door scaler effects (see set_door_prim
         * and the render_frame scaler setup). Unlike the Add* sprite/quads it is
         * submitted to the front OT at the given depth without texture-page
         * checks. The record is copied into the renderer's arena.
         *
         * @param p The scaler descriptor (matrix rows, projection, screen centre).
         * @param z The depth used to sort the primitive into the ordering table.
         * @return 1 on success.
         */
        virtual int addScaler(const PrimScaler* p, int z) = 0;

        /**
         * Uploads `image` as a new GPU texture and returns its handle.
         *
         * The handle can be stored anywhere the game likes; texture page
         * bookkeeping is the game's job, not the renderer's.
         *
         * @param image The decoded pixels/palette to upload.
         * @param mode Bit flags controlling the upload (palette handling, RGB
         *     conversion, reload/skip behaviour — see marni create_texture_handle).
         * @return The texture handle, or 0 on failure.
         */
        virtual int loadTexture(const Image& image, uint32_t mode) = 0;

        /**
         * Unloads a texture handle previously returned by loadTexture().
         *
         * @param handle The texture handle to release.
         */
        virtual void unloadTexture(int handle) = 0;

        /**
         * Unloads every texture currently loaded.
         *
         * Used during the title / logo sequence to reset all texture state.
         */
        virtual void unloadAllTextures() = 0;

        /**
         * Initialises the graphics subsystem (MARNI device, surfaces, order
         * tables). Allocates the global Marni instance and stores it in
         * gGameTable.pMarni. Called once at startup, before any drawing.
         */
        virtual void init() = 0;

        /**
         * Tears down the graphics subsystem: releases all textures, unloads
         * the registered surfaces and destroys the Marni instance. Idempotent.
         * Called on quit and on window close.
         */
        virtual void shutdown() = 0;

        /**
         * Toggles between the current windowed mode and the fullscreen mode
         * (ALT+ENTER). Leaving fullscreen restores the last windowed mode.
         *
         * @return true if the display mode was changed, false otherwise.
         */
        virtual bool toggleFullscreen() = 0;

        /**
         * Cycles to the next windowed render resolution (F8). A no-op while
         * fullscreen; the fullscreen mode is exclusive to toggleFullscreen().
         *
         * @return true if the display mode was changed, false otherwise.
         */
        virtual bool changeResolution() = 0;

        /**
         * Returns the number of display modes available to the renderer.
         *
         * @return The mode count when the GPU is active, 0 otherwise.
         */
        virtual int requestDisplayModeCount() = 0;

        /**
         * Loads the game configuration (MARNI config plus window settings).
         * Called once at startup.
         */
        virtual void configReadAll() = 0;

        /**
         * Saves the game configuration back to disk.
         */
        virtual void configFlushAll() = 0;

        /**
         * Resets the config path strings and shuts the config system down.
         * Called on fatal exit.
         */
        virtual void configShutdown() = 0;

        /**
         * Toggles the bilinear texture filter flag in the MARNI config (F7).
         */
        virtual void configFlipFilter() = 0;

        /**
         * Applies the GPU filtering flags (GPU_17/GPU_18) selected by
         * gGameTable.byte_680592. Called every frame and from the gallery.
         */
        virtual void setGpuFlag() = 0;

        /**
         * Transfers the screen font bitmap onto the back buffer surface.
         * Called every frame before the flip.
         */
        virtual void fontTrans() = 0;

        /**
         * Advances the currently playing movie by one frame, dropping the
         * fullscreen window styles when the movie has finished.
         */
        virtual void movieUpdate() = 0;
    };

    // Decorator that records every draw call to the per-frame DrawStats while
    // delegating the actual drawing to an inner renderer. Records even when the
    // inner renderer rejects a submission.
    class LoggingRenderer final : public Renderer
    {
    public:
        explicit LoggingRenderer(std::unique_ptr<Renderer> inner);

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

        // Per-frame draw-call statistics accumulated by this logger.
        const DrawStats& drawStats() const;

    private:
        void record(DrawKind kind, int z, int page, int16_t x0, int16_t y0, int16_t x1, int16_t y1);

        std::unique_ptr<Renderer> inner;
        DrawStats stats;
    };

    // The global renderer used by the marni_draw hooks.
    extern std::unique_ptr<Renderer> g_renderer;

    // Constructs the global renderer (a LoggingRenderer wrapping the real
    // renderer). Called from marni::init_draw_hooks().
    void initRenderer();
}
