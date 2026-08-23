#pragma once

#include <cstdint>

namespace openre::graphics
{
    /**
     * Writes an RGB555 image to `path` as a BMP file. When the image contains
     * at most 256 unique colors an exact palettized 8bpp BMP is written;
     * otherwise a lossless 24bpp BMP is written instead. Rows are stored
     * bottom-up as per the BMP convention.
     * Returns true when the file was written successfully.
     */
    bool bmp_write_rgb555(const char* path, int width, int height, const uint16_t* pixels);
}
