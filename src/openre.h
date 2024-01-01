#pragma once

#include "re2.h"
#include <cstdint>

namespace openre
{
    extern GameTable& gGameTable;
    extern uint32_t& gGameFlags;
    extern uint32_t& gErrorCode;
    extern uint32_t& _memTop;
    extern Unknown68A204*& dword_68A204;
    extern PlayerEntity& gPlayerEntity;
    extern uint16_t& gPoisonStatus;
    extern uint8_t& gPoisonTimer;

    void task_sleep(int frames);
}
