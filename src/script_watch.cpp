#include "script_watch.h"

#include <array>
#include <stdexcept>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace openre::script
{
    FileWatcher::FileWatcher(fs::path directory)
        : _path(std::move(directory))
    {
#ifdef _WIN32
        _directoryHandle = CreateFileW(
            _path.native().c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr);
        if (_directoryHandle == INVALID_HANDLE_VALUE)
        {
            throw std::runtime_error("Unable to open directory '" + _path.u8string() + "'");
        }

        _thread = std::thread(&FileWatcher::watchDirectory, this);
#else
        throw std::runtime_error("FileWatcher is only supported on Windows");
#endif
    }

    FileWatcher::~FileWatcher()
    {
#ifdef _WIN32
        _finished = true;
        if (_directoryHandle != nullptr && _directoryHandle != INVALID_HANDLE_VALUE)
        {
            CancelIoEx(_directoryHandle, nullptr);
        }
        if (_thread.joinable())
        {
            _thread.join();
        }
        if (_directoryHandle != nullptr && _directoryHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(_directoryHandle);
        }
#endif
    }

    void FileWatcher::watchDirectory()
    {
#ifdef _WIN32
        std::array<char, 1024> buffer;
        DWORD bytesReturned = 0;
        while (!_finished
               && ReadDirectoryChangesW(
                   _directoryHandle,
                   buffer.data(),
                   static_cast<DWORD>(buffer.size()),
                   TRUE,
                   FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
                   &bytesReturned,
                   nullptr,
                   nullptr))
        {
            if (bytesReturned == 0 || !onFileChanged)
            {
                continue;
            }

            size_t offset = 0;
            FILE_NOTIFY_INFORMATION* info = nullptr;
            do
            {
                info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
                offset += info->NextEntryOffset;

                std::wstring_view fileName(info->FileName, info->FileNameLength / sizeof(wchar_t));

                FileEvent event;
                switch (info->Action)
                {
                case FILE_ACTION_ADDED: event = FileEvent::created; break;
                case FILE_ACTION_REMOVED: event = FileEvent::deleted; break;
                case FILE_ACTION_MODIFIED: event = FileEvent::modified; break;
                case FILE_ACTION_RENAMED_OLD_NAME: event = FileEvent::deleted; break;
                case FILE_ACTION_RENAMED_NEW_NAME: event = FileEvent::created; break;
                default: continue;
                }

                onFileChanged(_path / fs::path(fileName), event);
            } while (info->NextEntryOffset != 0);
        }
#endif
    }
}
