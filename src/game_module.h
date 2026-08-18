#pragma once

#include <filesystem>

namespace openre
{
    /**
     * Resolves the active game module directory from the OPENRE_GAME
     * environment variable: searches `<cwd>/games/<name>` then
     * `<SDL_GetBasePath()>/games/<name>`, returning the first directory that
     * contains a main.lua. Returns an empty path when OPENRE_GAME is unset or
     * no module is found.
     */
    std::filesystem::path resolve_game_module_dir();

    /// The resolved module directory for this run (empty when no module).
    const std::filesystem::path& game_module_dir();

    /// Whether a game module is active for this run.
    bool game_module_active();

    /// Sets the active module directory at boot (from resolve_game_module_dir).
    void set_game_module_dir(std::filesystem::path dir);
}