// #define USE_ORIGINAL_FILEIO

#include "file.h"
#include "gfx.h"
#include "interop.hpp"
#include "openre.h"
#include <windows.h>
#include <string>
#include <cstdlib>

namespace openre::file
{
    enum
    {
        READ_SAVE_FILE_SUCCESS,
        READ_SAVE_FILE_ERROR = 2,
    };

    // 0x00508DC0
    void file_error()
    {
        using sig = void (*)();
        auto p = (sig)0x00508DC0;
        p();
    }

    // --- Helpers to access OG std::string globals ---

    static const char* og_string_data(uint32_t addr)
    {
        return interop::thiscall<const char*, void*>(0x50C3F0, (void*)(uintptr_t)addr);
    }

    static void og_string_assign(uint32_t addr, const char* str)
    {
        interop::thiscall<void, void*, const char*>(0x50C420, (void*)(uintptr_t)addr, str);
    }

    // 0x005092A0
    static bool check_disk_id()
    {
        return interop::call<bool>(0x5092A0);
    }

    // 0x00508F30
    static bool dlg_disk_retry()
    {
        // Shows CD swap dialog. Returns true if user wants to retry, false if cancelled.
        return interop::call<INT_PTR, LPCSTR>(0x508F30, (LPCSTR)0xA4) != 1019;
    }

    // --- Internal: matches OpenFile at 0x509040 ---
    // Tries all mode/sub-approach combos, retries with disk checks, and
    // shows the CD swap dialog when silent==0.
    static HANDLE open_file_impl(const char* path, int mode, bool silent)
    {
        // If OPENRE_RE2_DATA is set, bypass OG path resolution entirely
        {
            const char* re2Data = std::getenv("OPENRE_RE2_DATA");
            if (re2Data && re2Data[0]) {
                std::string fullPath = std::string(re2Data) + "\\" + path;
                og_string_assign(0x689F3C, fullPath.c_str());
                UINT oldMode = SetErrorMode(0x8001);
                HANDLE hFile = CreateFileA(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
                SetErrorMode(oldMode);
                return hFile;
            }
        }

        HANDLE hFile = INVALID_HANDLE_VALUE;
        for (;;) {
            // Mode loop: 0=Class prefix, 1=Module directory, 2=CD path
            for (int currentMode = 0; currentMode < 3 && hFile == INVALID_HANDLE_VALUE; currentMode++) {
                std::string basePath;
                switch (currentMode) {
                case 0:
                    if (const char* s = og_string_data(0x669F4C))
                        basePath = s;
                    break;
                case 1: {
                    char buf[MAX_PATH];
                    if (GetModuleFileNameA(NULL, buf, MAX_PATH)) {
                        std::string ms(buf);
                        auto p = ms.find_last_of('\\');
                        if (p != std::string::npos)
                            basePath = ms.substr(0, p + 1);
                    }
                    break;
                }
                case 2:
                    if (const char* s = og_string_data(0x689F34))
                        basePath = s;
                    break;
                }
                // Sub-mode loop: 0=direct, 1=append "data\"
                for (int sub = 0; sub < 2 && hFile == INVALID_HANDLE_VALUE; sub++) {
                    std::string fullPath = (sub == 0) ? (basePath + path) : (basePath + "data\\" + path);
                    // Store resolved path in OG global dword_689F3C
                    og_string_assign(0x689F3C, fullPath.c_str());
                    // Suppress OS error dialogs (SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX)
                    UINT oldMode = SetErrorMode(0x8001);
                    hFile = CreateFileA(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
                    SetErrorMode(oldMode);
                }
            }
            // If all modes failed, check for a valid disc
            if (hFile == INVALID_HANDLE_VALUE) {
                if (check_disk_id())
                    continue; // disc found — retry all modes
            }
            // In silent mode with a known CD path, exit without dialog
            if (silent) {
                if (const char* s = og_string_data(0x689F34)) {
                    if (s[0])
                        break;
                }
            }
            if (hFile != INVALID_HANDLE_VALUE)
                break;
            // Show CD swap dialog
            if (!dlg_disk_retry())
                break;
            // Otherwise retry all modes
        }
        return hFile;
    }

    // 0x00509020
    HANDLE file_open_handle(const char* path, int mode)
    {
        return open_file_impl(path, mode, false);
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
        auto hFile = file_open_handle(path, length);
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
            gGameTable.error_no = 11;
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

    // 0x00505B20
    void sub_505B20()
    {
        interop::call(0x00505B20);
    }

    // 0x004DD360
    int osp_read()
    {
        const char* ospFilepath = "common\\bin\\osp.bin";

        gGameTable.osp_mask_flag = 1;
        const size_t length = 4224;
        const size_t offset = length * (gGameTable.current_room + 32 * gGameTable.current_stage);

        auto bytesRead = read_partial_file_into_buffer(ospFilepath, gGameTable.psp_lookup, offset, length, 4);
        if (bytesRead == 0)
        {
            gGameTable.error_no = 0;
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
            return READ_SAVE_FILE_ERROR;
        }

        DWORD bytesRead;
        int result = READ_SAVE_FILE_ERROR;
        if (ReadFile(file, buffer, size, &bytesRead, 0) && bytesRead == size)
        {
            result = READ_SAVE_FILE_SUCCESS;
        }
        CloseHandle(file);
        return result;
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
        size_t result = 0;
        if (WriteFile(file, buffer, size, &bytesWritten, 0))
        {
            update_timer();
            result = bytesWritten;
        }
        CloseHandle(file);
        return result;
    }

    // 0x005095D0
    static int file_exists(const char* path, int mode)
    {
        auto hFile = open_file_impl(path, mode, true);
        if (hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
            return 1;
        }
        return 0;
    }

    // 0x00441630
    void* file_alloc(const size_t size)
    {
        if (gGameTable.file_buffer)
        {
            operator_delete(gGameTable.file_buffer);
            gGameTable.file_buffer = 0;
        }
        if (!size)
        {
            return 0;
        }

        auto memoryBlock = operator_new(size);
        gGameTable.file_buffer = memoryBlock;
        return memoryBlock;
    }

    // 0x00509620
    static void adt_store_last_filename(const char* filename)
    {
        using sig = void (*)(const char*);
        auto p = (sig)0x00509620;
        p(filename);
    }

    // 0x0043C590
    int load_adt(const char* path, void* dst, int mode)
    {
        auto hFile = file_open_handle(path, mode);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            adt_store_last_filename(path);
            gGameTable.error_no = 11;
            return 0;
        }

        auto fileSize = GetFileSize(hFile, NULL);
        if (fileSize == INVALID_FILE_SIZE || fileSize == 0)
        {
            CloseHandle(hFile);
            adt_store_last_filename(path);
            gGameTable.error_no = 11;
            return 0;
        }

        std::vector<uint8_t> compressed(fileSize);
        DWORD bytesRead;
        if (!ReadFile(hFile, compressed.data(), fileSize, &bytesRead, NULL) || bytesRead != fileSize)
        {
            CloseHandle(hFile);
            adt_store_last_filename(path);
            gGameTable.error_no = 11;
            return 0;
        }
        CloseHandle(hFile);

        auto decompressed = openre::graphics::decodeAdt(compressed);
        if (decompressed.empty())
        {
            adt_store_last_filename(path);
            gGameTable.error_no = 11;
            return 0;
        }

        std::memcpy(dst, decompressed.data(), decompressed.size());
        adt_store_last_filename(path);
        return static_cast<int>(decompressed.size());
    }

    // 0x0043FF40
    int tim_buffer_to_surface(int* timPtr, int page, int mode)
    {
        return interop::call<int, int*, int, int>(0x0043FF40, timPtr, page, mode);
    }

    void file_init_hooks()
    {
        interop::writeJmp(0x004DD360, &osp_read);
        interop::writeJmp(0x00509020, &file_open_handle);
        interop::writeJmp(0x005095D0, &file_exists);
        interop::writeJmp(0x00509780, &file_read_save);
        interop::writeJmp(0x005097E0, &file_write_save);
    }
}
