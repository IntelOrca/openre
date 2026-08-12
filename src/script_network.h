#pragma once

struct lua_State;

namespace openre::script
{
    class LuaVm;

    // Registers re.network.* (TCP listener/socket objects) into the given state.
    void registerNetworkBindings(lua_State* L, LuaVm* vm);

    // Start/stop the shared background socket thread.
    // networkInit() is called from script::init(); networkShutdown() from script::shutdown().
    void networkInit();
    void networkShutdown();
}
