#pragma once

#include <cstdint>
#include <vector>

namespace openre::graphics
{
    std::vector<uint8_t> decodeAdt(const void* input, size_t length);
    int load_adt(const char* path, uint32_t* bufferSize, int mode);
}
