#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct lua_State;

namespace openre::script
{
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

        // Compile and execute a Lua chunk in this VM. On success returns true
        // with the single returned value (converted to a string when it is not
        // already one, so callers can pass it straight back to Lua) in
        // `outResult`; on failure returns false with the error message in
        // `outResult`. Never raises into the caller.
        bool runString(const std::string& code, std::string& outResult);

        // Force a full garbage collection cycle in this VM.
        void collectGarbage();

        // Close the Lua state, running all __gc finalizers (disposes sockets etc).
        void dispose();

    private:
        std::string _name;
        std::vector<std::filesystem::path> _searchPaths;
        lua_State* _state{};
    };

    std::unique_ptr<LuaVm> createLuaVm(std::string name, std::vector<std::filesystem::path> searchPaths);

    // Whether `vm` is currently alive (constructed, not yet destroyed). Used
    // by engine hooks that captured a LuaVm pointer (e.g. input handler
    // callbacks) to avoid dispatching into a destroyed VM.
    bool isVmAlive(const LuaVm* vm);
}
