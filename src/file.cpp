#include "file.h"
#include "gfx.h"
#include "interop.hpp"
#include "openre.h"
#include "str.h"
#include "stream.h"
#include "system_filesystem.h"
#include <cstdlib>
#include <cstring>
#include <string>

// Memory card save structures (forward-declared in file.h)
// Must be packed to match the original binary's 1-byte alignment.
#pragma pack(push, 1)
struct SaveFile
{
    unsigned char magic[2];
    unsigned char type;
    unsigned char blockEntry;
    unsigned char title[64];
    unsigned char reserved[28];
    unsigned short clut[16];
    unsigned char icon[128];
    char extra;
    char field_101;
    char field_102;
    char field_103;
    char field_104;
    char field_105;
    char field_106;
    char field_107;
    char field_108;
    char field_109;
    char field_10A;
    char field_10B;
    char field_10C;
    char field_10D;
    char field_10E;
    char field_10F;
    char field_110;
    char field_111;
    char field_112;
    char field_113;
};
assert_struct_size(SaveFile, 276);

struct SaveFileName
{
    char data[261];
};
assert_struct_size(SaveFileName, 261);
#pragma pack(pop)

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

    // --- File I/O wrapper with error dialog suppression and logging ---

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

    // 0x00502D40
    size_t read_file_into_buffer(const char* path, void* buffer, size_t length)
    {
        size_t result = 0;
        auto data = system::fs::readAllBytes((std::string("data://") + path).c_str());
        if (!data.empty())
        {
            auto bytesToCopy = data.size();
            if (bytesToCopy > 8 * 1024 * 1024)
                bytesToCopy = 8 * 1024 * 1024;
            std::memcpy(buffer, data.data(), bytesToCopy);
            result = bytesToCopy;
        }

        if (result == 0)
        {
            gGameTable.error_no = 11;
        }
        return result;
    }

    // 0x005094B0
    static uint32_t bufferize_file_0(void* filename, void* buffer, uint32_t type)
    {
        auto data = system::fs::readAllBytes((std::string("data://") + (const char*)filename).c_str());
        if (data.empty())
        {
            gGameTable.error_no = 11;
            return 0;
        }

        std::memcpy(buffer, data.data(), data.size());
        update_timer();
        return static_cast<uint32_t>(data.size());
    }

    // 0x00509540
    uint32_t read_partial_file_into_buffer(const char* path, void* buffer, size_t offset, size_t length, size_t mode)
    {
        auto stream = system::fs::open((std::string("data://") + path).c_str(), system::fs::FileMode::read);
        if (stream == nullptr)
        {
            gGameTable.error_no = 11;
            return 0;
        }

        if (stream->seek(static_cast<int64_t>(offset), SEEK_SET) >= 0)
        {
            auto bytesRead = stream->read(buffer, length);
            if (bytesRead == length)
            {
                update_timer();
                return static_cast<uint32_t>(bytesRead);
            }
        }

        gGameTable.error_no = 11;
        return 0;
    }

    // 0x00435430
    static const char* Save_open_fp(
        const char* path, void* buffer, uint32_t bytes_to_read, long distance_to_move, uint32_t* out_time_high,
        uint32_t* out_time_low)
    {
        abort();
        return nullptr;
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
    int file_read_save(void* buffer, const char* filename, size_t size)
    {
        auto data = system::fs::readAllBytes((std::string("save://") + str::sjis_to_utf8(filename)).c_str());
        if (data.empty() || data.size() < size)
            return READ_SAVE_FILE_ERROR;

        std::memcpy(buffer, data.data(), size);
        return READ_SAVE_FILE_SUCCESS;
    }

    // 0x005097E0
    size_t file_write_save(const char* filename, void* buffer, size_t size)
    {
        auto result = system::fs::writeAllBytes((std::string("save://") + str::sjis_to_utf8(filename)).c_str(), buffer, size);
        if (result == 0)
        {
            update_timer();
            return size;
        }
        return 0;
    }

    // 0x00432600
    bool remove_save(const char* path)
    {
        return system::fs::remove((std::string("save://") + str::sjis_to_utf8(path)).c_str());
    }

    // 0x00435430
    // Reads a chunk of a save file and returns its modification time. Called by
    // the original Card_write to read save card metadata. The original used
    // CreateFileA directly, which cannot open files stored with UTF-8 names by
    // SDL when given a Shift-JIS filename on a non-Japanese system, so we route
    // it through the SDL filesystem layer with codepage conversion.
    static const char*
    save_open_fp(const char* filename, void* buffer, uint32_t bytes, int32_t offset, uint32_t* timeHigh, uint32_t* timeLow)
    {
        auto utf8Path = str::sjis_to_utf8(filename);
        // Card_write appends a backslash to the folder which already ends with
        // one, producing a double separator SDL cannot open; collapse them.
        std::string normalized;
        normalized.reserve(utf8Path.size());
        for (size_t i = 0; i < utf8Path.size(); ++i)
        {
            if (i > 0 && utf8Path[i] == '\\' && utf8Path[i - 1] == '\\')
                continue;
            normalized += utf8Path[i];
        }
        auto data = system::fs::readAllBytes((std::string("save://") + normalized).c_str());
        *timeHigh = 0;
        *timeLow = 0;
        if (data.empty() || (size_t)offset + bytes > data.size())
        {
            // A zero timestamp would make Card_write's insertion loop run past
            // the end of the card array; force insertion at the front instead.
            *timeHigh = 0x7FFFFFFF;
            *timeLow = 0xFFFFFFFF;
            return nullptr;
        }

        std::memcpy(buffer, data.data() + offset, bytes);

        auto info = system::fs::info((std::string("save://") + normalized).c_str());
        if (info.kind == system::fs::FileKind::file)
        {
            // fs::FileInfo time is nanoseconds since 1970; FILETIME is 100ns
            // since 1601.
            uint64_t ft = info.lastModified / 100 + 116444736000000000ULL;
            *timeHigh = static_cast<uint32_t>(ft >> 32);
            *timeLow = static_cast<uint32_t>(ft & 0xFFFFFFFF);
        }

        update_timer();
        return filename;
    }

    // 0x005095D0
    int file_exists(const char* path, int mode)
    {
        auto info = system::fs::info((std::string("data://") + path).c_str());
        str::string_assign_cstr(&gGameTable.ss_file_string, info.physicalPath.c_str());
        return info.kind != system::fs::FileKind::none ? 1 : 0;
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
        abort();
        return 0;
    }

    // 0x0043BC90
    static short file_read_chunk2(int num_bits)
    {
        abort();
        return 0;
    }

    // 0x00509620
    static void adt_store_last_filename(const char* filename)
    {
        str::string_copy(&gGameTable.ss_file_string, filename);
    }

    // 0x0043C590
    int load_adt(const char* path, void* dst, int mode)
    {
        auto compressed = system::fs::readAllBytes((std::string("data://") + path).c_str());
        if (compressed.empty())
        {
            adt_store_last_filename(path);
            gGameTable.error_no = 11;
            return 0;
        }

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

    // 0x0043C700
    int load_adt_sub(const char* path, uint8_t* dst, int pos, int mode)
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

        auto stream = system::fs::open((std::string("data://") + path).c_str(), system::fs::FileMode::read);
        if (stream == nullptr)
        {
            adt_store_last_filename(path);
            gGameTable.error_no = 11;
            return 0;
        }

        if (stream->seek(pos, SEEK_SET) < 0)
        {
            adt_store_last_filename(path);
            gGameTable.error_no = 11;
            return 0;
        }

        gGameTable.adt_file_handle = (void*)(uintptr_t)0xDEADBEEF;
        {
            auto output = openre::graphics::decodeAdt(*stream);
            if (!output.empty())
            {
                auto* dstPtr = gGameTable.adt_out_ptr;
                std::memcpy(dstPtr, output.data(), output.size());
                gGameTable.adt_out_offset = (uint32_t)output.size();
            }
        }

        gGameTable.adt_file_handle = 0;
        update_timer();

        return gGameTable.adt_out_offset;
    }

    // 0x00442D50
    int CreateSaveFolder(const char* path)
    {
        return system::fs::createDirectory((std::string("save://") + str::sjis_to_utf8(path)).c_str()) ? 1 : 0;
    }

    // 0x004326E0
    static int ck_valid_save(const char* folder, void* entry)
    {
        return interop::call<int, const char*, void*>(0x004326E0, folder, entry);
    }

    // 0x00431F40
    int SaveGetPlID(const char* folder, int* cnt0, int* cnt1)
    {
        *cnt1 = 0;
        *cnt0 = 0;

        auto folderUtf8 = str::sjis_to_utf8(folder);
        auto entries = system::fs::getDirectoryContents((std::string("save://") + folderUtf8).c_str(), "*");
        if (entries.empty())
            return -1;

        for (const auto& entry : entries)
        {
            auto sjisEntry = str::utf8_to_sjis(entry);
            if (sjisEntry[0] != '.' && ck_valid_save(folder, (void*)sjisEntry.c_str()))
                ++*cnt0;
        }

        const char* saveFolder = reinterpret_cast<const char*>(0x986280);
        if (strcmp(saveFolder, folder) == 0 && --*cnt1 < 0)
            *cnt1 = 0;

        return 0;
    }

    // 0x004C7A30
    static int Card_write(SaveFile* Head, const char* Name, const char* Title)
    {
        return interop::call<int, SaveFile*, const char*, const char*>(0x4C7A30, Head, Name, Title);
    }

    // 0x00431D80
    // Enumerate save files in a directory, filling the cards and names arrays.
    // Allocates/reallocates the arrays when the count is nonzero.
    // Returns 1 on success (all files enumerated), 0 on failure.
    int save_list_files(const char* file_name, int cnt0, void** cards, int cnt1, void** names)
    {
        // Allocate/reallocate cards array
        if (cnt0 > 0)
        {
            free(*cards);
            *cards = calloc(cnt0, sizeof(SaveFile));
            if (!*cards)
                return 0;
        }

        // Allocate/reallocate names array
        if (cnt1 > 0)
        {
            free(*names);
            *names = calloc(cnt1, sizeof(SaveFileName));
            if (!*names)
                return 0;
        }

        // Enumerate save files and build the card list
        auto fileUtf8 = str::sjis_to_utf8(file_name);
        auto entries = system::fs::getDirectoryContents((std::string("save://") + fileUtf8).c_str(), "*");
        if (entries.empty())
            return 0;

        for (const auto& entry : entries)
        {
            auto sjisEntry = str::utf8_to_sjis(entry);
            if (sjisEntry[0] != '.' && ck_valid_save(file_name, (void*)sjisEntry.c_str()))
            {
                Card_write((SaveFile*)*cards, file_name, sjisEntry.c_str());
            }
        }
        return 1;
    }

    void file_init_hooks()
    {
        interop::writeJmp(0x00431D80, &save_list_files);
        interop::writeJmp(0x00432600, &remove_save);
        interop::writeJmp(0x0043BBC0, &file_read_chunk);
        interop::writeJmp(0x0043BC90, &file_read_chunk2);
        interop::writeJmp(0x0043C590, &load_adt);
        interop::writeJmp(0x0043C700, &load_adt_sub);
        interop::writeJmp(0x0043C890, &decompress_file_page);
        interop::writeJmp(0x00435430, &save_open_fp);
        interop::writeJmp(0x00442D50, &CreateSaveFolder);
        interop::writeJmp(0x004DD360, &osp_read);
        interop::writeJmp(0x005094B0, &bufferize_file_0);
        interop::writeJmp(0x00509540, &read_partial_file_into_buffer);
        interop::writeJmp(0x005095D0, &file_exists);
        interop::writeJmp(0x00509780, &file_read_save);
        interop::writeJmp(0x005097E0, &file_write_save);
    }
}
