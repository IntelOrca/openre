#include "script_shared.h"
#include <cstddef>
#include <cstring>
#include <lua.hpp>

namespace openre::script
{
    namespace
    {
        constexpr const char* METATABLE_NAME = "__shared_proxy";
        constexpr int MAX_COPY_DEPTH = 64;

        void pushProxy(lua_State* L_dst, ScriptShared* self, int storeTableRef);

        // The proxy userdata carries the target state pointer and registry
        // ref side by side.
        ScriptShared* readShared(std::byte* ud)
        {
            ScriptShared* self;
            std::memcpy(&self, ud, sizeof(self));
            return self;
        }
        int readTableRef(std::byte* ud)
        {
            int ref;
            std::memcpy(&ref, ud + sizeof(ScriptShared*), sizeof(ref));
            return ref;
        }

        bool isSupportedKey(lua_State* L, int index)
        {
            const int type = lua_type(L, index);
            return type == LUA_TSTRING || type == LUA_TNUMBER;
        }

        // Push a copy of the key at `index` in L onto the store state.
        // Returns false when the key is neither string nor number.
        bool pushKeyToStore(lua_State* store, lua_State* L, int index)
        {
            switch (lua_type(L, index))
            {
            case LUA_TSTRING: lua_pushstring(store, lua_tostring(L, index)); return true;
            case LUA_TNUMBER:
                if (lua_isinteger(L, index))
                {
                    lua_pushinteger(store, lua_tointeger(L, index));
                }
                else
                {
                    lua_pushnumber(store, lua_tonumber(L, index));
                }
                return true;
            default: return false;
            }
        }

        // Push a primitive (boolean/number/string) from the top of the store
        // stack onto L.
        void pushPrimitiveTo(lua_State* L, lua_State* store)
        {
            switch (lua_type(store, -1))
            {
            case LUA_TBOOLEAN: lua_pushboolean(L, lua_toboolean(store, -1)); break;
            case LUA_TNUMBER:
                if (lua_isinteger(store, -1))
                {
                    lua_pushinteger(L, lua_tointeger(store, -1));
                }
                else
                {
                    lua_pushnumber(L, lua_tonumber(store, -1));
                }
                break;
            default: lua_pushstring(L, lua_tostring(store, -1)); break;
            }
        }

        // Copy a value from `src` into `store`, leaving exactly one new value
        // on the store stack. Tables are copied recursively into fresh store
        // tables; only plain data (booleans/numbers/strings) is copied —
        // anything else becomes nil, and recursion stops past MAX_COPY_DEPTH.
        // `absSrcIndex` must be an absolute (positive) index into src.
        void copyValueToStore(lua_State* store, lua_State* src, int absSrcIndex, int depth)
        {
            if (depth > MAX_COPY_DEPTH || !lua_checkstack(src, 2) || !lua_checkstack(store, 4))
            {
                lua_pushnil(store);
                return;
            }

            switch (lua_type(src, absSrcIndex))
            {
            case LUA_TBOOLEAN: lua_pushboolean(store, lua_toboolean(src, absSrcIndex)); return;
            case LUA_TNUMBER:
                if (lua_isinteger(src, absSrcIndex))
                {
                    lua_pushinteger(store, lua_tointeger(src, absSrcIndex));
                }
                else
                {
                    lua_pushnumber(store, lua_tonumber(src, absSrcIndex));
                }
                return;
            case LUA_TSTRING: lua_pushstring(store, lua_tostring(src, absSrcIndex)); return;
            case LUA_TTABLE: break;
            default: lua_pushnil(store); return;
            }

            lua_newtable(store);
            const int newTableIdx = lua_gettop(store);

            // lua_rawset expects [..., key, value] with the value on top.
            lua_pushnil(src);
            while (lua_next(src, absSrcIndex) != 0)
            {
                const int valueIdx = lua_gettop(src);
                if (isSupportedKey(src, valueIdx - 1) && pushKeyToStore(store, src, valueIdx - 1))
                {
                    copyValueToStore(store, src, valueIdx, depth + 1); // Value on top.
                    if (lua_isnil(store, -1))
                    {
                        lua_pop(store, 2); // Drop unsupported values with their key.
                    }
                    else
                    {
                        lua_rawset(store, newTableIdx);
                    }
                }
                lua_pop(src, 1); // Pop the value, keep the key for lua_next.
            }
        }

        // Indexing a proxy reads through to the referenced table in the
        // shared store. Nested tables come back as proxies into the same
        // store, so mutations through them stay visible everywhere.
        int proxy__index(lua_State* L)
        {
            auto* ud = static_cast<std::byte*>(lua_touserdata(L, 1));
            auto* self = readShared(ud);
            auto* store = self->store();

            if (!pushKeyToStore(store, L, 2))
            {
                return luaL_error(L, "shared keys must be string or number");
            }

            const int base = lua_gettop(store);                      // Index of the key just pushed.
            lua_rawgeti(store, LUA_REGISTRYINDEX, readTableRef(ud)); // [key, table]
            lua_insert(store, base);                                 // [table, key]
            lua_rawget(store, base);                                 // [table, value]

            switch (lua_type(store, -1))
            {
            case LUA_TNIL:
            {
                lua_pop(store, 2);
                lua_pushnil(L);
                break;
            }
            case LUA_TTABLE:
            {
                // Ref the child table (pops it) so it survives while no
                // proxy holds it, and hand out a proxy pointing at it.
                const int childRef = luaL_ref(store, LUA_REGISTRYINDEX);
                lua_settop(store, base - 1);
                pushProxy(L, self, childRef);
                break;
            }
            default:
            {
                pushPrimitiveTo(L, store);
                lua_settop(store, base - 1);
                break;
            }
            }
            return 1;
        }

        // Writing through a proxy copies the value into the shared store.
        // Functions/userdata/nil are silently dropped.
        int proxy__newindex(lua_State* L)
        {
            auto* ud = static_cast<std::byte*>(lua_touserdata(L, 1));
            auto* self = readShared(ud);
            auto* store = self->store();

            const int valueType = lua_type(L, 3);
            const bool supported
                = valueType == LUA_TTABLE || valueType == LUA_TBOOLEAN || valueType == LUA_TNUMBER || valueType == LUA_TSTRING;
            if (!supported)
            {
                return 0; // Functions/userdata/nil are silently dropped.
            }
            if (!pushKeyToStore(store, L, 2))
            {
                return luaL_error(L, "shared keys must be string or number");
            }

            const int base = lua_gettop(store);
            if (!lua_checkstack(store, 8))
            {
                lua_settop(store, base - 1);
                return luaL_error(L, "shared store stack exhausted");
            }

            lua_rawgeti(store, LUA_REGISTRYINDEX, readTableRef(ud)); // [key, table]
            lua_insert(store, base);                                 // [table, key]
            // Stack is now [..., table, key]; pushing the value below leaves
            // [..., table, key, value] as lua_rawset expects.

            if (valueType == LUA_TTABLE)
            {
                copyValueToStore(store, L, 3, 0); // Leaves the copy on top.
            }
            else
            {
                switch (valueType)
                {
                case LUA_TBOOLEAN: lua_pushboolean(store, lua_toboolean(L, 3)); break;
                case LUA_TNUMBER:
                    if (lua_isinteger(L, 3))
                    {
                        lua_pushinteger(store, lua_tointeger(L, 3));
                    }
                    else
                    {
                        lua_pushnumber(store, lua_tonumber(L, 3));
                    }
                    break;
                default: lua_pushstring(store, lua_tostring(L, 3)); break;
                }
            }

            lua_rawset(store, -3); // Pops key + value.
            lua_settop(store, 0);  // The store keeps no stack state between calls.
            return 0;
        }

        void pushProxy(lua_State* L_dst, ScriptShared* self, int storeTableRef)
        {
            auto* ud = static_cast<std::byte*>(lua_newuserdata(L_dst, sizeof(ScriptShared*) + sizeof(int)));
            std::memcpy(ud, &self, sizeof(self));
            std::memcpy(ud + sizeof(ScriptShared*), &storeTableRef, sizeof(storeTableRef));

            // The store's own state never runs script code, so each consumer
            // VM builds the proxy metatable locally around the C functions.
            if (luaL_newmetatable(L_dst, METATABLE_NAME))
            {
                lua_pushcfunction(L_dst, proxy__index);
                lua_setfield(L_dst, -2, "__index");
                lua_pushcfunction(L_dst, proxy__newindex);
                lua_setfield(L_dst, -2, "__newindex");
            }
            lua_setmetatable(L_dst, -2);
        }
    }

    ScriptShared& ScriptShared::get()
    {
        static ScriptShared instance;
        return instance;
    }

    ScriptShared::ScriptShared()
    {
        _store = luaL_newstate();
        if (_store == nullptr)
        {
            return;
        }

        lua_newtable(_store);
        _rootRef = luaL_ref(_store, LUA_REGISTRYINDEX);
    }

    void installSharedTable(lua_State* L)
    {
        auto& shared = ScriptShared::get();
        pushProxy(L, &shared, shared.rootRef());
        lua_setglobal(L, "shared");
    }
}
