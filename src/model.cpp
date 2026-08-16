#include "model.h"
#include "interop.hpp"
#include "marni.h"
#include "openre.h"
#include "system_filesystem.h"
#include <cstring>
#include <string>

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

    // TMD file object created by TMDFile::Create. The original loaded the whole
    // file into a heap buffer (kept for the lifetime of the object) so the
    // remaining OG TMDFile::* parse helpers can walk it.
    struct TmdFile
    {
        uint8_t is_open; // 0x00
        uint8_t pad_01[3];
        char* name; // 0x04
        void* data; // 0x08
    };
    assert_struct_size(TmdFile, 0x0C);

    // MARNI_POLY_OBJECT: vtbl at +0 with In/Open slots, then 0x38 bytes of
    // fields, then the 32-byte field_38 work area used by TMDObject::Open.
    struct MarniPolyObject
    {
        void** vtbl; // 0x00
        uint8_t pad_04[52];
        int32_t field_38[8]; // 0x38
    };
    assert_struct_size(MarniPolyObject, 0x58);

    using PolyObjectOpenFn = int(__thiscall*)(MarniPolyObject*, void*, int, int);

    // 0x00430880
    // Frees the TMD file contents so the object can be reused.
    static void tmdfile_reset(TmdFile* self)
    {
        operator_delete(self->name);
        self->name = nullptr;
        operator_delete(self->data);
        self->data = nullptr;
        self->is_open = 1;
    }

    // 0x004308B0
    // Loads a TMD file into the object. Returns 1 whether or not the file could
    // be opened (mirroring the original).
    static int tmdfile_create(TmdFile* self, const char* path)
    {
        tmdfile_reset(self);

        auto data = system::fs::readAllBytes((std::string("data://") + path).c_str());
        if (data.empty())
        {
            marni::out("failed to open the file. TMDFile::Create", "");
            return 1;
        }

        void* buffer = operator_new((size_t)(4 * ((int)data.size() / 4) + 4));
        std::memcpy(buffer, data.data(), data.size());
        self->data = buffer;

        self->name = (char*)operator_new(strlen(path) + 1);
        strcpy(self->name, path);

        self->is_open = 1;
        return 1;
    }

    // 0x00430F20
    // Destroys the current polygon object contents before reloading.
    static void tmdobject_reset(MarniPolyObject* self)
    {
        interop::thiscall<void, MarniPolyObject*>(0x00430F20, self);
    }

    // 0x004309B0
    static int __stdcall tmdobject_create(MarniPolyObject* self, const char* path, int a3, int a4)
    {
        tmdobject_reset(self);

        auto data = system::fs::readAllBytes((std::string("data://") + path).c_str());
        if (data.empty())
        {
            marni::out("failed to open the file. TMDObject::Create", "");
            return 0;
        }

        void* buffer = operator_new((size_t)(4 * ((int)data.size() / 4) + 4));
        std::memcpy(buffer, data.data(), data.size());

        // The object's vtable In/Open slot parses the TMD buffer. This stays a
        // call into the original binary (MarniPolygonObject::Open / TM2Object::In).
        auto open = reinterpret_cast<PolyObjectOpenFn>(self->vtbl[1]);
        int result = open(self, buffer, a3, a4);

        operator_delete(buffer);
        return result;
    }

    void model_init_hooks()
    {
        interop::hookThisCall(0x004309B0, &tmdobject_create);
    }
}
