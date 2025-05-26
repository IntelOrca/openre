#include "gfx.h"

#include <fstream>

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
        uint32_t compression;
        uint32_t sizeImage;
        uint32_t xPelsPerMeter;
        uint32_t yPelsPerMeter;
        uint32_t clrUsed;
        uint32_t clrImportant;
    };
#pragma pack(pop)

    template<typename T> static T align(T value, uint32_t powerof2)
    {
        auto mask = powerof2 - 1;
        return (value + mask) & ~mask;
    }

    TextureBuffer bmp2TextureBuffer(DataBlock input)
    {
        auto input8 = static_cast<const uint8_t*>(input.data);
        auto header1 = (BitmapFileHeader*)input8;
        auto header2 = (BitmapHeader*)(input8 + 14);
        auto pixelData = input8 + header1->pixelOffset;
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

    void dumpTextureBuffer(const std::filesystem::path& path, const TextureBuffer& textureBuffer)
    {
        auto bytesPerPixel = textureBuffer.bpp / 8;
        auto pitch = textureBuffer.width * 4;
        auto padding = pitch - textureBuffer.width * bytesPerPixel;
        auto pixelDataLen = pitch * textureBuffer.height;

        BitmapFileHeader header1{};
        header1.signature = 0x4D42;
        header1.fileSize = 54 + pixelDataLen;
        header1.pixelOffset = 54;

        BitmapHeader header2{};
        header2.size = 40;
        header2.width = textureBuffer.width;
        header2.height = textureBuffer.height;
        header2.planes = 1;
        header2.bpp = textureBuffer.bpp;
        header2.sizeImage = pixelDataLen;
        header2.xPelsPerMeter = 2835;
        header2.yPelsPerMeter = 2835;

        std::ofstream f;
        f.exceptions(std::ofstream::failbit | std::ofstream::badbit);
        f.open(path);
        f.write((const char*)&header1, sizeof(header1));
        f.write((const char*)&header2, sizeof(header2));

        uint8_t paddingData[8]{};
        auto src = textureBuffer.pixels.data();
        src += textureBuffer.width * textureBuffer.height * bytesPerPixel;
        for (size_t y = 0; y < textureBuffer.height; y++)
        {
            src -= textureBuffer.width * bytesPerPixel;
            auto srcLine = src;
            for (size_t x = 0; x < textureBuffer.width; x++)
            {
                f.put(srcLine[2]);
                f.put(srcLine[1]);
                f.put(srcLine[0]);
                if (textureBuffer.bpp == 32)
                    f.put(srcLine[3]);
                srcLine += bytesPerPixel;
            }
            f.write((const char*)paddingData, padding);
        }

        f.close();
    }
}
