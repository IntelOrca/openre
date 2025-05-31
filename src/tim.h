#pragma once

#include <cstdint>

namespace openre::tim
{
    struct Tim;

    int tim_buffer_to_surface(Tim* pTim, uint32_t page, uint32_t mode);
    void tim_init_hooks();
}
