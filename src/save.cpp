#include "save.h"
#include "audio.h"
#include "error.h"
#include "file.h"
#include "hud.h"
#include "interop.hpp"
#include "item.h"
#include "openre.h"
#include "player.h"
#include "re2.h"
#include "scheduler.h"
#include "title.h"

#include <cstdlib>
#include <cstring>
#include <windows.h>

using namespace openre;
using namespace openre::audio;
using namespace openre::error;
using namespace openre::file;
using namespace openre::hud;
using namespace openre::player;
using namespace openre::title;

namespace openre::save
{
    // 0x004C6C40
    static void cardaccess_init()
    {
        auto& ctcb = *gGameTable.ctcb;

        std::memset(gGameTable.card_save_buf, 0, sizeof(gGameTable.card_save_buf));

        switch (ctcb.var_0A)
        {
        case 2: ctcb.var_0A = 0; return;
        case 1: break;
        default:
            hud_fade_set(0x103, 0, 7, 1);
            hud_fade_adjust(3, 0, 0, nullptr);
            std::memset(gGameTable.card_work_ptr, 0, 0x475 * sizeof(uint32_t));
            gGameTable.byte_98F07A = 2;
            if (!load_adt("common\\data\\type00.adt", gGameTable.bg_buffer, 4))
            {
                file_error();
                return;
            }
            bg_to_surface(gGameTable.bg_buffer);
            bg_set_mode(0, 0);
            hud_fade_set(512, -6144, 7, 1);
            ctcb.var_0A = 1;
            break;
        }

        if (!hud_fade_status(0))
        {
            task_sleep(1);
            return;
        }

        auto p = gGameTable.card_work_ptr;
        *(uint16_t*)(p + 0x18) = 0;
        *(uint16_t*)(p + 0x24) = 23;
        *(uint16_t*)(p + 0x26) = 50;
        *(uint16_t*)(p + 0x28) = 276;
        *(uint16_t*)(p + 0x2A) = 106;
        p[0x15] = 0;
        *(uint16_t*)(p + 0x1A) = 0;
        p[0x16] = 0;
        p[0x17] = 0;
        ctcb.var_0A = 2;
        task_sleep(2);
    }

    // 0x004C6D80
    static void cardaccess_exit(int mode)
    {
        auto& ctcb = *gGameTable.ctcb;

        switch (ctcb.var_0A)
        {
        case 2:
            ctcb.var_0A = 0;
            if (mode == 0 && !check_flag(FlagGroup::System, FG_SYSTEM_10))
            {
                gGameTable.byte_98F07A = 2;
                gGameTable.byte_98F07B = 2;
            }
            return;
        case 1: break;
        default:
            hud_fade_off(3);
            hud_fade_set(512, 6144, 7, 1);
            ctcb.var_0A = 1;
            break;
        }

        if (hud_fade_status(0))
        {
            hud_fade_set(512, 0, 7, 1);
            hud_fade_adjust(0, 0x7FFF, 0xFFFFFF, nullptr);
            ctcb.var_0A = 2;
        }
        task_sleep(1);
    }

    // 0x004C6E30
    static void card_mess_disp(int rno, int mode, uint32_t cursor, int errCode)
    {
        interop::call<void, int, int, uint32_t, int>(0x004C6E30, rno, mode, cursor, errCode);
    }

    // 0x00432840
    static void ck_480p()
    {
        interop::call(0x00432840);
    }

    // 0x00432070
    static void sub_432070(uint32_t* a1, uint32_t* a2)
    {
        interop::call<void, uint32_t*, uint32_t*>(0x00432070, a1, a2);
    }

    // 0x00431D10
    static int sub_431D10(int a1, int* a2, char a3)
    {
        return interop::call<int, int, int*, char>(0x00431D10, a1, a2, a3);
    }

    // 0x00509940
    static void sub_509940(char* a1)
    {
        interop::call<void, char*>(0x00509940, a1);
    }

    // 0x005099A0
    static void sub_5099A0(char* a1)
    {
        interop::call<void, char*>(0x005099A0, a1);
    }

    // 0x00509AF0
    static int sub_509AF0(const char* a1)
    {
        return interop::call<int, const char*>(0x00509AF0, a1);
    }

    // 0x00509B20
    static void sub_509B20(char* a1, const char* a2, int a3)
    {
        interop::call<void, char*, const char*, int>(0x00509B20, a1, a2, a3);
    }

    // 0x00509B80
    static void sub_509B80(char* a1, const char* a2, int a3, int a4)
    {
        interop::call<void, char*, const char*, int, int>(0x00509B80, a1, a2, a3, a4);
    }

    // 0x00509BE0
    static int sub_509BE0(const char* a1)
    {
        return interop::call<int, const char*>(0x00509BE0, a1);
    }

    // 0x00432110
    static int format_save_name0(char* str, int player, int saveCnt, int saveRoom, int extremeLv)
    {
        return interop::call<int, char*, int, int, int, int>(0x00432110, str, player, saveCnt, saveRoom, extremeLv);
    }

    // 0x00432380
    static int format_save_name1(char* str, int player, int saveCnt, int saveRoom, int extremeLv, int a6, int a7)
    {
        return interop::call<int, char*, int, int, int, int, int, int>(
            0x00432380, str, player, saveCnt, saveRoom, extremeLv, a6, a7);
    }

    // 0x00432620
    static int save_print_tbl(int string)
    {
        return interop::call<int, int>(0x00432620, string);
    }

    // 0x004C7830
    static void save_push()
    {
        interop::call(0x004C7830);
    }

    // 0x004C7980
    static void load_pop()
    {
        interop::call(0x004C7980);
    }

    // 0x00432670
    static int16_t get_menu_key()
    {
        return interop::call<int16_t>(0x00432670);
    }

    // Backs up the inventory and consumes one ink ribbon before writing a save.
    static void card_consume_ink_ribbon()
    {
        std::memcpy(gGameTable.inventory_bk, gGameTable.inventory, sizeof(gGameTable.inventory_bk));
        gGameTable.byte_689AA8 = (uint8_t)(gGameTable.word_98E9B4 >> 8);
        auto index = inventory_find_item(ITEM_TYPE_INK_RIBBON);
        auto quantity = (uint8_t)(gGameTable.inventory[index].Quantity - 1);
        gGameTable.inventory[index].Quantity = quantity;
        if (quantity == 0)
        {
            gGameTable.inventory[index].Type = ITEM_TYPE_NONE;
            gGameTable.inventory[index].Part = 0;
            sort_inventory();
        }
        gGameTable.word_98E9BC++;
        save_push();
    }

    // Determines the save file player icon id and extreme battle level for the current game.
    static int card_get_save_type(int& extremeLv)
    {
        uint8_t saveType = 0;
        if ((gGameTable.dword_98E9B0 & 0x4000) != 0)
        {
            switch (gGameTable.byte_98E9A6)
            {
            case 0: saveType = 6; break;
            case 1: saveType = 7; break;
            case 11: saveType = 9; break;
            case 14: saveType = 8; break;
            }
            extremeLv = (uint16_t)gGameTable.nExtremeLv;
        }
        else
        {
            if (check_flag(FlagGroup::Status, FG_STATUS_SCENARIO))
                saveType = (gGameTable.byte_98E9A6 & 1) + 2;
            else
                saveType = gGameTable.byte_98E9A6 & 1;
            extremeLv = 0;
        }
        return saveType;
    }

    // Fills the metadata of the save file header at p_card_save after a successful write.
    static void card_fill_save_header()
    {
        gGameTable.p_card_save[0] = 0;
        if ((gGameTable.dword_98E9B0 & 0x4000) != 0)
        {
            switch (gGameTable.byte_98E9A6)
            {
            case 0: gGameTable.p_card_save[261] = 6; break;
            case 1: gGameTable.p_card_save[261] = 7; break;
            case 11: gGameTable.p_card_save[261] = 9; break;
            case 14: gGameTable.p_card_save[261] = 8; break;
            }
            gGameTable.p_card_save[275] = 0;
        }
        else
        {
            auto type = (uint8_t)(gGameTable.byte_98E9A6 & 1);
            if (check_flag(FlagGroup::Status, FG_STATUS_SCENARIO))
                type += 2;
            gGameTable.p_card_save[261] = type;
            gGameTable.p_card_save[275] = check_flag(FlagGroup::Status, FG_STATUS_EASY) ? 1 : 0;
        }
        gGameTable.p_card_save[262] = (uint8_t)gGameTable.word_98E9BC;
        gGameTable.p_card_save[263] = gGameTable.byte_98E9A7;
        *(uint32_t*)(gGameTable.p_card_save + 264) = 0;
        *(uint32_t*)(gGameTable.p_card_save + 268) = 0;
        gGameTable.p_card_save[272] = 0;
        gGameTable.p_card_save[273] = gGameTable.byte_98EF2E;
        gGameTable.p_card_save[274] = (uint8_t)gGameTable.nExtremeLv;
    }

    // Writes the save file to disk, rolling back the inventory and save count on failure.
    // Returns the next card state (successState, or 98 when the write failed).
    static int card_write_save_file(int successState)
    {
        if (check_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE))
        {
            gGameTable.byte_98EF2E = 1;
            set_flag(FlagGroup::Status, FG_STATUS_17, true);
        }
        else
        {
            gGameTable.byte_98EF2E = 0;
        }
        if ((int)file_write_save(gGameTable.save_path, &gGameTable.table_start, 0x800) <= 0)
        {
            if (gGameTable.card_write_result != 0)
            {
                gGameTable.word_98E9BC--;
                std::memcpy(gGameTable.inventory, gGameTable.inventory_bk, sizeof(gGameTable.inventory_bk));
                gGameTable.word_98E9B4 = (gGameTable.word_98E9B4 & 0x00FF) | ((uint16_t)gGameTable.byte_689AA8 << 8);
                gGameTable.card_write_result = 3;
                return 98;
            }
        }
        else
        {
            gGameTable.card_write_result = 0;
        }
        card_fill_save_header();
        gGameTable.card_mess_timer = 0;
        return successState;
    }

    enum CardState : uint8_t
    {
        CARD_STATE_INIT = 0,            // Initialize display and save folder
        CARD_STATE_ENUMERATE = 1,       // Enumerate save files in the current folder
        CARD_STATE_MENU = 2,            // Main save/load list navigation
        CARD_STATE_SAVE_NEW = 3,        // Write save data to a new file
        CARD_STATE_LOAD = 10,           // Read save data from the selected file
        CARD_STATE_SAVE_OPTIONS = 30,   // Overwrite/update/cancel sub menu
        CARD_STATE_SAVE_OVERWRITE = 31, // Write save data over the selected file
        CARD_STATE_TYPE_OVERWRITE = 32, // Typewriter name animation after overwriting
        CARD_STATE_WAIT_OVERWRITE = 33, // Pause once the overwrite animation finishes
        CARD_STATE_TYPE_NEW = 34,       // Typewriter name animation after a new save
        CARD_STATE_WAIT_NEW = 36,       // Pause once the new save animation finishes
        CARD_STATE_CONFIRM_EXIT = 96,   // "Data was not saved" yes/no prompt
        CARD_STATE_EXIT = 97,           // Leave the memory card screen
        CARD_STATE_ERROR = 98,          // Error message display
        CARD_STATE_CANCEL = 99,         // Cancel out of the current screen
    };

    // 0x004C58A0
    static void card_access()
    {
        auto& ctcb = *gGameTable.ctcb;
        auto& cardMode = ((uint8_t*)&gGameTable.card_state)[0];
        auto& cardState = ((uint8_t*)&gGameTable.card_state)[3];

        auto exitCardAccess = [&]() {
            rsrc_release();
            cardaccess_exit(cardMode);
            if (ctcb.var_13)
                ctcb.var_09 = 4;
        };

        switch (ctcb.var_09)
        {
        case 0:
            cardState = CARD_STATE_INIT;
            gGameTable.p_card_work = gGameTable.card_work;
            gGameTable.card_scroll = 0;
            gGameTable.card_cursor = 0;
            gGameTable.card_sub_cursor = 0;
            gGameTable.card_mess_timer = 0;
            gGameTable.card_name_index = 0;
            [[fallthrough]];
        default:
            cardMode = check_flag(FlagGroup::System, FG_SYSTEM_21);
            set_flag(FlagGroup::System, FG_SYSTEM_21, false);
            [[fallthrough]];
        case 3:
            cardaccess_init();
            if (ctcb.var_13)
            {
                ctcb.var_09 = 3;
                return;
            }
            gGameTable.p_card_work[23] = 1;
            gGameTable.p_card_work[28] = 0;
            gGameTable.p_card_work[30] = 0;
            break;
        case 1:
        case 2: break;
        case 4: exitCardAccess(); return;
        }

        int32_t messErr = 0;
        switch (cardState)
        {
        case CARD_STATE_INIT:
            ck_480p();
            font_create();
            sub_432070(&gGameTable.dword_663190, (uint32_t*)&gGameTable.pMem);
            strcpy(gGameTable.save_folder, GetSaveFolder());
            cardState = CARD_STATE_ENUMERATE;
            break;
        case CARD_STATE_ENUMERATE:
        {
            auto saveFolder = GetSaveFolder();
            auto plId = (uint8_t)SaveGetPlID(saveFolder, &gGameTable.cnt0, &gGameTable.cnt1);
            if (plId == 0xFF)
                return;
            save_list_files(GetSaveFolder(), gGameTable.cnt0, &gGameTable.Cards, gGameTable.cnt1, &gGameTable.Names);
            gGameTable.dword_986394 = 10;
            auto remaining = gGameTable.cnt0 - cardMode;
            if (remaining + gGameTable.cnt1 + 2 < 10)
                gGameTable.dword_986394 = remaining + gGameTable.cnt1 + 2;
            cardState = CARD_STATE_MENU;
            break;
        }
        case CARD_STATE_MENU:
            if (gGameTable.card_mess_timer != 0)
            {
                gGameTable.card_mess_timer--;
                break;
            }
            switch (get_menu_key())
            {
            case VK_PRIOR:
                gGameTable.card_scroll += gGameTable.dword_986394 / 2;
                {
                    auto maxScroll = gGameTable.cnt0 - cardMode - gGameTable.dword_986394;
                    if (gGameTable.card_scroll >= maxScroll + gGameTable.cnt1 + 2)
                        gGameTable.card_scroll = maxScroll + gGameTable.cnt1 + 2;
                }
                gGameTable.card_cursor = 0;
                break;
            case VK_NEXT:
                gGameTable.card_scroll += gGameTable.dword_986394 / -2;
                if (gGameTable.card_scroll < 0)
                    gGameTable.card_scroll = 0;
                gGameTable.card_cursor = gGameTable.dword_986394 - 1;
                break;
            case VK_END:
                gGameTable.card_fade = 255;
                messErr = 1;
                break;
            case VK_HOME:
                gGameTable.card_fade = 0;
                messErr = 1;
                break;
            default:
                if ((gGameTable.word_9885FC & 0x4000) != 0)
                {
                    if (++gGameTable.card_cursor >= gGameTable.dword_986394)
                    {
                        gGameTable.card_cursor = gGameTable.dword_986394 - 1;
                        auto scroll = gGameTable.card_scroll + 1;
                        gGameTable.card_scroll = scroll;
                        if (scroll > gGameTable.cnt0 - cardMode - gGameTable.dword_986394 + gGameTable.cnt1 + 2)
                        {
                            gGameTable.card_cursor = 0;
                            gGameTable.card_scroll = 0;
                        }
                        else if (scroll < 0)
                        {
                            gGameTable.card_scroll = 0;
                        }
                    }
                    gGameTable.card_mess_timer = 3;
                    snd_se_on(0x4040000);
                }
                else if ((gGameTable.word_9885FC & 0x1000) != 0)
                {
                    gGameTable.card_mess_timer = 2;
                    if (--gGameTable.card_cursor < 0)
                    {
                        gGameTable.card_cursor = 0;
                        if (--gGameTable.card_scroll < 0)
                        {
                            gGameTable.card_cursor = gGameTable.dword_986394 - 1;
                            gGameTable.card_mess_timer = 8;
                            gGameTable.card_scroll = gGameTable.cnt0 - cardMode - gGameTable.dword_986394 + gGameTable.cnt1 + 2;
                        }
                    }
                    snd_se_on(0x4040000);
                    gGameTable.card_mess_timer = 3;
                }
                else if ((gGameTable.word_9885FC & 0x2000) != 0)
                {
                    gGameTable.card_fade += 8;
                    messErr = 1;
                }
                else if ((gGameTable.word_9885FC & 0x8000) != 0)
                {
                    gGameTable.card_fade -= 8;
                    if (gGameTable.card_fade < 0)
                        gGameTable.card_fade = 0;
                    messErr = 1;
                }
                else
                {
                    if ((gGameTable.key_trg & 0x1000) != 0 || (gGameTable.dword_9885F8 & 0x800) != 0)
                    {
                        switch (sub_431D10(gGameTable.card_scroll, &gGameTable.card_select, cardMode))
                        {
                        case 0:
                            cardState = CARD_STATE_SAVE_NEW;
                            snd_se_on(0x4060000);
                            gGameTable.p_card_save = gGameTable.card_save_buf;
                            break;
                        case 1:
                            gGameTable.card_sub_cursor = 0;
                            cardState = cardMode != 1 ? CARD_STATE_SAVE_OPTIONS : CARD_STATE_LOAD;
                            snd_se_on(0x4060000);
                            gGameTable.p_card_save = (uint8_t*)gGameTable.Cards + 276 * gGameTable.card_select;
                            break;
                        case 2:
                            sub_5099A0((char*)gGameTable.Names + 261 * gGameTable.card_select);
                            cardState = CARD_STATE_ENUMERATE;
                            gGameTable.card_scroll = 0;
                            gGameTable.card_cursor = 0;
                            snd_se_on(0x4060000);
                            break;
                        case 3:
                            sub_509940((char*)gGameTable.pMem + 8 * gGameTable.card_select);
                            cardState = CARD_STATE_ENUMERATE;
                            gGameTable.card_scroll = 0;
                            gGameTable.card_cursor = 0;
                            snd_se_on(0x4060000);
                            break;
                        case 4:
                            snd_se_on(0x4050000);
                            cardState = CARD_STATE_CANCEL;
                            break;
                        default: break;
                        }
                    }
                    else if ((gGameTable.key_trg & 0x2000) != 0)
                    {
                        snd_se_on(0x4050000);
                        cardState = CARD_STATE_CANCEL;
                    }
                }
                break;
            }
            break;
        case CARD_STATE_SAVE_NEW:
        {
            card_consume_ink_ribbon();
            int extremeLv;
            auto saveType = card_get_save_type(extremeLv);
            format_save_name0(
                gGameTable.save_name, saveType, (uint16_t)gGameTable.word_98E9BC, gGameTable.byte_98E9A7, extremeLv);
            strcpy(gGameTable.save_path, GetSaveFolder());
            CreateSaveFolder(gGameTable.save_path);
            strcat(gGameTable.save_path, gGameTable.save_name);
            strcat(gGameTable.save_path, ".BIOHAZARD2");
            cardState = card_write_save_file(CARD_STATE_TYPE_NEW);
            break;
        }
        case CARD_STATE_LOAD:
        {
            strcpy(gGameTable.save_path, GetSaveFolder());
            strcat(gGameTable.save_path, (const char*)gGameTable.Cards + 276 * gGameTable.card_select);
            auto result = file_read_save(gGameTable.p_card_work + 500, gGameTable.save_path, 0x800);
            gGameTable.card_write_result = result;
            if (result != 0)
            {
                gGameTable.card_write_result = 2;
                cardState = CARD_STATE_ERROR;
                break;
            }
            std::memcpy(&gGameTable.table_start, gGameTable.p_card_work + 500, 0x798);
            load_pop();
            if (gGameTable.byte_98EF2E != 0)
            {
                set_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE, true);
                gGameTable.fg_status = gGameTable.dword_98E9B0 | 0x4000;
            }
            else
            {
                set_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE, false);
                gGameTable.fg_status = gGameTable.dword_98E9B0 & ~0x4000u;
            }
            gGameTable.dword_989EC4 = gGameTable.dword_98EF20;
            gGameTable.dword_989EC8 = gGameTable.dword_98EF24;
            gGameTable.dword_989ECC = gGameTable.dword_98EF28;
            gGameTable.card_write_result = 0;
            cardState = CARD_STATE_EXIT;
            break;
        }
        case CARD_STATE_SAVE_OPTIONS:
            if ((gGameTable.dword_9885FE & 0x8000) != 0)
            {
                if (--gGameTable.card_sub_cursor < 0)
                    gGameTable.card_sub_cursor = 2;
                snd_se_on(0x4040000);
            }
            else if ((gGameTable.dword_9885FE & 0x2000) != 0)
            {
                if (++gGameTable.card_sub_cursor >= 3)
                    gGameTable.card_sub_cursor = 0;
                snd_se_on(0x4040000);
            }
            else if ((gGameTable.key_trg & 0x1000) == 0 && (gGameTable.dword_9885F8 & 0x800) == 0)
            {
                if ((gGameTable.key_trg & 0x2000) != 0)
                {
                    snd_se_on(0x4050000);
                    cardState = CARD_STATE_MENU;
                }
            }
            else if (gGameTable.card_sub_cursor == 0)
            {
                strcpy(gGameTable.save_path, GetSaveFolder());
                strcat(gGameTable.save_path, (const char*)gGameTable.Cards + 276 * gGameTable.card_select);
                remove_save(gGameTable.save_path);
                cardState = CARD_STATE_SAVE_OVERWRITE;
                snd_se_on(0x4060000);
            }
            else if (gGameTable.card_sub_cursor == 1)
            {
                auto dot = strrchr(gGameTable.save_name, '.');
                if (dot != nullptr)
                    *dot = 0;
                cardState = CARD_STATE_SAVE_OVERWRITE;
                snd_se_on(0x4060000);
            }
            else if (gGameTable.card_sub_cursor == 2)
            {
                snd_se_on(0x4050000);
                cardState = CARD_STATE_MENU;
            }
            break;
        case CARD_STATE_SAVE_OVERWRITE:
        {
            card_consume_ink_ribbon();
            if (gGameTable.card_sub_cursor != 0)
            {
                strcpy(gGameTable.save_name, (const char*)gGameTable.Cards + 276 * gGameTable.card_select);
                auto dot = strrchr(gGameTable.save_name, '.');
                if (dot != nullptr && *dot != 0)
                    *dot = 0;
            }
            else
            {
                int extremeLv;
                auto saveType = card_get_save_type(extremeLv);
                format_save_name1(
                    gGameTable.save_name,
                    saveType,
                    (uint16_t)gGameTable.word_98E9BC,
                    gGameTable.byte_98E9A7,
                    extremeLv,
                    gGameTable.card_cursor,
                    gGameTable.card_select);
            }
            strcpy(gGameTable.save_path, GetSaveFolder());
            CreateSaveFolder(gGameTable.save_path);
            strcat(gGameTable.save_path, gGameTable.save_name);
            auto dot = strrchr(gGameTable.save_path, '.');
            if (dot == nullptr)
            {
                strcat(gGameTable.save_path, ".BIOHAZARD2");
            }
            else
            {
                char ext[260];
                strcpy(ext, dot);
                _strlwr(ext);
                if (strcmp(ext, ".biohazard2") != 0)
                    strcat(gGameTable.save_path, ".BIOHAZARD2");
            }
            cardState = card_write_save_file(CARD_STATE_TYPE_OVERWRITE);
            break;
        }
        case CARD_STATE_TYPE_OVERWRITE:
            if (gGameTable.card_mess_timer != 0)
            {
                gGameTable.card_mess_timer--;
                break;
            }
            if (gGameTable.card_name_index >= sub_509AF0(gGameTable.save_name))
            {
                cardState = CARD_STATE_WAIT_OVERWRITE;
                gGameTable.card_mess_timer = 60;
            }
            else
            {
                sub_509B20((char*)gGameTable.p_card_save, gGameTable.save_name, ++gGameTable.card_name_index);
                sub_509B80(gGameTable.save_path, gGameTable.save_name, gGameTable.card_name_index - 1, 1);
                if (sub_509BE0(gGameTable.save_path))
                {
                    gGameTable.card_mess_timer = 4;
                    snd_se_on(0x2240000);
                }
                else
                {
                    gGameTable.card_mess_timer = 8;
                    snd_se_on(0x2220000);
                }
            }
            break;
        case CARD_STATE_WAIT_OVERWRITE:
        case CARD_STATE_WAIT_NEW:
            if ((gGameTable.key_trg & 0x3000) != 0 || (gGameTable.dword_9885F8 & 0x800) != 0)
            {
                gGameTable.card_mess_timer = 0;
                cardState = CARD_STATE_EXIT;
            }
            else if (gGameTable.card_mess_timer <= 0)
            {
                cardState = CARD_STATE_EXIT;
            }
            else
            {
                gGameTable.card_mess_timer--;
            }
            break;
        case CARD_STATE_TYPE_NEW:
            if (gGameTable.card_mess_timer != 0)
            {
                gGameTable.card_mess_timer--;
                break;
            }
            if (gGameTable.card_name_index >= sub_509AF0(gGameTable.save_name))
            {
                cardState = CARD_STATE_WAIT_NEW;
                gGameTable.card_mess_timer = 60;
            }
            else
            {
                sub_509B20((char*)gGameTable.p_card_save, gGameTable.save_name, ++gGameTable.card_name_index);
                sub_509B80(gGameTable.save_path, gGameTable.save_name, gGameTable.card_name_index - 1, 1);
                if (sub_509BE0(gGameTable.save_path))
                {
                    gGameTable.card_mess_timer = 8;
                    snd_se_on(0x2240000);
                }
                else
                {
                    gGameTable.card_mess_timer = 4;
                    snd_se_on(0x2220000);
                }
            }
            break;
        case CARD_STATE_CONFIRM_EXIT:
            if ((gGameTable.dword_9885FE & 0xA000) != 0)
            {
                gGameTable.card_sub_cursor ^= 1;
                snd_se_on(0x4040000);
            }
            if ((gGameTable.key_trg & 0x1000) != 0)
            {
                if (gGameTable.card_sub_cursor == 0)
                {
                    snd_se_on(0x4050000);
                    exitCardAccess();
                    return;
                }
                snd_se_on(0x4060000);
                cardState = CARD_STATE_MENU;
            }
            if ((gGameTable.key_trg & 0x2000) != 0)
            {
                snd_se_on(0x4060000);
                cardState = CARD_STATE_MENU;
            }
            break;
        case CARD_STATE_EXIT:
            gGameTable.card_write_result = 0;
            exitCardAccess();
            return;
        case CARD_STATE_ERROR:
            if ((gGameTable.key_trg & 0x3000) != 0 || (gGameTable.dword_9885F8 & 0x800) != 0)
            {
                snd_se_on(0x4040000);
                cardState = CARD_STATE_MENU;
            }
            save_print_tbl(gGameTable.card_write_result);
            break;
        case CARD_STATE_CANCEL:
            gGameTable.card_write_result = 0;
            if (cardMode != 0)
            {
                if (cardMode == 1)
                {
                    set_flag(FlagGroup::System, FG_SYSTEM_21, true);
                }
                exitCardAccess();
                return;
            }
            if (!check_flag(FlagGroup::System, FG_SYSTEM_10))
            {
                exitCardAccess();
                return;
            }
            cardState = CARD_STATE_CONFIRM_EXIT;
            gGameTable.card_sub_cursor = 1;
            break;
        default: break;
        }

        card_mess_disp(cardState, cardMode, gGameTable.card_cursor, messErr);
        ctcb.var_09 = 2;
        task_sleep(1);
    }

    // 0x004C57E0
    void mem_card()
    {
        auto& ctcb = *gGameTable.ctcb;
        if (ctcb.var_08 != 0)
        {
            if (ctcb.var_08 != 1)
                return;
        }
        else
        {
            config_write();
            task_suspend(0);
            movie_suspend_texture_use();
            ctcb.var_08 = 1;
        }

        card_access();
        if (ctcb.var_13 == 0)
        {
            movie_resume_texture_use();
            bg_load();
            task_signal(0);
            task_exit();
            ctcb.var_08 = 0;
        }
    }

    // 0x00509840
    char* GetSaveFolder()
    {
        return interop::call<char*>(0x00509840);
    }

    void save_init_hooks()
    {
        interop::writeJmp(0x004C57E0, &mem_card);
    }
}
