#include "hud.h"
#include "file.h"
#include "openre.h"
#include "re2.h"

using namespace openre::file;

namespace openre::hud
{
    static Action* gHudImplTable = (Action*)0x5402C0;
    static HudInfo*& gHudInfoPtr = *((HudInfo**)0x53DB70);
    static HudInfo& gHudInfo = *((HudInfo*)0x691F60);

    static uint8_t*& dword_53DB74 = *((uint8_t**)0x53DB74);
    static uint32_t& dword_689DF8 = *((uint32_t*)0x689DF8);
    static uint8_t& _st = *((uint8_t*)0x691F61);
    static uint32_t& dword_691F74 = *((uint32_t*)0x691F74);
    static uint8_t& byte_691F76 = *((uint8_t*)0x691F76);
    static uint16_t& word_692FBE = *((uint16_t*)0x692FBE);
    static uint16_t& word_692FC0 = *((uint16_t*)0x692FC0);
    static uint8_t* _bgBuffer = (uint8_t*)0x8C1880;

    // 0x004C4AD0
    static int hud_screen_fade(int a0)
    {
        using sig = int (*)(int);
        auto p = (sig)0x004C4AD0;
        return p(a0);
    }

    static void sub_43FF40(void* buffer, int a1, int a2)
    {
        using sig = int (*)(void*, int, int);
        auto p = (sig)0x0043FF40;
        p(buffer, a1, a2);
    }

    static void sub_4FCA30()
    {
        using sig = void (*)();
        auto p = (sig)0x004FCA30;
        p();
    }

    static void sub_4C49C0(int a0, int a1, int a2, int a3)
    {
        using sig = void (*)(int, int, int, int);
        auto p = (sig)0x004C49C0;
        p(a0, a1, a2, a3);
    }

    // 0x00502460
    static void hud_render_inventory_text(int a0, int a1, int a2, int a3)
    {
        using sig = void (*)(int, int, int, int);
        auto p = (sig)0x00502460;
        p(a0, a1, a2, a3);
    }

    // 0x004FE5F0
    static void hud_render_selection(int a0)
    {
        using sig = void (*)(int);
        auto p = (sig)0x004FE5F0;
        p(a0);
    }

    // 0x004FF360
    static void hud_render_items()
    {
        using sig = void (*)();
        auto p = (sig)0x004FF360;
        p();
    }

    // 0x004FE130
    static void hud_render_weapon_amount()
    {
        using sig = void (*)();
        auto p = (sig)0x004FE130;
        p();
    }

    // 0x004FF710
    static void hud_render_weapon()
    {
        using sig = void (*)();
        auto p = (sig)0x004FF710;
        p();
    }

    // 0x004FEC80
    static void hud_render_text_bg()
    {
        using sig = void (*)();
        auto p = (sig)0x004FEC80;
        p();
    }

    static void hud_render_itembox_items()
    {
        using sig = void (*)();
        auto p = (sig)0x004FCBC0;
        p();
    }

    // 0x004FBEB0
    static void hud_itembox_1()
    {
        if (dword_68A204->var_09 == 0)
        {
            gHudInfoPtr = &gHudInfo;
            dword_53DB74 = (uint8_t*)0x009864E0;
            if (read_file_into_buffer("common\\data\\itembox.tim", _bgBuffer, 4) == 0)
            {
                file_error();
                return;
            }

            sub_43FF40(_bgBuffer, 29, 0);
            gHudInfoPtr->routine = 1;
            gHudInfoPtr->var_01 = 3;
            gHudInfoPtr->var_25 = 0;
            gHudInfoPtr->var_24 = 0;
            sub_4FCA30();

            word_692FBE = 1;
            word_692FC0 = 0;
            dword_689DF8 = 5;
            for (auto i = 5; i > 0; i--)
            {
                dword_689DF8--;
            }
            sub_4C49C0(512, -6144, 7, 1);
            task_sleep(1);
            dword_68A204->var_09 = 1;
        }
        else if (dword_68A204->var_09 == 1)
        {
            hud_render_itembox_items();
            hud_render_items();
            hud_render_weapon_amount();
            hud_render_weapon();
            hud_render_text_bg();
            hud_render_inventory_text(16, 175, 2, dword_53DB74[gHudInfoPtr->var_0C * 4 + 0x8854]);
            hud_render_selection(dword_53DB74[gHudInfoPtr->var_0C * 4 + 0x8856]);
            if (hud_screen_fade(0) == 0)
            {
                task_sleep(1);
            }
            else
            {
                dword_68A204->var_09 = 0;
            }
        }
    }

    static const Action _itemBoxRender[] = {
        (Action)0x00502130,
        (Action)0x00502150,
        (Action)0x004FC0C0,
        (Action)0x004FC110
    };

    static void hud_itembox_2()
    {
        byte_691F76 = 1;
        _itemBoxRender[_st]();
        hud_render_itembox_items();
        hud_render_items();
        hud_render_weapon_amount();
        hud_render_weapon();
        hud_render_text_bg();
    }

    static void set_hud_hook(uint8_t kind, Action impl)
    {
        gHudImplTable[kind] = impl;
    }

    void hud_init_hooks()
    {
        set_hud_hook(3, &hud_itembox_1);
        set_hud_hook(4, &hud_itembox_2);
    }
}
