#pragma once

#include <cstddef>
#include <string>

namespace openre::system::fs
{
    std::string resolvePath(const char* path);
    bool exists(const char* path, std::string* resolvedPath);
    int readAllBytes(const char* path, void* dst, size_t* length);
}
