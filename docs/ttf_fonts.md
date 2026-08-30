# TTF font rendering

Investigation into rendering real TTF/OTF fonts (via SDL3_ttf) for the Lua ECS
text pipeline (`gfx.loadFont` + `TextComponent`), as opposed to the existing
`.fnt` binary fonts and legacy RE2 grid atlases.

Goal: `gfx.loadFont("somefont.ttf")` works like today, returns a font handle,
and `TextComponent`s render with it. Focus languages: English, French, Japanese,
plus technical/code symbols. Fancy joint ligatures (Arabic-style joining) out of
scope.

---

## Current text pipeline (context)

- `FontResource` (src/font.h): holds either
  - parsed `.fnt` files → explicit per-codepoint `FontGlyph` records (src rect,
    kerning bearings `left/right`, texture index), or
  - raw RE2 atlas (`common/data/font0`) → grid math, no glyph map.
- `text::decodeText` (src/text_layout.cpp) turns UTF-8 (+`<span color="#..">`)
  into `GlyphRun`s. `.fnt` glyphs → `hasSrc`/src rect/bearings; everything else
  → grid cells or blank advance.
- `layoutEntity` creates one `SpriteComponent` per glyph under the text entity,
  centered on the transform, tinted by `color`. `render2d::SpriteRenderer`
  (SDL_GPU) draws them with `src` sampling a `TextureResource`.
- Line box for `.fnt` = `defaultBottom - defaultTop`; glyphs are all drawn at
  the same Y (vertical bearings are currently reserved/0).

Implication: a TTF-backed font can drop into the **same** `FontGlyph` map model
and nothing in the ECS/render path needs to change.

## Approach chosen (v1)

**CPU-rasterized, on-demand glyph atlas** using SDL3_ttf:

- `loadFontResource` recognises `.ttf`/`.otf`/`.ttc` (with optional `:NN`
  suffix for pixel size, e.g. `fonts/Barlow-Regular.ttf:20`).
- The `FontResource` keeps an open `TTF_Font*` plus a growable set of
  fixed-size RGBA atlas *pages* backed by `render2d` dynamic textures.
- Glyphs are rasterized **lazily, on first use** (only what the game actually
  draws - important for Japanese where a full kanji atlas would be huge) and
  packed in rows into the current page.
- Each rasterized glyph produces a normal `FontGlyph`: src rect into its page
  texture + horizontal/vertical bearings from `TTF_GetGlyphMetrics`, so
  `decodeText`/`layoutEntity` treat it exactly like a `.fnt` glyph.

Why this over the alternatives:

| Option | Pros | Cons |
|---|---|---|
| Per-codepoint raster atlas (chosen) | Zero render-path changes; span `<color>`, alignment, wrapping all work; per-glyph tint uses existing sprite color; lazy = Japanese-friendly | No shaping (ligatures/paired kerns beyond what we add); no fallback fonts |
| Whole-string `TTF_RenderText_Blended` textures | Real HarfBuzz shaping for free | New texture per string; breaks span coloring, per-glyph sprites, wrapping in current system; expensive to integrate into ECS |
| `TTF_CreateGPUTextEngine` (SDL_GPU atlas) | Engine-correct shaping + packed atlas handled by SDL_ttf | SDL_ttf's atlas texture lives outside our render2d registry; would need a new draw path or texture-adoption layer; bracket - big lift |

## SDL3_ttf facts learned

- Latest stable: **3.2.2** (`libsdl-org/SDL_ttf`). VC devel zip layout differs
  from SDL3: headers in `include/SDL3_ttf/*.h`, **lib and DLL both under
  `lib/x86/`** (no `bin/` folder). DLL bundles FreeType + HarfBuzz.
- `TTF_OpenFontIO(SDL_IOStream*, closeio, float ptsize)` opens from memory.
  TTC face selection via `TTF_OpenFontWithProperties` +
  `TTF_PROP_FONT_CREATE_FACE_NUMBER` (MS Gothic etc are .ttc).
- `TTF_GetGlyphMetrics(font, cp, &minx, &maxx, &miny, &maxy, &advance)` and
  `TTF_GetGlyphKerning(font, prev, cp, &kerning)` exist at codepoint level.
- `TTF_GetGlyphImage` returns a cached glyph surface (white channels for
  alpha) - but `TTF_RenderGlyph_Blended` is a cleaner fit for packing into
  our own atlas.
- Default DPI is 72, so at default DPI, `ptsize == pixel size`.
- Text engine API (`TTF_CreateGPUTextEngine`) exists for SDL_GPU but needs an
  unconventional atlas-adoption path in our renderer (see table above).

## Vertical metrics (TTF -> FontGlyph bearings)

`FontGlyph.left/right` already model "draw at pen_x - left, advance
left+right". I reuse the reserved `top` field the same way vertically:
- horizontal: `left = -minx`, `right = advance - left` (so pen advances by
  `TTF advance`)
- vertical: `top = maxy - ascent` where glyph top goes at
  `baseline - maxy` / `lineTop + ascent - maxy`
- (signs to be confirmed empirically once rendering)

## Verification (sessions 2026-08-24)

Confirmed end-to-end on a temp game `games/ttftest` (not the re2 game):

- `gfx.loadFont('fonts/Barlow-Regular.ttf', 16)` etc. all load cleanly and
  return usable handles. Both `.ttf` (Barlow, Inter) and `.ttc` (msgothic)
  work.
- `TextComponent`s render EN / FR (accents OK) / JA (hiragana, katakana,
  kanji, full-width punct) and a code block with `[]{}()<>=+-*/%&|^~!?` etc.
  Screenshot pixel analysis shows genuine glyph shapes (not blank / not tofu
  boxes) and per-span colors (red/green/blue `<span>` rendering works).
- Process is stable: ran 35+ minutes without a crash; atlas grows lazily.
- **Stale-binary trap (important)**: `run_ttftest.bat` launches a *relative*
  `bin\Debug\openre.exe`, so it depends on the shell's CWD. An early "crash"
  was traced to a **stale build in a different worktree** (intelorca-*
  glowing-fiesta) being launched from the default shell CWD. Verify the
  running DLL (`Get-FileHash`) or use an env-qualified absolute launch before
  trusting a crash report.
- **Crash-dump workflow (Windows WER)**: parse `%LOCALAPPDATA%\CrashDumps`
  `.dmp` with Python `minidump`; i386 CONTEXT: ContextFlags@0x00,
  FloatSave@0x1C, SegGs@0x8C, Edi@0x9C, Esi@0xA0, Ebp@0xA4, Esp@0xAC,
  Eip@0xB8; raw stack via `seg.read(...)`; capstone+pefile to disassemble at
  the faulting EIP and resolve RVA to the module. DbgHelp off-line
  `SymLoadModuleExW` returned cryptically (error 87) here; DIA via
  `comtypes`/`msdia140.dll` was not registered - the minidump+capstone path
  was sufficient.

Vertical metrics are confirmed working in the render (`top = maxy - ascent`);
no sign flip was needed for the tested fonts (Barlow, Inter, MS Gothic).

### Vertical alignment bug found & fixed (2026-08-24)

The first "everything renders" impressions were fooled by the stale-binary
trap (above): the *current* build's atlas was actually almost empty because
`blitGlyphToPage` wrote every column of a glyph row into the SAME destination
pixel (missing `col * 4` offset on `dRow`), leaving scattered 1px smears.
Fixed by indexing `d = dRow + col*4`, verified with a standalone SDL3_ttf
probe replicating the exact blit (surface had correct alpha coverage; the
blit alone corrupted the atlas).

Then the user reported the text was vertically "all over the place". The
cause was the per-glyph `top` bearing. SDL3_ttf's `TTF_RenderGlyph_Blended`
returns surfaces that are always `lineSkip` tall with the **baseline fixed at
row `ascent`** (ink top at `ascent - maxy`, ink bottom at `ascent - miny`),
so the surface already encodes correct vertical placement. The old formula
`g.top = maxy - ascent` shifted each glyph's sprite top by `-maxy`, placing
"g" (maxy=12) 4px lower than "A" (maxy=16) relative to the baseline - hence
jumbled baselines. Fix: `g.top = 0`, so every glyph's surface top sits on the
line top and all baselines land at `pen_y + ascent`. Also fixed `lineHeight()`
for TTF to `ascent - descent` (`defaultBottom = -descent`) since blended
surfaces are `lineSkip` tall (ascent 24 / descent -4 / lineSkip 29), not
`ascent + descent`. Verified with pixel analysis: caps heights and baselines
consistent within each rendered word; descenders drop below baseline.

## Open questions / future work

- Variable fonts (`Inter-VariableFont_opsz,wght.ttf`) - SDL_ttf reports weight;
  variation axis wiring is a possible follow-up.
- Fallback fonts (`TTF_AddFallbackFont`) for glyphs missing from the primary.
- Bilinear sampler for scaled text (render2d currently NEAREST).
- Combining marks / ligatures: out of scope, explicitly not handled by the
  per-codepoint path.

## Kerning: effectively a no-op for GPOS fonts (2026-08-24)

The layout code applies pair kerning via `FontResource::kerning` ->
`TTF_GetGlyphKerning` (subtracted from the pen advance). Empirically, for both
Barlow and Inter this returns **0 for every pair at every pixel size**: both
fonts encode kerning in the **GPOS** table, and FreeType's
`FT_Get_Kerning(FT_KERNING_DEFAULT)` (which SDL3_ttf's `TTF_GetGlyphKerning`
wraps) only reads the legacy **`kern`** table. So the kerning subtraction is
currently a silent no-op for our test fonts. It is correct as written (subtract
kern to tighten pairs) for fonts that do carry a `kern` table, so it should be
kept - but don't expect it to tighten Barlow/Inter text. If real pair kerning
is wanted, it needs HarfBuzz shaping (out of scope).

## Verification note: BOM in reference text files (2026-08-24)

While pixel-diffing the screenshot against standalone SDL3_ttf reference
renders, English/French rows showed ~1500 "missing" pixels per row while Inter
rows were clean. Root cause was **not the game**: the reference text files
(`row_eng.txt` etc.) carried a UTF-8 BOM, which the reference probe decoded as
an extra ~13px-advance blank codepoint, shifting the whole row. After stripping
the BOMs all four rows diff clean (≤170 px per ~15k-ink row = subpixel AA
noise). Lesson: keep probe/reference inputs byte-identical to what the game
actually renders, including invisible prefix bytes.

## Newline handling fixed (2026-08-24)

A multi-line code block rendered as ONE giant horizontal line. Cause: `\n`
(0x0A) was decoded as a normal codepoint; TTF has no glyph for it, so it fell
into the blank-glyph path and its `advanceX` collapsed to `fallbackAdvanceX`,
advancing the pen horizontally forever.

Fix (src/text_layout.cpp/h):
- `GlyphRun` gains a `lineBreak` flag.
- `decodeText` emits a `lineBreak` marker run for `\n` and skips `\r` (so
  CRLF yields a single break).
- `measureWidth` returns the width of the **longest** line instead of the
  total, so centre/right alignment measures per line.
- `layoutEntity` counts lines for `totalH` (lineH per line) and, on a
  `lineBreak` run, resets `x = originX` and adds `lineH` to `y`.

Verified on ttftest: the 5-line code block now stacks correctly and the rows
below it (Symbols, spans) moved down without overlapping after spacing the
test's fixed row layout for the extra lines. No glyph clipping remains on any
row when compared to per-glyph SDL3_ttf reference pixels.

## Glyph tail clipping: quad snapping shrunk fractional-height quads (2026-08-25)

The Inter20 'g' in the ttftest code block (line 2, "integer") rendered with its
descender loop squished/clipped even though the atlas texture contained the
full glyph and `ensureGlyph` recorded `srcH = surf->h` correctly.

Root cause was **not** the glyph rect calculation but `buildSpriteQuad`
(src/render2d.cpp): text quads carry fractional device sizes/positions (the
layout transform is not exactly integer scale), and after commit 4f7179f both
edges are rounded independently via `snapToPixel`. Rounding both edges can
shrink a quad by up to one pixel per axis (e.g. a 21-texel-tall glyph drawn
into 19-20 device rows). With nearest sampling every source texel then gets
squashed into fewer fragments - thin features such as descender tails lose
their bottom row(s), which reads as "the src rect is too short".

Fix: after snapping, restore the ideal rounded span (`x0 + round(dst.width)`)
against the snapped origin, only when snapping squeezed the quad. This never
moves a shared seam that abutting quads already agree on; it only inflates
quads that snapping shrank.

Verified by exact pixel comparison of the ttftest screenshot against the atlas
page cells: post-fix the Inter20 'g' cell matches the screen at err ~0.05
(pixel-identical) at both 1600x900 and 1280x720, while the pre-fix build did
not contain the cell's pixel pattern anywhere around the glyph (visibly
vertically squashed). Diffing pre/post frames shows the correction lands on
the bottom halves of glyphs across several text rows.

## Configurable fonts: retro sprite vs modern TTF (2026-08-29)

RE2 ships two sprite fonts used for all UI text: `fonts/std.fnt` (14px line
box) and `fonts/sm.fnt` (8px line box). Every ECS/Core-UI game can now swap
those for a bundled TTF at runtime behind a single registered option.

### Font roles

Logical fonts are addressed by **role** (`ui.font.get('big')` /
`ui.font.get('small')`), never by path. The core Lua catalogue
`games/core/script/ui/font.lua` maps `role × [options] font mode` to a
resource and pins it into the native registry via `core.setRoleFont`:

| role  | retro (0)              | modern (1)                          |
|-------|------------------------|-------------------------------------|
| big   | `fonts/std.fnt`        | `fonts/Barlow-Regular.ttf` @ 12px   |
| small | `fonts/sm.fnt`         | `fonts/Barlow-Regular.ttf` @ 7px    |

The TTF sizes were chosen so SDL3_ttf metrics match the sprite line boxes
exactly (verified in-game):

| font | ascent | descent | lineHeight |
|------|-------:|--------:|-----------:|
| std.fnt             | —  | — | **14** |
| Barlow-Regular.ttf @ 12px | 12 | -2 | **14** |
| sm.fnt              | —  | — | **8**  |
| Barlow-Regular.ttf @ 7px  |  7 | -1 | **8**  |

The catalogue owns the policy (which family, which size); native code only
understands roles:
- C++: `getRoleFont`/`previousRoleFont`/`setRoleFont`/`syncRoleSwap`
  (src/font.cpp). Engine-owned text (input-hint labels, movie captions) uses
  `getRoleFont`. `FontSwapSystem` pairs `previousRoleFont` + `getRoleFont` to
  remap scene text when a role changes.
- Lua: `ui.font.get('big' | 'small')` (games/core/script/ui/font.lua). Adopted
  by `games/core/script/ui/*` and the re2 menus/options/load screens. A generic
  native boot default (`seedRoleFont`) - the platform *system* font at a plain
  line-box size - covers pre-Lua text and games that never call the catalogue,
  so no game asset or font family is hardcoded in the engine. `ui.font.refresh()`
  re-pins both roles for the current mode; `options.lua` calls it when the Font
  row changes.

### The `[options] font` toggle (live swap)

- A native enum option `font` (`Retro`/`Modern`, default `Retro`) is registered
  in src/options.cpp and persists to `[options]` in openre.ini. It appears in
  the in-game options menu automatically.
- Changing it in the menu runs `ui.font.refresh()`, which re-loads both roles
  for the new mode and pins them via `core.setRoleFont`. The registry slides
  the old cookie into `previous` and keeps a reference on the new one, so both
  resources stay alive until the swap is applied.
- `core.FontSwap` (src/ecs_systems.cpp) runs each contiguous frame; when any
  role's `previous` differs from `current` it walks the scene remapping every
  `core.Text` entity whose font resource is one of the *previous* role fonts
  onto the `current` cookie, then calls `syncRoleSwap()` (which also collapses
  the pending reference). Cookies are distinct numbers per load, so the walk
  compares **`FontResource` pointers** (resources dedupe by path in the
  resource manager); anything that is not a role font is left alone.
- Setting a new font changes the layout fingerprint, so `TextLayoutSystem`
  re-lays all affected text in the same frame - the swap is realtime.
- The registry keeps every registered role cookie alive (`setRoleFont` takes
  its own reference), so scene text never renders against a dead cookie even
  if the calling Lua handle is later collected.

### Glyph fallback chain

`openre::resolveGlyph(FontResource&, uint32_t)` is the shared per-codepoint
resolver used by both text legs (`decodeText` for `.fnt`, `ensureGlyph` for
TTF):

```
sprite (.fnt) primary  -> role fallback TTF -> system TTF -> tofu box
TTF       primary      -> system TTF        -> tofu box
```

- **Role fallback TTF**: a per-role fallback font registered by the catalogue
  via the third `core.setRoleFont` argument. re2 points the retro sprite roles
  at `games/core/fonts/Barlow-Regular.ttf` (OFL) opened at each sprite's line
  box, so accents (é, ç), kana and kanji render instead of blanking under the
  retro sprite fonts - the same path save-name rows on the Load Game screen
  take for Japanese save files. The engine never names the font: which
  fallback each role uses is game policy.
- **System TTF**: a best-effort known path per platform (Windows
  `%SystemRoot%\Fonts\msgothic.ttc` / `arialuni.ttf`, macOS Arial Unicode/ST
  Heiti, Linux DejaVu/Noto CJK), validated at runtime and cached per size.
  No font-config enumeration in v1.
- **Tofu**: a synthetic hollow box drawn into a dedicated atlas page
  (one per size), shown only when every fallback lacks the codepoint.
- Spaces always stay blank (never drawn/fallbacked/tofued).

### Reference-bug fixed: role-font lifetime (2026-08-29)

The original `core.loadFont` (pre-catalogue; now replaced by `ui/font`)
handed out the role cache's *shared* cookie without acquiring its own resource
reference; a collected `FontHandle` then released
the sprite role font (count 1 -> 0) mid-game, and the recycled cookie number
was picked up by an unrelated resource, so scene text that lazily rebuilt
against a dead cookie rendered nothing. The fix and the later catalogue
refactor both anchor role fonts for the process lifetime: the Lua catalogue
caches one handle per (role, mode) and the native registry keeps its own
reference per registered cookie (`setRoleFont` -> `dupRef`, released by
`syncRoleSwap`), so a registered role cookie always resolves. Verified by
`tools/test_font_swap.py` (realtime swap + consecutive toggling + fresh-entity
remap, with assertions keyed on resource *paths*, since cookie numbers change
per load).

See `docs/openre.lua` for the `core.setRoleFont` binding and
`tools/test_font_swap.py` for the realtime harness.
