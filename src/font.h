#pragma once

#include "resmgr.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openre
{
    /**
     * One glyph record from a .fnt font.
     */
    struct FontGlyph
    {
        // Index into FontResource::textures.
        uint16_t textureIndex = 0;
        // Kerning: the glyph is drawn at pen_x - left and the pen then
        // advances by left + right. top/bottom are vertical bearings
        // (reserved, currently always 0).
        int32_t left = 0;
        int32_t top = 0;
        int32_t right = 0;
        int32_t bottom = 0;
        // Source rectangle within the texture.
        float srcX = 0;
        float srcY = 0;
        float srcW = 0;
        float srcH = 0;
    };

    /**
     * A font. Two flavours:
     *
     * - A parsed .fnt file (`fromFnt`): explicit glyph records keyed by
     *   Unicode codepoint across one or more textures (`textures` holds each
     *   referenced texture's TextureResource cookie). Characters missing from
     *   the map draw nothing and advance by defaultLeft + defaultRight; the
     *   line box height is defaultBottom - defaultTop (the defaults are
     *   signed vertical extents either side of the line).
     * - A raw RE2 font atlas ("common/data/font0", optionally ":small"):
     *   the atlas is uploaded as a TextureResource whose cookie is in
     *   `atlas`, and glyphs are located by implicit grid math (see
     *   text_layout.cpp). ASCII 0x20-0x7E maps to cell = codepoint in the
     *   14px/18-column grid; `small` selects the compact 8px-cell grid with
     *   32 cells per row at cell = codepoint - 0x20. Kana and kanji use the
     *   RE2 text-character table.
     *
     * .fnt fonts also fill the legacy `atlas`/`width`/`height` fields with
     * their first texture so grid-based fallbacks keep working.
     */
    class FontResource : public Resource
    {
    public:
        // Releases the atlas texture references owned by this font, so the
        // shared atlas unloads with the last font that uses it.
        ~FontResource() override;

        const char* getName() const override
        {
            return "font";
        }

        // ResourceCookie to the TextureResource holding the atlas. Glyph
        // sprite components set their `texture` field to this.
        ResourceCookie atlas = 0;

        int width = 0;  // atlas width in pixels
        int height = 0; // atlas height in pixels

        // Which physical font this atlas is (0 = font0, 1 = font1). Legacy
        // fonts only.
        int page = 0;

        // True for a "virtual" sub-font exposing the small compact ASCII set
        // (0x20-0x7F in an 8px-cell, 32-per-row grid on the same font0
        // texture). Legacy fonts only.
        bool small = false;

        // True when this resource was parsed from a .fnt file.
        bool fromFnt = false;

        // ---- TTF-backed fonts (fromTtf == true) ----
        // One CPU mirror + packing cursor per atlas page. `textures[i]` holds
        // the matching TextureResource cookie; the pixel buffer mirrors the
        // GPU texture so freshly rasterized glyphs can be blitted in and
        // re-uploaded without ever downloading pixels.
        struct TtfPage
        {
            std::vector<uint8_t> pixels;
            int cursorX = 0;
            int cursorY = 0;
            int rowH = 0;
        };
        // True when this resource was created from a TTF/OTF/TTC file.
        bool fromTtf = false;
        // The open TTF_Font* (SDL3_ttf). Opaque here so font.h does not drag
        // SDL_ttf headers into every translation unit.
        void* ttfFont = nullptr;
        // The raw font file bytes. SDL3_ttf reads glyph data lazily through
        // the SDL_IOStream it was opened with, so the source buffer must stay
        // alive for the whole lifetime of the font (not just the loader call).
        std::vector<uint8_t> ttfData;
        // Point/pixel size the font was opened at (SDL3_ttf defaults to 72 DPI
        // so 1pt == 1px).
        int pixelSize = 0;
        // Font-wide vertical metrics at this size (TTF only); also expressed
        // via defaultTop/defaultBottom below.
        int ascent = 0;
        int descent = 0;
        std::vector<TtfPage> ttfPages;

        // Rasterizes `codepoint` into the TTF glyph atlas on first use,
        // creating its FontGlyph record so layout can resolve it like any
        // other explicit glyph. Missing/blank glyphs still get an
        // advance-only record. Returns the record, or nullptr when the font
        // is not TTF-backed or has no usable rasterizer.
        const FontGlyph* ensureGlyph(uint32_t codepoint);

        // Default metrics for characters without an explicit record.
        int32_t defaultLeft = 0;
        int32_t defaultRight = 0;
        int32_t defaultTop = 0;
        int32_t defaultBottom = 0;

        struct FntTexture
        {
            std::string path;
            int width = 0;
            int height = 0;
            ResourceCookie atlas = 0;
        };
        std::vector<FntTexture> textures;
        std::unordered_map<uint32_t, FontGlyph> glyphs;

        int lineHeight() const
        {
            return this->defaultBottom - this->defaultTop;
        }

        const FontGlyph* findGlyph(uint32_t codepoint) const
        {
            auto it = this->glyphs.find(codepoint);
            return it != this->glyphs.end() ? &it->second : nullptr;
        }

        // Pair kerning (pixels to subtract from the pen advance between two
        // glyphs) for TTF-backed fonts. Returns 0 for non-TTF fonts.
        int kerning(uint32_t previous, uint32_t current) const;
    };

    /**
     * Loads a RE2 font (.fnt file, raw font0/font1 atlas, or a TTF/OTF/TTC
     * font) and registers it with the resource manager, returning a
     * ResourceCookie holding one reference (0 on failure).
     *
     * For an atlas path like "common/data/font0" the extension is probed
     * ("" / ".tim" / ".adt") via data://. The ":small" suffix selects the
     * compact ASCII sub-font of the same atlas. A path ending in ".fnt" is
     * parsed as the binary OpenRE font format; texture paths inside it are
     * resolved like any other logical asset path. A path ending in
     * ".ttf"/".otf"/".ttc" is loaded through SDL3_ttf at `size` pixels
     * (default 16; the size is also part of the resource key, so the same
     * file at different sizes registers separately).
     */
    ResourceCookie loadFontResource(std::string_view path, int size = 0);
}
