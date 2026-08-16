#pragma once

#include <string>

namespace openre
{
    // Command-line start options extracted by cmdline_parse().
    //
    //   openre.exe <savefile> [-p <player>] [-r <roomcode>] [-s <scenario>]
    //
    //   <savefile>  First positional argument; a path to a 0x800-byte save file
    //               which is loaded before entering gameplay.
    //   -p <n>      Player id (0=Leon, 1=Claire, ... PLD_* enum) or a name
    //               alias ("leon" = 0, "claire" = 1).
    //   -r <code>   Community room code: first char is stage+1 (1-7), the rest is
    //               the room number in hex. e.g. -r 100 = stage 0 room 0 (main
    //               hall, room1000.rdt), -r 10C = stage 0 room 0xC (room10C0.rdt).
    //   -s <n>      Scenario: 0 or 'a'/'A' = A, 1 or 'b'/'B' = B.
    struct CmdlineOptions
    {
        bool saveRequested = false;
        std::string savePath;
        int player = -1;   // PLD_* id (0=Leon, 1=Claire, ...); -1 = not given
        int stage = -1;    // 0-based stage; -1 = not given
        int room = -1;     // room number; -1 = not given
        int scenario = -1; // 0 = A, 1 = B; -1 = not given
        bool startActive = false;

        bool startRequested() const
        {
            return saveRequested || player >= 0 || stage >= 0;
        }
    };

    // Parses lpCmdLine into the global options; called from win_main.
    void cmdline_parse(const char* lpCmdLine);

    // Accessors for the parsed options.
    const CmdlineOptions& cmdline_options();
    bool cmdline_start_requested();
    bool cmdline_start_active();
    void cmdline_mark_start();
    void cmdline_consume_start();
    bool cmdline_save_requested();
    const char* cmdline_save_path();
    int cmdline_player();
    int cmdline_stage();
    int cmdline_room();
    int cmdline_scenario();
}
