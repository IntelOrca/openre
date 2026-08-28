#pragma once

#include <cstdint>
#include <string>
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

    /**
     * Runtime RE1 room info for the engine's room loader. RE1 rooms are named
     * ROOM<stage><room><variant>.RDT (4 hex digits): stage 1..7, room 0..0x1F,
     * variant 0/1. The converted rooms live in the pack as
     * room/room<S><RR><V>.rdt and share a cut-background room code of the
     * first three digits (e.g. "100" for ROOM1000/ROOM1001).
     *
     * Maps an engine (stage, room, variant) triple to the pack-relative room
     * path and the room code used by roomcut/<code>_<cut>.bmp. Returns false
     * when the triple is out of range.
     */
    struct Re1RoomInfo
    {
        std::string roomPath; // pack path, e.g. "room\\room1000.rdt"
        std::string roomCode; // 3 hex digits, e.g. "100"
    };
    bool re1_room_info(int stage, int room, int variant, Re1RoomInfo& out);
}
