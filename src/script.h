#pragma once

#include <filesystem>

namespace openre::script
{
    // Called once from openre::onAttach.
    void init();
    // Called on DLL detach to stop the network thread.
    void shutdown();
    // Called once per frame from the game loop.
    void tick();

    // Runs the tick hooks of a single named script (see ScriptManager::pumpScript).
    void pumpScript(const std::string& name);

    // Loads a game module (games/<name>/main.lua) into the script manager so
    // it receives tick hooks alongside the regular OPENRE_SCRIPTS files.
    void loadGameModule(const std::filesystem::path& dir);
}
