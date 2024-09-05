#include "model.h"
#include "interop.hpp"

namespace openre
{
    static void update_address(void*& offset, void* baseAddress)
    {
        auto b = reinterpret_cast<uintptr_t>(baseAddress);
        auto o = reinterpret_cast<uintptr_t>(offset);
        offset = reinterpret_cast<void*>(b + o);
    }

    // 0x004C5130
    static void mapModelingData(Tmd* tmd)
    {
        tmd->Absolute = 1;
        auto numEntries = tmd->NumEntries;
        auto baseAddress = reinterpret_cast<void*>(&tmd->Entries[0]);
        for (size_t i = 0; i < numEntries; i++)
        {
            auto& entry = tmd->Entries[i];
            update_address(entry.PositionData, baseAddress);
            update_address(entry.NormalData, baseAddress);
            update_address(entry.PrimitiveData, baseAddress);
            update_address(entry.TextureData, baseAddress);
        }
    }

    /**
     * Changes the mesh offsets to pointers.
     * 0x00502D90
     */
    size_t mapping_tmd(int a1, Md1* md1, int page, int clut)
    {
        mapModelingData(&md1->Data);
        return md1->Length;
    }
}
