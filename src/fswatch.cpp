#include "fswatch.h"

#include <algorithm>
#include <array>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace openre
{
    class NotImplementedException : public std::exception
    {
    private:
        std::string msg;

    public:
        NotImplementedException() {}

        NotImplementedException(std::string_view message)
            : msg(message)
        {
        }

        virtual const char* what() const noexcept override
        {
            return msg.c_str();
        }
    };
}

namespace openre::filesystem
{
    FileWatcher::FileWatcher(fs::path directory)
        : _path(directory)
    {
#ifdef _WIN32
        _directoryHandle = CreateFileW(
            directory.native().c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr);
        if (_directoryHandle == INVALID_HANDLE_VALUE)
        {
            throw std::runtime_error("Unable to open directory '" + directory.u8string() + "'");
        }
#else
        throw NotImplementedException();
#endif
        _watchThread = std::thread(&FileWatcher::watchDirectory, this);
    }

    FileWatcher::~FileWatcher()
    {
#ifdef _WIN32
        CancelIoEx(_directoryHandle, nullptr);
        _watchThread.join();
        CloseHandle(_directoryHandle);
#else
        throw NotImplementedException();
#endif
    }

    void FileWatcher::watchDirectory()
    {
#if defined(_WIN32)
        std::array<char, 1024> eventData;
        DWORD bytesReturned;
        while (ReadDirectoryChangesW(
            _directoryHandle,
            eventData.data(),
            static_cast<DWORD>(eventData.size()),
            TRUE,
            FILE_NOTIFY_CHANGE_LAST_WRITE,
            &bytesReturned,
            nullptr,
            nullptr))
        {
            if (bytesReturned != 0 && onFileChanged)
            {
                FILE_NOTIFY_INFORMATION* notifyInfo;
                size_t offset = 0;
                do
                {
                    notifyInfo = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(eventData.data() + offset);
                    offset += notifyInfo->NextEntryOffset;

                    std::wstring_view fileNameW(notifyInfo->FileName, notifyInfo->FileNameLength / sizeof(wchar_t));
                    auto path = _path / fs::path(fileNameW);
                    onFileChanged(path);
                } while (notifyInfo->NextEntryOffset != 0);
            }
        }
#else
        throw NotImplementedException();
#endif
    }
}
