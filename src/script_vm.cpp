#include "script_vm.h"
#include "logger.h"
#include "script_bindings.h"
#include <filesystem>
#include <lua.hpp>
#include <string>
#include <utility>
#include <vector>

namespace openre::script
{
    namespace
    {
        constexpr const char* MODULE_CACHE_KEY = "openre.script.module_cache";

        constexpr luaL_Reg kStandardLibraries[] = {
            { "_G", luaopen_base },
            { LUA_COLIBNAME, luaopen_coroutine },
            { LUA_TABLIBNAME, luaopen_table },
            { LUA_STRLIBNAME, luaopen_string },
            { LUA_UTF8LIBNAME, luaopen_utf8 },
            { LUA_MATHLIBNAME, luaopen_math },
        };

        constexpr const char* kUnsafeGlobals[] = {
            "collectgarbage", "dofile",   "_G",     "getfenv", "getmetatable", "load",         "loadfile",
            "loadstring",     "rawequal", "rawget", "rawset",  "setfenv",      "setmetatable",
        };

        bool isInside(const std::filesystem::path& base, const std::filesystem::path& candidate)
        {
            auto relative = candidate.lexically_relative(base);
            if (relative.empty())
            {
                return false;
            }
            for (const auto& part : relative)
            {
                if (part == "..")
                {
                    return false;
                }
            }
            return true;
        }

        // Controlled require. Upvalues: 1 = LuaVm* (lightuserdata), 2 = search paths (array table).
        int requireModule(lua_State* L)
        {
            auto* vm = static_cast<LuaVm*>(lua_touserdata(L, lua_upvalueindex(1)));
            (void)vm;

            auto* name = luaL_checkstring(L, 1);

            std::string relativePath(name);
            for (auto& c : relativePath)
            {
                if (c == '.')
                {
                    c = '/';
                }
            }
            relativePath += ".lua";

            luaL_getsubtable(L, LUA_REGISTRYINDEX, MODULE_CACHE_KEY);
            lua_pushstring(L, name);
            lua_rawget(L, -2);
            if (!lua_isnil(L, -1))
            {
                lua_remove(L, -2);
                return 1;
            }
            lua_pop(L, 1);

            std::filesystem::path resolvedPath;
            auto pathCount = static_cast<int>(lua_rawlen(L, lua_upvalueindex(2)));
            for (int i = 1; i <= pathCount; i++)
            {
                lua_rawgeti(L, lua_upvalueindex(2), i);
                auto* searchPath = lua_tostring(L, -1);
                lua_pop(L, 1);
                if (searchPath == nullptr)
                {
                    continue;
                }

                auto candidate = std::filesystem::path(searchPath) / relativePath;
                if (!isInside(std::filesystem::path(searchPath), candidate))
                {
                    continue;
                }
                if (!std::filesystem::is_regular_file(candidate))
                {
                    continue;
                }

                resolvedPath = candidate;
                break;
            }

            if (resolvedPath.empty())
            {
                return luaL_error(L, "module '%s' not found in script search paths", name);
            }

            if (luaL_loadfilex(L, resolvedPath.string().c_str(), nullptr) != LUA_OK)
            {
                return lua_error(L);
            }

            lua_pushvalue(L, 1);
            if (lua_pcall(L, 1, 1, 0) != LUA_OK)
            {
                return lua_error(L);
            }

            lua_pushvalue(L, -1);
            lua_setfield(L, -3, name);
            lua_remove(L, -2);
            return 1;
        }
    }

    LuaVm::LuaVm(std::string name, std::vector<std::filesystem::path> searchPaths)
        : _name(std::move(name))
        , _searchPaths(std::move(searchPaths))
        , _state(luaL_newstate())
    {
        for (const auto& lib : kStandardLibraries)
        {
            luaL_requiref(_state, lib.name, lib.func, 1);
            lua_pop(_state, 1);
        }

        for (const char* global : kUnsafeGlobals)
        {
            lua_pushnil(_state);
            lua_setglobal(_state, global);
        }

        registerBindings(_state, this);

        lua_pushlightuserdata(_state, this);
        lua_newtable(_state);
        for (size_t i = 0; i < _searchPaths.size(); i++)
        {
            lua_pushstring(_state, _searchPaths[i].string().c_str());
            lua_rawseti(_state, -2, static_cast<lua_Integer>(i + 1));
        }
        lua_pushcclosure(_state, requireModule, 2);
        lua_setglobal(_state, "require");
    }

    LuaVm::~LuaVm()
    {
        dispose();
    }

    void LuaVm::dispose()
    {
        if (_state != nullptr)
        {
            lua_close(_state);
            _state = nullptr;
        }
    }

    void LuaVm::run(const std::filesystem::path& path)
    {
        auto result = luaL_dofile(_state, path.string().c_str());
        if (result != LUA_OK)
        {
            auto* errorString = lua_tostring(_state, -1);
            openre::logging::logWarning("[script] {}: {}", _name, errorString != nullptr ? errorString : "(unknown error)");
            lua_pop(_state, 1);
        }
    }

    void LuaVm::callHooks(HookKind kind)
    {
        for (const auto& subscription : _subscriptions)
        {
            if (subscription.kind != kind)
            {
                continue;
            }

            lua_rawgeti(_state, LUA_REGISTRYINDEX, subscription.registryRef);
            auto result = lua_pcall(_state, 0, 0, 0);
            if (result != LUA_OK)
            {
                auto* errorString = lua_tostring(_state, -1);
                openre::logging::logWarning("[script] {}: {}", _name, errorString != nullptr ? errorString : "(unknown error)");
                lua_pop(_state, 1);
            }
        }
    }

    void LuaVm::subscribe(HookKind kind, int registryRef)
    {
        _subscriptions.push_back({ kind, registryRef });
    }

    std::unique_ptr<LuaVm> createLuaVm(std::string name, std::vector<std::filesystem::path> searchPaths)
    {
        return std::make_unique<LuaVm>(std::move(name), std::move(searchPaths));
    }
}
