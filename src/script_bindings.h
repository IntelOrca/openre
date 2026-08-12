#pragma once

struct lua_State;

namespace openre::script
{
    class LuaVm;

    // Sets up the sandboxed base environment and the `re.*` API table into the
    // given state. Must be called during VM creation after unsafe globals are
    // removed. `vm` is captured as an upvalue for functions that need it.
    void registerBindings(lua_State* L, LuaVm* vm);
}
