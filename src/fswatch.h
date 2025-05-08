#pragma once

#include <filesystem>
#include <functional>
#include <string_view>
#include <thread>

namespace openre::filesystem
{
    /**
     * Creates a new thread that watches a directory tree for file modifications.
     */
    class FileWatcher
    {
    private:
        std::thread _watchThread;
        std::filesystem::path _path;
        void* _directoryHandle{};
        bool _finished{};

    public:
        std::function<void(std::filesystem::path fileName)> onFileChanged;

        FileWatcher(std::filesystem::path directory);
        ~FileWatcher();

    private:
        void watchDirectory();
    };
}
