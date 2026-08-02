#pragma once

#include "openre.h"

namespace openre::save
{
    // 0x004C57E0
    void mem_card();

    // 0x00509840
    char* GetSaveFolder();

    void save_init_hooks();
}
