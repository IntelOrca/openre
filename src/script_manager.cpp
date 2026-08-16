#include "script_manager.h"

#include "logger.h"

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

    void ScriptManager::unloadScript(const std::string& basename)
    {
        _scripts.erase(basename);
    }
}
