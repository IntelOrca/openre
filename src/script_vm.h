#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct lua_State;

namespace openre::script
{
    enum class HookKind
    {
        tick = 1,
    };

    // A single Lua execution environment for one script file.
    // All Lua execution happens on the game thread.
    class LuaVm
    {
    public:
        LuaVm(std::string name, std::vector<std::filesystem::path> searchPaths);
        ~LuaVm();

        LuaVm(const LuaVm&) = delete;
        LuaVm& operator=(const LuaVm&) = delete;

        const std::string& name() const
        {
            return _name;
        }
        lua_State* state() const
        {
            return _state;
        }

        // Compile and execute the script file. Errors are logged at warning level.
        void run(const std::filesystem::path& path);

        // Invoke all hooks subscribed for `kind`, each wrapped in pcall.
        void callHooks(HookKind kind);

        // Close the Lua state, running all __gc finalizers (disposes sockets etc).
        void dispose();

        // Store a Lua registry reference for a subscribed hook (re.subscribe).
        void subscribe(HookKind kind, int registryRef);

    private:
        struct HookInfo
        {
            HookKind kind;
            int registryRef;
        };

        std::string _name;
        std::vector<std::filesystem::path> _searchPaths;
        lua_State* _state{};
        std::vector<HookInfo> _subscriptions;
    };

    std::unique_ptr<LuaVm> createLuaVm(std::string name, std::vector<std::filesystem::path> searchPaths);
}
