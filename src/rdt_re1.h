#pragma once

#include <cstdint>
#include <vector>

namespace openre::rdt_re1
{
    /**
     * Converts a Resident Evil 1 room file (STAGE* / ROOM*.RDT) into an RE2
     * RDT room file. The RE1 RDT is fully parsed into structs owned by this
     * module (all RE1-side structs live in rdt_re1.cpp); re2.h structs are
     * used only as copy destinations when mapping RE1 data into RE2 format.
     *
     * Returns the RE2 RDT bytes, or an empty vector on failure.
     */
    std::vector<uint8_t> convert_re1_rdt(const uint8_t* data, size_t size);

    /**
     * Converts an RE1 cut background pak (STAGE* / RC*.pak) into an RGB555
     * pixel buffer. The pak is an LZ-compressed 16bpp TIM. Returns false on
     * failure. On success the image dimensions are reported in w/h (camera
     * cuts are 320x240; the one-off 316x236 menu screen has different ones).
     */
    bool convert_re1_background(const uint8_t* data, size_t size, std::vector<uint8_t>& rgb555, int& w, int& h);
}