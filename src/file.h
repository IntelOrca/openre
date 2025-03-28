#pragma once

#include <cstdint>

namespace openre::file
{
    void file_error();
    size_t read_file_into_buffer(const char* path, void* buffer, size_t length);
    uint32_t read_partial_file_into_buffer(const char* path, void* buffer, size_t offset, size_t length, size_t unk);
    void sub_505B20();
    void* file_alloc(const size_t size);
    int load_adt(const char* path, uint32_t* bufferSize, int mode);
    int osp_read();
    int tim_buffer_to_surface(int* timPtr, int page, int mode);

    void file_init_hooks();
}
