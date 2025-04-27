#include "relua.h"
#include "mod.h"
#include "openre.h"
#include "sce.h"
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
    constexpr const char* METATABLE_ENTITY = "meta_entity";

    class LuaManager
    {
    private:
        static inline std::unique_ptr<LuaManager> instance;
        lua_State* _state;
        std::vector<std::vector<int>> _subscriptions;

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
            const auto& list = _subscriptions;
            auto listIndex = static_cast<size_t>(kind);
            if (list.size() <= listIndex)
                return;

            const auto& subList = list[listIndex];
            for (auto ref : subList)
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
            setGlobal("re.getEntity", apiGetEntity);

            setGlobal("HookKind.tick", static_cast<int32_t>(HookKind::Tick));

            setGlobal("EntityKind.player", 1);
            setGlobal("EntityKind.splayer", 2);
            setGlobal("EntityKind.enemy", 3);
            setGlobal("EntityKind.object", 4);
            setGlobal("EntityKind.door", 5);

            setMetatable(METATABLE_ENTITY, entity_get, entity_set);
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

        void setMetatable(const char* name, lua_CFunction getter, lua_CFunction setter)
        {
            luaL_newmetatable(_state, name);
            lua_pushcfunction(_state, getter);
            lua_setfield(_state, -2, "__index");
            lua_pushcfunction(_state, setter);
            lua_setfield(_state, -2, "__newindex");
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

            auto kind = static_cast<size_t>(lua_tointeger(L, 1));
            if (kind <= static_cast<size_t>(HookKind::Undefined))
                return 0;
            if (kind > static_cast<size_t>(HookKind::Tick))
                return 0;

            lua_pushvalue(L, 2);
            int ref = luaL_ref(L, LUA_REGISTRYINDEX);

            auto& list = ptr->_subscriptions;
            if (list.size() <= kind)
            {
                list.resize(kind + 1);
            }
            list[kind].push_back(ref);
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

        static int apiGetEntity(lua_State* L)
        {
            auto custom = (int32_t*)lua_newuserdata(L, 8);
            custom[0] = static_cast<int32_t>(luaL_checknumber(L, 1));
            custom[1] = static_cast<int32_t>(luaL_checknumber(L, 2));
            luaL_getmetatable(L, METATABLE_ENTITY);
            lua_setmetatable(L, -2);
            return 1;
        }

        static int entity_get(lua_State* L)
        {
            auto userdata = (int32_t*)luaL_checkudata(L, 1, METATABLE_ENTITY);
            auto key = luaL_checkstring(L, 2);
            if (strcmp(key, "kind") == 0)
            {
                lua_pushnumber(L, userdata[0]);
            }
            else if (strcmp(key, "index") == 0)
            {
                lua_pushnumber(L, userdata[1]);
            }
            else if (userdata[0] == 3)
            {
                auto enemy = (EnemyEntity*)sce::GetEnemyEntity(userdata[1]);
                if (enemy != nullptr)
                {
                    if (strcmp(key, "type") == 0)
                    {
                        lua_pushnumber(L, enemy->id);
                    }
                    else if (strcmp(key, "life") == 0)
                    {
                        lua_pushnumber(L, enemy->life);
                    }
                    else
                    {
                        lua_pushnil(L);
                    }
                }
                else
                {
                    lua_pushnil(L);
                }
            }
            else
            {
                lua_pushnil(L);
            }
            return 1;
        }

        static int entity_set(lua_State* L)
        {
            auto userdata = (int32_t*)luaL_checkudata(L, 1, METATABLE_ENTITY);
            auto key = luaL_checkstring(L, 2);
            auto value = luaL_checkinteger(L, 3);
            if (userdata[0] == 3)
            {
                auto enemy = (EnemyEntity*)sce::GetEnemyEntity(userdata[1]);
                if (enemy != nullptr)
                {
                    if (strcmp(key, "life") == 0)
                    {
                        enemy->life = static_cast<int16_t>(value);
                    }
                }
            }
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
