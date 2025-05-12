#pragma once

#include <cstdint>
#include <vector>

namespace openre::audio
{
    struct AudioBuffer
    {
        std::vector<uint8_t> samples;
        uint32_t sampleRate{};
        uint8_t channels{};
        uint8_t fmt{};
    };

    struct AudioFormat
    {
        uint32_t sampleRate{};
        uint8_t channels{};
        uint8_t fmt;
    };
}
