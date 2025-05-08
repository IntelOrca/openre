#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace openre
{
    using TextureHandle = uint32_t;

    class Stream
    {
    public:
        virtual ~Stream() = default;

        virtual size_t read(void* buffer, size_t size) = 0;
        virtual size_t write(const void* buffer, size_t size) = 0;
        virtual void seek(int64_t offset, int origin) = 0;
        virtual int64_t tell() const = 0;
    };

    struct StreamResult
    {
        std::unique_ptr<Stream> stream;
        uint8_t found{};
        uint8_t extensionIndex{};
        uint8_t isMod{};
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
    };

    struct OpenREPrim
    {
        OpenREPrimKind kind;
        TextureHandle texture;
        OpenREVertex vertices[4];
    };

    class OpenREShell
    {
    public:
        virtual void setUpdate(std::function<void()> callback) = 0;
        virtual void run() = 0;

        // General
        virtual StreamResult getStream(std::string_view path, const std::vector<std::string_view>& extensions) = 0;

        // Graphics
        virtual TextureHandle loadTexture(uint32_t width, uint32_t height, void* pixels) = 0;
        virtual void pushPrimitive(const OpenREPrim& prim) = 0;
    };

    void openreMain(int argc, const char** argv);
}
