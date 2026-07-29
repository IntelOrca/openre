#include "file.h"
#include "gfx.h"
#include "interop.hpp"
#include "logger.h"
#include "openre.h"
#include "stream.h"
#include <cstdlib>
#include <string>
#include <windows.h>

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
        for (auto& task : gGameTable.tasks)
        {
            task.sleep = 0;
            task.var_13 = 1;
        }
        if (gGameTable.error_no == 0)
            gGameTable.error_no = 12;
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

    // --- File I/O wrapper with error dialog suppression and logging ---

    static HANDLE
    file_open_internal(const char* path, DWORD access, DWORD shareMode, DWORD creationDisposition, bool suppressErrors)
    {
        UINT oldMode = 0;
        if (suppressErrors)
            oldMode = SetErrorMode(0x8001);
        HANDLE hFile = CreateFileA(path, access, shareMode, NULL, creationDisposition, 0, NULL);
        if (suppressErrors)
            SetErrorMode(oldMode);

        const char* type = (access & GENERIC_WRITE) ? "WRITE" : "READ";
        if (hFile != INVALID_HANDLE_VALUE)
            logging::logInfo("[{} SUCCESS] {}", type, path);
        else
            logging::logWarning("[{} FAIL] {}", type, path);

        return hFile;
    }

    // 0x005092A0
    static bool check_disk_id()
    {
        // We are not implementing this function
        return false;
    }

    // 0x00508F30
    static bool dlg_disk_retry()
    {
        // We are not implementing this function
        return false;
    }

    // --- Internal: matches OpenFile at 0x509040 ---
    // Tries all mode/sub-approach combos, retries with disk checks, and
    // shows the CD swap dialog when silent==0.
    static HANDLE open_file_impl(const char* path, int mode, bool silent)
    {
        // If OPENRE_RE2_DATA is set, bypass OG path resolution entirely
        {
            const char* re2Data = std::getenv("OPENRE_RE2_DATA");
            if (re2Data && re2Data[0])
            {
                std::string fullPath = std::string(re2Data) + "\\" + path;
                oldstring_set_2(&gGameTable.ss_file_string, fullPath.c_str());
                return file_open_internal(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, true);
            }
        }

        HANDLE hFile = INVALID_HANDLE_VALUE;
        for (;;)
        {
            // Mode loop: 0=Class prefix, 1=Module directory, 2=CD path
            for (int currentMode = 0; currentMode < 3 && hFile == INVALID_HANDLE_VALUE; currentMode++)
            {
                std::string basePath;
                switch (currentMode)
                {
                case 0:
                    if (const char* s = og_string_data(0x669F4C))
                        basePath = s;
                    break;
                case 1:
                {
                    char buf[MAX_PATH];
                    if (GetModuleFileNameA(NULL, buf, MAX_PATH))
                    {
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
                for (int sub = 0; sub < 2 && hFile == INVALID_HANDLE_VALUE; sub++)
                {
                    std::string fullPath = (sub == 0) ? (basePath + path) : (basePath + "data\\" + path);
                    // Store resolved path in OG global ss_file_string
                    oldstring_set_2(&gGameTable.ss_file_string, fullPath.c_str());
                    hFile = file_open_internal(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, true);
                }
            }
            // If all modes failed, check for a valid disc
            if (hFile == INVALID_HANDLE_VALUE)
            {
                if (check_disk_id())
                    continue; // disc found — retry all modes
            }
            // In silent mode with a known CD path, exit without dialog
            if (silent)
            {
                if (const char* s = og_string_data(0x689F34))
                {
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
    }

    // 0x005094B0
    static DWORD bufferize_file_0(LPVOID filename, LPVOID buffer, DWORD type)
    {
        auto hFile = file_open_handle((const char*)filename, (int)type);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            gGameTable.error_no = 11;
            return 0;
        }

        DWORD fileSize = GetFileSize(hFile, NULL);
        DWORD bytesRead = 0;
        if (ReadFile(hFile, buffer, fileSize, &bytesRead, NULL) && fileSize == bytesRead)
        {
            update_timer();
            CloseHandle(hFile);
            return bytesRead;
        }
        else
        {
            gGameTable.error_no = 11;
            CloseHandle(hFile);
            return 0;
        }
    }

    // 0x00509540
    uint32_t read_partial_file_into_buffer(const char* path, void* buffer, size_t offset, size_t length, size_t mode)
    {
        auto hFile = file_open_handle(path, (int)mode);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            gGameTable.error_no = 11;
            return 0;
        }

        if (SetFilePointer(hFile, (LONG)offset, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER
            && ReadFile(hFile, buffer, (DWORD)length, (LPDWORD)&mode, NULL) && length == mode)
        {
            update_timer();
            uint32_t bytesRead = (uint32_t)mode;
            CloseHandle(hFile);
            return bytesRead;
        }
        else
        {
            gGameTable.error_no = 11;
            CloseHandle(hFile);
            return 0;
        }
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
        auto file = file_open_internal(filename, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, false);
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
        auto file = file_open_internal(filename, GENERIC_WRITE, FILE_SHARE_WRITE, CREATE_ALWAYS, false);
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

    // 0x00432600
    static bool remove_save(LPCSTR lpFileName)
    {
        return DeleteFileA(lpFileName);
    }

    // 0x005095D0
    int file_exists(const char* path, int mode)
    {
        auto hFile = open_file_impl(path, mode, true);
        if (hFile != INVALID_HANDLE_VALUE)
        {
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

    // 0x0043BBC0
    static unsigned int file_read_chunk()
    {
        if (--gGameTable.dword_671414 >= 0)
            return (gGameTable.dword_67140C >> gGameTable.dword_671414) & 1;

        gGameTable.dword_671414 = 7;

        if (gGameTable.dword_99DAC8)
        {
            gGameTable.dword_67140C = *(char*)(gGameTable.adt_in_pos + gGameTable.adt_in_base);
            gGameTable.adt_in_pos++;
            return (gGameTable.dword_67140C >> 7) & 1;
        }

        int pos = gGameTable.dword_99DAB8;
        if (pos >= (int)gGameTable.adt_bytes_read)
        {
            if (!ReadFile(
                    gGameTable.adt_file_handle, gGameTable.adt_buffer_in, 0x8000, (LPDWORD)&gGameTable.adt_bytes_read, NULL))
            {
                CloseHandle(gGameTable.adt_file_handle);
                gGameTable.adt_file_handle = 0;
                return 0;
            }
            pos = 0;
            gGameTable.dword_99DAB8 = 0;
        }

        gGameTable.dword_67140C = ((unsigned char*)gGameTable.adt_buffer_in)[pos];
        gGameTable.dword_99DAB8 = pos + 1;
        return (gGameTable.dword_67140C >> 7) & 1;
    }

    // 0x0043BC90
    static short file_read_chunk2(int num_bits)
    {
        int bits_remaining = gGameTable.dword_671414;
        unsigned int bit_buffer = gGameTable.dword_67140C;
        int result = 0;

        if (gGameTable.dword_99DAC8)
        {
            int remaining = num_bits;

            if (num_bits > bits_remaining)
            {
                int pos = gGameTable.adt_in_pos;

                do
                {
                    remaining -= bits_remaining;
                    result |= (bit_buffer & ((1 << bits_remaining) - 1)) << remaining;
                    bit_buffer = *(char*)(pos + gGameTable.adt_in_base);
                    bits_remaining = 8;
                    gGameTable.dword_67140C = bit_buffer;
                    gGameTable.dword_671414 = 8;
                    pos++;
                    gGameTable.adt_in_pos = pos;
                } while (remaining > 8);
            }

            gGameTable.dword_671414 = bits_remaining - remaining;
            return result | ((1 << remaining) - 1) & (bit_buffer >> (bits_remaining - remaining));
        }
        else
        {
            int remaining = num_bits;

            if (num_bits > bits_remaining)
            {
                int file_pos = gGameTable.dword_99DAB8;
                unsigned char* buf = (unsigned char*)gGameTable.adt_buffer_in;

                do
                {
                    remaining -= bits_remaining;
                    result |= (bit_buffer & ((1 << bits_remaining) - 1)) << remaining;

                    if (file_pos >= (int)gGameTable.adt_bytes_read)
                    {
                        if (!ReadFile(gGameTable.adt_file_handle, buf, 0x8000, (LPDWORD)&gGameTable.adt_bytes_read, NULL))
                        {
                            CloseHandle(gGameTable.adt_file_handle);
                            gGameTable.adt_file_handle = 0;
                            return 0;
                        }
                        buf = (unsigned char*)gGameTable.adt_buffer_in;
                        file_pos = 0;
                        gGameTable.dword_99DAB8 = 0;
                    }

                    bits_remaining = 8;
                    bit_buffer = buf[file_pos++];
                    gGameTable.dword_67140C = bit_buffer;
                    gGameTable.dword_99DAB8 = file_pos;
                    gGameTable.dword_671414 = 8;
                } while (remaining > 8);
            }

            gGameTable.dword_671414 = bits_remaining - remaining;
            return result | ((1 << remaining) - 1) & (bit_buffer >> (bits_remaining - remaining));
        }
    }

    // 0x00509620
    static void adt_store_last_filename(const char* filename)
    {
        og_string_assign((uint32_t)&gGameTable.ss_file_string, filename);
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

    // 0x00441670
    static void* alloc0(size_t size)
    {
        if (gGameTable.pAllocator0)
        {
            operator_delete(gGameTable.pAllocator0);
            gGameTable.pAllocator0 = nullptr;
        }
        if (!size)
            return nullptr;

        auto memoryBlock = operator_new(size);
        gGameTable.pAllocator0 = memoryBlock;
        return memoryBlock;
    }

    // 0x004416B0
    static void* alloc1(size_t size)
    {
        if (gGameTable.pAllocator1)
        {
            operator_delete(gGameTable.pAllocator1);
            gGameTable.pAllocator1 = nullptr;
        }
        if (!size)
            return nullptr;

        auto memoryBlock = operator_new(size);
        gGameTable.pAllocator1 = memoryBlock;
        return memoryBlock;
    }

    // 0x0043C390
    static void decompress_adt()
    {
        auto* src = (const uint8_t*)gGameTable.adt_in_base;
        std::vector<uint8_t> input(src, src + 0x8000);
        auto output = openre::graphics::decodeAdt(input);
        if (!output.empty())
        {
            auto* dst = gGameTable.adt_out_ptr;
            std::memcpy(dst, output.data(), output.size());
            gGameTable.adt_out_offset = (uint32_t)output.size();
        }
    }

    // 0x0043C890
    static int decompress_file_page(const uint8_t* in_data, int out_ptr)
    {
        gGameTable.dword_99DAC8 = 1;
        gGameTable.dword_671404 = 0;
        gGameTable.dword_671408 = 0;
        gGameTable.dword_67140C = 0;
        gGameTable.dword_671410 = 0;
        gGameTable.dword_671414 = 0;
        gGameTable.dword_524E08 = 8;
        gGameTable.dword_671418 = 0;
        gGameTable.dword_524E0C = 0x4000;
        gGameTable.adt_out_offset = 0;
        gGameTable.dword_99DAB8 = 0;
        gGameTable.dword_99DAB0 = 0;
        gGameTable.adt_out_ptr = (uint8_t*)(uintptr_t)out_ptr;
        gGameTable.adt_in_base = (uint32_t)in_data;
        gGameTable.adt_in_pos = 4;

        gGameTable.adt_buffer_in = alloc0(0x8000);
        gGameTable.adt_buffer_out = (uint32_t)alloc1(0x5D80);
        decompress_adt();
        alloc1(0);
        alloc0(0);
        update_timer();

        return gGameTable.adt_out_offset;
    }

    // 0x0043FF40
    int tim_buffer_to_surface(int* timPtr, int page, int mode)
    {
        return interop::call<int, int*, int, int>(0x0043FF40, timPtr, page, mode);
    }

    // 0x0043C0E0
    static void file_close()
    {
        // This shouldn't be called as we have implemented ADT reading
        abort();
    }

    class Win32FileStream : public Stream
    {
    private:
        HANDLE _hFile;

    public:
        Win32FileStream(HANDLE hFile)
            : _hFile(hFile)
        {
        }

        size_t read(void* buffer, size_t size) override
        {
            DWORD bytesRead = 0;
            if (!ReadFile(_hFile, buffer, static_cast<DWORD>(size), &bytesRead, NULL))
                return 0;
            return bytesRead;
        }

        size_t write(const void* buffer, size_t size) override
        {
            DWORD bytesWritten = 0;
            if (!WriteFile(_hFile, buffer, static_cast<DWORD>(size), &bytesWritten, NULL))
                return 0;
            return bytesWritten;
        }

        int64_t seek(int64_t offset, int origin) override
        {
            DWORD moveMethod;
            switch (origin)
            {
            case SEEK_CUR: moveMethod = FILE_CURRENT; break;
            case SEEK_END: moveMethod = FILE_END; break;
            default: moveMethod = FILE_BEGIN; break;
            }
            LARGE_INTEGER liOffset;
            liOffset.QuadPart = offset;
            liOffset.LowPart = SetFilePointer(_hFile, liOffset.LowPart, &liOffset.HighPart, moveMethod);
            if (liOffset.LowPart == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
                return -1;
            return liOffset.QuadPart;
        }

        int64_t tell() const override
        {
            LARGE_INTEGER liOffset;
            liOffset.QuadPart = 0;
            liOffset.LowPart = SetFilePointer(_hFile, 0, &liOffset.HighPart, FILE_CURRENT);
            return liOffset.QuadPart;
        }
    };

    // 0x0043C700
    static int load_adt_sub(const char* path, uint8_t* dst, int pos, int mode)
    {
        gGameTable.dword_99DAC8 = 0;

        if (gGameTable.error_no == 11)
            return 0;

        gGameTable.dword_671404 = 0;
        gGameTable.dword_671408 = 0;
        gGameTable.dword_67140C = 0;
        gGameTable.dword_671410 = 0;
        gGameTable.dword_671414 = 0;
        gGameTable.dword_524E08 = 8;
        gGameTable.dword_671418 = 0;
        gGameTable.dword_524E0C = 0x4000;
        gGameTable.adt_out_offset = 0;
        gGameTable.dword_99DAB8 = 0;
        gGameTable.dword_99DAB0 = 0;
        gGameTable.adt_out_ptr = dst;

        auto hFile = file_open_handle(path, mode);
        gGameTable.adt_file_handle = hFile;

        if (hFile == INVALID_HANDLE_VALUE)
        {
            adt_store_last_filename(path);
            gGameTable.error_no = 11;
            return 0;
        }

        if (SetFilePointer(hFile, pos, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
        {
            CloseHandle(hFile);
            adt_store_last_filename(path);
            gGameTable.error_no = 11;
            return 0;
        }

        {
            Win32FileStream stream(hFile);
            auto output = openre::graphics::decodeAdt(stream);
            if (!output.empty())
            {
                auto* dstPtr = gGameTable.adt_out_ptr;
                std::memcpy(dstPtr, output.data(), output.size());
                gGameTable.adt_out_offset = (uint32_t)output.size();
            }
        }

        if (!gGameTable.adt_file_handle)
        {
            adt_store_last_filename(path);
            gGameTable.error_no = 11;
            return 0;
        }

        CloseHandle(hFile);
        update_timer();

        return gGameTable.adt_out_offset;
    }

    void file_init_hooks()
    {
        interop::writeJmp(0x00432600, &remove_save);
        interop::writeJmp(0x0043BBC0, &file_read_chunk);
        interop::writeJmp(0x0043BC90, &file_read_chunk2);
        interop::writeJmp(0x0043C590, &load_adt);
        interop::writeJmp(0x0043C700, &load_adt_sub);
        interop::writeJmp(0x0043C890, &decompress_file_page);
        interop::writeJmp(0x004DD360, &osp_read);
        interop::writeJmp(0x00509020, &file_open_handle);
        interop::writeJmp(0x005094B0, &bufferize_file_0);
        interop::writeJmp(0x00509540, &read_partial_file_into_buffer);
        interop::writeJmp(0x005095D0, &file_exists);
        interop::writeJmp(0x00509780, &file_read_save);
        interop::writeJmp(0x005097E0, &file_write_save);
    }
}
