#include "gfx.h"

#include <cstring>
#include <webp/decode.h>

namespace openre::graphics
{
    TextureBuffer webp2TextureBuffer(DataBlock input)
    {
        int width, height;
        auto rgba = WebPDecodeRGBA(static_cast<const uint8_t*>(input.data), input.len, &width, &height);
        if (rgba == nullptr)
            return {};

        TextureBuffer result;
        result.width = static_cast<uint32_t>(width);
        result.height = static_cast<uint32_t>(height);
        result.bpp = 32;
        result.pixels.resize(result.width * result.height * result.bpp);
        std::memcpy(result.pixels.data(), rgba, width * height * 4);
        WebPFree(rgba);
        return result;
    }
}
