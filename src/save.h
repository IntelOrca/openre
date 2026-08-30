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

    // Loads a save file specified on the command line before entering gameplay.
    // Reads the raw save image into the card buffer and applies it immediately.
    bool cmdline_load_save(const char* path);

    // Reads a save file into the card buffer without applying it. The module
    // load-game flow uses this in core.loadGame and defers application to
    // apply_saved_image() inside game_boot_init, because the boot chain's
    // init_global() wipe would otherwise discard the restored stage/room.
    bool cmdline_read_save(const char* path);

    // Copies the save image previously read into the card buffer into the live
    // game table and restores the saved state (mirrors CARD_STATE_LOAD).
    void apply_saved_image();

    void save_init_hooks();
}
