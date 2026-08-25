#include "text_layout.h"

#include "ecs_components.h"
#include "ecs_scene.h"
#include "font.h"
#include "render2d.h"
#include "resmgr.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace openre::text
{
    using openre::ecs::Color;
    using openre::ecs::Entity;
    using openre::ecs::SpriteComponent;
    using openre::ecs::TextAlignX;
    using openre::ecs::TextAlignY;
    using openre::ecs::TextFlags;
    using openre::ecs::TransformComponent;
    using openre::ecs::UiSizeComponent;

    namespace
    {
        constexpr int kGridCols = 18;
        constexpr int kSmallGridCols = 32;

        // Decodes one UTF-8 sequence at `str` into a codepoint, advancing
        // `str` past it. Returns false on invalid/truncated input (the byte is
        // treated as a lone codepoint and consumed as one).
        bool decodeUtf8(const char*& str, const char* end, uint32_t& out)
        {
            const uint8_t c0 = (uint8_t)*str;
            if (c0 < 0x80)
            {
                out = c0;
                ++str;
                return true;
            }
            if (c0 < 0xC2 || str + 1 >= end)
            {
                out = c0;
                ++str;
                return false;
            }
            const uint8_t c1 = (uint8_t)str[1];
            if ((c1 & 0xC0) != 0x80)
            {
                out = c0;
                ++str;
                return false;
            }
            if (c0 < 0xE0) // 2-byte
            {
                out = ((uint32_t)(c0 & 0x1F) << 6) | (c1 & 0x3F);
                str += 2;
                return true;
            }
            if (str + 2 >= end)
            {
                out = c0;
                ++str;
                return false;
            }
            const uint8_t c2 = (uint8_t)str[2];
            if ((c2 & 0xC0) != 0x80)
            {
                out = c0;
                ++str;
                return false;
            }
            if (c0 < 0xF0) // 3-byte
            {
                out = ((uint32_t)(c0 & 0x0F) << 12) | ((uint32_t)(c1 & 0x3F) << 6) | (c2 & 0x3F);
                str += 3;
                return true;
            }
            if (str + 3 >= end)
            {
                out = c0;
                ++str;
                return false;
            }
            const uint8_t c3 = (uint8_t)str[3];
            if ((c3 & 0xC0) != 0x80)
            {
                out = c0;
                ++str;
                return false;
            }
            out = ((uint32_t)(c0 & 0x07) << 18) | ((uint32_t)(c1 & 0x3F) << 12) | ((uint32_t)(c2 & 0x3F) << 6) | (c3 & 0x3F);
            str += 4;
            return true;
        }

        // Parses a #rgb, #rrggbb or #rrggbbaa hex colour.
        bool parseHexColor(std::string_view s, ecs::Color& out)
        {
            if (s.empty() || s[0] != '#')
                return false;
            auto nib = [](char c) -> int {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                return -1;
            };
            std::vector<int> vals;
            for (size_t i = 1; i < s.size(); i++)
            {
                int n = nib(s[i]);
                if (n < 0)
                    return false;
                vals.push_back(n);
            }
            auto byte = [&](int a, int b) { return (uint8_t)((a << 4) | b); };
            if (vals.size() == 3)
            {
                out = ecs::Color{ byte(vals[0], vals[0]), byte(vals[1], vals[1]), byte(vals[2], vals[2]), 255 };
                return true;
            }
            if (vals.size() == 6)
            {
                out = ecs::Color{ byte(vals[0], vals[1]), byte(vals[2], vals[3]), byte(vals[4], vals[5]), 255 };
                return true;
            }
            if (vals.size() == 8)
            {
                out = ecs::Color{
                    byte(vals[0], vals[1]), byte(vals[2], vals[3]), byte(vals[4], vals[5]), byte(vals[6], vals[7])
                };
                return true;
            }
            return false;
        }
    } // namespace

    int lookupCell(uint32_t codepoint, int& outPage, bool small)
    {
        outPage = 0;

        // Small variant: the grid holds ASCII 0x20-0x7F in order; cell index
        // is (codepoint - 0x20), so space=0 and 'A'=33 (col 1, row 1).
        if (small)
        {
            if (codepoint >= 0x20 && codepoint <= 0x7F)
                return (int)(codepoint - 0x20);
            return -1; // unsupported -> blank
        }

        if (codepoint >= 0x20 && codepoint <= 0x7E)
            return (int)codepoint;

        // Hiragana / katakana mapped from the RE2 text-character table. The
        // table lists kana at their font-atlas cell indices (verified against
        // our PC font0.tim: 0xA7=ア, 0x85=が, 0x57=あ). Represent the mapping as
        // {unicode, cell} pairs.
        struct Kana
        {
            uint32_t unicode;
            int cell;
        };
        // Sorted by unicode.
        static const Kana kanaTable[] = {
            // Hiragana: ぁ 0x3041 .. ん 0x3093
            { 0x3041, 0x9E }, // ぁ
            { 0x3042, 0x57 }, // あ
            { 0x3043, 0x9F }, // ぃ
            { 0x3044, 0x58 }, // い
            { 0x3045, 0xA0 }, // ぅ
            { 0x3046, 0x59 }, // う
            { 0x3047, 0xA1 }, // ぇ
            { 0x3048, 0x5A }, // え
            { 0x3049, 0xA2 }, // ぉ
            { 0x304A, 0x5B }, // お
            { 0x304B, 0x5C }, // か
            { 0x304C, 0x85 }, // が
            { 0x304D, 0x5D }, // き
            { 0x304E, 0x86 }, // ぎ
            { 0x304F, 0x5E }, // く
            { 0x3050, 0x87 }, // ぐ
            { 0x3051, 0x5F }, // け
            { 0x3052, 0x88 }, // げ
            { 0x3053, 0x60 }, // こ
            { 0x3054, 0x89 }, // ご
            { 0x3055, 0x61 }, // さ
            { 0x3056, 0x8A }, // ざ
            { 0x3057, 0x62 }, // し
            { 0x3058, 0x8B }, // じ
            { 0x3059, 0x63 }, // す
            { 0x305A, 0x8C }, // ず
            { 0x305B, 0x64 }, // せ
            { 0x305C, 0x8D }, // ぜ
            { 0x305D, 0x65 }, // そ
            { 0x305E, 0x8E }, // ぞ
            { 0x305F, 0x66 }, // た
            { 0x3060, 0x8F }, // だ
            { 0x3061, 0x67 }, // ち
            { 0x3062, 0x90 }, // ぢ
            { 0x3063, 0xA6 }, // っ
            { 0x3064, 0x68 }, // つ
            { 0x3065, 0x91 }, // づ
            { 0x3066, 0x69 }, // て
            { 0x3067, 0x92 }, // で
            { 0x3068, 0x6A }, // と
            { 0x3069, 0x93 }, // ど
            { 0x306A, 0x6B }, // な
            { 0x306B, 0x6C }, // に
            { 0x306C, 0x6D }, // ぬ
            { 0x306D, 0x6E }, // ね
            { 0x306E, 0x6F }, // の
            { 0x306F, 0x70 }, // は
            { 0x3070, 0x94 }, // ば
            { 0x3071, 0x99 }, // ぱ
            { 0x3072, 0x71 }, // ひ
            { 0x3073, 0x95 }, // び
            { 0x3074, 0x9A }, // ぴ
            { 0x3075, 0x72 }, // ふ
            { 0x3076, 0x96 }, // ぶ
            { 0x3077, 0x9B }, // ぷ
            { 0x3078, 0x73 }, // へ
            { 0x3079, 0x97 }, // べ
            { 0x307A, 0x9C }, // ぺ
            { 0x307B, 0x74 }, // ほ
            { 0x307C, 0x98 }, // ぼ
            { 0x307D, 0x9D }, // ぽ
            { 0x307E, 0x75 }, // ま
            { 0x307F, 0x76 }, // み
            { 0x3080, 0x77 }, // む
            { 0x3081, 0x78 }, // め
            { 0x3082, 0x79 }, // も
            { 0x3083, 0xA3 }, // ゃ
            { 0x3084, 0x7A }, // や
            { 0x3085, 0xA4 }, // ゅ
            { 0x3086, 0x7B }, // ゆ
            { 0x3087, 0xA5 }, // ょ
            { 0x3088, 0x7C }, // よ
            { 0x3089, 0x7D }, // ら
            { 0x308A, 0x7E }, // り
            { 0x308B, 0x7F }, // る
            { 0x308C, 0x80 }, // れ
            { 0x308D, 0x81 }, // ろ
            { 0x308E, 0x9E }, // ゎ (reserve small wa)
            { 0x308F, 0x82 }, // わ
            { 0x3090, 0x82 }, // ゐ
            { 0x3091, 0x80 }, // ゑ
            { 0x3092, 0x83 }, // を
            { 0x3093, 0x84 }, // ん
            // Katakana: ア U+30A1..ヴ U+30F4
            { 0x30A1, 0xF1 }, // ァ
            { 0x30A2, 0xA7 }, // ア
            { 0x30A3, 0xF2 }, // ィ
            { 0x30A4, 0xA8 }, // イ
            { 0x30A5, 0xF3 }, // ゥ
            { 0x30A6, 0xA9 }, // ウ
            { 0x30A7, 0xF4 }, // ェ
            { 0x30A8, 0xAA }, // エ
            { 0x30A9, 0xF5 }, // ォ
            { 0x30AA, 0xAB }, // オ
            { 0x30AB, 0xAC }, // カ
            { 0x30AC, 0xD5 }, // ガ
            { 0x30AD, 0xAD }, // キ
            { 0x30AE, 0xD6 }, // ギ
            { 0x30AF, 0xAE }, // ク
            { 0x30B0, 0xD7 }, // グ
            { 0x30B1, 0xAF }, // ケ
            { 0x30B2, 0xD8 }, // ゲ
            { 0x30B3, 0xB0 }, // コ
            { 0x30B4, 0xD9 }, // ゴ
            { 0x30B5, 0xB1 }, // サ
            { 0x30B6, 0xDA }, // ザ
            { 0x30B7, 0xB2 }, // シ
            { 0x30B8, 0xDB }, // ジ
            { 0x30B9, 0xB3 }, // ス
            { 0x30BA, 0xDC }, // ズ
            { 0x30BB, 0xB4 }, // セ
            { 0x30BC, 0xDD }, // ゼ
            { 0x30BD, 0xB5 }, // ソ
            { 0x30BE, 0xDE }, // ゾ
            { 0x30BF, 0xB6 }, // タ
            { 0x30C0, 0xDF }, // ダ
            { 0x30C1, 0xB7 }, // チ
            { 0x30C2, 0xE0 }, // ヂ
            { 0x30C3, 0xF6 }, // ッ
            { 0x30C4, 0xB8 }, // ツ
            { 0x30C5, 0xE1 }, // ヅ
            { 0x30C6, 0xB9 }, // テ
            { 0x30C7, 0xE2 }, // デ
            { 0x30C8, 0xBA }, // ト
            { 0x30C9, 0xE3 }, // ド
            { 0x30CA, 0xBB }, // ナ
            { 0x30CB, 0xBC }, // ニ
            { 0x30CC, 0xBD }, // ヌ
            { 0x30CD, 0xBE }, // ネ
            { 0x30CE, 0xBF }, // ノ
            { 0x30CF, 0xC0 }, // ハ
            { 0x30D0, 0xE4 }, // バ
            { 0x30D1, 0xE9 }, // パ
            { 0x30D2, 0xC1 }, // ヒ
            { 0x30D3, 0xE5 }, // ビ
            { 0x30D4, 0xEA }, // ピ
            { 0x30D5, 0xC2 }, // フ
            { 0x30D6, 0xE6 }, // ブ
            { 0x30D7, 0xEB }, // プ
            { 0x30D8, 0xC3 }, // ヘ
            { 0x30D9, 0xE7 }, // ベ
            { 0x30DA, 0xEC }, // ペ
            { 0x30DB, 0xC4 }, // ホ
            { 0x30DC, 0xE8 }, // ボ
            { 0x30DD, 0xED }, // ポ
            { 0x30DE, 0xC5 }, // マ
            { 0x30DF, 0xC6 }, // ミ
            { 0x30E0, 0xC7 }, // ム
            { 0x30E1, 0xC8 }, // メ
            { 0x30E2, 0xC9 }, // モ
            { 0x30E3, 0xF3 }, // ャ
            { 0x30E4, 0xCA }, // ヤ
            { 0x30E5, 0xF4 }, // ュ
            { 0x30E6, 0xCB }, // ユ
            { 0x30E7, 0xF5 }, // ョ
            { 0x30E8, 0xCC }, // ヨ
            { 0x30E9, 0xCD }, // ラ
            { 0x30EA, 0xCE }, // リ
            { 0x30EB, 0xCF }, // ル
            { 0x30EC, 0xD0 }, // レ
            { 0x30ED, 0xD1 }, // ロ
            { 0x30EF, 0xD2 }, // ワ
            { 0x30F2, 0xD3 }, // ヲ
            { 0x30F3, 0xD4 }, // ン
            { 0x30F4, 0xED }, // ヴ
        };
        static const int kanaCount = (int)(sizeof(kanaTable) / sizeof(kanaTable[0]));

        // Binary search.
        int lo = 0, hi = kanaCount - 1;
        while (lo <= hi)
        {
            int mid = (lo + hi) / 2;
            if (kanaTable[mid].unicode < codepoint)
                lo = mid + 1;
            else if (kanaTable[mid].unicode > codepoint)
                hi = mid - 1;
            else
                return kanaTable[mid].cell;
        }
        return -1;
    }

    std::vector<GlyphRun>
    decodeText(std::string_view text, const FontResource* font, float fallbackAdvanceX, float fallbackAdvanceY)
    {
        std::vector<GlyphRun> runs;
        runs.reserve(text.size());

        ecs::Color color{ 255, 255, 255, 255 };
        int page = 0;

        const char* p = text.data();
        const char* end = text.data() + text.size();
        while (p < end)
        {
            // Markup: <span ...>
            if (*p == '<')
            {
                const char* close = std::strchr(p, '>');
                if (close != nullptr)
                {
                    const std::string_view tag(p, (size_t)(close - p) + 1);
                    // color="#ff0"
                    const char* eq = std::strstr(p, "color=");
                    if (eq != nullptr && eq < close)
                    {
                        const char* q = std::strchr(eq, '"');
                        const char* q2 = q != nullptr ? std::strchr(q + 1, '"') : nullptr;
                        if (q != nullptr && q2 != nullptr)
                        {
                            ecs::Color c;
                            if (parseHexColor(std::string_view(q + 1, (size_t)(q2 - q) - 1), c))
                                color = c;
                        }
                    }
                    p = close + 1;
                    continue;
                }
            }

            uint32_t cp = 0;
            decodeUtf8(p, end, cp);

            GlyphRun g;
            g.color = color;
            g.advanceY = fallbackAdvanceY;

            const FontGlyph* glyph = font != nullptr && font->fromFnt ? font->findGlyph(cp) : nullptr;
            if (glyph != nullptr && cp != 0x20)
            {
                // Explicit .fnt record: kerning-aware metrics and source rect.
                g.hasSrc = true;
                g.srcX = glyph->srcX;
                g.srcY = glyph->srcY;
                g.srcW = glyph->srcW;
                g.srcH = glyph->srcH;
                g.texture = font->textures[glyph->textureIndex].atlas;
                g.advanceX = static_cast<float>(glyph->left + glyph->right);
                g.leftBearing = static_cast<float>(glyph->left);
            }
            else if (font != nullptr && font->fromFnt)
            {
                // Spaces and characters missing from the map draw nothing but
                // still advance the pen, by their recorded metrics or the
                // header defaults.
                g.blank = true;
                const int32_t left = glyph != nullptr ? glyph->left : font->defaultLeft;
                const int32_t right = glyph != nullptr ? glyph->right : font->defaultRight;
                g.advanceX = static_cast<float>(left + right);
                if (g.advanceX <= 0)
                    g.advanceX = fallbackAdvanceX > 0 ? fallbackAdvanceX : kCellSize;
            }
            else
            {
                int cell = lookupCell(cp, page, font != nullptr && font->small);
                g.cell = cell >= 0 ? cell : 0;
                g.page = page;
                // Literal spaces (and any glyph we can't resolve) are blank:
                // they advance the pen but draw nothing. Cell 0x20 in the PC
                // atlas is not a blank cell (it holds a stray glyph), so we
                // must not draw it for spaces.
                g.blank = cp == 0x20 || cell < 0;
                g.advanceX = fallbackAdvanceX > 0 ? fallbackAdvanceX : kCellSize;
            }
            runs.push_back(g);
        }
        return runs;
    }

    float measureWidth(const std::vector<GlyphRun>& runs)
    {
        float w = 0;
        for (const auto& g : runs)
            w += g.advanceX;
        return w;
    }

    void fingerprint(ecs::Scene& scene, ecs::Entity entity, const ecs::TextComponent& text, std::string& outFingerprint)
    {
        char buf[64];
        const auto* size = scene.tryGetComponent<UiSizeComponent>(entity);
        const float w = size != nullptr ? size->width : 0.0f;
        const float h = size != nullptr ? size->height : 0.0f;
        std::snprintf(buf, sizeof(buf), "|flags=%u|font=%u|w=%g|h=%g|", text.flags, text.font, w, h);
        outFingerprint = text.text + buf;
    }

    // Destroys every glyph entity recorded on `text` and clears the list.
    void destroyGlyphs(ecs::Scene& scene, ecs::TextComponent& text)
    {
        for (Entity g : text.glyphs)
        {
            if (scene.isValid(g))
                scene.destroyEntity(g);
        }
        text.glyphs.clear();
    }

    void layoutEntity(ecs::Scene& scene, ecs::Entity entity, ecs::TextComponent& text)
    {
        // Resolve the font resource to find its atlas texture cookie.
        const FontResource* font = nullptr;
        ResourceCookie atlas = 0;
        bool small = false;
        if (text.font != 0)
        {
            auto* resource = ResourceManager::get().fromCookie<FontResource>(text.font);
            if (resource != nullptr)
            {
                font = resource;
                atlas = font->atlas;
                small = font->small;
            }
        }
        if (font == nullptr || atlas == 0)
        {
            // No usable font: drop any previous glyphs so stale sprites do
            // not linger on screen.
            destroyGlyphs(scene, text);
            return;
        }

        const float cellSize = small ? kSmallCellSize : kCellSize;
        auto runs = decodeText(text.text, font, cellSize, cellSize);

        const float totalW = measureWidth(runs);
        const float totalH = font->fromFnt ? static_cast<float>(font->lineHeight()) : cellSize;

        // The text entity's transform is the centre of its bounding box, and
        // glyphs are rendered centred on their own transforms (see render2d), so
        // align within a box centred on the origin.
        const TextAlignX ax = (TextAlignX)((text.flags & (uint32_t)TextFlags::XMask) >> 0);
        const TextAlignY ay = (TextAlignY)((text.flags & (uint32_t)TextFlags::YMask) >> 2);
        const auto* size = scene.tryGetComponent<UiSizeComponent>(entity);
        const float bandW = size != nullptr ? size->width : 0.0f;
        const float bandH = size != nullptr ? size->height : 0.0f;
        float originX = 0;
        if (ax == TextAlignX::Center)
            originX = -totalW * 0.5f;
        else if (ax == TextAlignX::Right)
            originX = bandW * 0.5f - totalW;
        else
            originX = -bandW * 0.5f; // Left
        float originY = 0;
        if (ay == TextAlignY::Middle)
            originY = -totalH * 0.5f;
        else if (ay == TextAlignY::Bottom)
            originY = bandH * 0.5f - totalH;
        else
            originY = -bandH * 0.5f; // Top

        float x = originX;
        float y = originY;

        // Match the glyph child entities to the drawable run count: destroy
        // surplus glyphs, create missing ones (as direct children of the text
        // entity), then update every survivor in place.
        size_t drawableCount = 0;
        for (const auto& run : runs)
        {
            if (!run.blank)
                drawableCount++;
        }
        text.glyphs.erase(
            std::remove_if(text.glyphs.begin(), text.glyphs.end(), [&](Entity g) { return !scene.isValid(g); }),
            text.glyphs.end());
        while (text.glyphs.size() > drawableCount)
        {
            scene.destroyEntity(text.glyphs.back());
            text.glyphs.pop_back();
        }
        while (text.glyphs.size() < drawableCount)
            text.glyphs.push_back(scene.createEntity(entity));

        size_t glyphIndex = 0;
        for (auto& run : runs)
        {
            // Blank glyphs (spaces) advance the pen but draw nothing.
            if (run.blank)
            {
                x += run.advanceX;
                continue;
            }

            Entity g = text.glyphs[glyphIndex++];
            auto& transform = scene.getComponent<TransformComponent>(g);
            // Sprites are centred on their position (render2d). Grid glyphs
            // fill the full cell; .fnt glyphs sit at pen_x - leftBearing and
            // are centred on their own source rectangle.
            if (run.hasSrc)
                transform.position.x = x - run.leftBearing + run.srcW * 0.5f;
            else
                transform.position.x = x + run.advanceX * 0.5f;
            transform.position.y = y + run.advanceY * 0.5f;

            auto* sprite = scene.tryGetComponent<SpriteComponent>(g);
            if (sprite == nullptr)
                sprite = &scene.addComponent<SpriteComponent>(g);
            auto* glyphSize = scene.tryGetComponent<UiSizeComponent>(g);
            if (glyphSize == nullptr)
                glyphSize = &scene.addComponent<UiSizeComponent>(g);
            if (run.hasSrc)
            {
                glyphSize->width = run.srcW;
                glyphSize->height = run.srcH;
                sprite->src = { run.srcX, run.srcY, run.srcW, run.srcH };
                sprite->texture = run.texture;
            }
            else
            {
                glyphSize->width = run.advanceX;
                glyphSize->height = run.advanceY;
                sprite->texture = atlas;
                if (small)
                {
                    sprite->src = {
                        kSmallCellSize * (run.cell % kSmallGridCols),
                        kSmallCellSize * (run.cell / kSmallGridCols),
                        kSmallCellSize,
                        kSmallCellSize,
                    };
                }
                else
                {
                    sprite->src = {
                        14.0f * (run.cell % kGridCols),
                        14.0f * (run.cell / kGridCols),
                        kCellSize,
                        kCellSize,
                    };
                }
            }
            sprite->color = run.color;
            x += run.advanceX;
        }
    }
}
