#pragma once

struct lua_State;

namespace openre::script
{
    // Process-lifetime singleton owning a private lua_State that holds the
    // data behind the cross-VM `shared` global. Every LuaVm installs a proxy
    // to it, so values written by one VM are visible to all others and survive
    // a script VM being destroyed and recreated (hot reload).
    class ScriptShared
    {
    public:
        static ScriptShared& get();

        ScriptShared(const ScriptShared&) = delete;
        ScriptShared& operator=(const ScriptShared&) = delete;

        lua_State* store() const
        {
            return _store;
        }
        int rootRef() const
        {
            return _rootRef;
        }

    private:
        ScriptShared();

        lua_State* _store{};
        int _rootRef = -1; // LUA_NOREF
    };

    // Install the persistent `shared` proxy as a global in `L`.
    void installSharedTable(lua_State* L);
}
