#pragma once

#include "ecs.h"
#include "ecs_components.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openre::ecs
{
    class Scene;
}

namespace openre::text
{
    /// One resolved glyph of a text string: the font-atlas cell to draw, its
    /// colour, and its advance offset. Produced by laying out a TextComponent.
    struct GlyphRun
    {
        // 0-based index into the font atlas grid this glyph lives in.
        int cell = 0;
        // Which font atlas page the glyph lives on (0 = font0, 1 = font1).
        int page = 0;
        // Horizontal advance (cell width in the atlas, normally 14px).
        float advanceX = 0;
        float advanceY = 0;
        // Colour resolved from <span> markup.
        ecs::Color color;
        // True for characters that occupy horizontal advance but draw no
        // glyph (e.g. the space). No sprite is created, but x still advances.
        bool blank = false;
    };

    /// Resolves a UTF-8 codepoint to a font-atlas cell index + page.
    /// ASCII 0x20-0x7E map directly to their codepoint on page 0; kana and
    /// kanji use the RE2 text-character table (see glyph.cpp). When `small`
    /// is true the compact ASCII set (0x20-0x7F in an 8px-cell, 32-per-row
    /// grid, cell = codepoint - 0x20) is used instead.
    int lookupCell(uint32_t codepoint, int& outPage, bool small);

    /// Decodes `text` (with optional <span> markup) into a flat list of glyph
    /// cells/colours, measuring line width as it goes.
    std::vector<GlyphRun> decodeText(std::string_view text, float advanceX, float advanceY, bool small);

    /// Width of the longest (single) line in `runs`.
    float measureWidth(const std::vector<GlyphRun>& runs);

    /// Rebuilds the per-glyph child SpriteComponents beneath `entity` for the
    /// given TextComponent. Clears the previous layout container first. Called
    /// by TextLayoutSystem only when relayout is required.
    void layoutEntity(ecs::Scene& scene, ecs::Entity entity, ecs::TextComponent& text);

    /// Whether `text`'s inputs differ from the last laid-out fingerprint. Sets
    /// `outFingerprint` to the current fingerprint for the caller to store.
    void fingerprint(const ecs::TextComponent& text, std::string& outFingerprint);
}
