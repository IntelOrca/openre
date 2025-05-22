#pragma once

#include <cstdint>
#include <vector>

namespace openre
{
    struct DataBlock
    {
        const void* data{};
        size_t len{};

        DataBlock() {}
        DataBlock(const void* data, size_t len)
            : data(data)
            , len(len)
        {
        }

        template<typename T>
        DataBlock(const std::vector<T>& v)
            : data((const void*)v.data())
            , len(v.size() * sizeof(T))
        {
        }

        template<typename T, size_t TSize>
        DataBlock(T (&arr)[TSize])
            : data(arr)
            , len(TSize)
        {
        }
    };
}
