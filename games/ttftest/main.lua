-- ttftest — visual playground for SDL3_ttf font rendering.
--
-- Loads real TTF fonts through gfx.loadFont and exercises TextComponent with
-- English, French, Japanese (kanji/kana/full-width punctuation) and code
-- symbols. Any glyph is rasterized lazily the first time it is used, so
-- watch for atlas growth in the log as more text appears.
--
-- Run: set OPENRE_GAME=ttftest (see run_ttftest.bat).

local root = scene.getRoot("user")

local W = gfx.screenWidth()
local H = gfx.screenHeight()

-- Dark backdrop so glyph extents / descenders are easy to see.
local bg = scene.createEntity(root)
scene.setField(bg, 'x', W * 0.5)
scene.setField(bg, 'y', H * 0.5)
local bgSprite = scene.addComponent(bg, 'core.sprite')
scene.setField(bgSprite, 'color', { 18, 22, 30, 255 })
scene.setField(bgSprite, 'size.width', W)
scene.setField(bgSprite, 'size.height', H)
scene.setField(bgSprite, 'flags', SpriteFlags.stretchX | SpriteFlags.stretchY)

-- Fonts -----------------------------------------------------------------
local barlow16 = gfx.loadFont('fonts/Barlow-Regular.ttf', 16)
local barlow24 = gfx.loadFont('fonts/Barlow-Regular.ttf', 24)
local barlowB  = gfx.loadFont('fonts/Barlow-Bold.ttf', 28)
local inter20  = gfx.loadFont('fonts/Inter-Regular.ttf', 20)
local ja20     = gfx.loadFont('fonts/msgothic.ttc', 20)
local ja28     = gfx.loadFont('fonts/msgothic.ttc', 28)

local function textLine(str, font, x, y, flags)
    local e = scene.createEntity(root)
    scene.setField(e, 'x', x)
    scene.setField(e, 'y', y)
    local t = scene.addComponent(e, 'core.text')
    scene.setField(t, 'font', font)
    scene.setField(t, 'text', str)
    scene.setField(t, 'size.width', 1)
    scene.setField(t, 'size.height', 1)
    if flags then scene.setField(t, 'flags', flags) end
    return e
end

-- Room for ~12 rows on a 720px window.
local y = 24
local function row(str, font)
    textLine(str, font, 16, y)
    y = y + 52
end

row('<span color="#ffdd66">OpenRE TTF font test</span>', barlowB)

row('English: The quick brown fox jumps over the lazy dog.', barlow24)

row('French: L\'été, nous mangions déjà à la plage. Ça coûte très cher!', barlow24)

row('Japanese: こんにちは世界！これは日本語のテキスト表示テストです。', ja28)

row('Kanji mix: 漫画・学校・電車・天気・情報・機械 1234567890', ja20)

row('Hiragana: あいうえおかきくけこさしすせそ', ja20)

local code = [[int main() {
    int x = (count + 7) * 2 / 3;   // integer maths
    printf("value: %d\n", x);      /* escapes, braces, commas */
    return (x > 0) ? -x : ~x & 0xFF;
}]]
row(code, inter20)
-- The code block is multi-line; skip enough room for all of its lines
-- so the following rows don't overlap. lineHeight for inter20 is ~24px,
-- and the code string contains 4 newlines.
y = y + 4 * 24

row('Symbols: []{}()<>=+-*/%&|^~!?;:.,\'"\\_#$@', inter20)

row(
    '<span color="#ff8888">red</span> <span color="#88ff88">green</span> <span color="#8888ff">blue</span> normal',
    barlow24)

-- Alignment samples (the entity sits at the box centre).
local e = scene.createEntity(root)
scene.setField(e, 'x', W * 0.5)
scene.setField(e, 'y', y + 26)
local t = scene.addComponent(e, 'core.text')
scene.setField(t, 'font', barlow16)
scene.setField(t, 'text', 'Centred 16px - first line of a future menu')
scene.setField(t, 'size.width', W)
scene.setField(t, 'size.height', 32)
scene.setField(t, 'flags', TextFlags.xCenter | TextFlags.yMiddle)
y = y + 60

textLine('Right-aligned small: 16px ASCII only.', barlow16, W - 16, y, TextFlags.xRight)
y = y + 40

-- Late glyphs: append text a few seconds in to exercise atlas growth with
-- kanji that haven't been seen yet.
local late = textLine('Late: 東京都コンピューター研究所へ行きます。', ja20, 16, y)
local lateShown = false
re.subscribe(HookKind.tick, function()
    if not lateShown then
        lateShown = true
        scene.setField(t, 'text',
            'Centred 16px - updated text after startup (second line here)')
    end
end)