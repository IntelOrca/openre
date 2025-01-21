#pragma once

#include "re2.h"
#include <cstdint>

namespace openre::camera
{
    VCut* sub_4E5020(uint8_t cut_id);
    VCut* cut_change(uint8_t cut_id);
    void cut_check(uint8_t cut_id);

    void camera_init_hooks();
};