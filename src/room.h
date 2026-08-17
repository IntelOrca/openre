#pragma once

#include <cstdint>

namespace openre::room
{
    void room_set();
    // 0x004FAF80
    // Returns the map area index (0-19) for the given stage/room, used to select
    // the map texture file and the fg_map_area flag bit.
    uint32_t get_map_area_index(uint32_t stage, uint32_t room);
}
