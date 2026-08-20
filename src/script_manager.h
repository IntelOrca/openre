#pragma once

#include "script_vm.h"
#include "script_watch.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace openre::script
{
    // Loads and hot-reloads scripts from the OPENRE_SCRIPTS directories.
    // All Lua execution happens on the game thread via tick(); file events are
    // queued from the watcher threads and drained inside tick().
    class ScriptManager
    {
    public:
        static ScriptManager& get();

        ScriptManager(const ScriptManager&) = delete;
        ScriptManager& operator=(const ScriptManager&) = delete;

        // Called once per frame from the game loop.
        void tick();

        // Runs the tick hooks of a single named script. Used to keep specific
        // scripts (e.g. the web inspector) alive while the main loop is blocked
        // inside a long-running hook such as a blocking movie playback.
        void pumpScript(const std::string& name);

        // Loads a game module directory (games/<name>) - runs main.lua in a
        // dedicated Lua VM registered so tick() drives it. No-op if already
        // loaded.
        void loadGameModule(const std::filesystem::path& dir);

    private:
        ScriptManager() = default;

        void loadScripts();
        void watch();
        void onFileEvent(const std::filesystem::path& path, FileEvent event);
        void processPendingEvents();
        void loadScript(const std::filesystem::path& path);
        void unloadScript(const std::string& basename);

        bool _loaded{};
        bool _gameModuleLoaded{};
        std::vector<std::filesystem::path> _searchPaths;
        std::vector<std::unique_ptr<FileWatcher>> _watchers;
        // Key: script basename without extension.
        std::unordered_map<std::string, std::unique_ptr<LuaVm>> _scripts;
        std::mutex _eventMutex;
        std::vector<std::pair<std::filesystem::path, FileEvent>> _pendingEvents;
    };
}
