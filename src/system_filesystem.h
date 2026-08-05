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

    enum class FileKind
    {
        none, // path does not exist
        directory,
        file,
    };

    struct FileInfo
    {
        FileKind kind = FileKind::none;
        std::string physicalPath;
        uint64_t size = 0;
        uint64_t lastModified = 0; // nanoseconds since 1970-01-01 UTC
    };

    FileInfo info(const char* path);

    std::vector<uint8_t> readAllBytes(const char* path);

    std::unique_ptr<Stream> open(const char* path, FileMode mode);
    int writeAllBytes(const char* path, const void* data, size_t len);
    bool remove(const char* path);
    bool createDirectory(const char* path);
    std::vector<std::string> getDirectoryContents(const char* path, const char* pattern);
}
