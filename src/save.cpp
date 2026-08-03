#include "save.h"
#include "audio.h"
#include "error.h"
#include "file.h"
#include "hud.h"
#include "input.h"
#include "interop.hpp"
#include "item.h"
#include "marni.h"
#include "openre.h"
#include "player.h"
#include "re2.h"
#include "scheduler.h"
#include "str.h"
#include "title.h"

#include <cstdlib>
#include <cstring>
#include <windows.h>

using namespace openre;
using namespace openre::audio;
using namespace openre::error;
using namespace openre::file;
using namespace openre::hud;
using namespace openre::input;
using namespace openre::player;
using namespace openre::title;

namespace openre::save
{
    // Save file name string tables (Shift-JIS). Maps to aSavePlayers,
    // aSaveNumbers, aSaveRooms and asc_5220D4 in the original binary.
    // Shared by format_save_name0 and format_save_name1.
    static const char* const _save_player_names[] = {
        "\x83\x8c\x83\x49\x83\x93",         // レオン (Leon A)
        "\x83\x4e\x83\x8c\x83\x41",         // クレア (Claire A)
        "\x83\x8c\x83\x49\x83\x93\x97\xa0", // レオン裏 (Leon B)
        "\x83\x4e\x83\x8c\x83\x41\x97\xa0", // クレア裏 (Claire B)
        nullptr,
        nullptr,
        "\x83\x8c\x83\x49\x83\x93", // レオン (extreme battle)
        "\x83\x4e\x83\x8c\x83\x41", // クレア (extreme battle)
        "\x83\x47\x83\x43\x83\x5f", // エイダ (extreme battle)
        "\x83\x4e\x83\x8a\x83\x58", // クリス (extreme battle)
    };

    static const char* const _save_number_names[] = {
        "\x82\x4f", // ０
        "\x82\x50", // １
        "\x82\x51", // ２
        "\x82\x52", // ３
        "\x82\x53", // ４
        "\x82\x54", // ５
        "\x82\x55", // ６
        "\x82\x56", // ７
        "\x82\x57", // ８
        "\x82\x58", // ９
    };

    static const char* const _save_room_names[] = {
        "\x8c\x78\x8e\x40\x8f\x90\x81\x40\x81\x40\x91\xd2\x8d\x87\x8e\xba", // 警察署　　待合室
        "\x8c\x78\x8e\x40\x8f\x90\x81\x40\x81\x40\x83\x7a\x81\x5b\x83\x8b", // 警察署　　ホール
        "\x8c\x78\x8e\x40\x8f\x90\x81\x40\x8e\xca\x90\x5e\x88\xc3\x8e\xba", // 警察署　写真暗室
        "\x8f\x88\x97\x9d\x8f\xea\x81\x40\x81\x40\x8d\xb6\x95\xa8\x92\x75", // 処理場　　左物置
        "\x8f\x88\x97\x9d\x8f\xea\x81\x40\x81\x40\x89\x45\x95\xa8\x92\x75", // 処理場　　右物置
        "\x89\xba\x90\x85\x81\x40\x91\xe6\x82\x50\x8a\xc7\x97\x9d\x8e\xba", // 下水　第１管理室
        "\x89\xba\x90\x85\x81\x40\x91\xe6\x82\x51\x8a\xc7\x97\x9d\x8e\xba", // 下水　第２管理室
        "\x89\xba\x90\x85\x81\x40\x8f\x88\x97\x9d\x83\x76\x81\x5b\x83\x8b", // 下水　処理プール
        "\x8d\x48\x8f\xea\x81\x40\x81\x40\x83\x70\x83\x6c\x83\x8b\x8e\xba", // 工場　　パネル室
        "\x8c\xa4\x8b\x86\x8f\x8a\x81\x40\x83\x7c\x83\x93\x83\x76\x8e\xba", // 研究所　ポンプ室
        "\x8c\xa4\x8b\x86\x8f\x8a\x81\x40\x81\x40\x8c\x78\x94\xf5\x8e\xba", // 研究所　　警備室
        "\x8c\xa4\x8b\x86\x8f\x8a\x83\x82\x83\x6a\x83\x5e\x81\x7c\x8e\xba", // 研究所モニタ−室
        "\x83\x4b\x83\x93\x83\x56\x83\x87\x83\x62\x83\x76",                 // ガンショップ
        "\x8c\x78\x8e\x40\x8f\x90\x81\x40\x95\xa8\x92\x75\x8f\xac\x89\xae", // 警察署　物置小屋
        "\x8c\xa4\x8b\x86\x8f\x8a\x81\x40\x81\x40\x81\x40\x8e\xd4\x93\xe0", // 研究所　　　車内
        "\x83\x56\x83\x69\x83\x8a\x83\x49\x81\x40\x82\x50\x82\x93\x82\x94", // シナリオ　１ｓｔ
    };

    static const char* const _save_name_sep = "\x81\x51"; // ＿

    // Memory card result message table (Shift-JIS). Maps to aSaveWarning
    // in the original binary. Indexed by save_print_tbl.
    static const char* const _save_warning[] = {
        "\x82\xb1\x82\xea\x82\xcd\x83\x4e\x83\x8c\x83\x41\x95\xd2\x82\xcc\x83\x66\x81\x5b\x83\x5e\x82\xc5\x82\xb7", // これはクレア編のデータです
        "\x82\xb1\x82\xea\x82\xcd\x83\x8c\x83\x49\x83\x93\x95\xd2\x82\xcc\x83\x66\x81\x5b\x83\x5e\x82\xc5\x82\xb7", // これはレオン編のデータです
        "\x93\xc7\x82\xdd\x8d\x9e\x82\xdd\x82\xc9\x8e\xb8\x94\x73\x82\xb5\x82\xdc\x82\xb5\x82\xbd", // 読み込みに失敗しました
        "\x8f\x91\x82\xab\x8d\x9e\x82\xdd\x82\xc9\x8e\xb8\x94\x73\x82\xb5\x82\xdc\x82\xb5\x82\xbd", // 書き込みに失敗しました
        nullptr,
    };

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
        gGameTable.is_480p = gGameTable.pMarni->xsize != 320;
    }

    // 0x00432070
    static void sub_432070(uint32_t* a1, uint32_t* a2)
    {
        *a1 = 0;
    }

    // 0x00431D10
    // Resolves which menu entry the card cursor is on within the save/load list.
    // The return value selects the caller's action: 0 = new save, 1 = select an
    // existing card, 4 = cancel, 99 = invalid position. On 1, *select receives
    // the card index.
    static int card_menu_action(int scroll, int* select, char mode)
    {
        int index = gGameTable.card_cursor + scroll;
        if (index == -mode)
        {
            // Cursor is on the "new save" entry.
            *select = -1;
            return 0;
        }
        if (index >= gGameTable.cnt0 - mode + 1)
        {
            // Cursor is on the last entry (cancel) or beyond it.
            return gGameTable.cnt1 - mode + gGameTable.cnt0 + 1 != index ? 99 : 4;
        }
        // Cursor is on one of the existing card slots.
        *select = gGameTable.card_cursor + mode + scroll - 1;
        return 1;
    }

    // The OG save-path scratch std::string (OldStdString {data, length}) at
    // 0x689F44, used by the save file name helpers below.
    static OldStdString* save_path_string()
    {
        return reinterpret_cast<OldStdString*>(0x689F44);
    }

    // 0x00509860
    // Builds a save path from the current module file name when no save path is set.
    static int sub_509860()
    {
        return interop::call<int>(0x00509860);
    }

    // 0x00509940
    // Copies the given save path into the global save-path string, ensuring it is
    // non-empty and ends with a backslash (appending one if the last Shift-JIS
    // character is not a backslash). Falls back to the module path when empty.
    static int sub_509940(char* savePath)
    {
        str::string_copy(save_path_string(), savePath);
        if (str::string_sjis_len(save_path_string()) == 0)
            return sub_509860();

        int lastSlash = str::string_find_last(save_path_string(), "\\");
        int lastChar = str::string_sjis_len(save_path_string()) - 1;
        if (lastSlash != lastChar)
            return (int)str::string_append(save_path_string(), "\\");
        return lastChar;
    }

    // 0x00509B20
    static void sub_509B20(char* dest, const char* src, int count)
    {
        OldStdString src_str;
        OldStdString sliced;

        // Build src_str from the source string, then truncate it to the first
        // `count` Shift-JIS characters and copy the result into dest.
        // Used for the typewriter reveal animation of the save name.
        str::string_ctor_from_cstr(&src_str, src);
        str::string_slice(&src_str, &sliced, count);
        str::string_assign(&src_str, &sliced);
        str::string_dtor(&sliced);
        strcpy(dest, str::string_get_data(&src_str));
        str::string_dtor(&src_str);
    }

    // 0x00509B80
    static void sub_509B80(char* a1, const char* a2, int a3, int a4)
    {
        OldStdString src_str;
        OldStdString sliced;

        // Build src_str from the source string, then extract the substring
        // starting after `a3` Shift-JIS characters, keeping up to `a4`
        // characters, and copy the result into a1.
        // Used to grab the current character of the save name being typed.
        str::string_ctor_from_cstr(&src_str, a2);
        str::string_sjis_copy(&src_str, &sliced, a3, a4);
        str::string_assign(&src_str, &sliced);
        str::string_dtor(&sliced);
        strcpy(a1, str::string_get_data(&src_str));
        str::string_dtor(&src_str);
    }

    // 0x00509BE0
    static int sub_509BE0(const char* a1)
    {
        OldStdString name;
        str::string_ctor_from_cstr(&name, a1);
        // A save name consisting of only a space, underscore, or Shift-JIS blank
        // character is treated as an invalid/blank name (error sound is played).
        if (str::string_eq_cstr(&name, " ")            // 0x51D868 - half-width space
            || str::string_eq_cstr(&name, "\x81\x40")  // 0x540B48 - full-width space
            || str::string_eq_cstr(&name, "_")         // 0x540B44 - underscore
            || str::string_eq_cstr(&name, "\x81\x51")) // 0x5220D4
        {
            str::string_dtor(&name);
            return 1;
        }
        else
        {
            str::string_dtor(&name);
            return 0;
        }
    }

    // 0x005099A0
    // Changes the current save folder. If `name` is ".." the folder moves up one
    // directory level; otherwise a subfolder named `name` is entered. The resulting
    // path is stored as the global save folder via sub_509940.
    static void sub_5099A0(char* name)
    {
        OldStdString nameStr; // std::string built from the card name
        OldStdString path;    // working folder path
        OldStdString tmp;     // temporary substring

        str::string_ctor_from_cstr(&nameStr, name);

        if (str::string_eq_cstr(&nameStr, ".."))
        {
            // Move up one directory level.
            str::string_ctor_from_cstr(&path, GetSaveFolder());

            // tmp = the last character of the current folder.
            str::string_right(&path, &tmp, 1);
            bool hasTrailingSlash = str::string_eq_cstr(&tmp, "\\");
            str::string_dtor(&tmp);

            if (hasTrailingSlash)
            {
                // Drop the trailing backslash.
                str::string_slice(&path, &tmp, str::string_sjis_len(&path) - 1);
                str::string_assign(&path, &tmp);
                str::string_dtor(&tmp);
            }

            // Truncate at the last backslash to reach the parent directory.
            int sep = str::string_find_last(&path, "\\");
            if (sep >= 0)
            {
                str::string_slice(&path, &tmp, sep);
                str::string_assign(&path, &tmp);
                str::string_dtor(&tmp);
            }
        }
        else
        {
            // Enter a subfolder named after the card.
            str::string_ctor_from_cstr(&path, GetSaveFolder());

            str::string_right(&path, &tmp, 1);
            bool missingTrailingSlash = str::string_ne_cstr(&tmp, "\\");
            str::string_dtor(&tmp);

            if (missingTrailingSlash)
                str::string_append(&path, "\\");

            str::string_append(&path, name);
        }

        str::string_append(&path, "\\");
        sub_509940((char*)str::string_get_data(&path));
        str::string_dtor(&path);
        str::string_dtor(&nameStr);
    }

    // 0x00432110
    // Builds the memory card save file name (without the .BIOHAZARD2 extension)
    // into str. The name is composed of Shift-JIS parts as:
    //   <player> [<extreme level digit>]＿<save count>＿<room>
    // e.g. "レオン＿０１＿警察署　　待合室" (＿ = fullwidth underscore 0x8151).
    // If a save file already on the memory card (Cards list) uses that name,
    // "_2", "_3", ... is appended and the scan is repeated until the name is unique.
    static int format_save_name0(char* str, int player, int saveCnt, int saveRoom, int extremeLv)
    {
        char base[264];      // player + extreme level + save count + room
        char candidate[264]; // candidate save name (gets "_N" suffix on collision)
        char cardName[264];  // existing card save name with the extension stripped

        strcpy(base, _save_player_names[player]);
        if (extremeLv)
            strcat(base, _save_number_names[extremeLv]);
        strcat(base, _save_name_sep); // ＿
        strcat(base, _save_number_names[saveCnt / 10]);
        strcat(base, _save_number_names[saveCnt % 10]);
        strcat(base, _save_name_sep); // ＿
        strcat(base, _save_room_names[saveRoom]);
        strcpy(candidate, base);

        // Reject names already present on the memory card by appending
        // "_2", "_3", ... and re-scanning until the name is unique.
        int dupCount = 1;
        for (;;)
        {
            bool duplicate = false;
            for (int cardIndex = 0; cardIndex < gGameTable.cnt0; cardIndex++)
            {
                if (dupCount == 0)
                    return wsprintfA(str, candidate); // unreachable: dupCount only increments

                strcpy(cardName, (const char*)gGameTable.Cards + 276 * cardIndex);
                if (char* dot = strrchr(cardName, '.'))
                    *dot = '\0';
                if (strcmp(cardName, candidate) == 0)
                {
                    wsprintfA(candidate, "%s_%d", base, ++dupCount);
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
                break;
        }

        strcpy(str, candidate);
        return strlen(str); // the original returns an unused leftover value here
    }

    // 0x00432380
    static int
    format_save_name1(char* str, int player, int saveCnt, int saveRoom, int extremeLv, int cardCursor, int cardSelect)
    {
        char name[264];      // base name without numeric suffix
        char candidate[264]; // current candidate (base or base_N)
        char cardName[264];  // file name of an existing card entry

        // Build "<player>[<extreme>]＿<saveCnt>＿<room>".
        strcpy(name, _save_player_names[player]);
        if (extremeLv)
            strcat(name, _save_number_names[extremeLv]);
        strcat(name, _save_name_sep);
        strcat(name, _save_number_names[saveCnt / 10]);
        strcat(name, _save_number_names[saveCnt % 10]);
        strcat(name, _save_name_sep);
        strcat(name, _save_room_names[saveRoom]);
        strcpy(candidate, name);

        // If the name already exists on the card, and not in the slot being
        // overwritten (index cardCursor + cardSelect - 1), append a "_N"
        // suffix and scan the card again.
        int suffix = 1;
        bool restart;
        do
        {
            restart = false;
            for (int i = 0; i < gGameTable.cnt0; i++)
            {
                if (!suffix)
                    return wsprintfA(str, candidate); // unreachable: suffix only increments
                strcpy(cardName, (const char*)gGameTable.Cards + i * 276);
                char* dot = strrchr(cardName, '.');
                if (dot)
                    *dot = 0;
                if (strcmp(cardName, candidate) == 0)
                {
                    if (cardCursor + cardSelect - 1 != i)
                    {
                        wsprintfA(candidate, "%s_%d", name, ++suffix);
                        restart = true;
                    }
                    break;
                }
            }
        } while (restart);

        strcpy(str, candidate);
        return 0;
    }

    // 0x00432620
    // Displays the memory card result message on the save screen.
    // 'string' indexes a table of Shift-JIS messages (see card_write_result):
    //   0 = Claire scenario data, 1 = Leon scenario data,
    //   2 = load failed, 3 = save failed, 4 = no message.
    // Uses the shared SavePrint (0x00431470) implementation in openre.cpp.
    static int save_print_tbl(int string)
    {
        if (gGameTable.is_480p)
            return SavePrint(0, 416, _save_warning[string], 3, 0);
        else
            return SavePrint(0, 208, _save_warning[string], 3, 0);
    }

    // 0x004C7830
    static uint8_t save_push()
    {
        // Backs up the current player state and game flags into the save data
        // buffer (the reverse of load_pop).
        gGameTable.byte_98E9A6 = gGameTable.pl.id;
        gGameTable.word_98E9BE = *(uint16_t*)&gGameTable.pl.m.pos.x;
        gGameTable.word_98E9C2 = *(uint16_t*)&gGameTable.pl.m.pos.z;
        gGameTable.word_98E9C0 = *(uint16_t*)&gGameTable.pl.m.pos.y;
        gGameTable.word_98EE78 = gGameTable.pl.cdir.y;

        if (check_flag(FlagGroup::System, FG_SYSTEM_10))
        {
            gGameTable.dword_98E99C = 0;
        }
        else
        {
            gGameTable.dword_98E99C = set_game_seconds(1);
        }

        if (check_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE))
        {
            set_flag(FlagGroup::Status, FG_STATUS_17, true);
            gGameTable.nExtremeLv = (int16_t)gGameTable.ex_battle_mode;
            std::memcpy(&gGameTable.dword_98EEF0, &gGameTable.dword_989E94, 0x3C);
        }

        gGameTable.word_98E9B4 = (uint16_t)((gGameTable.byte_691F68 << 8) | gGameTable.byte_691F6A);

        if (check_flag(FlagGroup::System, FG_SYSTEM_EASY))
        {
            set_flag(FlagGroup::Status, FG_STATUS_EASY, true);
        }
        if (check_flag(FlagGroup::System, FG_SYSTEM_12))
        {
            set_flag(FlagGroup::Status, 2, true); // fg_status bit 29 (0x20000000)
        }

        gGameTable.dword_98E9B0 = gGameTable.fg_status;
        gGameTable.word_98E9B6 = gGameTable.pl.life;
        gGameTable.byte_98E9AB = gGameTable.poison_timer;
        gGameTable.word_98E9AC = gGameTable.poison_status;

        gGameTable.pad_98E9A8[0] = gGameTable.sfx_vol; // byte_98E9A8
        uint8_t result = gGameTable.byte_98F1B6;
        gGameTable.pad_98E9A8[1] = gGameTable.bgm_vol; // byte_98E9A9
        gGameTable.byte_98E9A5 = gGameTable.byte_9888D9;
        gGameTable.byte_98E9AA = gGameTable.byte_98F1B6;
        return result;
    }

    // 0x004C7980
    static char load_pop()
    {
        // Restore the flag state saved into the memory card buffer (see save_push).
        gGameTable.fg_status = gGameTable.dword_98E9B0;
        if ((gGameTable.dword_98E9B0 & 0x4000000) != 0)
            gGameTable.fg_system |= 0x20; // FG_SYSTEM_EASY
        if ((gGameTable.dword_98E9B0 & 0x20000000) != 0)
            gGameTable.fg_system |= 0x80000; // FG_SYSTEM_12
        gGameTable.byte_989EEA = (uint8_t)gGameTable.current_cut;
        marni::out();
        gGameTable.byte_691F68 = (uint8_t)(gGameTable.word_98E9B4 >> 8);
        gGameTable.byte_691F6A = (uint8_t)(gGameTable.word_98E9B4 & 0xFF);
        gGameTable.pl.life = gGameTable.word_98E9B6;
        gGameTable.poison_timer = gGameTable.byte_98E9AB;
        gGameTable.poison_status = (uint16_t)gGameTable.word_98E9AC;
        auto result = gGameTable.byte_98E9A5;
        gGameTable.sfx_vol = gGameTable.pad_98E9A8[0];
        gGameTable.bgm_vol = gGameTable.pad_98E9A8[1];
        gGameTable.byte_9888D9 = result;
        gGameTable.byte_98F1B6 = gGameTable.byte_98E9AA;
        return result;
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
                        switch (card_menu_action(gGameTable.card_scroll, &gGameTable.card_select, cardMode))
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
            if (gGameTable.card_name_index >= str::string_sjis_len_cstr(gGameTable.save_name))
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
            if (gGameTable.card_name_index >= str::string_sjis_len_cstr(gGameTable.save_name))
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
    // Returns the current save folder path. On first use the path is built from
    // the module file name (the directory of the running executable, with a
    // trailing backslash) and cached in the OG save-path string at 0x689F44.
    char* GetSaveFolder()
    {
        if (str::string_sjis_len(save_path_string()) == 0)
            sub_509860();
        return save_path_string()->data;
    }

    void save_init_hooks()
    {
        interop::writeJmp(0x004C57E0, &mem_card);
    }
}
