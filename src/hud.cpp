#include "hud.h"
#include "audio.h"
#include "file.h"
#include "interop.hpp"
#include "item.h"
#include "itembox.h"
#include "openre.h"

using namespace openre::audio;
using namespace openre::file;
using namespace openre::itembox;

namespace openre::hud
{
    enum
    {
        ITEM_BOX_STATE_SELECT_INVENTORY,
        ITEM_BOX_STATE_SELECT_BOX,
        ITEM_BOX_STATE_SCROLL_UP,
        ITEM_BOX_STATE_SCROLL_DOWN,
        ITEM_BOX_STATE_EXCHANGE,
        ITEM_BOX_STATE_5,
    };

    using Action = void (*)();

    static Action* gHudImplTable = (Action*)0x5402C0;
    static HudInfo*& gHudInfoPtr = *((HudInfo**)0x53DB70);
    static HudInfo& gHudInfo = *((HudInfo*)0x691F60);
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

    // 0x004FCA30
    static void hud_init_itembox()
    {
        using sig = void (*)();
        auto p = (sig)0x004FCA30;
        p();
    }

    // 0x004C49C0
    void hud_fade_set(short a0, short add, char mask, char pri)
    {
        auto fadeIdx = a0 & 0xFF;
        auto fade = gGameTable.fade_table[fadeIdx];
        fade.pri = pri;
        fade.hrate = (a0 >> 8) & 0xFF;
        fade.add = add;
        fade.psxRect = { 0, 0, 1572, 8960 };
        fade.mask_r = -((mask & 4) != 0);
        fade.mask_g = -((mask & 2) != 0);
        fade.mask_b = -((mask & 1) != 0);
        if (add < 0)
        {
            fade.kido = add + 0x8000;
        }
        else
        {
            fade.kido = 0;
        }
        gGameTable.fade_table[fadeIdx] = fade;
    }

    // 0x004C4A50
    void hud_fade_adjust(int no, int16_t kido, uint32_t rgb, PsxRect* rect)
    {
        auto fade = gGameTable.fade_table[no];
        fade.kido = kido;

        uint32_t rgbc = (fade.tiles[0].code << 24) | rgb;
        auto tileIdx = static_cast<uint8_t>(gGameTable.dword_9888D8);
        fade.tiles[tileIdx].r = (rgbc >> 24 & 0xff);
        fade.tiles[tileIdx].g = (rgbc >> 16 & 0xff);
        fade.tiles[tileIdx].b = (rgbc >> 8 & 0xff);
        fade.tiles[tileIdx].code = (rgbc & 0xff);

        if (rect != nullptr)
        {
            fade.psxRect = *rect;
        }
        else
        {
            fade.psxRect = PsxRect{ 0, 0, 1572, 8960 };
        }
        gGameTable.fade_table[no] = fade;
    }

    // 0x004D0EC0
    void hud_fade_adjust2(int no, int16_t kido, uint32_t rgb, PsxRect* rect)
    {
        auto fade = gGameTable.fade_table[no];
        fade.kido = kido;
        int rgbc = (gGameTable.byte_98F98F[no] << 24) | rgb;

        for (int i = 0; i < 2; i++)
        {
            fade.tiles[i].r = (rgbc >> 24 & 0xff);
            fade.tiles[i].g = (rgbc >> 16 & 0xff);
            fade.tiles[i].b = (rgbc >> 8 & 0xff);
            fade.tiles[i].code = (rgbc & 0xff);

            if (rect != nullptr)
            {
                fade.tiles[i].psxRect = *rect;
            }
            else
            {
                fade.tiles[i].psxRect = PsxRect{ 0, 0, 1572, 8960 };
            }
        }
        gGameTable.fade_table[no] = fade;
    }

    // 0x004C4AD0
    bool hud_fade_status(int no)
    {
        return gGameTable.fade_table[no].kido < 0;
    }

    // 0x004C4AB0
    void hud_fade_off(int no)
    {
        gGameTable.fade_table[no].kido = -1;
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

    // 0x004FCBC0
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
            gGameTable.byte_53DB74 = (uint8_t*)0x009864E0;
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
            hud_init_itembox();

            gGameTable.word_692FBE = 1;
            gGameTable.word_692FC0 = 0;
            gGameTable.dword_689DF8 = 5;
            for (auto i = 5; i > 0; i--)
            {
                gGameTable.dword_689DF8--;
            }
            hud_fade_set(512, -6144, 7, 1);
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
            hud_render_inventory_text(16, 175, 2, gGameTable.byte_53DB74[gHudInfoPtr->var_0C * 4 + 0x8854]);
            hud_render_selection(gGameTable.byte_53DB74[gHudInfoPtr->var_0C * 4 + 0x8856]);
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

    // 0x004FC5B0
    static void exchange_item()
    {
        using sig = void (*)();
        auto p = (sig)0x004FC5B0;
        p();
    }

    // 0x004FC0C0
    static void hud_select_menu_m()
    {
        if ((gGameTable.word_9885FC & 0x4000) != 0)
        {
            gGameTable._st++;
            gGameTable.byte_691F6C = 1;
            snd_se_on(0x4040000);
        }
        else if ((gGameTable.key_trg & 0x3000) != 0)
        {
            snd_se_on(0x4060000);
            gGameTable._st = 0;
        }
    }

    // 0x004FC110
    static void hud_select_item_m()
    {
        switch (gGameTable.itembox_state)
        {
        case ITEM_BOX_STATE_SELECT_INVENTORY:
        {
            if (gGameTable.fg_system & 0x80000000)
            {
                auto selection = gGameTable.byte_691F6C;
                auto prevSelection = selection;
                auto inventorySize = gGameTable.inventory_size;
                if (gGameTable.word_9885FC & 0x2000) // right
                {
                    if (selection < inventorySize - 1)
                    {
                        const auto& item = gGameTable.inventory[1 + selection];
                        if (item.Part == 1)
                        {
                            selection++;
                        }
                        selection++;
                    }
                }
                if (gGameTable.word_9885FC & 0x8000) // left
                {
                    if (selection > 0 && selection != 10)
                    {
                        const auto& item = gGameTable.inventory[1 + selection];
                        if (item.Part == 2)
                        {
                            if (selection == 1)
                                selection = 2;
                            else
                                selection--;
                        }
                        selection--;
                    }
                }
                if (gGameTable.word_9885FC & 0x4000) // down
                {
                    if (selection >= inventorySize - 2)
                    {
                        if (selection == 10)
                        {
                            selection = 1;
                        }
                    }
                    else
                    {
                        selection += 2;
                    }
                }
                if (gGameTable.word_9885FC & 0x1000) // up
                {
                    if (selection <= 1 || selection == 10)
                    {
                        prevSelection = 10;
                        gGameTable._st--;
                    }
                    else
                    {
                        selection -= 2;
                    }
                }
                if (prevSelection != selection)
                {
                    gGameTable.byte_691F6C = selection;
                    snd_se_on(0x4040000);
                }
            }
            if (gGameTable.key_trg & 0x1000)
            {
                // Action
                snd_se_on(0x4060000);
                gGameTable.itembox_state = ITEM_BOX_STATE_SELECT_BOX;
            }
            else if (gGameTable.key_trg & 0x2000)
            {
                // Cancel
                snd_se_on(0x4050000);
                gGameTable._st = 0;
            }
            else if (gGameTable.dword_9885FE & 0x0800)
            {
                if (gGameTable.byte_691F76 == 1)
                {
                    snd_se_on(0x4050000);
                    gGameTable._st = 1;
                }
            }

            // NEW! sort item box
            else if (gGameTable.key_trg & 0x100)
            {
                // Aim
                sort_itembox();
                snd_se_on(0x4060000);
            }
            break;
        }
        case ITEM_BOX_STATE_SELECT_BOX:
        {
            if (!(gGameTable.fg_system & 0x80000000))
            {
                if (!(gGameTable.key_trg & 0x2000))
                {
                    if (gGameTable.key_trg & 0x1000)
                    {
                        snd_se_on(0x4060000);
                        gGameTable.itembox_state = ITEM_BOX_STATE_EXCHANGE;
                    }
                    break;
                }
                snd_se_on(0x4050000);
                gGameTable.itembox_state = ITEM_BOX_STATE_SELECT_INVENTORY;
                break;
            }
            if (gGameTable.word_9885FC & 0x1000) // up
            {
                snd_se_on(0x2140000);
                gGameTable.itembox_state = ITEM_BOX_STATE_SCROLL_UP;
                break;
            }
            if (gGameTable.word_9885FC & 0x4000) // down
            {
                snd_se_on(0x2140000);
                gGameTable.itembox_state = ITEM_BOX_STATE_SCROLL_DOWN;
                break;
            }
            if (gGameTable.word_9885FC & 4)
            {
                if (!(gGameTable.key_trg & 0x2000))
                {
                    snd_se_on(0x4060000);
                    gGameTable.itembox_slot_id = (gGameTable.itembox_slot_id - 5) & 0x3F;
                    const auto& type = gGameTable.itembox[(gGameTable.itembox_slot_id - 1) & 0x3F].Type;
                    hud_render_inventory_text(
                        gGameTable.word_691FB0 + 7,
                        gGameTable.word_691FB0 + gGameTable.byte_691F85 + 9,
                        6,
                        type != ITEM_TYPE_NONE ? type : 100);
                    break;
                }
                snd_se_on(0x4050000);
                gGameTable.itembox_state = ITEM_BOX_STATE_SELECT_INVENTORY;
                break;
            }
            if (!(gGameTable.word_9885FC & 8)) // inventory item selected
            {
                if (!(gGameTable.key_trg & 0x2000))
                {
                    if (gGameTable.key_trg & 0x1000)
                    {
                        snd_se_on(0x4060000);
                        gGameTable.itembox_state = ITEM_BOX_STATE_EXCHANGE;
                    }
                    break;
                }
                snd_se_on(0x4050000);
                gGameTable.itembox_state = ITEM_BOX_STATE_SELECT_INVENTORY;
                break;
            }

            snd_se_on(0x4060000);
            gGameTable.itembox_slot_id = (gGameTable.itembox_slot_id + 5) & 0x3F;
            break;
        }
        case ITEM_BOX_STATE_SCROLL_UP:
        {
            gGameTable.byte_691F76 = 0;
            if ((gGameTable.byte_691F63++) <= 5)
            {
                gGameTable.byte_691F85 += 3;
            }
            else
            {
                if (gGameTable.word_9885FC & 0x1000)
                {
                    snd_se_on(0x2140000);
                }
                else
                {
                    gGameTable.itembox_state = ITEM_BOX_STATE_SELECT_BOX;
                }
                gGameTable.byte_691F85 = 0;
                gGameTable.byte_691F63 = 0;
                gGameTable.itembox_slot_id = (gGameTable.itembox_slot_id - 1) & 0x3F;
            }
            const auto& item = gGameTable.itembox[(gGameTable.itembox_slot_id - 1) & 0x3F];
            auto text = item.Type == ITEM_TYPE_NONE ? 100 : item.Type;
            hud_render_inventory_text(gGameTable.word_691FB0 + 7, gGameTable.word_691FB2 + gGameTable.byte_691F85 + 9, 6, text);
            break;
        }
        case ITEM_BOX_STATE_SCROLL_DOWN:
        {
            gGameTable.byte_691F76 = 0;
            if (gGameTable.byte_691F63++ <= 5)
            {
                gGameTable.byte_691F85 -= 3;
            }
            else
            {
                if (gGameTable.word_9885FC & 0x4000)
                {
                    snd_se_on(0x2140000);
                }
                else
                {
                    gGameTable.itembox_state = ITEM_BOX_STATE_SELECT_BOX;
                }
                gGameTable.byte_691F85 = 0;
                gGameTable.byte_691F63 = 0;
                gGameTable.itembox_slot_id = (gGameTable.itembox_slot_id + 1) & 0x3F;
            }
            const auto& item = gGameTable.itembox[(gGameTable.itembox_slot_id + 5) & 0x3F];
            auto text = item.Type == ITEM_TYPE_NONE ? 100 : item.Type;
            hud_render_inventory_text(
                gGameTable.word_691FB0 + 7, gGameTable.word_691FB2 + gGameTable.byte_691F85 + 129, 6, text);
            break;
        }
        case ITEM_BOX_STATE_EXCHANGE:
        {
            gGameTable.byte_691F76 = 0;
            gGameTable.itembox_state = ITEM_BOX_STATE_SELECT_INVENTORY;
            exchange_item();
            break;
        }
        case ITEM_BOX_STATE_5:
        {
            gGameTable.byte_691F76 = 0;
            if (gGameTable.fg_message >= 0)
            {
                gGameTable.itembox_state = ITEM_BOX_STATE_SELECT_BOX;
            }
            hud_render_selection(gGameTable.inventory[gGameTable.byte_691F6C].Part);
            return;
        }
        }

        const auto& item = gGameTable.inventory[gGameTable.byte_691F6C];
        hud_render_inventory_text(16, 175, 6, item.Type);
        hud_render_selection(item.Part);
    }

    static const Action _itemBoxRender[] = {
        (Action)0x00502130,
        (Action)0x00502150,
        hud_select_menu_m,
        hud_select_item_m,
    };

    static void hud_itembox_2()
    {
        gGameTable.byte_691F76 = 1;
        _itemBoxRender[gGameTable._st]();
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

        interop::writeJmp(0x004C49C0, &hud_fade_set);
        interop::writeJmp(0x004C4A50, &hud_fade_adjust);
        interop::writeJmp(0x004D0EC0, &hud_fade_adjust2);
        interop::writeJmp(0x004C4AD0, &hud_fade_status);
        interop::writeJmp(0x004C4AB0, &hud_fade_off);
    }
}
