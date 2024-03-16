#include "relua.h"
#include "mod.h"
#include "openre.h"
#include <cstdio>
#include <filesystem>
#include <lua.hpp>
#include <memory>
#include <string>

namespace fs = std::filesystem;

using namespace openre::modding;

namespace openre::lua
{
    class LuaManager
    {
    private:
        static inline std::unique_ptr<LuaManager> instance;
        lua_State* _state;
        std::vector<int> _subscriptionsTick;

    public:
        static LuaManager& get()
        {
            if (instance == nullptr)
            {
                instance = std::make_unique<LuaManager>();
            }
            return *instance;
        }

        LuaManager()
        {
            _state = luaL_newstate();
            luaL_openlibs(_state);
            setupGlobals();
        }

        LuaManager(const LuaManager&) = delete;

        ~LuaManager()
        {
            lua_close(_state);
        }

        void runMod(Mod& mod)
        {
            mod.log("Starting");

            const auto& scriptPath = mod.scriptPath;
            auto result = luaL_dofile(_state, scriptPath.string().c_str());
            if (result != LUA_OK)
            {
                auto errString = lua_tostring(_state, -1);
                mod.log("Error: " + std::string(errString));
            }
        }

        void callHooks(HookKind kind)
        {
            for (auto ref : _subscriptionsTick)
            {
                lua_rawgeti(_state, LUA_REGISTRYINDEX, ref);
                lua_pcall(_state, 0, 0, 0);
            }
        }

    private:
        void setupGlobals()
        {
            auto L = _state;

            lua_newtable(L);
            lua_setglobal(L, "re");

            defineApi("subscribe", apiSubscribe);
            defineApi("getFlag", apiGetFlag);
            defineApi("setFlag", apiSetFlag);
            setGlobal("HOOK_TICK", 1);
        }

        void setGlobal(const char* name, int32_t value)
        {
            lua_pushinteger(_state, value);
            lua_setglobal(_state, name);
        }

        void defineApi(const char* name, lua_CFunction f)
        {
            lua_getglobal(_state, "re");
            lua_pushstring(_state, name);
            lua_pushlightuserdata(_state, this);
            lua_pushcclosure(_state, f, 1);
            lua_settable(_state, -3);
        }

        static int apiSubscribe(lua_State* L)
        {
            auto ptr = static_cast<LuaManager*>(lua_touserdata(L, lua_upvalueindex(1)));

            auto kind = lua_tointeger(L, 1);
            lua_pushvalue(L, 2);
            int ref = luaL_ref(L, LUA_REGISTRYINDEX);

            ptr->_subscriptionsTick.push_back(ref);
            return 0;
        }

        static int apiGetFlag(lua_State* L)
        {
            auto group = lua_tointeger(L, 1);
            auto index = lua_tointeger(L, 2);
            auto value = check_flag(static_cast<FlagGroup>(group), static_cast<uint32_t>(index));
            lua_pushboolean(L, value);
            return 1;
        }

        static int apiSetFlag(lua_State* L)
        {
            auto group = lua_tointeger(L, 1);
            auto index = lua_tointeger(L, 2);
            auto value = lua_toboolean(L, 3);
            set_flag(static_cast<FlagGroup>(group), static_cast<uint32_t>(index), value);
            return 0;
        }
    };

    void relua_init()
    {
        auto& luaManager = LuaManager::get();
        auto& modManager = ModManager::get();

        modManager.loadMods();
        for (auto& mod : modManager.mods)
        {
            luaManager.runMod(mod);
        }
    }

    void relua_call_hooks(HookKind kind)
    {
        auto& luaManager = LuaManager::get();
        luaManager.callHooks(kind);
    }
}
