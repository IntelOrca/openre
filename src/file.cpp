#define USE_ORIGINAL_FILEIO

#include "file.h"
#include "interop.hpp"
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
    static HANDLE file_open_handle(const char* path, int a1)
    {
        using sig = HANDLE (*)(const char*, int);
        auto p = (sig)0x00509020;
        return p(path, a1);
    }

    // 0x00502D40
    size_t read_file_into_buffer(const char* path, void* buffer, size_t length)
    {
#ifdef USE_ORIGINAL_FILEIO
        using sig = uint32_t (*)(const char*, void*, size_t);
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
    uint32_t read_partial_file_into_buffer(const char* path, void* buffer, size_t offset, size_t length, size_t mode)
    {
        using sig = uint32_t (*)(const char*, void*, size_t, size_t, size_t);
        auto p = (sig)0x00509540;
        return p(path, buffer, offset, length, mode);
    }

    // 0x004DD360
    static int osp_read()
    {
        const char* ospFilepath = "common\\bin\\osp.bin";

        gGameTable.osp_mask_flag = 1;
        const size_t length = 4224;
        const size_t offset = length * (gGameTable.current_room + 32 * gGameTable.current_stage);

        auto bytesRead = read_partial_file_into_buffer(ospFilepath, gGameTable.psp_lookup, offset, length, 4);
        if (bytesRead == 0)
        {
            gErrorCode = 0;
            gGameTable.osp_mask_flag = 0;
        }
        return bytesRead;
    }

    // 0x00509780
    static int file_read_save(void* buffer, const char* filename, size_t size)
    {
        auto file = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
        if (file == INVALID_HANDLE_VALUE)
        {
            return 2;
        }

        DWORD bytesRead;
        if (!ReadFile(file, buffer, size, &bytesRead, 0) || bytesRead != size)
        {
            return 2;
        }
        CloseHandle(file);
        return 0;
    }

    // 0x005097E0
    static size_t file_write_save(const char* filename, void* buffer, size_t size)
    {
        auto file = CreateFileA(filename, GENERIC_WRITE, FILE_SHARE_WRITE, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (file == INVALID_HANDLE_VALUE)
        {
            return 0;
        }

        DWORD bytesWritten;
        if (!WriteFile(file, buffer, size, &bytesWritten, 0))
        {
            return 0;
        }
        update_timer();
        CloseHandle(file);
        return bytesWritten;
    }

    void file_init_hooks()
    {
        interop::writeJmp(0x004DD360, &osp_read);
        interop::writeJmp(0x00509780, &file_read_save);
        interop::writeJmp(0x005097E0, &file_write_save);
    }
}
