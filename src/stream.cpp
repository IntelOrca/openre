#include "stream.h"

#include <cstdio>
#include <cstring>

namespace openre
{
    MemoryStream::MemoryStream(const uint8_t* data, size_t size)
        : _data(data)
        , _size(size)
    {
    }

    MemoryStream::MemoryStream(const std::vector<uint8_t>& vec)
        : _data(vec.data())
        , _size(vec.size())
    {
    }

    size_t MemoryStream::read(void* buffer, size_t size)
    {
        auto available = _size - _position;
        auto toRead = (size < available) ? size : available;
        std::memcpy(buffer, _data + _position, toRead);
        _position += toRead;
        return toRead;
    }

    size_t MemoryStream::write(const void* buffer, size_t size)
    {
        (void)buffer;
        (void)size;
        return 0;
    }

    int64_t MemoryStream::seek(int64_t offset, int origin)
    {
        switch (origin)
        {
        case SEEK_SET: _position = static_cast<size_t>(offset); break;
        case SEEK_CUR: _position += static_cast<size_t>(offset); break;
        case SEEK_END: _position = _size + static_cast<size_t>(offset); break;
        }
        if (_position > _size)
            _position = _size;
        return static_cast<int64_t>(_position);
    }

    int64_t MemoryStream::tell() const
    {
        return static_cast<int64_t>(_position);
    }
}
