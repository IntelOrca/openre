#pragma once

#include <cstdint>
#include "re2.h"

namespace openre
{
    extern GameTable& gGameTable;
    extern uint32_t& gGameFlags;
    extern uint32_t& gErrorCode;
    extern uint32_t& dword_988624;
    extern Unknown68A204*& dword_68A204;

    void sub_508CE0(int a0);
}
