#include "tim.h"
#include "interop.hpp"
#include "marni.h"
#include "openre.h"
#include <array>

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
        self->vtbl = (void**)0x005173FC;
        if (path != nullptr)
        {
        }
    }

    static void timobject_dtor(TimObject* self)
    {
        self->vtbl = (void**)0x005173FC;
        marni::surface2_vrelease(self);
        marni::surface2_release(self);
    }

    // 0x0042FB70
    static void timobject_in(TimObject* self, Tim* pTim)
    {
        interop::thiscall<TimObject*, void*>(0x0042FB70, self, pTim);
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
        interop::writeJmp(0x0043FF40, &tim_buffer_to_surface);
    }
}
