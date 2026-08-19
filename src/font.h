#pragma once

#include "resmgr.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace openre
{
    /**
     * A decoded RE2 font atlas (font0.tim / font1.tim) plus the glyph-cell
     * lookup used to map Unicode text into 14x14 texture cells.
     *
     * - The atlas is uploaded as a TextureResource; its cookie is stored in
     *   `atlas` so glyph SpriteComponents can reference it via SpriteComponent.
     * - The texture's palette index 0 is forced transparent so glyph cells
     *   render with a clear background (the raw font palette uses entry 0 for
     *   an opaque green).
     * - Glyph cell for byte/code N is: u = 14 * (N % 18), v = 14 * (N / 18).
     *   ASCII 0x20-0x7E map directly to their codepoint. Kana/kanji use the
     *   RE2 text character table (see glyph.cpp).
     *
     * `small` is a virtual sub-font exposing the compact ASCII set (0x20-0x7F)
     * in a grid of 8px cells, 32 per row. The cell index is (codepoint -
     * 0x20): space=0, '!'=1, ..., 'A'=33 (col 1, row 1). Glyph cell N is at
     * u = 8 * (N % 32), v = 8 * (N / 32).
     */
    class FontResource : public Resource
    {
    public:
        ~FontResource() override = default;

        const char* getName() const override
        {
            return "font";
        }

        // ResourceCookie to the TextureResource holding the atlas. Glyph
        // sprite components set their `texture` field to this.
        ResourceCookie atlas = 0;

        int width = 0;  // atlas width in pixels
        int height = 0; // atlas height in pixels

        // Which physical font this atlas is (0 = font0, 1 = font1).
        int page = 0;

        // True for a "virtual" sub-font exposing the small compact ASCII set
        // (0x20-0x7F in an 8px-cell, 32-per-row grid on the same font0 texture).
        bool small = false;
    };

    /**
     * Loads a RE2 font (font0.tim / font1.tim) and registers it with the
     * resource manager, returning a ResourceCookie holding one reference
     * (0 on failure). `path` is a logical asset path like
     * "common/data/font0" (probes "" / ".tim" / ".adt" via data://),
     * matching loadTextureResource.
     */
    ResourceCookie loadFontResource(std::string_view path);
}
