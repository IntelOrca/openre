#include "script_watch.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <thread>
#include <utility>

namespace fs = std::filesystem;

namespace openre::script
{
    namespace
    {
        // Snapshot of a single regular file, used to detect modifications.
        struct FileInfo
        {
            std::uintmax_t size = 0;
            fs::file_time_type lastWriteTime;

            bool operator==(const FileInfo& other) const
            {
                return size == other.size && lastWriteTime == other.lastWriteTime;
            }

            bool operator!=(const FileInfo& other) const
            {
                return !(*this == other);
            }
        };

        using Snapshot = std::map<fs::path, FileInfo>;

        // Recursively builds a snapshot of the directory tree keyed by the
        // path relative to the watched root. Errors (permission denied, files
        // removed mid-scan) are swallowed so the watch keeps running.
        Snapshot scanTree(const fs::path& root)
        {
            Snapshot snapshot;
            std::error_code ec;
            fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
            fs::recursive_directory_iterator end;
            for (; it != end && !ec; it.increment(ec))
            {
                std::error_code entryEc;
                if (!it->is_regular_file(entryEc))
                {
                    continue;
                }

                std::error_code timeEc;
                snapshot[it->path().lexically_relative(root)] = FileInfo{
                    it->file_size(entryEc),
                    it->last_write_time(timeEc),
                };
            }
            return snapshot;
        }
    }

    FileWatcher::FileWatcher(fs::path directory)
        : _path(std::move(directory))
    {
        if (!fs::is_directory(_path))
        {
            throw std::runtime_error("Unable to open directory '" + _path.u8string() + "'");
        }

        _thread = std::thread(&FileWatcher::watchDirectory, this);
    }

    FileWatcher::~FileWatcher()
    {
        _finished = true;
        if (_thread.joinable())
        {
            _thread.join();
        }
    }

    void FileWatcher::watchDirectory()
    {
        constexpr auto kPollInterval = std::chrono::milliseconds(500);

        Snapshot previous = scanTree(_path);

        while (!_finished)
        {
            std::this_thread::sleep_for(kPollInterval);
            if (_finished)
            {
                break;
            }

            Snapshot current = scanTree(_path);

            auto prevIt = previous.begin();
            auto curIt = current.begin();
            while (prevIt != previous.end() || curIt != current.end())
            {
                if (curIt == current.end() || (prevIt != previous.end() && prevIt->first < curIt->first))
                {
                    // Present before, gone now -> deleted.
                    if (onFileChanged)
                    {
                        onFileChanged(_path / prevIt->first, FileEvent::deleted);
                    }
                    ++prevIt;
                }
                else if (prevIt == previous.end() || (curIt != current.end() && curIt->first < prevIt->first))
                {
                    // New entry -> created.
                    if (onFileChanged)
                    {
                        onFileChanged(_path / curIt->first, FileEvent::created);
                    }
                    ++curIt;
                }
                else
                {
                    // Same path in both; size or mtime changed -> modified.
                    if (prevIt->second != curIt->second && onFileChanged)
                    {
                        onFileChanged(_path / curIt->first, FileEvent::modified);
                    }
                    ++prevIt;
                    ++curIt;
                }
            }

            previous.swap(current);
        }
    }
}
