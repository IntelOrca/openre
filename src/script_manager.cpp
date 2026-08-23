#include "script_manager.h"

#include "ecs_scene.h"
#include "logger.h"
#include "openre.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <utility>

namespace fs = std::filesystem;

namespace openre::script
{
    namespace
    {
        // Reserved script key that the loaded game module is registered under
        // (namespaces are not valid basenames, so this cannot collide with
        // OPENRE_SCRIPTS files).
        constexpr const char* GAME_MODULE_KEY = "module.game";

        bool isLuaFile(const fs::path& path)
        {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return extension == ".lua";
        }
    }

    ScriptManager& ScriptManager::get()
    {
        static std::unique_ptr<ScriptManager> instance(new ScriptManager());
        return *instance;
    }

    void ScriptManager::tick()
    {
        if (!_loaded)
        {
            _loaded = true;
            loadScripts();
            watch();
        }

        processPendingEvents();

        for (auto& entry : _scripts)
        {
            if (entry.second)
            {
                entry.second->callHooks(HookKind::tick);
            }
        }

        // A game-module change schedules a reload that mutates _scripts, so do it
        // after the tick hooks have run to avoid invalidating the iterator above.
        if (_pendingGameModuleReload)
        {
            _pendingGameModuleReload = false;
            reloadGameModule();
        }

        // OPENRE_LUA_GC forces a full collect on every frame so lifetime bugs
        // (e.g. textures held only by a fleeting Lua reference) surface
        // immediately instead of waiting for the natural GC cadence.
        static const bool forceGc = [] {
            const char* e = std::getenv("OPENRE_LUA_GC");
            return e != nullptr && (e[0] == '1' || e[0] == 't' || e[0] == 'T' || e[0] == 'y' || e[0] == 'Y');
        }();
        if (forceGc)
        {
            for (auto& entry : _scripts)
            {
                if (entry.second)
                    entry.second->collectGarbage();
            }
        }
    }

    void ScriptManager::pumpScript(const std::string& name)
    {
        auto it = _scripts.find(name);
        if (it != _scripts.end() && it->second)
            it->second->callHooks(HookKind::tick);
    }

    bool ScriptManager::evalIn(const std::string& name, const std::string& code, std::string& outResult)
    {
        auto it = _scripts.find(name);
        if (it == _scripts.end() || !it->second)
        {
            outResult = "no script VM registered under '" + name + "'";
            return false;
        }
        return it->second->runString(code, outResult);
    }

    void ScriptManager::loadScripts()
    {
        const char* env = std::getenv("OPENRE_SCRIPTS");
        if (env == nullptr || env[0] == '\0')
        {
            return;
        }

        // Collect the configured directories first so that _searchPaths keeps the
        // same left-to-right order as the OPENRE_SCRIPTS entries.
        std::vector<fs::path> directories;
        const std::string value(env);
        size_t start = 0;
        while (start <= value.size())
        {
            const size_t separator = value.find(';', start);
            const size_t count = separator == std::string::npos ? std::string::npos : separator - start;
            const std::string entry = value.substr(start, count);
            start = separator == std::string::npos ? value.size() + 1 : separator + 1;

            if (entry.empty())
            {
                continue;
            }

            const fs::path dir(entry);
            if (!dir.is_absolute())
            {
                logging::logWarning("[script] OPENRE_SCRIPTS entry is not absolute, ignoring: {}", entry);
                continue;
            }

            std::error_code ec;
            if (!fs::is_directory(dir, ec))
            {
                logging::logWarning("[script] OPENRE_SCRIPTS entry is not a directory, ignoring: {}", entry);
                continue;
            }

            directories.push_back(dir.lexically_normal());
        }

        _searchPaths = directories;

        for (const auto& dir : directories)
        {
            std::vector<fs::path> files;
            std::error_code ec;
            for (fs::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec))
            {
                std::error_code entryEc;
                if (it->is_regular_file(entryEc) && isLuaFile(it->path()))
                {
                    files.push_back(it->path());
                }
            }

            std::sort(files.begin(), files.end());

            for (const auto& file : files)
            {
                const std::string basename = file.stem().string();
                if (_scripts.find(basename) != _scripts.end())
                {
                    logging::logDebug(
                        "[script] Skipping duplicate script '{}' (already loaded from an earlier directory)", basename);
                    continue;
                }
                loadScript(file);
            }
        }
    }

    void ScriptManager::watch()
    {
        for (const auto& dir : _searchPaths)
        {
            auto watcher = std::make_unique<FileWatcher>(dir);
            watcher->onFileChanged = [this](const fs::path& path, FileEvent event) { onFileEvent(path, event); };
            _watchers.push_back(std::move(watcher));
        }
    }

    void ScriptManager::onFileEvent(const fs::path& path, FileEvent event)
    {
        std::lock_guard<std::mutex> lock(_eventMutex);
        _pendingEvents.emplace_back(path, event);
    }

    void ScriptManager::processPendingEvents()
    {
        std::vector<std::pair<fs::path, FileEvent>> events;
        {
            std::lock_guard<std::mutex> lock(_eventMutex);
            _pendingEvents.swap(events);
        }

        for (const auto& item : events)
        {
            const fs::path& path = item.first;
            const FileEvent event = item.second;

            if (!isLuaFile(path))
            {
                continue;
            }

            // Only react to files directly inside a script directory; files in
            // subdirectories are require()'d modules and are not reloaded.
            const fs::path parent = path.parent_path().lexically_normal();
            if (std::find(_searchPaths.begin(), _searchPaths.end(), parent) == _searchPaths.end())
            {
                continue;
            }

            const std::string basename = path.stem().string();
            const bool loaded = _scripts.find(basename) != _scripts.end();

            switch (event)
            {
            case FileEvent::created:
                if (!loaded)
                {
                    loadScript(path);
                }
                break;
            case FileEvent::modified:
                if (loaded)
                {
                    unloadScript(basename);
                    loadScript(path);
                }
                break;
            case FileEvent::deleted:
                if (loaded)
                {
                    unloadScript(basename);
                }
                break;
            }
        }
    }

    void ScriptManager::loadScript(const fs::path& path)
    {
        const std::string basename = path.stem().string();
        auto vm = createLuaVm(basename, _searchPaths);
        vm->run(path);
        _scripts[basename] = std::move(vm);
    }

    void ScriptManager::loadGameModule(const fs::path& dir)
    {
        if (_gameModuleLoaded)
            return;
        _gameModuleLoaded = true;
        _gameModuleDir = dir.lexically_normal();

        // The game module has no top-level-script hot reload of its own, but its
        // scripts are edited constantly while iterating on the UI. Watch its
        // directory: any change schedules a full module reload (deferred to the
        // end of tick()). The watcher lives here rather than watch() because the
        // module directory is only known once the game is chosen, and only once
        // so a reload does not stack duplicate watchers.
        if (!_gameModuleWatched)
        {
            _gameModuleWatched = true;
            auto watcher = std::make_unique<FileWatcher>(_gameModuleDir);
            watcher->onFileChanged = [this](const fs::path&, FileEvent) { _pendingGameModuleReload = true; };
            _watchers.push_back(std::move(watcher));
        }

        auto vm = createLuaVm("game", { dir });
        vm->run(dir / "main.lua");
        _scripts[GAME_MODULE_KEY] = std::move(vm);
    }

    void ScriptManager::reloadGameModule()
    {
        logging::logInfo("[script] hot-reloading game module '{}'", _gameModuleDir.string());

        // Tear down the current module's scene and VM. Scene::clear() removes
        // every entity except the hidden root, so the next main.lua starts from a
        // clean slate. The shared cross-VM table (ScriptShared) lives outside the
        // VM, so any state stashed there (e.g. an open options overlay) survives.
        if (auto* scene = openre::current_scene())
        {
            scene->clear();
        }
        _scripts.erase(GAME_MODULE_KEY);
        _gameModuleLoaded = false;

        // Skip the splash/movie on reload so iterating on a single script is fast.
        _putenv("OPENRE_FAST_RELOAD=1");

        loadGameModule(_gameModuleDir);
    }

    void ScriptManager::unloadScript(const std::string& basename)
    {
        _scripts.erase(basename);
    }
}
