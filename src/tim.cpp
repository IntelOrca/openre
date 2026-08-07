#include "tim.h"
#include "interop.hpp"
#include "marni.h"
#include "openre.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace openre::tim
{
    struct TimHeader
    {
        uint8_t magic;
        uint8_t version;
        uint8_t pad_02[2];
        uint8_t fmt;
        uint8_t pad_05[3];
    };

    struct TimClut
    {
        uint32_t length;
        uint16_t x;
        uint16_t y;
        uint16_t colors[1];
    };

    struct TimPixelData
    {
        uint32_t length;
        uint16_t x;
        uint16_t y;
        uint16_t width;
        uint16_t height;
        uint8_t scan0[1];
    };

    struct Tim
    {
        TimHeader header;
        TimClut clut;
    };

    struct TimObject : public MarniSurface
    {
        uint32_t var_3C;
    };

    // 0x0042FE50
    static void timobject_ctor(TimObject* self, const char* path)
    {
        std::memset(self, 0, sizeof(*self));
        marni::surface2_ctor(self);
        self->vtbl = (MarniSurfaceVTBL*)0x005173FC;
        if (path != nullptr)
        {
        }
    }

    static void timobject_dtor(TimObject* self)
    {
        self->vtbl = (MarniSurfaceVTBL*)0x005173FC;
        marni::surface2_vrelease(self);
        marni::surface2_release(self);
    }

    // 0x0042FB70
    static int __stdcall timobject_in(TimObject* self, Tim* pTim)
    {
        const uint32_t* pTimData = (const uint32_t*)pTim;

        self->pDDpalette = nullptr;
        self->pDDsurface = nullptr;
        self->var_3C = 0;
        self->is_vmem = 0;

        if (pTimData[0] != 16)
        {
            marni::out("can't identify to header ID MarniSystem TIMObject::In", "");
            return 0;
        }

        marni::surface2_vrelease(self);

        uint32_t flags = pTimData[1];
        bool hasClut = (flags & 8) != 0;
        const TimPixelData* pixelData;
        uint32_t clutWidth; // number of clut colors (lo16 of pTim[4])
        int palCount = 0;   // number of palettes (hi16 of pTim[4])
        const uint32_t* clutData = nullptr;
        int pTima = 1;

        if (hasClut)
        {
            // Clut block: pTim[2] = block length (unused), pTim[3] = clut x/y,
            // pTim[4] = colors/palettes; clut pixel data starts at pTim[5].
            self->pDDsurface = (void*)(uint16_t)pTimData[3];
            self->pDDpalette = (void**)(uintptr_t)(pTimData[3] >> 16);
            clutWidth = (uint16_t)pTimData[4];
            palCount = (int)(pTimData[4] >> 16);
            clutData = pTimData + 5;
            pixelData = (const TimPixelData*)((const uint8_t*)pTim + 20 + 2 * clutWidth * palCount);
        }
        else
        {
            // No clut: pixel data block starts right after the header/flags dwords.
            clutWidth = (uint32_t)(uintptr_t)pTim; // only ever compared against 0x10 below
            pixelData = (const TimPixelData*)(pTimData + 2);
        }

        self->is_vmem = (uint32_t)pixelData->x; // field_38 (lo16 of dword at pixel data +4)
        self->var_3C = (uint32_t)pixelData->y;  // field_3C (hi16 of dword at pixel data +4)
        int width = pixelData->width;
        int height = pixelData->height;
        const uint8_t* pBitmap = pixelData->scan0;

        int depth;
        switch (flags & 7)
        {
            case 0:
                depth = 4;
                if (clutWidth > 0x10)
                {
                    marni::out("in fact, this must be 16 as clut_width...%d, but I would recreate it as that the back clut slides into under. TIMObject::In", "");
                    pTima = 2;
                }
                break;
            case 1:
                depth = 8;
                break;
            case 2:
                depth = 16;
                break;
            default:
                marni::out("not supported type MarniSystem TIMObject::In", "");
                return 0;
        }

        int result;
        if (hasClut)
            result = marni::surface2_create_work(self, width * (16 / depth), height, depth, 16, palCount * pTima);
        else
            result = marni::surface2_create_work(self, width, height, 16, 0, -1);
        if (!result)
        {
            marni::out("failed to create the bits like you specified. TIMObject::In", "");
            return 0;
        }

        std::memcpy(self->pBitmap, pBitmap, 2 * width * height);
        if (hasClut)
            std::memcpy(self->pPalette, clutData, 2 * clutWidth * palCount);

        // 555 pixel format descriptor
        self->desc.r_shift = 0;
        self->desc.r_mask = 31;
        self->desc.r_bitcnt = 5;
        self->desc.g_shift = 5;
        self->desc.g_mask = 31;
        self->desc.g_bitcnt = 5;
        self->desc.b_shift = 10;
        self->desc.b_mask = 31;
        self->desc.b_bitcnt = 5;
        self->desc.a_shift = 0;
        self->desc.a_mask = 0;
        self->desc.a_bitcnt = 0;
        self->bOpen = 1;
        self->var_2A = 1;
        return 1;
    }

    // 0x0042FDB0
    static int __stdcall timobject_create(TimObject* self, const char* path)
    {
        marni::surface2_vrelease(self);

        FILE* fp = fopen(path, "rb");
        if (fp == nullptr)
        {
            marni::out("failed to open the file. TIMObject::Create", "");
            return 0;
        }

        fseek(fp, 0, SEEK_END);
        long size = ftell(fp);
        void* buffer = operator_new((size_t)(4 * (size / 4) + 4));
        fseek(fp, 0, SEEK_SET);
        fread(buffer, 1, size, fp);
        fclose(fp);

        int result = timobject_in(self, (Tim*)buffer);
        operator_delete(buffer);
        return result;
    }

    // 0x0043FF40
    int tim_buffer_to_surface(Tim* pTim, uint32_t page, uint32_t mode)
    {
        TimObject timObject;
        timobject_ctor(&timObject, nullptr);
        timobject_in(&timObject, pTim);
        if (page >= std::size(gGameTable.texture_pages))
        {
            timobject_dtor(&timObject);
            return 0;
        }

        marni::unload_texture_page(page);
        auto mode2 = mode == 0 ? 16 : 0;
        auto& tp = gGameTable.texture_pages[page];
        if ((pTim->header.fmt & 0x7) > 1)
        {
            mode2 |= 2;
            tp.handle = marni::create_texture_handle(gGameTable.pMarni, &timObject, mode2);
            tp.var_04 = 1;
        }
        else
        {
            if (timObject.pal_cnt <= 1)
                mode2 |= 2;
            else
                mode2 |= 0x22;
            tp.handle = marni::create_texture_handle(gGameTable.pMarni, &timObject, mode2);
            tp.var_04 = timObject.pal_cnt;
        }
        if (tp.handle == 0)
        {
            marni::unload_texture_page(page);
            timobject_dtor(&timObject);
            return 0;
        }

        update_timer();
        timobject_dtor(&timObject);
        return 1;
    }

    void tim_init_hooks()
    {
        interop::hookThisCall(0x0042FDB0, &timobject_create);
        interop::writeJmp(0x0043FF40, &tim_buffer_to_surface);
        interop::hookThisCall(0x0042FB70, &timobject_in);
    }
}
