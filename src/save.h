#pragma once

#include "openre.h"

namespace openre::save
{
    // 0x004310B0
    void save_reset();

    // 0x004310F0
    int save_print_flush();

    // 0x004315D0
    int save_menu_draw(int a1, int a2, int a3, int a4, int a5, int a6, char a7, uint8_t* a8);

    // 0x00432080
    void rsrc_release();

    // 0x004C57E0
    void mem_card();

    // 0x00509840
    char* GetSaveFolder();

    void save_init_hooks();
}
