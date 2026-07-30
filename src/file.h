#pragma once

#include <cstdint>

namespace openre::file
{
    void file_error();
    int file_exists(const char* path, int mode);
    size_t read_file_into_buffer(const char* path, void* buffer, size_t length);
    uint32_t read_partial_file_into_buffer(const char* path, void* buffer, size_t offset, size_t length, size_t mode);
    void sub_505B20();
    void* file_alloc(const size_t size);
    int load_adt(const char* path, void* dst, int mode);
    int load_adt_sub(const char* path, uint8_t* dst, int pos, int mode);
    int osp_read();
    int tim_buffer_to_surface(int* timPtr, int page, int mode);

    int SaveGetPlID(const char* folder, int* cnt0, int* cnt1);
    int save_list_files(const char* file_name, int cnt0, void** cards, int cnt1, void** names);
    int file_read_save(void* buffer, const char* filename, size_t size);
    size_t file_write_save(const char* filename, void* buffer, size_t size);
    bool remove_save(const char* path);
    int CreateSaveFolder(const char* path);
    void file_init_hooks();
}
