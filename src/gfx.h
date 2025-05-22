#pragma once

#include "data.hpp"

#include <cstdint>
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
    TextureBuffer webp2TextureBuffer(DataBlock input);

    // Legacy functions
    int load_adt(const char* path, uint32_t* bufferSize, int mode);
}
