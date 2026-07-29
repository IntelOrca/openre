#pragma once

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace openre
{
    class Stream
    {
    public:
        virtual ~Stream() = default;

        virtual size_t read(void* buffer, size_t size) = 0;
        virtual size_t write(const void* buffer, size_t size) = 0;
        virtual int64_t seek(int64_t offset, int origin) = 0;
        virtual int64_t tell() const = 0;
    };

    class MemoryStream : public Stream
    {
    public:
        MemoryStream(const uint8_t* data, size_t size);
        MemoryStream(const std::vector<uint8_t>& vec);

        size_t read(void* buffer, size_t size) override;
        size_t write(const void* buffer, size_t size) override;
        int64_t seek(int64_t offset, int origin) override;
        int64_t tell() const override;

    private:
        const uint8_t* _data;
        size_t _size;
        size_t _position = 0;
    };
}
