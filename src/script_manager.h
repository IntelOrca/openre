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

    private:
        ScriptManager() = default;

        void loadScripts();
        void watch();
        void onFileEvent(const std::filesystem::path& path, FileEvent event);
        void processPendingEvents();
        void loadScript(const std::filesystem::path& path);
        void unloadScript(const std::string& basename);

        bool _loaded{};
        std::vector<std::filesystem::path> _searchPaths;
        std::vector<std::unique_ptr<FileWatcher>> _watchers;
        // Key: script basename without extension.
        std::unordered_map<std::string, std::unique_ptr<LuaVm>> _scripts;
        std::mutex _eventMutex;
        std::vector<std::pair<std::filesystem::path, FileEvent>> _pendingEvents;
    };
}
