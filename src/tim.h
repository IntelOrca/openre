#pragma once

#include <cstdint>

namespace openre::marni
{
    class Image;
}

namespace openre::tim
{
    struct Tim;

    int tim_buffer_to_surface(Tim* pTim, uint32_t page, uint32_t mode);
    void tim_init_hooks();

    // Decodes a TIM buffer directly into an Image, skipping the marni surface
    // intermediate. Returns false if the buffer is not a valid TIM. The pixel
    // buffer is the raw 16-bit-aligned TIM pixel data (pitch * height bytes)
    // and the palette buffer the raw CLUT entries, matching the layout that
    // timobject_in used to produce via MarniSurface2.
    bool decodeTim(const uint8_t* data, marni::Image& image);
}
