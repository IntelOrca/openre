#include "relua.h"
#include "mod.h"
#include "openre.h"
#include <cstdio>
#include <filesystem>
#include <lua.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>

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
            setGlobal("re.subscribe", apiSubscribe);
            setGlobal("re.getFlag", apiGetFlag);
            setGlobal("re.setFlag", apiSetFlag);
            setGlobal("HookKind.tick", 1);
        }

        void setGlobal(std::string_view fullName, int32_t value)
        {
            auto [ns, name] = SplitNamespace(fullName);
            getOrCreateAndPushGlobal(ns);
            lua_pushlstring(_state, name.data(), name.size());
            lua_pushinteger(_state, value);
            lua_settable(_state, -3);
            lua_pop(_state, 1);
        }

        void setGlobal(std::string_view fullName, lua_CFunction f)
        {
            auto [ns, name] = SplitNamespace(fullName);
            getOrCreateAndPushGlobal(ns);
            lua_pushlstring(_state, name.data(), name.size());
            lua_pushlightuserdata(_state, this);
            lua_pushcclosure(_state, f, 1);
            lua_settable(_state, -3);
            lua_pop(_state, 1);
        }

        void getOrCreateAndPushGlobal(std::string_view s)
        {
            lua_pushglobaltable(_state);
            while (!s.empty())
            {
                auto part = s;
                auto delim = s.find('.');
                if (delim == std::string::npos)
                {
                    s = "";
                }
                else
                {
                    part = s.substr(0, delim);
                    s = s.substr(delim + 1);
                }

                lua_pushlstring(_state, part.data(), part.size());
                lua_gettable(_state, -2);
                if (!lua_istable(_state, -1))
                {
                    lua_pop(_state, 1);
                    lua_newtable(_state);
                    lua_pushlstring(_state, part.data(), part.size());
                    lua_pushvalue(_state, -2);
                    lua_settable(_state, -4);
                }
                lua_remove(_state, -2);
            }
        }

        void printStack()
        {
            auto top = lua_gettop(_state);
            std::printf("Lua Stack (size: %d):\n", top);
            for (auto i = 1; i <= top; i++)
            {
                auto type = lua_type(_state, i);
                printf("%d: ", i);
                switch (type)
                {
                case LUA_TSTRING: printf("string: %s", lua_tostring(_state, i)); break;
                case LUA_TNUMBER: printf("number: %f", lua_tonumber(_state, i)); break;
                case LUA_TBOOLEAN: // Boolean
                    printf("boolean: %s", lua_toboolean(_state, i) ? "true" : "false");
                    break;
                case LUA_TTABLE: printf("table"); break;
                case LUA_TFUNCTION: printf("function"); break;
                case LUA_TUSERDATA: printf("userdata"); break;
                case LUA_TTHREAD: printf("thread"); break;
                case LUA_TNIL: printf("nil"); break;
                default: printf("unknown"); break;
                }
                printf("\n");
            }
            printf("\n");
        }

        static std::tuple<std::string_view, std::string_view> SplitNamespace(std::string_view s)
        {
            auto delim = s.find_last_of('.');
            auto ns = delim == std::string::npos ? "" : s.substr(0, delim);
            auto name = s.substr(delim + 1);
            return { ns, name };
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
