#pragma once

#include <cstdint>
#include <vector>

namespace openre::graphics
{
    struct TextureBuffer
    {
        std::vector<uint8_t> pixels;
        uint32_t width{};
        uint32_t height{};
        uint8_t bpp{};
    };

    TextureBuffer adt2TextureBuffer(std::vector<uint8_t> input, uint32_t width, uint32_t height);
    TextureBuffer bmp2TextureBuffer(const std::vector<uint8_t>& input);
    TextureBuffer rgb555toTextureBuffer(std::vector<uint8_t> input, uint32_t width, uint32_t height);

    // Legacy functions
    int load_adt(const char* path, uint32_t* bufferSize, int mode);
}
