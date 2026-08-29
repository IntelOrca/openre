#include "script_vm.h"
#include "logger.h"
#include "script_bindings.h"
#include "script_shared.h"
#include <filesystem>
#include <lua.hpp>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace openre::script
{
    namespace
    {
        constexpr const char* MODULE_CACHE_KEY = "openre.script.module_cache";

        // All currently-alive LuaVm instances, so engine code holding a raw
        // LuaVm* (e.g. a Lua input handler callback captured by a scene
        // component) can check it is still safe to dispatch into. The game
        // module VM is destroyed on hot-reload while scene components may
        // still reference it for a moment.
        std::unordered_set<const LuaVm*> g_liveVms;

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
        installSharedTable(_state);

        lua_pushlightuserdata(_state, this);
        lua_newtable(_state);
        for (size_t i = 0; i < _searchPaths.size(); i++)
        {
            lua_pushstring(_state, _searchPaths[i].string().c_str());
            lua_rawseti(_state, -2, static_cast<lua_Integer>(i + 1));
        }
        lua_pushcclosure(_state, requireModule, 2);
        lua_setglobal(_state, "require");

        g_liveVms.insert(this);
    }

    LuaVm::~LuaVm()
    {
        g_liveVms.erase(this);
        dispose();
    }

    bool isVmAlive(const LuaVm* vm)
    {
        return vm != nullptr && g_liveVms.find(vm) != g_liveVms.end();
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

    bool LuaVm::runString(const std::string& code, std::string& outResult)
    {
        if (_state == nullptr)
        {
            outResult = "vm is closed";
            return false;
        }

        if (luaL_loadbuffer(_state, code.data(), code.size(), _name.c_str()) != LUA_OK)
        {
            auto* errorString = lua_tostring(_state, -1);
            outResult = errorString != nullptr ? errorString : "(compile error)";
            lua_pop(_state, 1);
            return false;
        }

        if (lua_pcall(_state, 0, 1, 0) != LUA_OK)
        {
            auto* errorString = lua_tostring(_state, -1);
            outResult = errorString != nullptr ? errorString : "(runtime error)";
            lua_pop(_state, 1);
            return false;
        }

        // Convert the single result to a string for transport back to the
        // caller's Lua state (the target VM is not directly reachable).
        const int type = lua_type(_state, -1);
        if (type == LUA_TNIL)
        {
            outResult.clear();
        }
        else if (type == LUA_TSTRING)
        {
            const auto len = static_cast<size_t>(lua_rawlen(_state, -1));
            outResult.assign(lua_tostring(_state, -1), len);
        }
        else if (type == LUA_TNUMBER || type == LUA_TBOOLEAN)
        {
            auto* text = lua_tostring(_state, -1);
            outResult = text != nullptr ? text : "";
        }
        else
        {
            outResult = "<" + std::string(lua_typename(_state, type)) + ">";
        }
        lua_pop(_state, 1);
        return true;
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

    void LuaVm::collectGarbage()
    {
        if (_state != nullptr)
            lua_gc(_state, LUA_GCCOLLECT, 0);
    }

    std::unique_ptr<LuaVm> createLuaVm(std::string name, std::vector<std::filesystem::path> searchPaths)
    {
        return std::make_unique<LuaVm>(std::move(name), std::move(searchPaths));
    }
}
