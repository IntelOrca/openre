#pragma once

#include "re2.h"

#pragma warning(push)
#pragma warning(disable : 4200)
#pragma pack(push, 1)

struct TmdEntry
{
    void* PositionData;
    size_t PositionCount;
    void* NormalData;
    size_t NormalCount;
    void* PrimitiveData;
    size_t PrimitiveCount;
    void* TextureData;
};

struct Tmd
{
    uint32_t Absolute;
    uint32_t NumEntries;
    TmdEntry Entries[];
};

struct Md1
{
    uint32_t Length;
    Tmd Data;
};

#pragma pack(pop)
#pragma warning(pop)

namespace openre
{
    constexpr uint8_t MODEL_FLAG_7 = 1 << 7;
    constexpr uint8_t MODEL_FLAG_6 = 1 << 6;

    size_t mapping_tmd(int a1, Md1* md1, int page, int clut);
}
