#include "gfx_bmp.h"

#include "logger.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

namespace openre::graphics
{
    namespace
    {
#pragma pack(push, 1)
        struct BmpFileHeader
        {
            uint16_t type; // 'BM'
            uint32_t size;
            uint16_t reserved1;
            uint16_t reserved2;
            uint32_t offset;
        };

        struct BmpInfoHeader
        {
            uint32_t size;
            int32_t width;
            int32_t height; // positive = bottom-up rows
            uint16_t planes;
            uint16_t bitCount;
            uint32_t compression; // 0 = BI_RGB
            uint32_t sizeImage;
            int32_t xPelsPerMeter;
            int32_t yPelsPerMeter;
            uint32_t clrUsed;
            uint32_t clrImportant;
        };
#pragma pack(pop)

        constexpr uint16_t kBmpType = 0x4D42; // 'B' | 'M' << 8

        // Expands a 5-bit channel value to 8 bits.
        uint8_t expand5(uint8_t v)
        {
            return (uint8_t)((v << 3) | (v >> 2));
        }

        // Stride of a BMP scanline, padded to 4 bytes.
        uint32_t rowPitch(int width, int bpp)
        {
            return ((uint32_t)width * bpp + 31u) / 32u * 4u;
        }
    }

    bool bmp_write_rgb555(const char* path, int width, int height, const uint16_t* pixels)
    {
        if (!path || !pixels || width <= 0 || height <= 0)
            return false;

        const size_t pixelCount = (size_t)width * height;

        // Unique-color count decides between an exact 8bpp palette and a
        // lossless 24bpp image.
        std::vector<uint16_t> colors(pixels, pixels + pixelCount);
        std::sort(colors.begin(), colors.end());
        colors.erase(std::unique(colors.begin(), colors.end()), colors.end());
        const bool indexed = colors.size() <= 256;

        const int bpp = indexed ? 8 : 24;
        const uint32_t pitch = rowPitch(width, bpp);
        const uint32_t paletteSize = indexed ? (uint32_t)(colors.size() * 4) : 0;
        const uint32_t imageSize = pitch * height;
        const uint32_t pixelOffset = sizeof(BmpFileHeader) + sizeof(BmpInfoHeader) + paletteSize;

        BmpFileHeader fileHeader{};
        fileHeader.type = kBmpType;
        fileHeader.size = pixelOffset + imageSize;
        fileHeader.offset = pixelOffset;

        BmpInfoHeader infoHeader{};
        infoHeader.size = sizeof(BmpInfoHeader);
        infoHeader.width = width;
        infoHeader.height = height;
        infoHeader.planes = 1;
        infoHeader.bitCount = bpp;
        infoHeader.compression = 0;
        infoHeader.sizeImage = imageSize;
        if (indexed)
        {
            infoHeader.clrUsed = (uint32_t)colors.size();
            infoHeader.clrImportant = (uint32_t)colors.size();
        }

        std::ofstream stream(path, std::ios::binary);
        if (!stream)
        {
            logging::logError("[gfx_bmp] failed to open '{}' for writing", path);
            return false;
        }

        stream.write((const char*)&fileHeader, sizeof(fileHeader));
        stream.write((const char*)&infoHeader, sizeof(infoHeader));

        if (indexed)
        {
            for (uint16_t c : colors)
            {
                // BMP palette entries are BGRA; PIX color is R | G << 5 | B << 10.
                const uint8_t bgra[4] = {
                    expand5((uint8_t)((c >> 10) & 0x1F)),
                    expand5((uint8_t)((c >> 5) & 0x1F)),
                    expand5((uint8_t)(c & 0x1F)),
                    0,
                };
                stream.write((const char*)bgra, sizeof(bgra));
            }
        }

        // Rows are stored bottom-up.
        std::vector<uint8_t> row(pitch, 0);
        for (int y = height - 1; y >= 0; y--)
        {
            const uint16_t* src = pixels + (size_t)y * width;
            if (indexed)
            {
                for (int x = 0; x < width; x++)
                {
                    row[x] = (uint8_t)(std::lower_bound(colors.begin(), colors.end(), src[x]) - colors.begin());
                }
            }
            else
            {
                for (int x = 0; x < width; x++)
                {
                    // BMP scanlines are BGR; PIX color is R | G << 5 | B << 10.
                    const uint16_t c = src[x];
                    row[x * 3 + 0] = expand5((uint8_t)((c >> 10) & 0x1F));
                    row[x * 3 + 1] = expand5((uint8_t)((c >> 5) & 0x1F));
                    row[x * 3 + 2] = expand5((uint8_t)(c & 0x1F));
                }
            }
            stream.write((const char*)row.data(), pitch);
        }

        if (!stream)
        {
            logging::logError("[gfx_bmp] failed to write '{}'", path);
            return false;
        }
        return true;
    }

    bool bmp_read_rgb555(const char* path, int& width, int& height, std::vector<uint16_t>& pixels)
    {
        width = 0;
        height = 0;
        pixels.clear();
        if (!path)
            return false;

        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            logging::logError("[gfx_bmp] failed to open '{}' for reading", path);
            return false;
        }

        BmpFileHeader fileHeader{};
        BmpInfoHeader infoHeader{};
        stream.read((char*)&fileHeader, sizeof(fileHeader));
        stream.read((char*)&infoHeader, sizeof(infoHeader));
        if (!stream || fileHeader.type != kBmpType || infoHeader.size < 40 || infoHeader.planes != 1 ||
            infoHeader.compression != 0)
        {
            logging::logError("[gfx_bmp] '{}' is not a valid BI_RGB BMP", path);
            return false;
        }

        if (infoHeader.bitCount != 8 && infoHeader.bitCount != 24)
        {
            logging::logError("[gfx_bmp] '{}' unsupported bit depth {}", path, infoHeader.bitCount);
            return false;
        }

        const bool indexed = infoHeader.bitCount == 8;
        const int w = std::abs(infoHeader.width);
        const int h = std::abs(infoHeader.height);
        if (w <= 0 || h <= 0)
            return false;

        // Read the palette (indexed images only), then position at the pixels.
        std::vector<uint8_t> palette;
        if (indexed)
        {
            stream.seekg(sizeof(BmpFileHeader) + sizeof(BmpInfoHeader), std::ios::beg);
            const uint32_t clrUsed = infoHeader.clrUsed ? infoHeader.clrUsed : 256;
            palette.resize((size_t)clrUsed * 4);
            stream.read((char*)palette.data(), (std::streamsize)palette.size());
            if (!stream)
                return false;
        }
        stream.seekg(fileHeader.offset, std::ios::beg);

        const uint32_t pitch = rowPitch(w, infoHeader.bitCount);
        std::vector<uint8_t> row(pitch, 0);
        pixels.resize((size_t)w * h);

        // Rows are stored bottom-up.
        for (int y = 0; y < h; y++)
        {
            const int srcY = infoHeader.height < 0 ? y : h - 1 - y;
            stream.read((char*)row.data(), pitch);
            if (!stream)
                return false;

            uint16_t* dst = pixels.data() + (size_t)srcY * w;
            if (indexed)
            {
                for (int x = 0; x < w; x++)
                {
                    const uint8_t idx = row[x];
                    if ((size_t)idx >= palette.size() / 4)
                        return false;
                    // Palette entries are BGRA; PIX color is R | G << 5 | B << 10.
                    const uint8_t* bgra = palette.data() + (size_t)idx * 4;
                    dst[x] = (uint16_t)((bgra[2] >> 3) | ((bgra[1] >> 3) << 5) | ((bgra[0] >> 3) << 10));
                }
            }
            else
            {
                for (int x = 0; x < w; x++)
                {
                    // BMP scanlines are BGR; PIX color is R | G << 5 | B << 10.
                    const uint8_t* bgr = row.data() + (size_t)x * 3;
                    dst[x] = (uint16_t)((bgr[2] >> 3) | ((bgr[1] >> 3) << 5) | ((bgr[0] >> 3) << 10));
                }
            }
        }

        width = w;
        height = h;
        return true;
    }
}
