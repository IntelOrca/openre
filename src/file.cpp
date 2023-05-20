#define USE_ORIGINAL_FILEIO

#include "file.h"
#include "openre.h"
#include <windows.h>

namespace openre::file
{
    // 0x00508DC0
    void file_error()
    {
        using sig = void (*)();
        auto p = (sig)0x00508DC0;
        p();
    }

    // 0x00509020
    static HANDLE sub_509020(const char* path, int a1)
    {
        using sig = HANDLE(*)(const char*, int);
        auto p = (sig)0x00509020;
        return p(path, a1);
    }

    // 0x00502D40
    size_t read_file_into_buffer(const char* path, void* buffer, size_t length)
    {
#ifdef USE_ORIGINAL_FILEIO
        using sig = uint32_t(*)(const char*, void*, size_t);
        auto p = (sig)0x00502D40;
        return p(path, buffer, length);
#else
        size_t result = 0;
        auto hFile = sub_509020(path, length);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            auto fileSize = GetFileSize(hFile, NULL);
            DWORD bytesRead = 0;
            if (ReadFile(hFile, buffer, fileSize, &bytesRead, nullptr) && bytesRead == fileSize)
            {
                result = bytesRead;
            }
            CloseHandle(hFile);
        }
        if (result == 0)
        {
            gErrorCode = 11;
    }
        return result;
#endif
}

    // 0x00509540
    uint32_t read_partial_file_into_buffer(const char* path, void* buffer, size_t offset, size_t length, size_t unk)
    {
        using sig = uint32_t(*)(const char*, void*, size_t, size_t, size_t);
        auto p = (sig)0x00509540;
        return p(path, buffer, offset, length, unk);
    }
}
