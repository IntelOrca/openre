#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace openre::render2d
{
    // Neutral decoded-image container. Decoupled from the legacy marni image
    // type so the scene-tree rendering path has no dependency on marni.
    struct Image
    {
        int width = 0;
        int height = 0;
        int depth = 0;         // pixel bit depth: 4, 8 or 16
        int palBpp = 0;        // palette entry bit depth (16); 0 = not paletted
        int palCnt = 0;        // number of palettes; 0 = not paletted
        bool psxFormat = true; // PSX 555 layout (red low, no alpha); false = alternate 555
        std::vector<uint8_t> pixels;
        std::vector<uint8_t> palette;
    };

    // Decodes a render2d::Image into 32-bit RGBA (per-pixel byte order
    // R, G, B, A). Returns false if the image format is unsupported or its
    // pixel buffer is too small. Used both for GPU upload and for exporting
    // a texture's pixels to the debug web UI.
    bool decodeImageToRgba(const Image& img, std::vector<uint8_t>& rgba);

    // Per-axis draw flags (mirrors the scene SpriteComponent flags).
    constexpr uint32_t kRepeatX = 1 << 0;
    constexpr uint32_t kRepeatY = 1 << 1;
    constexpr uint32_t kStretchX = 1 << 2;
    constexpr uint32_t kStretchY = 1 << 3;

    // Axis-aligned rectangle (top-left origin).
    struct Rect
    {
        float x = 0;
        float y = 0;
        float width = 0;
        float height = 0;
    };

    // One quad to draw during the next present(). Window pixel coordinates,
    // origin top-left. textureHandle == 0 selects a solid (untextured) quad
    // tinted with r/g/b/a - used for full-screen fade overlays.
    struct SpriteDraw
    {
        int textureHandle = 0;
        Rect src; // source rect in texture pixels; width/height 0 = full texture
        Rect dst; // destination rect in window pixels
        uint8_t r = 255;
        uint8_t g = 255;
        uint8_t b = 255;
        uint8_t a = 255;
        uint32_t flags = kStretchX | kStretchY;
        int z = 0; // draw priority; higher z = further back
    };

    // Full-window fade overlay set for the current frame by gfx.fade().
    struct FadeState
    {
        bool active = false;
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        uint8_t a = 0;
    };

    // Self-contained SDL_GPU 2D sprite renderer for the ECS scene tree. It
    // owns its textures, pipelines and vertex buffers and presents directly to
    // the swapchain in window pixel space. It intentionally has no connection
    // to the legacy marni renderer (no ordering tables, no marni texture
    // handles, no guest framebuffer).
    class SpriteRenderer
    {
    public:
        static SpriteRenderer& get();

        SpriteRenderer(const SpriteRenderer&) = delete;
        SpriteRenderer& operator=(const SpriteRenderer&) = delete;

        // Uploads an image to the GPU and returns an opaque handle (0 on
        // failure). The image data is copied, so the caller may free it.
        int loadTexture(const Image& image);

        // Allocates an RGBA8 2D texture that can be re-uploaded every frame via
        // updateDynamicTexture(). Returns an opaque handle (0 on failure),
        // usable with present() and unloadTexture(). Intended for video frames.
        int createDynamicTexture(int w, int h);

        // Re-uploads an RGBA8 frame (w*h*4 contiguous bytes) into a dynamic
        // texture previously created by createDynamicTexture().
        void updateDynamicTexture(int handle, const uint8_t* rgba, int w, int h);

        // Releases a texture previously returned by loadTexture().
        void unloadTexture(int handle);

        // Clears the window and draws the given sprites, then presents.
        void present(const SpriteDraw* sprites, size_t count);

        // Current window size in pixels.
        int screenWidth() const;
        int screenHeight() const;

        // Requests the next presented frame be copied into an internal
        // buffer; the download runs on the render thread inside that frame's
        // submit, so pair this with pollCapture() on a later tick. Returns
        // false when no frame is capturable (nothing presented yet).
        bool requestCapture();

        // Returns the BMP encoded from the most recent requestCapture() that
        // the render thread has serviced, or false while none is ready.
        // Consumes the result; each request yields one BMP.
        bool pollCapture(std::vector<uint8_t>& outBmp, int* outWidth = nullptr, int* outHeight = nullptr);

        // Per-frame fade overlay (set by the Lua gfx.fade binding).
        void setFade(const FadeState& state);
        FadeState takeFade();

        // Releases all GPU resources (called on the closing pass).
        void shutdown();

    private:
        SpriteRenderer();
        ~SpriteRenderer();

        struct Impl;
        Impl* impl;
    };
}