#pragma once

#include "ecs.h"
#include "ecs_components.h"
#include "resmgr.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openre
{
    class FontResource;
}

namespace openre::ecs
{
    class Scene;
}

namespace openre::text
{
    // Legacy font-atlas grid sizes shared with the Lua bindings
    // (gfx.measureText must mirror layout metrics exactly).
    // Big font page 0 (font0): 14px cells in an 18-column grid.
    inline constexpr float kCellSize = 14.0f;
    // Small variant (:small): a grid of 8px cells, 32 per row. The cell
    // index is (codepoint - 0x20), so space=0, '!'=1, 'A'=33 (col 1, row 1).
    inline constexpr float kSmallCellSize = 8.0f;

    /// One resolved glyph of a text string: the font-atlas cell (or explicit
    /// .fnt glyph) to draw, its colour, and its advance offset. Produced by
    /// laying out a TextComponent.
    struct GlyphRun
    {
        // Legacy grid fonts: 0-based index into the font atlas grid this
        // glyph lives in, and which atlas page it is on.
        int cell = 0;
        int page = 0;

        // .fnt fonts: explicit glyph geometry resolved from the font's glyph
        // map. When `hasSrc` is set the sprite uses this source rectangle and
        // texture; otherwise it is derived from `cell`/`page` grid math.
        bool hasSrc = false;
        float srcX = 0;
        float srcY = 0;
        float srcW = 0;
        float srcH = 0;
        ResourceCookie texture = 0;

        // Horizontal advance (left + right for .fnt glyphs, the cell width
        // otherwise).
        float advanceX = 0;
        float advanceY = 0;
        // The glyph is drawn at pen_x - leftBearing (.fnt kerning).
        float leftBearing = 0;

        // Colour resolved from <span> markup.
        ecs::Color color;

        // True for characters that occupy horizontal advance but draw no
        // glyph (e.g. the space). No sprite is created, but x still advances.
        bool blank = false;
    };

    /// Resolves a UTF-8 codepoint to a legacy font-atlas cell index + page.
    /// ASCII 0x20-0x7E map directly to their codepoint on page 0; kana and
    /// kanji use the RE2 text-character table. When `small` is true the
    /// compact ASCII set (0x20-0x7F in an 8px-cell, 32-per-row grid,
    /// cell = codepoint - 0x20) is used instead.
    int lookupCell(uint32_t codepoint, int& outPage, bool small);

    /// Decodes `text` (with optional <span> markup) into a flat list of
    /// glyphs, measuring line width as it goes. Glyphs are resolved against
    /// `font`: .fnt fonts provide explicit records with kerning, legacy
    /// atlas fonts use grid cells at `fallbackAdvanceX`. `fallbackAdvanceY`
    /// is the line height used for either flavour when the font does not
    /// override it.
    std::vector<GlyphRun>
    decodeText(std::string_view text, const FontResource* font, float fallbackAdvanceX, float fallbackAdvanceY);

    /// Width of the longest (single) line in `runs`.
    float measureWidth(const std::vector<GlyphRun>& runs);

    /// Rebuilds the per-glyph child SpriteComponents beneath `entity` for the
    /// given TextComponent. Glyph entities are tracked in TextComponent.glyphs
    /// and reused in place; only the difference is created/destroyed. Called
    /// by TextLayoutSystem only when relayout is required.
    void layoutEntity(ecs::Scene& scene, ecs::Entity entity, ecs::TextComponent& text);

    /// Whether `text`'s inputs differ from the last laid-out fingerprint. The
    /// alignment band is read from the entity's UiSizeComponent. Sets
    /// `outFingerprint` to the current fingerprint for the caller to store.
    void fingerprint(ecs::Scene& scene, ecs::Entity entity, const ecs::TextComponent& text, std::string& outFingerprint);
}
