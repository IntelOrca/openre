#include "hud.h"
#include "audio.h"
#include "file.h"
#include "interop.hpp"
#include "item.h"
#include "itembox.h"
#include "openre.h"
#include "player.h"

using namespace openre::audio;
using namespace openre::file;
using namespace openre::itembox;
using namespace openre::player;

namespace openre::hud
{
    enum
    {
        ITEM_BOX_STATE_SELECT_INVENTORY,
        ITEM_BOX_STATE_SELECT_BOX,
        ITEM_BOX_STATE_SCROLL_UP,
        ITEM_BOX_STATE_SCROLL_DOWN,
        ITEM_BOX_STATE_EXCHANGE,
        ITEM_BOX_STATE_NOT_ENOUGH_SPACE,
    };

    enum
    {
        EXCHANGE_INVENTORY_TO_BOX_SINGLE,
        EXCHANGE_BOX_TO_INVENTORY_WIDE,
        EXCHANGE_INVENTORY_TO_BOX_WIDE_SINGLE,
        EXCHANGE_INVENTORY_TO_BOX_WIDE,
    };

    enum
    {
        INVENTORY_ITEM_GRID,
        INVENTORY_ITEM_COMMAND_BOX,
        INVENTORY_ITEM_COMMAND_BOX_SELECT,
        INVENTORY_ITEM_SHOW_COMMAND_BOX,
        INVENTORY_ITEM_HIDE_COMMAND_BOX,
    };

    using Action = void (*)();

    static Action* gHudImplTable = (Action*)0x5402C0;
    static HudInfo*& gHudInfoPtr = *((HudInfo**)0x53DB70);
    static HudInfo& gHudInfo = *((HudInfo*)0x691F60);
    static uint8_t* _bgBuffer = (uint8_t*)0x8C1880;

    constexpr int8_t INVENTORY_IS_FULL = -1;

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
        auto tileIdx = gGameTable.byte_9888D8;
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

    // 0x004FFAB0
    static void hud_render_inventory_ecg(int playerLife)
    {
        interop::call<void, int>(0x004FFAB0, playerLife);
    }

    // 0x004FEA30
    static void hud_render_inventory_player_face()
    {
        interop::call(0x004FEA30);
    }

    // 0x004FEF70
    static void hud_render_inventory_topbar()
    {
        interop::call(0x004FEF70);
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

    // 0x00502720
    static void sub_502720(int a0)
    {
        using sig = void (*)(int);
        auto p = (sig)0x00502720;
        p(a0);
    }

    /**
     * 0x00502620
     *
     * If type == ITEM_TYPE_NONE, returns the index of the first empty inventory slot or INVENTORY_IS_FULL if the inventory is
     * full. If type != ITEM_TYPE_NONE, returns the number of empty inventory slots.
     */
    static int8_t search_item(ItemType type)
    {
        if (type == ITEM_TYPE_NONE)
        {
            for (int i = 0; i < gGameTable.inventory_size; i++)
            {
                if (gGameTable.inventory[i].Type == ITEM_TYPE_NONE)
                {
                    return i;
                }
            }
            return INVENTORY_IS_FULL;
        }

        auto emptySlotCount = 0;
        for (int i = 0; i < gGameTable.inventory_size; i++)
        {
            if (gGameTable.inventory[i].Type == ITEM_TYPE_NONE)
            {
                emptySlotCount++;
            }
        }
        return emptySlotCount;
    }

    // 0x00502690
    static void sort_inventory()
    {
        auto freeSlotId = search_item(ITEM_TYPE_NONE);
        if (freeSlotId == INVENTORY_IS_FULL || freeSlotId + 1 == 10)
        {
            return;
        }

        if (freeSlotId + 1 == 9)
        {
            gGameTable.inventory[10].Type = 0;
            gGameTable.inventory[10].Quantity = 0;
            gGameTable.inventory[10].Part = 0;
            return;
        }

        auto index = freeSlotId + 1;
        do
        {
            gGameTable.inventory[index - 1].Type = gGameTable.inventory[index].Type;
            gGameTable.inventory[index - 1].Quantity = gGameTable.inventory[index].Quantity;
            gGameTable.inventory[index - 1].Part = gGameTable.inventory[index].Part;

            if (gGameTable.byte_691F68 == index)
            {
                gGameTable.byte_691F68--;
            }
            if (gGameTable.byte_692D64 == index)
            {
                gGameTable.byte_692D64--;
            }
            index++;
        } while (index < gGameTable.inventory_size);

        gGameTable.inventory[index - 1].Type = 0;
        gGameTable.inventory[index - 1].Quantity = 0;
        gGameTable.inventory[index - 1].Part = 0;
    }

    // 0x004FC5B0
    static void exchange_item()
    {
        auto* inventorySlot = &gGameTable.inventory[gGameTable.inventory_cursor];
        const auto boxSlotId = (gGameTable.itembox_slot_id + 2) & 0x3f;
        auto& boxSlot = gGameTable.itembox[boxSlotId];

        auto numParts = inventorySlot->Part != 0 ? 2 : 0;
        if (boxSlot.Part)
        {
            numParts++;
        }

        // Moving stackable item from itembox to inventory
        if (boxSlot.Type >= ITEM_TYPE_AMMO_HANDGUN && boxSlot.Type < ITEM_TYPE_HANDGUN_PARTS)
        {
            if (boxSlot.Type == inventorySlot->Type)
            {
                const auto max = gGameTable.item_def_tbl[boxSlot.Type].max;

                if (inventorySlot->Quantity == max)
                {
                    const auto aux = inventorySlot->Quantity;
                    inventorySlot->Quantity = boxSlot.Quantity;
                    boxSlot.Quantity = aux;
                    return;
                }

                const auto v6 = boxSlot.Quantity + inventorySlot->Quantity;
                if (v6 > max)
                {
                    inventorySlot->Quantity = max;
                    boxSlot.Quantity = v6 - max;
                    return;
                }

                inventorySlot->Quantity += boxSlot.Quantity;
                boxSlot = ItemboxItem{};
                return;
            }
        }

        // Placing itembox item into an empty inventory slot
        if (inventorySlot->Type == ITEM_TYPE_NONE)
        {
            // Check first if item can be stacked, otherwise use first empty slot (switch case)
            for (int i = 0; i < gGameTable.inventory_size; i++)
            {
                auto& slot = gGameTable.inventory[i];
                const auto max = gGameTable.item_def_tbl[slot.Type].max;
                if (boxSlot.Type == slot.Type && slot.Quantity <= max - boxSlot.Quantity)
                {
                    slot.Quantity += boxSlot.Quantity;
                    boxSlot = ItemboxItem{};
                    return;
                }
            }
        }

        switch (numParts)
        {
        case EXCHANGE_INVENTORY_TO_BOX_SINGLE:
        {
            if (gGameTable.byte_691F68 == gGameTable.inventory_cursor)
            {
                gGameTable.byte_691F68 = 0x80;
                gGameTable.byte_691F6A = 0;
            }
            const auto item = static_cast<uint8_t>(search_item(ITEM_TYPE_NONE));
            if (item < gGameTable.inventory_cursor)
            {
                inventorySlot = &gGameTable.inventory[item];
            }

            const auto boxSlotType = inventorySlot->Type;
            const auto boxSlotQuantity = inventorySlot->Quantity;
            const auto boxSlotPart = inventorySlot->Part;
            inventorySlot->Type = boxSlot.Type;
            inventorySlot->Quantity = boxSlot.Quantity;
            inventorySlot->Part = boxSlot.Part;
            boxSlot.Type = boxSlotType;
            boxSlot.Quantity = boxSlotQuantity;
            boxSlot.Part = boxSlotPart;
            sort_inventory();
            return;
        }
        case EXCHANGE_BOX_TO_INVENTORY_WIDE:
        {
            const auto inventorySlotId = static_cast<int8_t>(search_item(ITEM_TYPE_NONE));
            if (inventorySlotId == INVENTORY_IS_FULL
                || (inventorySlotId + 1 >= gGameTable.inventory_size && inventorySlot->Type == ITEM_TYPE_NONE))
            {
                show_message(11468816, 0xe400, 8, 0);
                gGameTable.itembox_state = ITEM_BOX_STATE_NOT_ENOUGH_SPACE;
                return;
            }
            if (gGameTable.byte_691F68 == gGameTable.inventory_cursor)
            {
                gGameTable.byte_691F68 = 0x80;
                gGameTable.byte_691F6A = 0;
            }
            const auto boxSlotType = inventorySlot->Type;
            const auto boxSlotQuantity = inventorySlot->Quantity;
            const auto boxSlotPart = inventorySlot->Part;
            inventorySlot->Type = ITEM_TYPE_NONE;
            sort_inventory();
            sub_502720(1);
            gGameTable.inventory[0].Type = boxSlot.Type;
            gGameTable.inventory[0].Quantity = boxSlot.Quantity;
            gGameTable.inventory[0].Part = 1;
            gGameTable.inventory[1].Type = boxSlot.Type;
            gGameTable.inventory[1].Quantity = boxSlot.Quantity;
            gGameTable.inventory[1].Part = 2;
            boxSlot.Type = boxSlotType;
            boxSlot.Quantity = boxSlotQuantity;
            boxSlot.Part = boxSlotPart;
            return;
        }
        case EXCHANGE_INVENTORY_TO_BOX_WIDE_SINGLE:
        {
            const auto newBoxSlotType = inventorySlot->Type;
            const auto newBoxSlotQuantity = inventorySlot->Quantity;

            if (inventorySlot->Part == 2)
            {
                if (gGameTable.byte_691F68 == gGameTable.inventory_cursor
                    || gGameTable.byte_691F68 == gGameTable.inventory_cursor - 1)
                {
                    gGameTable.byte_691F68 = 0x80;
                    gGameTable.byte_691F6A = 0;
                }
                inventorySlot->Type = ITEM_TYPE_NONE;
                sort_inventory();
                auto* itemTwork = &gGameTable.item_twork;
                if (gGameTable.inventory_cursor > 0)
                {
                    itemTwork = &gGameTable.inventory[gGameTable.inventory_cursor - 1];
                }
                itemTwork->Type = ITEM_TYPE_NONE;
            }
            else
            {
                if (inventorySlot->Part != 1)
                {
                LABEL_41:
                    const auto inventorySlotId = static_cast<uint8_t>(search_item(ITEM_TYPE_NONE));
                    gGameTable.inventory[inventorySlotId].Type = boxSlot.Type;
                    gGameTable.inventory[inventorySlotId].Quantity = boxSlot.Quantity;
                    gGameTable.inventory[inventorySlotId].Part = boxSlot.Part;
                    boxSlot.Part = 3;
                    boxSlot.Type = newBoxSlotType;
                    boxSlot.Quantity = newBoxSlotQuantity;
                    break;
                }
                if (gGameTable.byte_691F68 == gGameTable.inventory_cursor
                    || gGameTable.byte_691F68 == gGameTable.inventory_cursor + 1)
                {
                    gGameTable.byte_691F68 = 0x80;
                    gGameTable.byte_691F6A = 0;
                }
                inventorySlot->Type = ITEM_TYPE_NONE;
                sort_inventory();
                inventorySlot->Type = ITEM_TYPE_NONE;
            }
            sort_inventory();
            goto LABEL_41;
        }
        case EXCHANGE_INVENTORY_TO_BOX_WIDE:
        {
            ItemType newBoxSlotType = ITEM_TYPE_NONE;
            uint8_t newBoxSlotQuantity = 0;
            if (inventorySlot->Part == 1)
            {
                if (gGameTable.byte_691F68 == gGameTable.inventory_cursor
                    || gGameTable.byte_691F68 == gGameTable.inventory_cursor + 1)
                {
                    gGameTable.byte_691F68 = 0x80;
                    gGameTable.byte_691F6A = 0;
                }

                newBoxSlotType = inventorySlot->Type;
                newBoxSlotQuantity = inventorySlot->Quantity;
                // First part
                inventorySlot->Type = boxSlot.Type;
                inventorySlot->Quantity = boxSlot.Quantity;
                inventorySlot->Part = 1;
                // Second part
                inventorySlot++;
                inventorySlot->Type = boxSlot.Type;
                inventorySlot->Quantity = boxSlot.Quantity;
                inventorySlot->Part = 2;
            }
            else
            {
                if (gGameTable.byte_691F68 == gGameTable.inventory_cursor
                    || gGameTable.byte_691F68 == gGameTable.inventory_cursor - 1)
                {
                    gGameTable.byte_691F68 = 0x80;
                    gGameTable.byte_691F6A = 0;
                }

                auto* itemTwork = &gGameTable.item_twork;
                if (gGameTable.inventory_cursor > 0)
                {
                    itemTwork = &gGameTable.inventory[gGameTable.inventory_cursor - 1];
                }

                newBoxSlotType = inventorySlot->Type;
                newBoxSlotQuantity = inventorySlot->Quantity;
                itemTwork->Type = boxSlot.Type;
                inventorySlot->Type = boxSlot.Type;
                inventorySlot->Part = 2;
                itemTwork->Quantity = boxSlot.Quantity;
                itemTwork->Part = 1;
            }

            inventorySlot->Quantity = boxSlot.Quantity;
            boxSlot.Part = 3;
            boxSlot.Type = newBoxSlotType;
            boxSlot.Quantity = newBoxSlotQuantity;
            break;
        }
        }
    }

    // 0x004FC0C0
    static void hud_select_menu_m()
    {
        if ((gGameTable.word_9885FC & 0x4000) != 0)
        {
            gGameTable._st++;
            gGameTable.inventory_cursor = 1;
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
                auto selection = gGameTable.inventory_cursor;
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
                    gGameTable.inventory_cursor = selection;
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
        case ITEM_BOX_STATE_NOT_ENOUGH_SPACE:
        {
            gGameTable.byte_691F76 = 0;
            if (gGameTable.fg_message >= 0)
            {
                gGameTable.itembox_state = ITEM_BOX_STATE_SELECT_BOX;
            }
            hud_render_selection(gGameTable.inventory[gGameTable.inventory_cursor].Part);
            return;
        }
        }

        const auto cursor = gGameTable.inventory_cursor;
        const auto& item = gGameTable.inventory[cursor];
        hud_render_selection(item.Part);
        hud_render_inventory_text(16, 175, 6, item.Type);
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

    // 0x00502590
    static uint8_t hud_check_item_mix()
    {
        auto redCursor = gGameTable.inventory_cursor;
        auto greenCursor = gGameTable.inventory_cursor_2;
        auto type = gGameTable.inventory[redCursor].Type;

        if (redCursor == greenCursor)
        {
            return 0;
        }
        if (gGameTable.inventory[greenCursor].Type == ITEM_TYPE_NONE)
        {
            return 0;
        }
        auto itemDef = gGameTable.item_def_tbl[type];
        if (itemDef.var_03 == 0)
        {
            return 0;
        }

        auto counter = 0;
        while (itemDef.mix->object_item_id != gGameTable.inventory[greenCursor].Type)
        {
            if (++counter >= itemDef.var_03)
            {
                return 0;
            }
            itemDef.mix++;
        }

        gGameTable.byte_691F86 = itemDef.mix->result_item;
        gGameTable.byte_691F87 = itemDef.mix->mixed_pix_no;
        return itemDef.mix->mix_type;
    }

    // 0x004FF1C0
    static void st_disp_menu1(int a0)
    {
        interop::call<void, int>(0x004FF1C0, a0);
    }

    // 0x004F88B0
    static void hud_inventory_mix_item()
    {
        interop::call(0x004F88B0);
    }

    // 0x004F9260
    static void hud_inventory_inspect_item()
    {
        interop::call(0x004F9260);
    }

    // 0x004F8620
    static void hud_inventory_equip_item()
    {
        interop::call(0x004F8620);
    }

    // 0x004FF060
    static void st_init_disp_menu1(uint8_t a0, uint8_t a1)
    {
        interop::call<void, uint8_t, uint8_t>(0x004FF060, a0, a1);
    }

    // 0x004F8160
    static void hud_inventory_item()
    {
        auto& cursor = gGameTable.inventory_cursor;
        auto& inventory = gGameTable.inventory;

        switch (gGameTable.itembox_state)
        {
        case INVENTORY_ITEM_GRID:
        {
            const auto prevCursor = cursor;
            int v1;
            gGameTable.byte_691F76 = 1;

            if (gGameTable.fg_system & 0x80000000)
            {
                if (gGameTable.word_9885FC & 0x2000 && cursor < gGameTable.inventory_size - 1)
                {
                    if (inventory[cursor].Part == 1 && cursor < gGameTable.inventory_size - 2)
                    {
                        cursor += 2;
                        v1 = cursor;
                    }
                    else
                    {
                        v1 = ++cursor;
                    }
                }
                else
                {
                    v1 = cursor;
                }
                if (gGameTable.word_9885FC & 0x8000 && v1 && v1 != 10)
                {
                    if (inventory[cursor].Part == 2)
                    {
                        v1 = v1 == 1 ? 2 : v1 - 1;
                    }
                    cursor = --v1;
                }
                if (gGameTable.word_9885FC & 0x4000)
                {
                    if (cursor >= gGameTable.inventory_size - 2)
                    {
                        if (v1 == 10)
                        {
                            v1 = 1;
                            cursor = v1;
                        }
                    }
                    else
                    {
                        v1 += 2;
                        cursor = v1;
                    }
                }
                if (gGameTable.word_9885FC & 0x1000)
                {
                    if (v1 == 10)
                    {
                        gGameTable._st = 2;
                        snd_se_on(0x4050000);
                    }
                    else
                    {
                        if (v1 <= 1)
                        {
                            cursor = 10;
                        }
                        else
                        {
                            cursor = v1 - 2;
                        }
                    }
                }
                if (prevCursor != cursor)
                {
                    snd_se_on(0x4040000);
                }
            }
            if (gGameTable.key_trg & 0x1000 && inventory[cursor].Type)
            {
                gGameTable.dword_689DF4 &= 0xFFFFFF00;
                auto type = inventory[cursor].Type;
                if (type >= ITEM_TYPE_AMMO_HANDGUN)
                {
                    st_init_disp_menu1(0, 0);
                }
                else if (type == ITEM_TYPE_CUSTOM_HANDGUN)
                {
                    auto state = check_flag(FlagGroup::Common, 0x7E);
                    st_init_disp_menu1(state ? 3 : 1, 1);
                    gGameTable.dword_689DF4 = (gGameTable.dword_689DF4 & 0xFFFFFF00) | 1;
                }
                else
                {
                    st_init_disp_menu1(1, 0);
                }
                snd_se_on(0x4060000);
                gGameTable.word_691FA8 += 65;
                gGameTable.itembox_state = INVENTORY_ITEM_SHOW_COMMAND_BOX;
                gGameTable.byte_691F63 = 0;
            }
            else if (gGameTable.key_trg & 0x2000)
            {
                snd_se_on(0x4050000);
                gGameTable._st = 2;
            }
            else if (gGameTable.dword_9885FE & 0x80000000 && gGameTable.byte_691F76 == 1)
            {
                snd_se_on(0x4050000);
                gGameTable._st = 0;
            }

            hud_render_inventory_text(16, 175, 2, inventory[cursor].Type);
            break;
        }
        case INVENTORY_ITEM_COMMAND_BOX:
        {
            gGameTable.byte_691F76 = 1;
            if (gGameTable.fg_system & 0x80000000)
            {
                if (gGameTable.dword_689DF4)
                {
                    if (gGameTable.word_9885FC & 0x4000)
                    {
                        if (gGameTable.byte_691F6F)
                        {
                            gGameTable.byte_691F6F--;
                        }
                        else
                        {
                            gGameTable.byte_691F6F = 3;
                        }
                    }
                    if (!(gGameTable.word_9885FC & 0x1000))
                    {
                        goto LABEL_63;
                    }
                    if (gGameTable.byte_691F6F == 3)
                    {
                        gGameTable.byte_691F6F = 0;
                        goto LABEL_63;
                    }
                }
                else
                {
                    if (gGameTable.word_9885FC & 0x4000)
                    {
                        if (gGameTable.byte_691F6F == 1)
                        {
                            gGameTable.byte_691F6F = 3;
                        }
                        else
                        {
                            gGameTable.byte_691F6F--;
                        }
                    }
                    if (!(gGameTable.word_9885FC & 0x1000))
                    {
                        goto LABEL_63;
                    }
                    if (gGameTable.byte_691F6F == 3)
                    {
                        gGameTable.byte_691F6F = 1;
                        goto LABEL_63;
                    }
                }
                gGameTable.byte_691F6F++;
            LABEL_63:
                if (gGameTable.word_9885FC & 0x5000)
                {
                    snd_se_on(0x4040000);
                }
            }
            if (gGameTable.key_trg & 0x1000)
            {
                gGameTable.itembox_state = INVENTORY_ITEM_COMMAND_BOX_SELECT;
                gGameTable.byte_691F63 = gGameTable.byte_691F6F;
                gGameTable.byte_691F64 = 0;
                gGameTable.inventory_cursor_2 = cursor;
                snd_se_on(0x4060000);
            }
            else if (gGameTable.key_trg & 0x2000)
            {
                gGameTable.itembox_state = INVENTORY_ITEM_HIDE_COMMAND_BOX;
                gGameTable.byte_691F63 = 0;
                snd_se_on(0x4050000);
            }
            st_disp_menu1(gGameTable.dword_689DF4);
            break;
        }
        case INVENTORY_ITEM_COMMAND_BOX_SELECT:
        {
            st_disp_menu1(gGameTable.dword_689DF4);
            switch (gGameTable.byte_691F63)
            {
            case 0:
            {
                auto state = check_flag(FlagGroup::Common, 0x7E);
                set_flag(FlagGroup::Common, 0x7E, !state);
                st_init_disp_menu1(state ? 1 : 3, 1);
                gGameTable.byte_691F6F = 0;
                gGameTable.itembox_state = INVENTORY_ITEM_COMMAND_BOX;
                gGameTable.byte_691F63 = 0;
                break;
            }
            case 1:
            {
                hud_inventory_mix_item();
                break;
            }
            case 2:
            {
                hud_inventory_inspect_item();
                break;
            }
            case 3:
            {
                hud_inventory_equip_item();
                break;
            }
            }
            break;
        }
        case INVENTORY_ITEM_SHOW_COMMAND_BOX:
        {
            if (!(gGameTable.byte_691F63++ <= 3))
            {
                gGameTable.itembox_state = INVENTORY_ITEM_COMMAND_BOX;
                gGameTable.byte_691F63 = 0;
            }
            gGameTable.word_691FA8 -= 13;
            st_disp_menu1(gGameTable.dword_689DF4);
            break;
        }
        case INVENTORY_ITEM_HIDE_COMMAND_BOX:
        {
            if (!(gGameTable.byte_691F63++ <= 3))
            {
                gGameTable.itembox_state = INVENTORY_ITEM_GRID;
                gGameTable.byte_691F63 = 0;
            }
            gGameTable.word_691FA8 += 13;
            st_disp_menu1(gGameTable.dword_689DF4);
            break;
        }
        }

        if (!gGameTable.ctcb->var_13)
        {
            hud_render_selection(inventory[cursor].Part);
        }
    }

    static const Action _inventoryRender[6] = {
        (Action)0x00502130, // st_fade_out_set
        (Action)0x00502150, // st_fade_out_wait
        (Action)0x004F8050, // hud_inventory_topbar_hover
        hud_inventory_item,
        (Action)0x004FA240, // st_menu0_map
        (Action)0x004F95B0  // st_menu0_file
    };

    // 0x004F8000
    static void hud_render_inventory()
    {
        _inventoryRender[gGameTable._st]();
        if (!gGameTable.ctcb->var_13)
        {
            hud_render_items();
            hud_render_weapon_amount();
            hud_render_weapon();
            auto playerLife = player_check_life();
            hud_render_inventory_ecg(playerLife);
            hud_render_inventory_player_face();
            hud_render_text_bg();
            hud_render_inventory_topbar();
        }
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
        interop::writeJmp(0x004FC5B0, &exchange_item);
        interop::writeJmp(0x00502590, &hud_check_item_mix);
        interop::writeJmp(0x004F8000, &hud_render_inventory);
        interop::writeJmp(0x00502620, &search_item);
        interop::writeJmp(0x00502690, &sort_inventory);
    }
}
