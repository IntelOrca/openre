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

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ddraw.h>
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

    // Save/load screen states. The card state value is passed to card_mess_disp as rno.
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

    // Message tables for the memory card screen (immutable data). data_savemes
    // holds the Shift-JIS message strings, selected by the uint16_t offsets in
    // ptr_savemes.
    static const char* data_savemes = (const char*)0x52DB68;
    static const uint16_t* ptr_savemes = (const uint16_t*)0x52DF20;

    // Save/load screen strings (Shift-JIS).
    static const char* aSave = (const char*)0x52E210;       // セーブ
    static const char* aLeaveSave = (const char*)0x52E218;  // セーブ画面から
    static const char* aLeaveLoad = (const char*)0x52E230;  // ロード画面から
    static const char* aFolder = (const char*)0x52E248;     // フォルダ
    static const char* aNo = (const char*)0x52E254;         // いいえ
    static const char* aYes = (const char*)0x52E25C;        // はい
    static const char* aDidNotSave = (const char*)0x52E264; // データをセーブしていません
    static const char* aLoad = (const char*)0x52E294;       // ロード
    static const char* aCancel = (const char*)0x52E29C;     // キャンセル
    static const char* aUpdate = (const char*)0x52E2A8;     // 更新
    static const char* aOverwrite = (const char*)0x52E2B0;  // 上書き
    static const char* aHowToSave = (const char*)0x52E2BC;  // どのように保存しますか？

    // 0x004C5850
    // Returns the byte length of a data_savemes message, scaled by 14 per
    // character. Used to compute how many bytes of the save-slot room name
    // message to copy for display.
    static int get_savemes_len(int index)
    {
        return interop::call<int, int>(0x004C5850, index);
    }

    // 0x004C7CD0
    static int get_mess_width(const uint8_t* str, int16_t mode)
    {
        return interop::call<int, const uint8_t*, int16_t>(0x004C7CD0, str, mode);
    }

    // 0x004C7810
    static int cursor_disp(int16_t x1, int16_t y)
    {
        return interop::call<int, int16_t, int16_t>(0x004C7810, x1, y);
    }

    // 0x004319A0
    static int print_save_list(int a1, int a2, int a3, int a4, int a5, int a6, char a7, uint32_t* a8, uint8_t* a9)
    {
        return interop::call<int, int, int, int, int, int, int, char, uint32_t*, uint8_t*>(
            0x004319A0, a1, a2, a3, a4, a5, a6, a7, a8, a9);
    }

    // 0x00431470
    static int SavePrint(int x, int y, const char* str, int color, int len);

    // 0x004C6E30
    // Draws the messages on the memory card save/load screen. Which messages are
    // drawn depends on the current card state (rno).
    static void card_mess_disp(int rno, int mode, uint32_t cursor, int errCode)
    {
        int y[2] = { 208, 416 };     // "how to save" / "did not save" prompt y
        int infoY[2] = { 217, 434 }; // "folder" / "leave" message y
        int titleY[2] = { 10, 20 };  // "save" / "load" title y
        int yes_x[2] = { 100, 200 };
        int no_x[2] = { 184, 368 };
        int yesno_x[2] = { 86, 170 };
        int x_overwrite[2] = { 63, 126 };
        int x_update[2] = { 149, 298 };
        int x_cancel[2] = { 211, 422 };
        int choice_x[3] = { 49, 135, 197 };
        int cursorYoff[2] = { 16, 15 };
        // The time text and the save-room message are rendered as one continuous
        // string: Mess_print reads past the 4-byte time text into the room
        // message (which ends with the 0xFE terminator). Keep them in a single
        // contiguous buffer to match the adjacent str1/dst locals of the OG code.
        char str1[4 + 64];
        char* dst = str1 + 4;
        char str[8];

        auto* cardStateBytes = (uint8_t*)&gGameTable.card_state;

        // Prints the save/load title, the save list (via the shared
        // save_menu_draw implementation) and the cursor tile.
        auto drawMenu = [&]() {
            SavePrint(0, titleY[gGameTable.is_480p], mode ? aLoad : aSave, 0, 0);
            if (save_menu_draw(
                    0,
                    0,
                    gGameTable.card_scroll,
                    (int)gGameTable.Cards,
                    (int)gGameTable.Names,
                    (int)gGameTable.pMem,
                    (char)mode,
                    (uint8_t*)&gGameTable.card_fade)
                && errCode == 1)
            {
                snd_se_on(0x4040000);
                gGameTable.card_mess_timer = 3;
            }
            Tile pPrim{};
            pPrim.r = 80;
            pPrim.g = 80;
            pPrim.b = 80;
            pPrim.tag = 1;
            pPrim.code = 2;
            pPrim.psxRect.x = 16;
            pPrim.psxRect.y = (int16_t)(15 * (int)cursor + 49);
            pPrim.psxRect.w = 288;
            pPrim.psxRect.h = 14;
            marni::add_tile(&pPrim, 1, 0);
        };

        // Prints the save list and title without the cursor tile.
        auto printList = [&]() {
            print_save_list(
                0,
                0,
                gGameTable.card_scroll,
                (int)gGameTable.Cards,
                (int)gGameTable.Names,
                (int)gGameTable.pMem,
                (char)mode,
                (uint32_t*)&gGameTable.card_fade,
                gGameTable.card_save_buf);
            SavePrint(0, titleY[gGameTable.is_480p], mode ? aLoad : aSave, 0, 0);
        };

        // Advances the sub-cursor blink counter and draws the cursor at the
        // selected entry while the counter is in its visible phase.
        auto blinkCursor = [&](const int* xs) {
            uint8_t counter;
            if (gGameTable.Mwork_pc2 == gGameTable.card_sub_cursor)
                counter = cardStateBytes[2];
            else
            {
                counter = 31;
                gGameTable.Mwork_pc2 = gGameTable.card_sub_cursor;
            }
            counter = (counter + 1) & 0x1F;
            cardStateBytes[2] = counter;
            if (counter < 0xF)
                cursor_disp((int16_t)xs[gGameTable.card_sub_cursor], (int16_t)(cursorYoff[gGameTable.is_480p] + 207));
        };

        switch (rno)
        {
        case CARD_STATE_MENU:
        case CARD_STATE_LOAD:
        case CARD_STATE_TYPE_OVERWRITE:
        case CARD_STATE_WAIT_OVERWRITE:
        {
            int idx = gGameTable.card_scroll + (int)cursor;
            if (idx < gGameTable.cnt0 - mode + 1 && (idx + mode) != 0)
            {
                // Draw the save-slot info message.
                auto* cards = (char*)gGameTable.Cards;
                int slot = idx + mode;
                std::memcpy(str, &data_savemes[ptr_savemes[(int8_t)cards[276 * slot - 15] + 31]], 8);
                auto* slotPtr = cards + 276 * slot;
                int v9 = (slotPtr[-15] & 1) ? 2 : 4;
                if (slotPtr[-3])
                {
                    v9 = 1;
                    str[3] += slotPtr[-2];
                }
                std::memcpy(str1, &data_savemes[ptr_savemes[42]], 4);
                str1[1] += (char)((int8_t)slotPtr[-14] / 10);
                str1[2] += (char)((int8_t)slotPtr[-14] % 10);
                int v11 = (int8_t)slotPtr[-13];
                int v12 = (int16_t)get_savemes_len(v11 + 43);
                std::memcpy(dst, &data_savemes[ptr_savemes[v11 + 43]], 2 * (v12 / 14) + 2);
                int mess_width = get_mess_width((const uint8_t*)str, 0);
                int v14 = (320 - get_mess_width((const uint8_t*)str1, 0) - mess_width) / 2;
                mess_print(v14, 216, (const uint8_t*)str, (short)((16 * v9) | 2));
                mess_print(mess_width + v14, 216, (const uint8_t*)str1, 2);
            }
            if (idx >= gGameTable.cnt0 - mode + 1 && (idx + mode) != 0 && idx < gGameTable.cnt1 - mode + gGameTable.cnt0 + 1)
            {
                SavePrint(0, infoY[gGameTable.is_480p], aFolder, 0, 0);
            }
            if ((idx + mode) != 0 && idx == gGameTable.cnt1 - mode + gGameTable.cnt0 + 1)
            {
                SavePrint(0, infoY[gGameTable.is_480p], mode ? aLeaveLoad : aLeaveSave, 0, 0);
            }
            drawMenu();
            break;
        }

        case CARD_STATE_SAVE_OPTIONS:
            SavePrint(0, y[gGameTable.is_480p], aHowToSave, 0, 0);
            SavePrint(
                x_overwrite[gGameTable.is_480p],
                y[gGameTable.is_480p] + gGameTable.byte_6634F8,
                aOverwrite,
                gGameTable.card_sub_cursor != 0 ? 0 : 3,
                0);
            SavePrint(
                x_update[gGameTable.is_480p],
                y[gGameTable.is_480p] + gGameTable.byte_6634F8,
                aUpdate,
                gGameTable.card_sub_cursor != 1 ? 0 : 3,
                0);
            SavePrint(
                x_cancel[gGameTable.is_480p],
                y[gGameTable.is_480p] + gGameTable.byte_6634F8,
                aCancel,
                gGameTable.card_sub_cursor != 2 ? 0 : 3,
                0);
            blinkCursor(choice_x);
            drawMenu();
            break;

        case CARD_STATE_SAVE_OVERWRITE:
        case CARD_STATE_ERROR: drawMenu(); break;

        case CARD_STATE_SAVE_NEW: printList(); break;

        case CARD_STATE_TYPE_NEW:
        case CARD_STATE_WAIT_NEW:
        {
            int idx = gGameTable.card_scroll + (int)cursor;
            // Draw the message for the save currently being written, taken from
            // the tail of card_save_buf (byte offsets 261..274 of the 276-byte slot).
            auto* sbuf = gGameTable.card_save_buf;
            std::memcpy(str, &data_savemes[ptr_savemes[(int8_t)sbuf[261] + 31]], 8);
            int v17 = (sbuf[261] & 1) ? 2 : 4;
            if (sbuf[273])
            {
                v17 = 1;
                str[3] += sbuf[274];
            }
            std::memcpy(str1, &data_savemes[ptr_savemes[42]], 4);
            str1[1] += (char)((int8_t)sbuf[262] / 10);
            str1[2] += (char)((int8_t)sbuf[262] % 10);
            int v18 = (int8_t)sbuf[263];
            int v19 = (int16_t)get_savemes_len(v18 + 43);
            std::memcpy(dst, &data_savemes[ptr_savemes[v18 + 43]], 2 * (v19 / 14) + 2);
            int v20 = get_mess_width((const uint8_t*)str, 0);
            int v21 = (320 - get_mess_width((const uint8_t*)str1, 0) - v20) / 2;
            mess_print(v21, 216, (const uint8_t*)str, (short)((16 * v17) | 2));
            mess_print(v20 + v21, 216, (const uint8_t*)str1, 2);

            if (idx >= gGameTable.cnt0 - mode + 1 && (idx + mode) != 0 && idx < gGameTable.cnt1 - mode + gGameTable.cnt0 + 1)
            {
                SavePrint(0, infoY[gGameTable.is_480p], aFolder, 0, 0);
            }
            if ((idx + mode) != 0 && idx == gGameTable.cnt1 - mode + gGameTable.cnt0 + 1)
            {
                SavePrint(0, infoY[gGameTable.is_480p], mode ? aLeaveLoad : aLeaveSave, 0, 0);
            }
            printList();
            break;
        }

        case CARD_STATE_CONFIRM_EXIT:
            SavePrint(0, y[gGameTable.is_480p], aDidNotSave, 0, 0);
            SavePrint(
                yes_x[gGameTable.is_480p],
                y[gGameTable.is_480p] + gGameTable.byte_6634F8,
                aYes,
                gGameTable.card_sub_cursor != 0 ? 0 : 3,
                0);
            SavePrint(
                no_x[gGameTable.is_480p],
                y[gGameTable.is_480p] + gGameTable.byte_6634F8,
                aNo,
                gGameTable.card_sub_cursor != 1 ? 0 : 3,
                0);
            blinkCursor(yesno_x);
            drawMenu();
            break;

        default: break;
        }

        // Restore the fade rectangle from the card work buffer.
        auto* cardWork = gGameTable.card_work_ptr;
        auto v26 = cardWork[3];
        *(uint16_t*)(cardWork + 0x18) = 0;
        cardStateBytes[1] = v26;
        hud_fade_adjust(3, (int16_t)(*(uint16_t*)(cardWork + 0x18) << 7), 0, (PsxRect*)(cardWork + 0x24));
    }

    // ── Save screen text rendering ────────────────────────────────────────

    // 0x004310B0
    // Clears the font string buffers that SavePrint writes into, so each frame
    // starts with an empty text queue.
    void save_reset()
    {
        auto* v0 = &gGameTable.FontXY[1];
        char* v1 = gGameTable.String;
        std::memset(gGameTable.FontColor, 0, sizeof(gGameTable.FontColor));
        do
        {
            *v1 = 0;
            *(v0 - 1) = 0;
            *v0 = 0;
            v1 += 261;
            v0 += 2;
        } while (v1 < reinterpret_cast<char*>(&gGameTable.hFont));
        gGameTable.FontIndex = 0;
    }

    // 0x004310F0
    // Flushes the text queued by SavePrint to the drawing surface via GDI.
    int save_print_flush()
    {
        if (!gGameTable.FontIndex)
            return 1;

        auto* pSurface = (LPDIRECTDRAWSURFACE7)gGameTable.pMarni->surface0.pDDsurface;
        HDC hdc;
        if (pSurface->GetDC(&hdc) != DD_OK)
            return 1;

        auto oldFont = SelectObject(hdc, gGameTable.hFont);
        if (!oldFont)
            return 0;
        auto oldMode = SetBkMode(hdc, TRANSPARENT);

        for (int i = 0; i < gGameTable.FontIndex; i++)
        {
            auto* str = &gGameTable.String[261 * i];
            if (!*str)
                continue;

            auto x = gGameTable.FontXY[2 * i];
            auto y = gGameTable.FontXY[2 * i + 1];

            if (x != 0)
            {
                // Centered text with shadow (offset by +1 pixel)
                auto shadowR = (std::max)(GetRValue(gGameTable.FontColor[i]) - 64, 0);
                auto shadowG = (std::max)(GetGValue(gGameTable.FontColor[i]) - 64, 0);
                auto shadowB = (std::max)(GetBValue(gGameTable.FontColor[i]) - 64, 0);
                auto shadowColor = RGB(shadowR, shadowG, shadowB);

                if (SetTextColor(hdc, shadowColor) == CLR_INVALID)
                    return 0;
                TextOutA(hdc, gGameTable.is_480p + x + 1, y, str, (int)strlen(str));

                if (SetTextColor(hdc, gGameTable.FontColor[i]) == CLR_INVALID)
                    return 0;
                if (!TextOutA(hdc, x, y, str, (int)strlen(str)))
                    return 0;
            }
            else
            {
                // Left-aligned text with clipping rectangle and shadow
                SIZE psizl;
                GetTextExtentPoint32A(hdc, str, (int)strlen(str), &psizl);
                auto cx = psizl.cx;

                RECT rect;
                int v6;
                if (gGameTable.is_480p)
                {
                    if (cx > 576)
                        cx = 576;
                    rect.left = 32;
                    rect.right = 608;
                    v6 = (576 - cx) / 2 + 32;
                    rect.bottom = y + gGameTable.byte_6634F8;
                }
                else
                {
                    if (cx > 288)
                        cx = 288;
                    rect.left = 16;
                    rect.right = 304;
                    v6 = (288 - cx) / 2 + 16;
                    rect.bottom = y + gGameTable.byte_6634F8;
                }
                rect.top = y;

                auto shadowR = (std::max)(GetRValue(gGameTable.FontColor[i]) - 64, 0);
                auto shadowG = (std::max)(GetGValue(gGameTable.FontColor[i]) - 64, 0);
                auto shadowB = (std::max)(GetBValue(gGameTable.FontColor[i]) - 64, 0);
                auto shadowColor = RGB(shadowR, shadowG, shadowB);

                if (SetTextColor(hdc, shadowColor) == CLR_INVALID)
                    return 0;
                ExtTextOutA(hdc, gGameTable.is_480p + v6 + 1, y, ETO_CLIPPED, &rect, str, (int)strlen(str), nullptr);

                if (SetTextColor(hdc, gGameTable.FontColor[i]) == CLR_INVALID)
                    return 0;
                if (!ExtTextOutA(hdc, v6, y, ETO_CLIPPED, &rect, str, (int)strlen(str), nullptr))
                    return 0;
            }
        }

        SelectObject(hdc, oldFont);
        SetBkMode(hdc, oldMode);
        pSurface->ReleaseDC(hdc);
        return 1;
    }

    // 0x00431470
    // Queues a string for the save screen text renderer, handling Shift-JIS
    // scroll cutting and color lookup. The strings are drawn by save_print_flush.
    static int SavePrint(int x, int y, const char* str, int color, int len)
    {
        char buf[264];

        // Skip 'len' bytes of the input, character-aware (Shift-JIS lead bytes are 2 bytes)
        int i;
        for (i = 0; i < len;)
        {
            auto c = (unsigned char)str[i];
            if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC))
                i += 2;
            else
                i += 1;
        }

        // Copy from the scroll-cut position into temp buffer
        {
            const char* src = &str[i];
            char* dst = buf;
            do
            {
                *dst++ = *src;
            } while (*src++);
        }

        // Truncate to at most 48 bytes, respecting Shift-JIS character boundaries
        {
            int j;
            int lastPos = 0;
            for (j = 0; j < 48;)
            {
                auto c = (unsigned char)buf[j];
                lastPos = j;
                if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC))
                    j += 2;
                else
                    j += 1;
            }
            buf[lastPos] = '\0';
        }

        // Copy into the global string buffer for this font slot
        int idx = gGameTable.FontIndex;
        strcpy(&gGameTable.String[261 * idx], buf);

        // Save the position
        gGameTable.FontXY[2 * idx] = x;
        gGameTable.FontXY[2 * idx + 1] = y;

        // Save the color
        switch (color)
        {
        case 0: gGameTable.FontColor[idx] = 0xD8E8E8; break;
        case 1: gGameTable.FontColor[idx] = 0xFF6030; break;
        case 2: gGameTable.FontColor[idx] = 0x4000BA; break;
        case 3: gGameTable.FontColor[idx] = 0x20BA00; break;
        case 4: gGameTable.FontColor[idx] = 0xBAC8C8; break;
        default: break;
        }

        idx++;
        gGameTable.FontIndex = idx;
        if (idx < 100)
            return 1;

        gGameTable.FontIndex = idx - 1;
        return 0;
    }

    // 0x00509930
    // Returns the byte length of the global save path std::string (0x689F44).
    static int save_path_len()
    {
        return interop::call<int>(0x00509930);
    }

    // 0x00432860
    // Strips a trailing ".biohazard2" or ".resident2" extension (case-insensitive)
    // from a memory card file name.
    static void strip_save_extension(char* str)
    {
        interop::call<void, char*>(0x00432860, str);
    }

    // 0x004315D0
    int save_menu_draw(int a1, int a2, int a3, int a4, int a5, int a6, char a7, uint8_t* a8)
    {
        static const char* aExit = (const char*)0x5220A4;
        static const char* aCreateNew = (const char*)0x5220B8;

        int y0[2] = { 30, 60 };
        int y1[2] = { 50, 100 };
        int y2[2] = { 288, 576 };
        char String[264];

        auto* pSurface = (LPDIRECTDRAWSURFACE7)gGameTable.pMarni->surface0.pDDsurface;
        HDC hdc;
        pSurface->GetDC(&hdc);
        auto oldFont = SelectObject(hdc, gGameTable.hFont);

        auto folderLen = save_path_len();
        auto saveFolder = GetSaveFolder();
        SIZE psizl;
        GetTextExtentPoint32A(hdc, saveFolder, folderLen, &psizl);

        auto is_480p = gGameTable.is_480p;
        int8_t scroll;
        int y2_val = y2[is_480p];
        if (psizl.cx <= y2_val)
            scroll = 0;
        else
            scroll = (int8_t)((psizl.cx - y2_val) / (gGameTable.FontH / 2));

        int scroll_pos = *(int32_t*)a8;
        if (scroll_pos < scroll)
            scroll = (int8_t)scroll_pos;

        int v12 = scroll;
        int v37 = scroll;
        SavePrint(0, y0[is_480p], saveFolder, 0, scroll);

        if (gGameTable.dword_986394 > 0)
        {
            int v14 = 0;
            int v15 = a3;
            do
            {
                if (a7 || (v14 + v15))
                {
                    int v16 = v14 + v15;
                    if (v16 >= gGameTable.cnt0 - a7 + 1)
                    {
                        int v27 = gGameTable.cnt1 - a7 + gGameTable.cnt0 + 1;
                        if (v16 >= v27)
                        {
                            if (v16 == v27)
                            {
                                SavePrint(0, y1[is_480p] + v14 * gGameTable.byte_6634F8, aExit, 0, 0);
                                break;
                            }
                        }
                        else
                        {
                            int v28 = v14 + a7 - gGameTable.cnt0 + v15 - 1;
                            wsprintfA(String, "[%s]", (const char*)(a5 + 261 * v28));
                            GetTextExtentPoint32A(hdc, String, (int)strlen(String), &psizl);
                            int8_t v30;
                            int v29 = y2[is_480p];
                            if (psizl.cx <= v29)
                                v30 = 0;
                            else
                                v30 = (int8_t)((psizl.cx - v29) / (gGameTable.FontH / 2));
                            if (*(int32_t*)a8 < v30)
                                v30 = (int8_t)*(int32_t*)a8;
                            if (v37 < v30)
                                v37 = v30;
                            SavePrint(0, y1[is_480p] + v14 * gGameTable.byte_6634F8, String, 4, v30);
                            v12 = v37;
                        }
                    }
                    else
                    {
                        int v17 = v15 + v14 + a7;
                        int v18 = a4 + 276 * v17;
                        strcpy(String, (char*)(v18 - 276));
                        strip_save_extension(String);
                        GetTextExtentPoint32A(hdc, String, (int)strlen(String), &psizl);
                        int8_t v23;
                        int v22 = y2[is_480p];
                        if (psizl.cx <= v22)
                            v23 = 0;
                        else
                            v23 = (int8_t)((psizl.cx - v22) / (gGameTable.FontH / 2));
                        if (*(int32_t*)a8 < v23)
                            v23 = (int8_t)*(int32_t*)a8;
                        int v24 = v23;
                        if (v37 < v23)
                            v37 = v23;
                        auto v25 = *(uint8_t*)(v18 - 15);
                        int v26;
                        if (v25 < 4)
                        {
                            if (*(uint8_t*)(v18 - 1))
                                v26 = 0;
                            else
                                v26 = ((v25 & 1) != 0) + 1;
                        }
                        else
                            v26 = 3;
                        SavePrint(0, y1[is_480p] + v14 * gGameTable.byte_6634F8, String, v26, v24);
                        v12 = v37;
                    }
                }
                else
                {
                    SavePrint(0, y1[is_480p], aCreateNew, 0, 0);
                }
                ++v14;
            } while (v14 < (int)gGameTable.dword_986394);
        }

        SelectObject(hdc, oldFont);
        pSurface->ReleaseDC(hdc);
        *(int32_t*)a8 = v12;
        auto result = gGameTable.dword_669B00 != v12;
        gGameTable.dword_669B00 = v12;
        return result;
    }

    // 0x00432080
    // Releases the memory card file lists (Cards, Names, pMem) and resets the
    // pointers so the next save screen run re-allocates them.
    void rsrc_release()
    {
        free(gGameTable.Cards);
        free(gGameTable.Names);
        free(gGameTable.pMem);
        gGameTable.Cards = nullptr;
        gGameTable.Names = nullptr;
        gGameTable.pMem = nullptr;
    }

    // 0x00432840
    static void ck_480p()
    {
        gGameTable.is_480p = gGameTable.pMarni->xsize != 320;
    }

    // 0x00432070
    // Clears a card-access work field during memory card screen initialization.
    static void reset_card_work(uint32_t* work, uint32_t* /*unused*/)
    {
        *work = 0;
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
    static int build_default_save_path()
    {
        char filename[264];
        OldStdString slice;

        // Use the directory of the running executable as the default save folder.
        GetModuleFileNameA(nullptr, filename, 0x105);
        str::string_copy(save_path_string(), filename);

        // Truncate to the directory portion (including the trailing backslash).
        int lastSlash = str::string_find_last(save_path_string(), "\\");
        str::string_slice(save_path_string(), &slice, lastSlash + 1);
        str::string_assign(save_path_string(), &slice);
        str::string_dtor(&slice);

        // Ensure the path ends with a backslash.
        int result = str::string_sjis_len(save_path_string()) - 1;
        if (str::string_find_last(save_path_string(), "\\") != result)
            return (int)str::string_append(save_path_string(), "\\");
        return result;
    }

    // 0x00509940
    // Copies the given save path into the global save-path string, ensuring it is
    // non-empty and ends with a backslash (appending one if the last Shift-JIS
    // character is not a backslash). Falls back to the module path when empty.
    static int set_save_folder(char* savePath)
    {
        str::string_copy(save_path_string(), savePath);
        if (str::string_sjis_len(save_path_string()) == 0)
            return build_default_save_path();

        int lastSlash = str::string_find_last(save_path_string(), "\\");
        int lastChar = str::string_sjis_len(save_path_string()) - 1;
        if (lastSlash != lastChar)
            return (int)str::string_append(save_path_string(), "\\");
        return lastChar;
    }

    // 0x00509B20
    static void save_name_prefix(char* dest, const char* src, int count)
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
    static void save_name_slice(char* dest, const char* src, int start, int count)
    {
        OldStdString src_str;
        OldStdString sliced;

        // Build src_str from the source string, then extract the substring
        // starting after `start` Shift-JIS characters, keeping up to `count`
        // characters, and copy the result into dest.
        // Used to grab the current character of the save name being typed.
        str::string_ctor_from_cstr(&src_str, src);
        str::string_sjis_copy(&src_str, &sliced, start, count);
        str::string_assign(&src_str, &sliced);
        str::string_dtor(&sliced);
        strcpy(dest, str::string_get_data(&src_str));
        str::string_dtor(&src_str);
    }

    // 0x00509BE0
    static int save_name_is_blank(const char* name)
    {
        OldStdString nameStr;
        str::string_ctor_from_cstr(&nameStr, name);
        // A save name consisting of only a space, underscore, or Shift-JIS blank
        // character is treated as an invalid/blank name (error sound is played).
        if (str::string_eq_cstr(&nameStr, " ")            // 0x51D868 - half-width space
            || str::string_eq_cstr(&nameStr, "\x81\x40")  // 0x540B48 - full-width space
            || str::string_eq_cstr(&nameStr, "_")         // 0x540B44 - underscore
            || str::string_eq_cstr(&nameStr, "\x81\x51")) // 0x5220D4
        {
            str::string_dtor(&nameStr);
            return 1;
        }
        else
        {
            str::string_dtor(&nameStr);
            return 0;
        }
    }

    // 0x005099A0
    // Changes the current save folder. If `name` is ".." the folder moves up one
    // directory level; otherwise a subfolder named `name` is entered. The resulting
    // path is stored as the global save folder via set_save_folder.
    static void change_save_folder(char* name)
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
        set_save_folder((char*)str::string_get_data(&path));
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
    // Uses the shared SavePrint (0x00431470) implementation to queue the text.
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
            reset_card_work(&gGameTable.dword_663190, (uint32_t*)&gGameTable.pMem);
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
                            change_save_folder((char*)gGameTable.Names + 261 * gGameTable.card_select);
                            cardState = CARD_STATE_ENUMERATE;
                            gGameTable.card_scroll = 0;
                            gGameTable.card_cursor = 0;
                            snd_se_on(0x4060000);
                            break;
                        case 3:
                            set_save_folder((char*)gGameTable.pMem + 8 * gGameTable.card_select);
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
                save_name_prefix((char*)gGameTable.p_card_save, gGameTable.save_name, ++gGameTable.card_name_index);
                save_name_slice(gGameTable.save_path, gGameTable.save_name, gGameTable.card_name_index - 1, 1);
                if (save_name_is_blank(gGameTable.save_path))
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
                save_name_prefix((char*)gGameTable.p_card_save, gGameTable.save_name, ++gGameTable.card_name_index);
                save_name_slice(gGameTable.save_path, gGameTable.save_name, gGameTable.card_name_index - 1, 1);
                if (save_name_is_blank(gGameTable.save_path))
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
            build_default_save_path();
        return save_path_string()->data;
    }

    void save_init_hooks()
    {
        interop::writeJmp(0x004C57E0, &mem_card);
        interop::writeJmp(0x00432080, &rsrc_release);
    }
}
