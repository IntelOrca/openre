#include "script.h"

#include "script_manager.h"
#include "script_network.h"

namespace openre::script
{
    void init()
    {
        networkInit();
    }

    void tick()
    {
        ScriptManager::get().tick();
    }

    void loadGameModule(const std::filesystem::path& dir)
    {
        ScriptManager::get().loadGameModule(dir);
    }

    void shutdown()
    {
        networkShutdown();
    }
}
