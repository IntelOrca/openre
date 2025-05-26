#include "gfx.h"

namespace openre::graphics
{
    struct TimHeader
    {
        uint32_t magic;
        uint32_t fmt;
    };

    struct Clut
    {
        uint32_t size;
        uint16_t x;
        uint16_t y;
        uint16_t width;
        uint16_t height;
        uint16_t colors[1];
    };

    struct ImageData
    {
        uint32_t size;
        uint16_t orgX;
        uint16_t orgY;
        uint16_t width;
        uint16_t height;
        uint8_t data[1];
    };

    constexpr uint8_t Fmt4bpp = 0b00;
    constexpr uint8_t Fmt8bpp = 0b01;
    constexpr uint8_t Fmt16bpp = 0b10;
    constexpr uint8_t Fmt24bpp = 0b11;

    static TextureBuffer read4bpp(Clut& clut, ImageData& imageData)
    {
        TextureBuffer result;
        result.width = imageData.width * 4;
        result.height = imageData.height;
        result.bpp = 32;
        result.pixels.resize(result.width * result.height * 4);

        auto dst = reinterpret_cast<uint32_t*>(result.pixels.data());
        auto src = imageData.data;
        auto pitch = imageData.width * 2;
        for (auto y = 0; y < imageData.height; y++)
        {
            for (auto x = 0; x < pitch; x++)
            {
                auto p = *src++;
                auto lower = p & 0x0F;
                auto upper = p >> 4;
                *dst++ = rgb555to8888(clut.colors[lower]);
                *dst++ = rgb555to8888(clut.colors[upper]);
            }
        }
        return result;
    }

    static TextureBuffer read8bpp(Clut& clut, ImageData& imageData)
    {
        TextureBuffer result;
        result.width = imageData.width * 2;
        result.height = imageData.height;
        result.bpp = 32;
        result.pixels.resize(result.width * result.height * 4);

        auto dst = reinterpret_cast<uint32_t*>(result.pixels.data());
        auto src = imageData.data;
        auto pitch = imageData.width;
        for (auto y = 0; y < imageData.height; y++)
        {
            for (auto x = 0; x < pitch; x++)
            {
                *dst++ = rgb555to8888(clut.colors[*src++]);
            }
        }
        return result;
    }

    static TextureBuffer read16bpp(ImageData& imageData)
    {
        TextureBuffer result;
        result.width = imageData.width;
        result.height = imageData.height;
        result.bpp = 32;
        result.pixels.resize(result.width * result.height * 4);

        auto dst = reinterpret_cast<uint32_t*>(result.pixels.data());
        auto src = reinterpret_cast<uint16_t*>(imageData.data);
        for (auto y = 0; y < imageData.height; y++)
        {
            for (auto x = 0; x < imageData.width; x++)
            {
                *dst++ = rgb555to8888(*src++);
            }
        }
        return result;
    }

    TextureBuffer tim2TextureBuffer(DataBlock input)
    {
        if (input.len < 8)
            return {};

        auto header = (TimHeader*)input.data;
        if (header->magic != 16)
            return {};

        auto bpp = header->fmt & 0b11;
        auto hasClut = header->fmt & 0b1000;
        if (hasClut)
        {
            auto clut = (Clut*)((uint8_t*)input.data + sizeof(TimHeader));
            auto imageData = (ImageData*)((uint8_t*)clut + clut->size);
            if (bpp == Fmt4bpp)
            {
                return read4bpp(*clut, *imageData);
            }
            else if (bpp == Fmt8bpp)
            {
                return read8bpp(*clut, *imageData);
            }
        }
        else
        {
            auto imageData = (ImageData*)((uint8_t*)input.data + sizeof(TimHeader));
            if (bpp == Fmt16bpp)
            {
                return read16bpp(*imageData);
            }
        }
        return {};
    }
}
