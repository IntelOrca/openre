#pragma once

#include <filesystem>

namespace openre::script
{
    // Called once from openre::onAttach.
    void init();
    // Called on DLL detach to stop the network thread.
    void shutdown();
    // Called once per frame to run script maintenance (see ScriptManager::tick).
    void tick();

    // Loads a game module (games/<name>/main.lua) into the script manager so
    // its registered systems run alongside the regular OPENRE_SCRIPTS files.
    void loadGameModule(const std::filesystem::path& dir);
}
