#pragma once

#include "data.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace openre::graphics
{
    std::vector<uint8_t> decodeAdt(const std::vector<uint8_t>& input);

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
