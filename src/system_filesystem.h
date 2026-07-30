#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openre
{
    class Stream;
}

namespace openre::system::fs
{
    enum class FileMode
    {
        read,
        write,
        readWrite,
    };

    std::string resolvePath(const char* path);
    bool exists(const char* path, std::string* resolvedPath);
    std::vector<uint8_t> readAllBytes(const char* path);

    std::unique_ptr<Stream> open(const char* path, FileMode mode);
    int writeAllBytes(const char* path, const void* data, size_t len);
    bool pathExists(const char* path);
    bool remove(const char* path);
    bool createDirectory(const char* path);
    std::vector<std::string> getDirectoryContents(const char* path, const char* pattern);
}
