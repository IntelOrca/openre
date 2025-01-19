#pragma once

#include "re2.h"

namespace openre::math
{
    Mat16& get_matrix(uint8_t type, uint8_t id);

    void math_init_hooks();
}