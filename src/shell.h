#pragma once

#include "gfx.h"
#include "input.h"
#include "movie.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace openre::graphics
{
    struct FontData;
    struct TextureBuffer;
}

namespace openre
{
    using TextureHandle = uint32_t;
    using FontHandle = uint32_t;
    using MovieHandle = uint32_t;

    class Stream
    {
    public:
        virtual ~Stream() = default;

        virtual size_t read(void* buffer, size_t size) = 0;
        virtual size_t write(const void* buffer, size_t size) = 0;
        virtual int64_t seek(int64_t offset, int origin) = 0;
        virtual int64_t tell() const = 0;
    };

    struct StreamResult
    {
        std::unique_ptr<Stream> stream;
        uint8_t found{};
        uint8_t extensionIndex{};
        uint8_t isMod{};
    };

    struct Color3f
    {
        float r;
        float g;
        float b;
    };

    struct Color4f : Color3f
    {
        float a;
    };

    struct Rumble
    {
        float low;
        float high;
    };

    struct OpenREVertex
    {
        float x;
        float y;
        float z;
        float s;
        float t;
    };

    enum class OpenREPrimKind : uint32_t
    {
        Unknown,
        TextureQuad,
        FontQuad,
        MovieQuad,
    };

    struct OpenREPrim
    {
        OpenREPrimKind kind;
        union
        {
            TextureHandle texture;
            FontHandle font;
            MovieHandle movie;
        };
        Color4f color;
        OpenREVertex vertices[4];
    };

    class InputState
    {
    public:
        std::array<bool, 32> commandsDown;
        std::array<bool, 32> commandsPressed;
        Color3f led;
        Rumble rumble;
    };

    class OpenREShell
    {
    public:
        virtual void setUpdate(std::function<void()> callback) = 0;
        virtual void run() = 0;

        // General
        virtual StreamResult getStream(std::string_view path, const std::vector<std::string_view>& extensions) = 0;

        // Graphics
        virtual openre::graphics::Size getRenderSize() = 0;
        virtual TextureHandle loadTexture(const openre::graphics::TextureBuffer& textureBuffer) = 0;
        virtual FontHandle
        loadFont(const openre::graphics::TextureBuffer& textureBuffer, const openre::graphics::FontData& fontData)
            = 0;
        virtual const openre::graphics::FontData* getFontData(FontHandle handle) = 0;
        virtual void pushPrimitive(const OpenREPrim& prim) = 0;

        // Movie
        virtual MovieHandle loadMovie(std::unique_ptr<openre::movie::MoviePlayer> movie) = 0;
        virtual openre::movie::MoviePlayer* getMovie(MovieHandle handle) = 0;

        // Input
        virtual InputState& getInputState() = 0;
    };

    std::unique_ptr<OpenREShell> createShell();
}

namespace openre::shellextensions
{
    OpenREPrim createQuad(float x, float y, float z, float w, float h, float s0 = 0, float t0 = 0, float s1 = 1, float t1 = 1);
    TextureHandle loadTexture(OpenREShell& shell, std::string_view path, uint32_t width, uint32_t height);
    void drawTexture(OpenREShell& shell, TextureHandle texture, float x, float y, float z, float w, float h);
    void drawTexture(
        OpenREShell& shell, TextureHandle texture, float x, float y, float z, float w, float h, float s0, float t0, float s1,
        float t1);
    void drawMovie(OpenREShell& shell, MovieHandle movie, float x, float y, float z, float w, float h);
    void fade(OpenREShell& shell, float r, float g, float b, float a);
    FontHandle loadFont(OpenREShell& shell, std::string_view path);
}
