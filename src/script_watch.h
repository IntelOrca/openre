#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

namespace openre::script
{
    enum class FileEvent
    {
        created,
        modified,
        deleted,
    };

    // Watches a directory tree for changes on a background thread by polling
    // the filesystem (cross-platform, no OS-specific APIs).
    class FileWatcher
    {
    public:
        // Invoked on the watcher thread for each file event.
        std::function<void(const std::filesystem::path& path, FileEvent event)> onFileChanged;

        explicit FileWatcher(std::filesystem::path directory);
        ~FileWatcher();

        FileWatcher(const FileWatcher&) = delete;
        FileWatcher& operator=(const FileWatcher&) = delete;

    private:
        void watchDirectory();

        std::filesystem::path _path;
        std::thread _thread;
        std::atomic<bool> _finished{};
    };
}
