#include "gfx.h"
#include "interop.hpp"

#include <cstring>
#include <filesystem>

namespace openre::graphics
{
    std::vector<uint8_t> decodeAdt(const std::vector<uint8_t>& input)
    {
        std::vector<uint8_t> result;
        std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "openre.adt";
        auto fp = fopen(tempPath.u8string().c_str(), "wb");
        if (fp != NULL)
        {
            auto bytesWritten = fwrite(input.data(), 1, input.size(), fp);
            fclose(fp);
            if (bytesWritten == input.size())
            {
                result.resize(0x80000);
                auto outLen = load_adt(tempPath.u8string().c_str(), (uint32_t*)result.data(), 0);
                result.resize(outLen);
            }
            std::filesystem::remove(tempPath);
        }
        return result;
    }

    /**
     * Reorganizes a 256x256 + 128*64 + 128*64 RGB555 buffer to a 320x240 RGB555 buffer.
     */
    static std::vector<uint8_t> reorgAdt(const std::vector<uint8_t>& input)
    {
        std::vector<uint8_t> output(320 * 240 * 2);
        auto src = input.data();
        auto dst = output.data();
        for (uint32_t y = 0; y < 240; y++)
        {
            std::memcpy(dst, src, 256 * 2);
            src += 256 * 2;
            dst += 320 * 2;
        }

        src = input.data() + (256 * 256 * 2);
        dst = output.data() + (256 * 2);
        for (uint32_t y = 0; y < 128; y++)
        {
            std::memcpy(dst, src, 64 * 2);
            src += 128 * 2;
            dst += 320 * 2;
        }

        src = input.data() + (256 * 256 * 2) + (64 * 2);
        for (uint32_t y = 128; y < 240; y++)
        {
            std::memcpy(dst, src, 64 * 2);
            src += 128 * 2;
            dst += 320 * 2;
        }

        return output;
    }

    TextureBuffer rgb555toTextureBuffer(std::vector<uint8_t> input, uint32_t width, uint32_t height)
    {
        auto numPixels = width * height;
        std::vector<uint8_t> output(numPixels * 3);

        auto src = input.data();
        auto dst = output.data();
        for (uint32_t i = 0; i < numPixels; i++)
        {
            auto c16 = src[0] | (src[1] << 8);
            auto c32 = rgb555to8888(c16);
            *dst++ = c32 & 0xFF;
            *dst++ = (c32 >> 8) & 0xFF;
            *dst++ = (c32 >> 16) & 0xFF;
            src += 2;
        }

        TextureBuffer result;
        result.pixels = std::move(output);
        result.width = width;
        result.height = height;
        result.bpp = 24;
        return result;
    }

    TextureBuffer adt2TextureBuffer(std::vector<uint8_t> input, uint32_t width, uint32_t height)
    {
        auto buffer = openre::graphics::decodeAdt(input);
        if (buffer.empty())
            return {};

        // 320x240 background ADT files are packed as 3 smaller textures
        if (width == 320 && height == 240)
            buffer = reorgAdt(buffer);

        return rgb555toTextureBuffer(buffer, width, height);
    }

    // 0x0043C590
    int load_adt(const char* path, uint32_t* bufferSize, int mode)
    {
        return interop::call<int, const char*, uint32_t*, int>(0x0043C590, path, bufferSize, mode);
    }
}
