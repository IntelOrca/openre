#pragma once

#include <cstdint>
#include <windows.h>

struct _CARD;
struct _NAME;

namespace openre::file
{
    void file_error();
    int file_exists(const char* path, int mode);
    void* file_open_handle(const char* path, int mode);
    size_t read_file_into_buffer(const char* path, void* buffer, size_t length);
    uint32_t read_partial_file_into_buffer(const char* path, void* buffer, size_t offset, size_t length, size_t mode);
    void sub_505B20();
    void* file_alloc(const size_t size);
    int load_adt(const char* path, void* dst, int mode);
    int load_adt_sub(const char* path, uint8_t* dst, int pos, int mode);
    int osp_read();
    int tim_buffer_to_surface(int* timPtr, int page, int mode);

    int SaveGetPlID(const char* folder, DWORD* cnt0, DWORD* cnt1);
    void file_init_hooks();
}
