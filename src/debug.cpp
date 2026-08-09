#include "debug.h"
#include "gfx_draw.h"
#include "openre.h"
#include "re2.h"
#include "system_window.h"

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace openre::debug
{
    namespace
    {
        bool s_enabled = false;

        uint32_t s_fps_last_ticks = 0;
        uint32_t s_fps_frame_count = 0;
        uint32_t s_fps = 0;

        // Scroll offset (lines) into the per-frame draw-call log.
        int s_log_scroll = 0;
    }

    // Queues a formatted string for the top-most GDI text layer (drawn by
    // save_print_flush just before the frame is presented).
    void print(int x, int y, uint32_t color, const char* fmt, ...)
    {
        if (!s_enabled)
            return;

        int idx = gGameTable.FontIndex;
        if (idx >= 100)
            return;

        char buf[261];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        strcpy(&gGameTable.String[261 * idx], buf);
        gGameTable.FontXY[2 * idx] = x;
        gGameTable.FontXY[2 * idx + 1] = y;
        gGameTable.FontColor[idx] = color;
        gGameTable.FontIndex = idx + 1;
    }

    // Measures the on-screen pixel width of a string in the game GDI font.
    // A compatible DC is cached and reused; it is recreated when the font is
    // recreated (e.g. on resolution change).
    static int measure_text(const char* str)
    {
        static HDC s_dc = nullptr;
        static void* s_font = nullptr;

        auto* hFont = gGameTable.hFont;
        if (hFont == nullptr)
            return 0;

        if (s_dc == nullptr || s_font != hFont)
        {
            if (s_dc != nullptr)
                DeleteDC(s_dc);
            s_dc = CreateCompatibleDC(nullptr);
            if (s_dc == nullptr)
                return 0;
            SelectObject(s_dc, (HFONT)hFont);
            s_font = hFont;
        }

        SIZE size;
        if (!GetTextExtentPoint32A(s_dc, str, (int)strlen(str), &size))
            return 0;
        return size.cx;
    }

    // Queues a string right-aligned so its right edge sits at x_right.
    static void print_right(int x_right, int y, uint32_t color, const char* fmt, ...)
    {
        if (!s_enabled)
            return;

        char buf[261];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        print(x_right - measure_text(buf), y, color, "%s", buf);
    }

    // Renders the debug overlay. Called each frame right before save_print_flush.
    void draw()
    {
        if (!s_enabled)
            return;

        auto* pMarni = gGameTable.pMarni;
        if (pMarni == nullptr)
            return;

        // Rolling FPS over ~500ms windows
        auto now = system::window::get_ticks();
        if (s_fps_last_ticks == 0)
        {
            s_fps_last_ticks = now;
        }
        else
        {
            s_fps_frame_count++;
            auto elapsed = now - s_fps_last_ticks;
            if (elapsed >= 500)
            {
                s_fps = s_fps_frame_count * 1000 / elapsed;
                s_fps_last_ticks = now;
                s_fps_frame_count = 0;
            }
        }

        const int margin = 4;
        const int lineHeight = gGameTable.byte_6634F8; // 30 (480p) or 15 (320p)
        const auto xsize = pMarni->xsize;
        const uint32_t color = 0xFFFFFF;

        // Left column
        int y = margin;
        print(margin, y, color, "OpenRE v%s", OPENRE_VERSION);
        y += lineHeight;
        print(margin, y, color, "FPS: %u", s_fps);
        y += lineHeight;
        print(margin, y, color, "Frame: %u", gGameTable.frame_current);
        y += lineHeight;
        print(margin, y, color, "Time: %us", gGameTable.game_seconds);
        y += lineHeight;
        print(margin, y, color, "Stage: %u  Room: %u", gGameTable.current_stage, gGameTable.current_room);
        y += lineHeight;

        auto* player = gGameTable.player_work;
        if (player != nullptr)
        {
            print(margin, y, color, "Pos: %d, %d, %d", player->m.pos.x, player->m.pos.y, player->m.pos.z);
            y += lineHeight;
            print(margin, y, color, "HP: %d/%d", player->life, player->max_life);
            y += lineHeight;
        }

        // Draw-call log (most recent first, scrollable with PgUp/PgDn)
        const auto& stats = gfx_draw::draw_stats();
        print(margin, y, color, "Draw calls: %d", stats.log_count);
        y += lineHeight;

        // Cap the scroll at the oldest entry.
        const int maxScroll = stats.log_count > 0 ? stats.log_count - 1 : 0;
        if (s_log_scroll > maxScroll)
            s_log_scroll = maxScroll;

        const char* names[static_cast<int>(gfx_draw::DrawKind::Count)] = {
            "Sprt", "FT4", "Mask", "BgScl", "SclSprt", "SclPoly", "GT4", "FT4_2", "F4", "Tile", "Line",
        };

        // Show the last ~20 entries that fit on screen.
        const int visible = (pMarni->ysize - y) / lineHeight - 1;
        const int rows = (std::min)(visible, 20);
        for (int i = stats.log_count - 1 - s_log_scroll, row = 0; i >= 0 && row < rows; i--, row++)
        {
            const auto& rec = stats.log[i % gfx_draw::DRAW_CALL_LOG_SIZE];
            const char* name = names[static_cast<int>(rec.kind)];
            if (rec.page == 0xFFFF)
                print(margin, y, color, "%s z=%d (%d,%d)-(%d,%d)", name, rec.z, rec.x0, rec.y0, rec.x1, rec.y1);
            else
                print(margin, y, color, "%s z=%d p=%d (%d,%d)-(%d,%d)", name, rec.z, rec.page, rec.x0, rec.y0, rec.x1, rec.y1);
            y += lineHeight;
        }

        // Right column
        y = margin;
        print_right(xsize - margin, y, color, "%ux%u", xsize, pMarni->ysize);
        y += lineHeight;
        print_right(xsize - margin, y, color, "%s", gGameTable.is_480p ? "480p" : "320p");
        y += lineHeight;
        print_right(xsize - margin, y, color, "Vsync: %u", gGameTable.vsync_rate);
        y += lineHeight;
        print_right(xsize - margin, y, color, "Enemies: %u", gGameTable.enemy_count);
    }

    void toggle()
    {
        s_enabled = !s_enabled;
    }

    void scroll_log(int lines)
    {
        if (!s_enabled)
            return;
        if (lines < 0 && s_log_scroll > 0)
            s_log_scroll = (std::max)(0, s_log_scroll + lines);
        else if (lines > 0)
            s_log_scroll += lines;
    }

    bool enabled()
    {
        return s_enabled;
    }
}
