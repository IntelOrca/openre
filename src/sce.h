#pragma once

#include <cstdint>

namespace openre::sce
{
    void sce_init_hooks();
    int bitarray_get(uint32_t* bitArray, int index);
    void bitarray_set(uint32_t* bitArray, int index);
    void bitarray_clr(uint32_t* bitArray, int index);
}
