#include "file.h"
#include "gfx.h"
#include "interop.hpp"
#include "openre.h"
#include "save.h"
#include "str.h"
#include "stream.h"
#include "system_filesystem.h"
#include "tim.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <windows.h>

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
    // Scans the installed drives for the game disc marker (disc.id) and records
    // the drive root in the disc-path string global (0x689F34), mirroring the
    // original. Returns true only when the recorded path changed to a non-empty
    // value. The reimplementation serves all game data from data://, so the
    // marker is never found on disk and this simply clears the disc path.
    static bool check_disk_id()
    {
        auto* discPath = reinterpret_cast<OldStdString*>(0x689F34);

        uint32_t logicalDrives = GetLogicalDrives();
        int foundDrive = -1;
        for (int i = 0; i < 0x20; i++)
        {
            if (((1u << i) & logicalDrives) == 0)
                continue;

            char fileName[128];
            sprintf(fileName, "%c:\\disc.id", i + 'A');

            auto data = system::fs::readAllBytes(fileName);
            if (data.empty())
                continue;

            // The original read the file into a buffer and treated it as a
            // NUL-terminated string, then stripped one trailing "\r\n" or "\n".
            std::string content(reinterpret_cast<const char*>(data.data()), data.size());
            auto nul = content.find('\0');
            if (nul != std::string::npos)
                content.resize(nul);
            if (content.size() >= 2 && content[content.size() - 2] == '\r' && content[content.size() - 1] == '\n')
                content.resize(content.size() - 2);
            else if (content.size() >= 1 && content.back() == '\n')
                content.pop_back();

            if (content == "bio2.658b45ea117473d4.disc")
            {
                foundDrive = i;
                break;
            }
        }

        std::string root;
        if (foundDrive >= 0)
        {
            root += (char)('A' + foundDrive);
            root += ":\\";
        }

        if (root != (discPath->data ? discPath->data : ""))
        {
            str::string_assign_cstr(discPath, root.c_str());
            return str::string_sjis_len(discPath) > 0;
        }
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
        // The original entry point is hooked to load_init_table_3, so call it
        // directly rather than going through the original binary.
        load_init_table_3();
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

    // 0x00509630
    // Finds the first free "capt%04d.bmp" screenshot name in the save folder
    // (0000..9999) and stores it in the output string. Returns 1 when a name was
    // found, 0 when all 10000 names are taken. The original probed the candidate
    // names with fopen("rb"); the BMP data itself is written later by
    // MarniBits::FileOut.
    static char FileWriteScreen(OldStdString* outPath)
    {
        std::string path = save::GetSaveFolder();
        if (path.empty() || path.back() != '\\')
            path += '\\';
        path += "capt";

        char name[128];
        for (int i = 0; i < 10000; i++)
        {
            sprintf(name, "%04d.bmp", i);
            auto candidate = path + name;
            if (system::fs::info(candidate.c_str()).kind != system::fs::FileKind::file)
            {
                str::string_assign_cstr(outPath, candidate.c_str());
                return 1;
            }
        }
        return 0;
    }

    // 0x00509040
    // Resolves a game data file and stores its physical path in the output
    // string. The original probed the install directory, the module directory
    // and the game disc for "path" / "data\path" and returned a Win32 HANDLE
    // that only the original ReadFile/CloseHandle consumers used. Every one of
    // those callers (open_handle, FileExists, Bg_cache_roomptr) has been
    // reimplemented on system::fs, so this only reports success — writing the
    // resolved data:// path to the output string and returning -1 (mirroring
    // INVALID_HANDLE_VALUE) on failure.
    static int open_file(OldStdString* outPath, const char* filename, int /*mode*/, char /*silent*/)
    {
        auto info = system::fs::info((std::string("data://") + filename).c_str());
        str::string_assign_cstr(outPath, info.physicalPath.c_str());
        return info.kind == system::fs::FileKind::file ? 1 : -1;
    }

    // 0x00509020
    static int open_handle(const char* filename, int mode)
    {
        return open_file(&gGameTable.ss_file_string, filename, mode, 0);
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
        // The original entry point is hooked to tim::tim_buffer_to_surface, so
        // delegate to the reimplementation (the real function takes a Tim*).
        return openre::tim::tim_buffer_to_surface(
            reinterpret_cast<openre::tim::Tim*>(timPtr), static_cast<uint32_t>(page), static_cast<uint32_t>(mode));
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

    // 0x0050C1A0
    // Shift-JIS-aware ASCII uppercasing used when validating save entries:
    // converts 'a'..'z' to uppercase in place, skipping double-byte character
    // lead bytes so their trailing bytes are never corrupted. Returns the
    // number of characters converted.
    static int string_upper_sjis(OldStdString* self)
    {
        char* p = self->data;
        int count = 0;
        char c = *p;
        if (c)
        {
            do
            {
                if (((uint8_t)c < 0x81 || (uint8_t)c > 0x9F) && ((uint8_t)c < 0xE0 || (uint8_t)c > 0xFC))
                {
                    if (c >= 'a' && c <= 'z')
                    {
                        ++count;
                        *p = (char)(c - 32);
                    }
                    ++p;
                }
                else
                {
                    p += 2;
                }
                c = *p;
            } while (*p);
        }
        return count;
    }

    // 0x004326E0
    // Validates a save file entry in the given folder. Builds the full save
    // path as "<folder>\<entry>", normalises the filename case (ASCII only),
    // then verifies the name mentions the game ("BIOHAZARD2" / "RESIDENT2")
    // and that the file starts with the "SC" magic. Returns 1 for a valid
    // save, 0 otherwise.
    static int ck_valid_save(const char* folder, void* entry)
    {
        OldStdString path;
        str::string_ctor_from_cstr(&path, folder);

        int result = 0;

        // Ensure the folder ends with a backslash before appending the entry
        // name, then build the full path in place.
        OldStdString lastChar;
        str::string_right(&path, &lastChar, 1);
        bool needsSeparator = str::string_ne_cstr(&lastChar, "\\");
        str::string_dtor(&lastChar);
        if (needsSeparator)
            str::string_append(&path, "\\");
        str::string_append(&path, static_cast<const char*>(entry));

        string_upper_sjis(&path);

        if (str::string_find_last(&path, ".BIOHAZARD2") < 0 && str::string_find_last(&path, "RESIDENT2") < 0)
        {
            str::string_dtor(&path);
            return 0;
        }

        uint8_t buffer[0x800];
        std::memset(buffer, 0, sizeof(buffer));
        if (file_read_save(buffer, str::string_get_data(&path), 0x800) != 0)
        {
            str::string_dtor(&path);
            return 0;
        }

        if (buffer[0] == 'S' && buffer[1] == 'C')
            result = 1;

        str::string_dtor(&path);
        return result;
    }

    // 0x00431F40
    int SaveGetPlID(const char* folder, int* cnt0, int* cnt1)
    {
        *cnt1 = 0;
        *cnt0 = 0;

        auto folderUtf8 = str::sjis_to_utf8(folder);
        auto savePath = std::string("save://") + folderUtf8;
        // Only report an error when the save folder does not exist at all
        // (mirrors FindFirstFileA failing in the original). An existing but
        // empty folder counts as zero saves so the memory card screen can show
        // an empty list instead of bailing out.
        if (system::fs::info(savePath.c_str()).kind != system::fs::FileKind::directory)
            return -1;
        auto entries = system::fs::getDirectoryContents(savePath.c_str(), "*");

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
    // Inserts a new save card into the sorted card array. Builds the path
    // "<Name>\<Title>", reads the save file's 0x598-byte data block at offset
    // 512 plus its modification time, then inserts the new entry ahead of the
    // first card with an older timestamp (cards are ordered newest first).
    // Fills in the card fields from the save data and returns the high dword
    // of the file time.
    static int Card_write(SaveFile* Head, const char* Name, const char* Title)
    {
        char fileName[260];
        strcpy(fileName, Name);
        size_t nameLen = strlen(fileName);
        fileName[nameLen] = '\\';
        fileName[nameLen + 1] = '\0';
        strcat(fileName, Title);

        uint8_t buffer[0x598];
        uint32_t timeHigh;
        uint32_t timeLow;
        save_open_fp(fileName, buffer, 0x598, 512, &timeHigh, &timeLow);

        int cnt0 = gGameTable.cnt0;
        int insertIndex = 0;
        if (cnt0 > 0)
        {
            uint64_t newTime = ((uint64_t)timeHigh << 32) | timeLow;
            for (; insertIndex < cnt0; insertIndex++)
            {
                const SaveFile* card = &Head[insertIndex];
                uint32_t cardTimeHigh;
                uint32_t cardTimeLow;
                std::memcpy(&cardTimeHigh, &card->field_108, sizeof(cardTimeHigh));
                std::memcpy(&cardTimeLow, &card->field_10C, sizeof(cardTimeLow));
                if ((((uint64_t)cardTimeHigh << 32) | cardTimeLow) < newTime)
                    break;
            }
        }

        // Shift the cards below the insertion point up by one slot.
        for (int i = cnt0 - 2; i >= insertIndex; i--)
        {
            Head[i + 1] = Head[i];
        }

        SaveFile* card = &Head[insertIndex];
        strcpy(reinterpret_cast<char*>(card), Title);
        strcpy(fileName, Title);
        *strrchr(fileName, '.') = '\0';

        uint8_t maxTitleLen;
        if (gGameTable.is_480p)
            maxTitleLen = gGameTable.pad_662E64[0];
        else
        {
            maxTitleLen = 32;
            gGameTable.pad_662E64[0] = 32;
        }
        size_t titleLen = strlen(fileName);
        card->field_110 = (char)(titleLen <= maxTitleLen ? 0 : titleLen - maxTitleLen);

        card->field_111 = (char)buffer[0x592];

        uint8_t v23 = buffer[10];
        uint32_t v26;
        std::memcpy(&v26, &buffer[20], sizeof(v26));
        if (card->field_111 == 0)
        {
            if (v26 & 0x40000000)
                card->field_105 = (v23 & 1) ? 3 : 2;
            else
                card->field_105 = (v23 & 1) ? 1 : 0;
            card->field_113 = (char)((v26 >> 26) & 1);
        }
        else
        {
            switch (v23)
            {
            case 0: card->field_105 = 6; break;
            case 1: card->field_105 = 7; break;
            case 11: card->field_105 = 9; break;
            case 14: card->field_105 = 8; break;
            default: break;
            }
            card->field_113 = 0;
        }

        card->field_106 = (char)buffer[32];
        card->field_107 = (char)buffer[11];
        std::memcpy(&card->field_108, &timeHigh, sizeof(timeHigh));
        std::memcpy(&card->field_10C, &timeLow, sizeof(timeLow));
        card->field_112 = (char)buffer[18];

        return (int)timeHigh;
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
        interop::writeJmp(0x00509020, &open_handle);
        interop::writeJmp(0x00509040, &open_file);
        interop::writeJmp(0x005092A0, &check_disk_id);
        interop::writeJmp(0x005094B0, &bufferize_file_0);
        interop::writeJmp(0x00509540, &read_partial_file_into_buffer);
        interop::writeJmp(0x005095D0, &file_exists);
        interop::writeJmp(0x00509630, &FileWriteScreen);
        interop::writeJmp(0x00509780, &file_read_save);
        interop::writeJmp(0x005097E0, &file_write_save);
    }
}
