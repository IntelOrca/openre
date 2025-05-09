#include "gfx.h"

namespace openre::graphics
{
#pragma pack(push, 1)
    struct BitmapFileHeader
    {
        uint16_t signature;
        uint32_t fileSize;
        uint16_t reserved1;
        uint16_t reserved2;
        uint32_t pixelOffset;
    };

    struct BitmapHeader
    {
        uint32_t size;
        uint32_t width;
        uint32_t height;
        uint16_t planes;
        uint16_t bpp;
    };
#pragma pack(pop)

    template<typename T>
    static T align(T value, uint32_t powerof2)
    {
        auto mask = powerof2 - 1;
        return (value + mask) & ~mask;
    }

    TextureBuffer bmp2TextureBuffer(const std::vector<uint8_t>& input)
    {
        auto header1 = (BitmapFileHeader*)input.data();
        auto header2 = (BitmapHeader*)(input.data() + 14);
        auto pixelData = input.data() + header1->pixelOffset;
        auto bytesPerPixel = header2->bpp / 8;

        TextureBuffer result;
        result.width = static_cast<uint32_t>(header2->width);
        result.height = static_cast<uint32_t>(header2->height);
        result.bpp = static_cast<uint8_t>(header2->bpp);
        result.pixels.resize(result.width * result.height * bytesPerPixel);

        auto pitch = align(header2->width * bytesPerPixel, 4);
        auto src = pixelData;
        auto dst = result.pixels.data() + ((result.height - 1) * result.width * bytesPerPixel);
        for (uint32_t y = 0; y < result.height; y++)
        {
            auto srcLine = src;
            auto dstLine = dst;
            for (uint32_t x = 0; x < result.width; x++)
            {
                auto b = *srcLine++;
                auto g = *srcLine++;
                auto r = *srcLine++;
                *dstLine++ = r;
                *dstLine++ = g;
                *dstLine++ = b;
                if (bytesPerPixel == 4)
                {
                    *dstLine++ = *srcLine++;
                }
            }
            src += pitch;
            dst -= result.width * bytesPerPixel;
        }
        return result;
    }
}
