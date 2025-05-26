#pragma once

#include "data.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace openre::graphics
{
    struct Size
    {
        uint32_t width{};
        uint32_t height{};
    };

    struct TextureBuffer
    {
        std::vector<uint8_t> pixels;
        uint32_t width{};
        uint32_t height{};
        uint8_t bpp{};
    };

    struct VideoFormat
    {
        openre::graphics::Size resolution;
        uint16_t framesPerSecond{};
    };

    TextureBuffer adt2TextureBuffer(std::vector<uint8_t> input, uint32_t width, uint32_t height);
    TextureBuffer bmp2TextureBuffer(DataBlock input);
    TextureBuffer rgb555toTextureBuffer(std::vector<uint8_t> input, uint32_t width, uint32_t height);
    TextureBuffer tim2TextureBuffer(DataBlock input);
    TextureBuffer webp2TextureBuffer(DataBlock input);

    void dumpTextureBuffer(const std::filesystem::path& path, const TextureBuffer& textureBuffer);

    // Legacy functions
    int load_adt(const char* path, uint32_t* bufferSize, int mode);

    inline uint32_t rgb555to8888(uint16_t c16)
    {
        // Black is transparent
        if (c16 == 0)
            return 0;

        // HSB denotes transparency unless color is black
        if ((c16 & 0x8000) && (c16 & 0x7FFF) != 0)
            return 0;

        auto r = ((c16 >> 0) & 0b11111) * 8;
        auto g = ((c16 >> 5) & 0b11111) * 8;
        auto b = ((c16 >> 10) & 0b11111) * 8;
        auto a = 255;
        return r | (g << 8) | (b << 16) | (a << 24);
    }
}
