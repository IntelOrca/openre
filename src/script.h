#pragma once

namespace openre::script
{
    // Called once from openre::onAttach.
    void init();
    // Called on DLL detach to stop the network thread.
    void shutdown();
    // Called once per frame from the game loop.
    void tick();
}
