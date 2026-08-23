#pragma once

namespace openre
{
    /**
     * Implements the "create-game" subcommand, which imports assets from an
     * original game installation into a game pack:
     *
     *   openre.exe create-game <target-pack-dir> --source <original-game-dir>
     *
     * `args`/`argc` use the usual argv convention (args[0] is the program
     * name). Returns the process exit code: 0 on success, non-zero on failure
     * or when the user aborts.
     */
    int game_create_main(const char** args, int argc);

    // True when args request the create-game subcommand.
    bool game_create_requested(const char** args, int argc);
}
