-- prologue.lua — new-game intro (image slideshow + narration, then the opening
-- movie), rebuilt from scratch.
--
-- Sequence (60 fps time base, matching the original):
--   * the screen fades in on the first city still,
--   * a set of illustrative stills / caption bands play in time with a dressed
--     per-section narration line, the soundtrack looping underneath,
--   * the screen fades to black, the last band dissolves, and the opening
--     movie plays (skippable — interrupting a movie skips only that movie),
--   * the module then calls opts.onFinished() to hand back to the caller.
--
-- API: prologue.start{ character='leon'|'claire', scenario='a'|'b', onFinished }.
-- Optional debug hooks used by the web tester: prologue.status() / prologue.skip().

local util = require('script/util')

-- ---------------------------------------------------------------------------
-- Constants
-- ---------------------------------------------------------------------------

local CANVAS_W    = 320
local CANVAS_H    = 240

-- Screen fade lengths in frames. The original hardware fade ramps the overlay
-- in units of 0x100 per frame between the clear and full-black covers which
-- works out to ~127 frames; the few-frame rounding is not perceptible.
local FADE_IN_FRAMES  = 124
local FADE_OUT_FRAMES = 124
local FADE_BLACK_HOLD = 100   -- extra black hold before the movie

-- Cross-fade length when one full-screen still surfaces over the previous
-- one (the "second background"). The incoming plate starts at 0% opacity and
-- ramps to 100% over this many frames, matching the original's fade cadence.
local SURF_FADE_FRAMES = 124

-- Soundtrack fade-out on the final slide (frames / target volume 0-100).
local BGM_FADE_FRAMES  = 120
local BGM_FADE_VOLUME  = 24
local BGM_VOLUME       = 100

-- Caption band alpha: the original strips are drawn as full-white semitransparent
-- quads; use an alpha that reads the same on a mid-tone backdrop.
local CAPTION_ALPHA = 128

-- Sprite record flag bits (bit 1 is unused in the original).
local F_IN     = 0x04
local F_OUT    = 0x08
local F_SCROLL = 0x10
local F_GROW   = 0x20
local F_SHRINK = 0x40

-- Draw priority (z) per sprite type, matching the original ordering table:
-- tiles (type 0) z=11 render first / behind, portraits (type 2) z=10 above
-- them, captions (type 1) z=4 render last / on top. The full-screen backdrops
-- live strictly behind every sprite, with the cross-fade top plate just above
-- the base one.
local Z_TILE    = 11
local Z_PORTRAIT = 10
local Z_CAPTION = 4
local BACKDROP_Z   = 20
local BACKDROP_TOP_Z = BACKDROP_Z - 1

-- Per-type draw priority for the sprite entities.
local Z_SPRITE = { [0] = Z_TILE, [1] = Z_CAPTION, [2] = Z_PORTRAIT }

-- Brightness model: on the original PSX per-channel colour runs 0..0x80 and
-- the sprite fade-in cap is 0x60 (75%). The white of a caption band is 0x80.
local BRIGHT_FULL = 0x80
local FADE_CAP    = 0x60
local FADE_IN_RATE  = 2    -- +0x02 per channel / frame (tiles & portraits)
local FADE_OUT_RATE = 2    -- -0x02 per channel / frame (tiles)
local FADE_OUT_RATE_PORTRAIT = 1 -- -0x01 per channel / frame (portraits)

-- Portrait grow/shrink bounds (hidden once breached).
local GROW_LIMIT   = 230
local SHRINK_LIMIT = 10

-- Scroll / portrait animation are stepped on a slow cadence like the original:
-- the city pan ticks every 11th frame, the portrait size every 4th frame.
local SCROLL_PERIOD  = 11
local SCROLL_STEP    = -1
local SCALE_PERIOD   = 4
local SCALE_STEP     = 1

-- The four city tiles are sprites 2..5; the pan stops (hides them) once the
-- marked bottom-right tile scrolls above this line.
local CITY_STOP_MARKER    = 3   -- gfx index of the stop tile
local CITY_STOP_Y         = 80
local CITY_TILES          = { 2, 3, 4, 5 }

-- ---------------------------------------------------------------------------
-- Graphics: page / rect data (source and destination sizes, PSX texture-page
-- style). A source width/height of 0 in the table means "full page" (256 wide)
-- and a destination width/height of 0 likewise means 256 destination pixels.
-- ---------------------------------------------------------------------------

-- gfx[0..13]: tiles, full-page stills and the two portrait pieces.
local GFX = {
    { file = 'common/data/open00', page = 0x12, tim = 0, sx = 0, sy = 0, sw = 256, sh = 160, dw = 256, dh = 160 },
    { file = 'common/data/open00', page = 0x13, tim = 1, sx = 0, sy = 0, sw = 64,  sh = 160, dw = 64,  dh = 160 },
    { file = 'common/data/open00', page = 0x14, tim = 2, sx = 0, sy = 0, sw = 256, sh = 160, dw = 256, dh = 160 },
    { file = 'common/data/open00', page = 0x15, tim = 3, sx = 0, sy = 0, sw = 64,  sh = 160, dw = 64,  dh = 160 },
    { file = 'common/data/open02', page = 0x1a, tim = 0, sx = 0, sy = 0, sw = 192, sh = 190, dw = 200, dh = 200 }, -- portrait grow
    { file = 'common/data/open01', page = 0x16, tim = 0, sx = 0, sy = 0, sw = 256, sh = 240, dw = 256, dh = 240 },
    { file = 'common/data/open01', page = 0x16, tim = 0, sx = 0, sy = 0, sw = 256, sh = 240, dw = 256, dh = 240 },
    { file = 'common/data/open01', page = 0x17, tim = 1, sx = 0, sy = 0, sw = 64,  sh = 240, dw = 64,  dh = 240 },
    { file = 'common/data/open01', page = 0x17, tim = 1, sx = 0, sy = 0, sw = 64,  sh = 240, dw = 64,  dh = 240 },
    { file = 'common/data/open02', page = 0x1b, tim = 1, sx = 0, sy = 0, sw = 192, sh = 190, dw = 20,  dh = 20 },  -- portrait shrink
    { file = 'common/data/open01', page = 0x18, tim = 2, sx = 0, sy = 0, sw = 256, sh = 240, dw = 256, dh = 240 },
    { file = 'common/data/open01', page = 0x18, tim = 2, sx = 0, sy = 0, sw = 256, sh = 240, dw = 256, dh = 240 },
    { file = 'common/data/open01', page = 0x19, tim = 3, sx = 0, sy = 0, sw = 64,  sh = 240, dw = 64,  dh = 240 },
    { file = 'common/data/open01', page = 0x19, tim = 3, sx = 0, sy = 0, sw = 64,  sh = 240, dw = 64,  dh = 240 },
}

-- Caption band strips (255 pixels wide, mostly 32 tall, taken from the caption
-- sheet). [index] = { file, tim, sy, sh }.
local function captionSheet(claireB)
    return {
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 0, sy = 0x00, sh = 0x20 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 0, sy = 0x20, sh = 0x20 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 0, sy = 0x40, sh = 0x20 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 0, sy = 0x60, sh = 0x20 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 0, sy = 0x80, sh = 0x20 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 0, sy = 0xa0, sh = 0x20 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 0, sy = 0xc0, sh = 0x20 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 1, sy = 0x00, sh = 0x20 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 1, sy = 0x20, sh = 0x20 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 1, sy = 0x40, sh = 0x20 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 1, sy = 0x60, sh = 0x20 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 1, sy = 0x80, sh = 0x20 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 1, sy = 0xa0, sh = 0x20 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 1, sy = 0x00, sh = 0x20 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 1, sy = 0x20, sh = 0x20 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 1, sy = 0x40, sh = 0x10 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 1, sy = 0x50, sh = 0x20 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 1, sy = 0x70, sh = 0x10 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 1, sy = 0x80, sh = 0x20 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 1, sy = 0xa0, sh = 0x20 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 1, sy = 0xc0, sh = 0x10 },
        { file = 'common/data/' .. (claireB and 'open04' or 'open03'), tim = 1, sy = 0xd0, sh = 0x10 },
    }
end

-- Per-variant screen-size stills (full 320x240 single-frame backdrops shown by
-- the "surface" event): the first still (work) and the second (bg).
local BACKDROPS = {
    { work = 'common/data/open10', bg = 'common/data/open11' }, -- scenario a
    { work = 'common/data/open13', bg = 'common/data/open14' }, -- leon b
    { work = 'common/data/open12', bg = 'common/data/open14' }, -- claire b
}

-- Sprite records for a variant: { type, gfx, x, y } where x/y is the top-left
-- corner in the 320x240 design space (the renderer is centred on the entity).
--  * type 0 = tiled still (from the gfx table),
--  * type 1 = caption band (from the caption sheet; the original draws a full
--    white semi-transparent quad),
--  * type 2 = portrait bit that grows/shrinks about the screen centre.
local SPRITES = {
    a = {
        { type = 1, gfx = 0, x = 32,  y = 180 }, -- city caption
        { type = 1, gfx = 0, x = 32,  y = 180 },
        { type = 0, gfx = 0, x = 0,   y = 10 },  -- city tiles (scroll up)
        { type = 0, gfx = 1, x = 256, y = 10 },
        { type = 0, gfx = 2, x = 0,   y = 170 },
        { type = 0, gfx = 3, x = 256, y = 170 },
        { type = 1, gfx = 1, x = 32,  y = 180 },
        { type = 1, gfx = 1, x = 32,  y = 180 },
        { type = 1, gfx = 2, x = 32,  y = 180 },
        { type = 1, gfx = 2, x = 32,  y = 180 },
        { type = 2, gfx = 4, x = 0,   y = 0 },   -- portrait (shrinks away)
        { type = 0, gfx = 5, x = 0,   y = 0 },   -- full-page left column
        { type = 0, gfx = 6, x = 0,   y = 0 },
        { type = 0, gfx = 7, x = 256, y = 0 },
        { type = 0, gfx = 8, x = 256, y = 0 },
        { type = 1, gfx = 3, x = 32,  y = 180 },
        { type = 1, gfx = 3, x = 32,  y = 180 },
        { type = 1, gfx = 4, x = 32,  y = 180 },
        { type = 1, gfx = 4, x = 32,  y = 180 },
        { type = 2, gfx = 9, x = 0,   y = 0 },   -- portrait (grows away)
        { type = 0, gfx = 10, x = 0,  y = 0 },   -- full-page left column
        { type = 0, gfx = 11, x = 0,  y = 0 },
        { type = 0, gfx = 12, x = 256, y = 0 },
        { type = 0, gfx = 13, x = 256, y = 0 },
        { type = 1, gfx = 5, x = 32,  y = 180 },
        { type = 1, gfx = 5, x = 32,  y = 180 },
        { type = 1, gfx = 6, x = 32,  y = 180 },
        { type = 1, gfx = 6, x = 32,  y = 180 },
    },
    leon_b = {
        { type = 1, gfx = 7, x = 32,  y = 180 },
        { type = 1, gfx = 7, x = 32,  y = 180 },
        { type = 1, gfx = 8, x = 32,  y = 180 },
        { type = 1, gfx = 8, x = 32,  y = 180 },
        { type = 1, gfx = 9, x = 32,  y = 180 },
        { type = 1, gfx = 9, x = 32,  y = 180 },
        { type = 1, gfx = 10, x = 32, y = 180 },
        { type = 1, gfx = 10, x = 32, y = 180 },
        { type = 1, gfx = 11, x = 32, y = 180 },
        { type = 1, gfx = 11, x = 32, y = 180 },
        { type = 1, gfx = 12, x = 32, y = 180 },
        { type = 1, gfx = 12, x = 32, y = 180 },
    },
    claire_b = {
        { type = 1, gfx = 13, x = 32,  y = 180 },
        { type = 1, gfx = 13, x = 32,  y = 180 },
        { type = 1, gfx = 14, x = 32,  y = 180 },
        { type = 1, gfx = 14, x = 32,  y = 180 },
        { type = 1, gfx = 15, x = 32,  y = 196 },
        { type = 1, gfx = 15, x = 32,  y = 196 },
        { type = 1, gfx = 16, x = 32,  y = 180 },
        { type = 1, gfx = 16, x = 32,  y = 180 },
        { type = 1, gfx = 17, x = 32,  y = 196 },
        { type = 1, gfx = 17, x = 32,  y = 196 },
        { type = 1, gfx = 18, x = 32,  y = 180 },
        { type = 1, gfx = 18, x = 32,  y = 180 },
        { type = 1, gfx = 19, x = 32,  y = 180 },
        { type = 1, gfx = 19, x = 32,  y = 180 },
        { type = 1, gfx = 20, x = 32,  y = 196 },
        { type = 1, gfx = 20, x = 32,  y = 196 },
        { type = 1, gfx = 21, x = 32,  y = 196 },
        { type = 1, gfx = 21, x = 32,  y = 196 },
    },
}

-- Sprite masks (bit i selects sprite i) used by the timelines. Named so the
-- slide descriptions stay legible.
local M = {
    capt0    = 0x0000003,  -- bits 0,1
    city     = 0x000003C,  -- bits 2..5
    capt1    = 0x00000C0,  -- bits 6,7
    capt2    = 0x0000300,  -- bits 8,9
    portrait = 0x0000400,  -- bit 10
    still1   = 0x0002800,  -- bits 11,13
    capt3    = 0x0018000,  -- bits 15,16
    capt4    = 0x0060000,  -- bits 17,18
    catPort  = 0x0080000,  -- bit 19
    still2   = 0x0500000,  -- bits 20,22
    capt5    = 0x3000000,  -- bits 24,25
    capt6    = 0xC000000,  -- bits 26,27
}

-- Mask subsets used by the second scenario variants (each less elaborate).
local M2 = {
    capt  = 0x0000003,  -- bits 0,1
    band1 = 0x000000C,  -- bits 2,3
    band2 = 0x0000030,  -- bits 4,5
    band3 = 0x00000C0,  -- bits 6,7
    band4 = 0x0000300,  -- bits 8,9
    band5 = 0x0000C00,  -- bits 10,11
    band6 = 0x0003000,  -- bits 12,13
    band7 = 0x000C000,  -- bits 14,15
    band8 = 0x0030000,  -- bits 16,17
}

-- Sprite "mode" values sent to the machine (visible + effect bits).
local MODE_SHOW          = 0x07 -- visible + fade in
local MODE_HIDE          = 0x0b -- visible + fade out
local MODE_SCROLL_IN     = 0x17 -- visible + fade in + scroll
local MODE_SCROLL_OUT    = 0x1b -- visible + fade out + scroll
local MODE_GROW_IN       = 0x27 -- visible + fade in + grow
local MODE_GROW_OUT      = 0x2b -- visible + fade out + grow
local MODE_SHRINK_IN     = 0x47 -- visible + fade in + shrink
local MODE_SHRINK_OUT    = 0x4b -- visible + fade out + shrink

-- ---------------------------------------------------------------------------
-- Timeline data
--
-- Each variant is a list of slides; every slide holds a list of timed actions
-- { f = frame, k = action, ... }. Actions:
--   * {'mask', m=, n=}             Prologue-style sprite-mode change,
--   * {'clearAll'}                 hides every sprite,
--   * {'surf', a='work'|'bg'}      reveals a full-screen still,
--   * {'fadeIn'} / {'fadeOut'}     screen fade,
--   * {'voice', a=clipNo}          play a narration line,
--   * {'bgmFade'}                  fade the soundtrack out,
--   * {'end', next=, fade=, terminal=, xtra=} transition to the next slide
--                                  (immediately; any screen fade keeps running
--                                  into the following slide). A terminal slide
--                                  stays put and, once its fade-out completes,
--                                  hides all sprites and enters the movie wait.
-- ---------------------------------------------------------------------------

local function X(...) return { ... } end -- tiny tuple helper

local SUBS_A = {
    [1] = {
        { f = 240, k = 'surf',   a = 'work' },
        { f = 240, k = 'fadeIn' },
        { f = 420, k = 'voice',  a = 1 },
        { f = 430, k = 'mask',   m = M.capt0, n = MODE_SHOW },
        { f = 793, k = 'mask',   m = M.capt0, n = MODE_HIDE },
                { f = 823, k = 'end', next = 2, fade = false,
          xtra = X(X('mask', M.city, MODE_SCROLL_IN)) },
    },
    [2] = {
        { f = 64,  k = 'voice',  a = 2 },
        { f = 74,  k = 'mask',   m = M.capt1, n = MODE_SHOW },
        { f = 349, k = 'mask',   m = M.capt1, n = MODE_HIDE },
        { f = 359, k = 'mask',   m = M.capt2, n = MODE_SHOW },
        { f = 875, k = 'mask',   m = M.capt2, n = MODE_HIDE },
        { f = 905, k = 'end', next = 3, fade = false,
          xtra = X(X('mask', M.city, MODE_SCROLL_OUT), X('mask', M.portrait, MODE_SHRINK_IN)) },
    },
    [3] = {
        { f = 94,  k = 'mask',   m = M.still1, n = MODE_SHOW },
        { f = 94,  k = 'voice',  a = 3 },
        { f = 104, k = 'mask',   m = M.capt3, n = MODE_SHOW },
        { f = 204, k = 'mask',   m = M.portrait, n = MODE_SHRINK_OUT },
        { f = 321, k = 'mask',   m = M.capt3, n = MODE_HIDE },
        { f = 351, k = 'end', next = 4, fade = false,
                          xtra = X(X('surf', 'bg'), X('mask', M.still1, MODE_HIDE)) },
    },
    [4] = {
        { f = 94,  k = 'clearAll' },
        { f = 94,  k = 'voice',  a = 4 },
        { f = 104, k = 'mask',   m = M.capt4, n = MODE_SHOW },
        { f = 550, k = 'mask',   m = M.capt4, n = MODE_HIDE },
                { f = 580, k = 'end', next = 5, fade = false,
          xtra = X(X('mask', M.catPort, MODE_GROW_IN)) },
    },
    [5] = {
        { f = 96,  k = 'mask',   m = M.still2, n = MODE_SHOW },
        { f = 96,  k = 'voice',  a = 5 },
        { f = 106, k = 'mask',   m = M.capt5, n = MODE_SHOW },
        { f = 261, k = 'mask',   m = M.capt5, n = MODE_HIDE },
        { f = 261, k = 'mask',   m = M.catPort, n = MODE_GROW_OUT },
        { f = 271, k = 'mask',   m = M.capt6, n = MODE_SHOW },
        { f = 445, k = 'end', next = 'movie', fade = 'out', terminal = true,
          xtra = X(X('bgmFade'), X('mask', M.capt6, MODE_HIDE)) },
    },
}

local SUBS_B = {
    [1] = {
        { f = 240, k = 'voice', a = 6 },
        { f = 250, k = 'mask',  m = M2.capt, n = MODE_SHOW },
        { f = 523, k = 'end', next = 2, fade = false,
          xtra = X(X('mask', M2.capt, MODE_HIDE)) },
    },
    [2] = {
        { f = 1,   k = 'voice', a = 7 },
        { f = 11,  k = 'mask',  m = M2.band1, n = MODE_SHOW },
        { f = 190, k = 'voice', a = 8 },
        { f = 388, k = 'end', next = 3, fade = 'in',
          xtra = X(X('mask', M2.band1, MODE_HIDE), X('surf', 'work')) },
    },
    [3] = {
        { f = 128, k = 'voice', a = 9 },
        { f = 138, k = 'mask',  m = M2.band2, n = MODE_SHOW },
        { f = 336, k = 'mask',  m = M2.band2, n = MODE_HIDE },
                { f = 396, k = 'end', next = 4, fade = false },
    },
    [4] = {
        { f = 60,  k = 'voice', a = 10 },
        { f = 70,  k = 'mask',  m = M2.band3, n = MODE_SHOW },
        { f = 274, k = 'end', next = 5, fade = false,
          xtra = X(X('mask', M2.band3, MODE_HIDE)) },
    },
    [5] = {
        { f = 1,   k = 'voice', a = 11 },
        { f = 11,  k = 'mask',  m = M2.band4, n = MODE_SHOW },
        { f = 253, k = 'end', next = 6, fade = false,
          xtra = X(X('mask', M2.band4, MODE_HIDE), X('surf', 'bg')) },
    },
    [6] = {
        { f = 128, k = 'voice', a = 12 },
        { f = 138, k = 'mask',  m = M2.band5, n = MODE_SHOW },
        { f = 307, k = 'mask',  m = M2.band5, n = MODE_HIDE },
        { f = 517, k = 'end', next = 'movie', fade = 'out', terminal = true,
          xtra = X(X('bgmFade')) },
    },
}

local SUBS_C = {
    [1] = {
        { f = 240, k = 'voice', a = 6 },
        { f = 250, k = 'mask',  m = M2.capt, n = MODE_SHOW },
        { f = 523, k = 'end', next = 2, fade = false,
          xtra = X(X('mask', M2.capt, MODE_HIDE)) },
    },
    [2] = {
        { f = 1,   k = 'voice', a = 7 },
        { f = 11,  k = 'mask',  m = M2.band1, n = MODE_SHOW },
        { f = 438, k = 'end', next = 3, fade = false,
          xtra = X(X('mask', M2.band1, MODE_HIDE)) },
    },
    [3] = {
        { f = 10,  k = 'voice', a = 8 },
        { f = 20,  k = 'mask',  m = M2.band2, n = MODE_SHOW },
        { f = 140, k = 'end', next = 4, fade = false,
          xtra = X(X('mask', M2.band2, MODE_HIDE)) },
    },
    [4] = {
        { f = 1,   k = 'voice', a = 9 },
        { f = 11,  k = 'mask',  m = M2.band3, n = MODE_SHOW },
        { f = 322, k = 'end', next = 5, fade = 'in',
          xtra = X(X('mask', M2.band3, MODE_HIDE), X('surf', 'work')) },
    },
    [5] = {
        { f = 128, k = 'voice', a = 14 },
            { f = 337, k = 'end', next = 6, fade = false },
    },
    [6] = {
        { f = 90,  k = 'voice', a = 10 },
        { f = 100, k = 'mask',  m = M2.band4, n = MODE_SHOW },
        { f = 336, k = 'end', next = 7, fade = false,
          xtra = X(X('mask', M2.band4, MODE_HIDE)) },
    },
    [7] = {
        { f = 1,   k = 'voice', a = 11 },
        { f = 11,  k = 'mask',  m = M2.band5, n = MODE_SHOW },
        { f = 266, k = 'end', next = 8, fade = false,
          xtra = X(X('mask', M2.band5, MODE_HIDE)) },
    },
    [8] = {
        { f = 1,   k = 'voice', a = 12 },
        { f = 11,  k = 'mask',  m = M2.band6, n = MODE_SHOW },
        { f = 441, k = 'mask',  m = M2.band6, n = MODE_HIDE },
                { f = 441, k = 'surf',  a = 'bg' },
                { f = 451, k = 'mask',  m = M2.band7, n = MODE_SHOW },
        { f = 738, k = 'mask',  m = M2.band7, n = MODE_HIDE },
        { f = 768, k = 'end', next = 9, fade = false },
    },
    [9] = {
        { f = 1,   k = 'voice', a = 13 },
        { f = 11,  k = 'mask',  m = M2.band8, n = MODE_SHOW },
        { f = 385, k = 'mask',  m = M2.band8, n = MODE_HIDE },
        { f = 595, k = 'end', next = 'movie', fade = 'out', terminal = true,
          xtra = X(X('bgmFade')) },
    },
}

-- Final variant records.
local VARIANTS = {
    leon_a = {
        sprites = SPRITES.a, backdrops = BACKDROPS[1],
        subs = SUBS_A,
        bgm = 'main2b.sap',
        voice = 'pl0/voice/stage0/',
        movie = 'pl0\\zmovie\\opn1stl.bin',
        movieAlt = 'pl0\\zmovie\\opn2ndl.bin',
    },
    claire_a = {
        sprites = SPRITES.a, backdrops = BACKDROPS[1],
        subs = SUBS_A,
        bgm = 'main2b.sap',
        voice = 'pl1/voice/stage0/',
        movie = 'pl1\\zmovie\\opn1stc.bin',
        movieAlt = 'pl1\\zmovie\\opn2ndc.bin',
    },
    leon_b = {
        sprites = SPRITES.leon_b, backdrops = BACKDROPS[2],
        subs = SUBS_B,
        bgm = 'main2c.sap',
        voice = 'pl0/voice/stage0/',
        movie = 'pl0\\zmovie\\opn1stl.bin',
        movieAlt = 'pl0\\zmovie\\opn2ndrl.bin',
    },
    claire_b = {
        sprites = SPRITES.claire_b, backdrops = BACKDROPS[3],
        subs = SUBS_C,
        bgm = 'main2c.sap',
        voice = 'pl1/voice/stage0/',
        movie = 'pl1\\zmovie\\opn1stc.bin',
        movieAlt = 'pl1\\zmovie\\opn2ndrc.bin',
    },
}

-- Convert a PSX per-channel brightness value (0..0x80) into a sprite colour
-- (positional {r,g,b,a}; the engine reads the table by index). The alpha
-- tracks the brightness ramp so a fade-out *dissolves* the sprite to
-- transparent instead of leaving an opaque black quad masking whatever is
-- underneath; it reaches full opacity at the fade cap (the stable end-state).
local function colour(lvl)
    local v = math.floor(lvl * 255 / BRIGHT_FULL)
    local a = math.floor(lvl * 255 / FADE_CAP)
    if a > 255 then a = 255 end
    return { v, v, v, a }
end

local WHITE_QUAD = { 255, 255, 255, CAPTION_ALPHA }

-- Fullly opaque white quad for the full-screen backdrops.
local OPAQUE_QUAD = { 255, 255, 255, 255 }

local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

-- ---------------------------------------------------------------------------
-- Instance state
-- ---------------------------------------------------------------------------

local active = nil -- the running prologue, or nil

local SPRITE_ENTITY_NAMES = {
    'prologue.sprite.1', 'prologue.sprite.2', 'prologue.sprite.3', 'prologue.sprite.4',
    'prologue.sprite.5', 'prologue.sprite.6', 'prologue.sprite.7', 'prologue.sprite.8',
    'prologue.sprite.9', 'prologue.sprite.10', 'prologue.sprite.11', 'prologue.sprite.12',
    'prologue.sprite.13', 'prologue.sprite.14', 'prologue.sprite.15', 'prologue.sprite.16',
    'prologue.sprite.17', 'prologue.sprite.18', 'prologue.sprite.19', 'prologue.sprite.20',
    'prologue.sprite.21', 'prologue.sprite.22', 'prologue.sprite.23', 'prologue.sprite.24',
    'prologue.sprite.25', 'prologue.sprite.26', 'prologue.sprite.27', 'prologue.sprite.28',
}

-- Load a texture once and cache it for the lifetime of one prologue run
-- (beginMovie clears the cache, so the assets unload with the slideshow).
-- `tim` selects a sub-TIM of a multi-TIM file; nil decodes the whole file.
local texCache = {}
local function texture(file, w, h, tim)
    local key = file .. '#' .. (tim or 'auto')
    local t = texCache[key]
    if t == nil then
        t = gfx.loadTexture(file, w or 0, h or 0, tim)
        texCache[key] = t
    end
    return t
end

-- Build one 320x240 canvas holding the backdrop, all sprite entities and the
-- two sound emitters. Returns the instance's scene roots.
local function buildScene(c)
    local canvas = scene.createEntity(scene.getRoot("user"))
    scene.setField(canvas, 'name', 'prologue.canvas')
    util.fitCanvas(canvas)
    c.canvas = canvas

    -- Two full-screen still layers behind every sprite: the base backdrop and,
        -- above it, a top layer used to cross-fade a new still over the current
        -- one (0% -> 100% opacity). Both are hidden until the first "surf".
        local bgE = scene.createEntity(canvas)
        scene.setField(bgE, 'name', 'prologue.backdrop')
        scene.setField(bgE, 'x', 0)
        scene.setField(bgE, 'y', 0)
        local bgS = scene.addComponent(bgE, 'core.sprite')
        scene.setField(bgS, 'size.width', 0) -- hidden
        scene.setField(bgS, 'size.height', 0)
        scene.setField(bgS, 'flags', SpriteFlags.stretchX | SpriteFlags.stretchY)
                scene.setField(bgS, 'z', BACKDROP_Z)
                c.backdrop = bgS

                local bgtE = scene.createEntity(canvas)
                scene.setField(bgtE, 'name', 'prologue.backdropTop')
                scene.setField(bgtE, 'x', 0)
                scene.setField(bgtE, 'y', 0)
                local bgtS = scene.addComponent(bgtE, 'core.sprite')
                scene.setField(bgtS, 'size.width', 0) -- hidden until a cross-fade starts
                scene.setField(bgtS, 'size.height', 0)
                scene.setField(bgtS, 'flags', SpriteFlags.stretchX | SpriteFlags.stretchY)
                scene.setField(bgtS, 'z', BACKDROP_TOP_Z)
                c.backdropTop = bgtS

    c.sprs = {}
    for i, rec in ipairs(c.def.sprites) do
        local e = scene.createEntity(canvas)
        scene.setField(e, 'name', SPRITE_ENTITY_NAMES[i] or ('prologue.sprite.' .. i))
        scene.setField(e, 'x', 0)
        scene.setField(e, 'y', 0)
        local s = scene.addComponent(e, 'core.sprite')
        scene.setField(s, 'size.width', 0)
        scene.setField(s, 'size.height', 0)
        scene.setField(s, 'flags', SpriteFlags.stretchX | SpriteFlags.stretchY)

        local entry = { rec = rec, flg = 0 }
        if rec.type == 1 then
                    local cap = c.def.captions[rec.gfx + 1]
            entry.w = 255
            entry.h = cap.sh
            entry.src = { x = 0, y = cap.sy, width = 255, height = cap.sh }
            entry.tex = texture(cap.file, 256, 256, cap.tim)
        else
            local g = GFX[rec.gfx + 1]
            entry.w = g.dw
            entry.h = g.dh
            entry.base = { dw = g.dw, dh = g.dh }
            entry.src = { x = g.sx, y = g.sy, width = g.sw, height = g.sh }
            entry.tex = texture(g.file, 256, 240, g.tim)
            entry.lvl = 0
            entry.y = rec.y
        end
                scene.setField(s, 'z', Z_SPRITE[rec.type])
                scene.setField(s, 'texture', entry.tex)
        scene.setField(s, 'src.x', entry.src.x)
        scene.setField(s, 'src.y', entry.src.y)
        scene.setField(s, 'src.width', entry.src.width)
        scene.setField(s, 'src.height', entry.src.height)
        scene.setField(s, 'color', rec.type == 1 and WHITE_QUAD or colour(0))
                entry.ent = e
                entry.comp = s
                c.sprs[i] = entry
    end

    -- BGM (looping) and narration emitters; both silent until used.
    local me = scene.createEntity(canvas)
    scene.setField(me, 'name', 'prologue.bgm')
    local bgm = scene.addComponent(me, 'core.audio_emitter')
    scene.setField(bgm, 'category', 'bgm')
    scene.setField(bgm, 'volume', BGM_VOLUME)
    scene.setField(bgm, 'pan', 0)
    scene.setField(bgm, 'loop', true)
    scene.setField(bgm, 'source', 'common/sound/bgm/' .. c.def.bgm)
    scene.setField(bgm, 'state', 'playing')
    c.bgm = bgm
    c.bgmStarted = true

    local ve = scene.createEntity(canvas)
    scene.setField(ve, 'name', 'prologue.voice')
    local voice = scene.addComponent(ve, 'core.audio_emitter')
    scene.setField(voice, 'category', 'voice')
    scene.setField(voice, 'volume', 100)
    scene.setField(voice, 'pan', 0)
    scene.setField(voice, 'loop', false)
    scene.setField(voice, 'source', '')
    scene.setField(voice, 'state', 'stopped')
    c.voice = voice
end

-- Apply a sprite-mode change: sprites selected by the mask take the mode's
-- flag bits; with `clearOthers` a non-selected sprite is hidden (mirrors the
-- original Prologue_set_mode).
local function setMode(c, mask, mode, clearOthers)
    for i, s in ipairs(c.sprs) do
        local sel = mask % 2 == 1
        mask = math.floor(mask / 2)
        if sel then
            s.flg = mode
        elseif clearOthers then
            s.flg = 0
        end
    end
end

local function applyAction(c, a)
    -- Actions are written two ways: named ({ k=, m=, n=, a= }) in the slide
    -- bodies, and positional ({'mask', mask, mode}) in the compact xtra lists.
    local k = a.k or a[1]
    if k == 'mask' then
        setMode(c, a.m or a[2], a.n or a[3], false)
    elseif k == 'clearAll' then
        setMode(c, 0, 0, true)
    elseif k == 'surf' then
        local which = a.a or a[2]
        local bd = which == 'work' and c.def.backdrops.work or c.def.backdrops.bg
        local tex = texCache[bd]
        if tex == nil then
            tex = texture(bd, 320, 240)
            texCache[bd] = tex
        end
            if not c.backdropShown then
                        -- First still: it takes the base layer, revealed by the screen
                        -- fade-in that accompanies this transition (from black).
                        scene.setField(c.backdrop, 'texture', tex)
                        scene.setField(c.backdrop, 'size.width', CANVAS_W)
                        scene.setField(c.backdrop, 'size.height', CANVAS_H)
                        scene.setField(c.backdrop, 'color', OPAQUE_QUAD)
                        c.backdropShown = true
                    else
                        -- Later stills cross-fade in over the current one instead of
                        -- cutting in from black: load it into the spare top layer at 0%
                        -- opacity and ramp it up to opaque over the next frames.
                        scene.setField(c.backdropTop, 'texture', tex)
                        scene.setField(c.backdropTop, 'size.width', CANVAS_W)
                        scene.setField(c.backdropTop, 'size.height', CANVAS_H)
                        scene.setField(c.backdropTop, 'color', { 255, 255, 255, 0 })
                        c.surf = { n = 0, tex = tex }
                    end
    elseif k == 'fadeIn' then
        c.fade.mode = 'in'
            c.fade.n = 0
    elseif k == 'fadeOut' then
        c.fade.mode = 'out'
            c.fade.n = 0
    elseif k == 'voice' then
        local no = a.a or a[2]
        local path = c.def.voice .. 'v' .. string.format('%03d', no) .. '.sap'
        c.voices[#c.voices + 1] = no
        scene.setField(c.voice, 'source', path)
        scene.setField(c.voice, 'state', 'stopped')
        scene.setField(c.voice, 'state', 'playing')
    elseif k == 'bgmFade' then
        c.terminal = true
        scene.setField(c.bgm, 'fadeFrames', BGM_FADE_FRAMES)
        scene.setField(c.bgm, 'fadeTargetVolume', BGM_FADE_VOLUME)
    end
end

-- Draw one sprite when its flag says so, otherwise keep it hidden.
local function updateSprites(c)
    -- Pan / portrait cadence (stepped on every Nth frame; the first frame ticks).
    if c.scrollTick == 0 then
        c.scrollTick = SCROLL_PERIOD - 1
        c.scrollStep = SCROLL_STEP
    else
        c.scrollTick = c.scrollTick - 1
        c.scrollStep = 0
    end
    if c.scaleTick == 0 then
        c.scaleTick = SCALE_PERIOD - 1
        c.scaleStep = SCALE_STEP
    else
        c.scaleTick = c.scaleTick - 1
        c.scaleStep = 0
    end

    for _, s in ipairs(c.sprs) do
        local flg = s.flg
        if flg % 2 == 0 then
            scene.setField(s.comp, 'size.width', 0)
            scene.setField(s.comp, 'size.height', 0)
        else
            local typ = s.rec.type
            if typ == 1 then
                if flg % 16 >= 8 then -- fade-out => dissolves instantly
                    s.flg = 0
                    scene.setField(s.comp, 'size.width', 0)
                    scene.setField(s.comp, 'size.height', 0)
                else
                    local cx = s.rec.x + math.floor(s.w / 2) - math.floor(CANVAS_W / 2)
                    local cy = s.rec.y + math.floor(s.h / 2) - math.floor(CANVAS_H / 2)
                    scene.setField(s.ent, 'x', cx)
                                        scene.setField(s.ent, 'y', cy)
                    scene.setField(s.comp, 'size.width', s.w)
                    scene.setField(s.comp, 'size.height', s.h)
                    scene.setField(s.comp, 'color', WHITE_QUAD)
                end
            elseif typ == 2 then
                -- Brightness.
                if flg % 8 >= 4 then -- fade in
                    s.lvl = s.lvl + FADE_IN_RATE
                    if s.lvl >= FADE_CAP then s.lvl = FADE_CAP; s.flg = s.flg - 4 end
                end
                if flg % 16 >= 8 then -- fade out
                    s.lvl = s.lvl - FADE_OUT_RATE_PORTRAIT
                    if s.lvl <= 0 then s.lvl = 0; s.flg = s.flg - 8 end
                end
                -- Size.
                if c.scaleStep ~= 0 then
                    if flg % 64 >= 32 then -- grow
                        s.w = s.w + 2 * c.scaleStep
                        s.h = s.h + 2 * c.scaleStep
                        if s.w > GROW_LIMIT then s.flg = 0 end
                    end
                    if flg % 128 >= 64 then -- shrink
                        s.w = s.w - 2 * c.scaleStep
                        s.h = s.h - 2 * c.scaleStep
                        if s.w < SHRINK_LIMIT then s.flg = 0 end
                    end
                end
                if s.flg % 2 == 0 then
                    scene.setField(s.comp, 'size.width', 0)
                    scene.setField(s.comp, 'size.height', 0)
                else
                    scene.setField(s.comp, 'size.width', s.w)
                    scene.setField(s.comp, 'size.height', s.h)
                    scene.setField(s.comp, 'color', colour(s.lvl))
                end
            else
                -- Brightness.
                if flg % 8 >= 4 then
                    s.lvl = s.lvl + FADE_IN_RATE
                    if s.lvl >= FADE_CAP then s.lvl = FADE_CAP; s.flg = s.flg - 4 end
                end
                if flg % 16 >= 8 then
                    s.lvl = s.lvl - FADE_OUT_RATE
                    if s.lvl <= 0 then s.lvl = 0; s.flg = s.flg - 8 end
                end
                -- City pan.
                if flg % 32 >= 16 and c.scrollStep ~= 0 then
                    s.rec.y = s.rec.y + c.scrollStep
                    if s.rec.gfx == CITY_STOP_MARKER and s.rec.y < CITY_STOP_Y then
                        for _, ci in ipairs(CITY_TILES) do
                            c.sprs[ci + 1].flg = 0
                        end
                    end
                end
                local w, h = s.w, s.h
                local cx = s.rec.x + math.floor(w / 2) - math.floor(CANVAS_W / 2)
                local cy = s.rec.y + math.floor(h / 2) - math.floor(CANVAS_H / 2)
                scene.setField(s.ent, 'x', cx)
                                scene.setField(s.ent, 'y', cy)
                scene.setField(s.comp, 'size.width', w)
                scene.setField(s.comp, 'size.height', h)
                scene.setField(s.comp, 'color', colour(s.lvl))
            end
        end
    end

    -- Once all four open00 city tiles have fully faded in they tile the whole
    -- screen, so the open10 base backdrop is permanently retired (hidden). It
    -- must never be seen again, even when the city tiles later fade out to
    -- reveal the open11 second backdrop beneath.
    if not c.backdropRetired and c.def.subs == SUBS_A then
        local allCity = true
        for _, ci in ipairs(CITY_TILES) do
            local t = c.sprs[ci + 1]
            if t.lvl < FADE_CAP then
                allCity = false
                break
            end
        end
        if allCity then
            c.backdropRetired = true
            scene.setField(c.backdrop, 'size.width', 0)
            scene.setField(c.backdrop, 'size.height', 0)
        end
    end
end

                -- Advance the backdrop cross-fade (the top layer ramps 0% -> 100% opacity over
-- the still currently shown on the base layer, then becomes the new base).
local function updateSurf(c)
    local s = c.surf
    if not s then return end
    s.n = s.n + 1
    if s.n >= SURF_FADE_FRAMES then
        -- Cross-fade finished: promote the top layer to the new base and hide
        -- the spare layer, so the next "surf" cross-fades over this still. The
        -- base may have been retired (hidden) once the city tiles covered it,
        -- so restore its size/colour as it becomes the open11 backdrop.
        scene.setField(c.backdrop, 'texture', s.tex)
        scene.setField(c.backdrop, 'size.width', CANVAS_W)
        scene.setField(c.backdrop, 'size.height', CANVAS_H)
        scene.setField(c.backdrop, 'color', OPAQUE_QUAD)
        scene.setField(c.backdropTop, 'size.width', 0)
        scene.setField(c.backdropTop, 'size.height', 0)
        c.surf = nil
    else
        local a = math.floor(255 * s.n / SURF_FADE_FRAMES)
        scene.setField(c.backdropTop, 'color', { 255, 255, 255, a })
    end
end

-- Screen fade controller. Returns true the frame an out-fade completes (the
-- screen just went to solid black).
local function updateFade(c)
    local f = c.fade
    if f.mode == 'in' then
        f.n = f.n + 1
        if f.n >= FADE_IN_FRAMES then f.mode = 'none'; f.n = FADE_IN_FRAMES end
        f.alpha = math.floor(255 * (FADE_IN_FRAMES - f.n) / FADE_IN_FRAMES)
    elseif f.mode == 'out' then
        f.n = f.n + 1
        if f.n >= FADE_OUT_FRAMES then
            f.mode = 'none'
            f.n = FADE_OUT_FRAMES
            f.alpha = 255
            return true
        end
        f.alpha = math.floor(255 * f.n / FADE_OUT_FRAMES)
    end
    return false
end

-- Hush the prologue's soundtrack and narration.
local function stopAudio(c)
    if c.bgm then
        scene.setField(c.bgm, 'state', 'stopped')
    end
    if c.voice then
        scene.setField(c.voice, 'state', 'stopped')
    end
end

-- Remove the whole prologue scene (canvas + sprites + emitters). A scene can
-- be destroyed once; later calls are no-ops.
local function destroyScene(c)
    if c.canvas then
        scene.destroy(c.canvas)
    end
    c.canvas = nil
    c.sprs = nil
    c.bgm = nil
    c.voice = nil
    c.backdrop = nil
        c.backdropTop = nil
        c.surf = nil
    end

local function beginMovie(c)
    c.phase = 'movie'
    local def = c.def
    -- The movie takes over the screen and the soundtrack: stop the narration
    -- and BGM emitters, then drop the prologue scene. Both openings play in
    -- sequence and interrupting one skips only that one.
    stopAudio(c)
    destroyScene(c)
    -- The slideshow's assets belong to this screen only: drop the texture
    -- cache and collect so everything is unloaded before the movie starts.
    for k in pairs(texCache) do
        texCache[k] = nil
    end
    re.collectGarbage()
    core.playMovie(def.movie)
    c.movies[#c.movies + 1] = def.movie
    core.playMovie(def.movieAlt)
    c.movies[#c.movies + 1] = def.movieAlt
end

-- End of the run: hush the audio, remove the intro and hand back.
local function finish(c)
    if c.finished then return end
    c.finished = true
    c.phase = 'done'
    stopAudio(c)
    destroyScene(c)
    -- Clear any full-screen fade the slideshow left so the caller's next
    -- screen isn't blacked out (the movies already cleared it).
    c.fade.mode = 'none'
    c.fade.alpha = 0
    gfx.fade(0, 0, 0, 0)
    local cb = c.onFinished
    c.onFinished = nil
    active = nil
    if cb then cb() end
end

local function skipPressed(c)
    if c.phase ~= 'slides' and c.phase ~= 'wait' then return end
    if c.phase == 'wait' then return end
    if c.phase == 'slides' and not c.skipped then
        -- Accelerate: fade out (visuals AND music), then drop straight into
        -- the movie wait. The movie still plays; skipping only interrupts the
        -- slideshow itself.
        c.skipped = true
        if c.fade.mode ~= 'out' then
            c.fade.mode = 'out'
            c.fade.n = 0
        end
        if c.bgm then
            -- Fade the BGM out over the same length as the visual fade so
            -- both end together on the black hold.
            scene.setField(c.bgm, 'fadeFrames', FADE_OUT_FRAMES)
            scene.setField(c.bgm, 'fadeTargetVolume', 0)
        end
    end
end

local function updateRun(c)
    if c.canvas == nil then
        -- The scene was already removed (e.g. the movie path); nothing to draw.
        return
    end
    util.fitCanvas(c.canvas)

    if c.phase == 'slides' then
        -- Slide machine.
        local evs = c.def.subs[c.sub]
        for _, a in ipairs(evs) do
            if c.frame == a.f then
                if a.k == 'end' then
                    local xtra = a.xtra or {}
                    for _, xa in ipairs(xtra) do applyAction(c, xa) end
                    if a.fade == 'in' then c.fade.mode = 'in'; c.fade.n = 0 end
                                        if a.fade == 'out' then c.fade.mode = 'out'; c.fade.n = 0 end
                    if a.terminal then
                        c.machine = a.next -- 'movie'
                    else
                        c.sub = a.next
                        c.frame = 0 -- incremented to 1 below
                    end
                else
                    applyAction(c, a)
                end
            end
        end
        c.frame = c.frame + 1

        local done = updateFade(c)
                if done then
                    if c.terminal or c.skipped then
                        -- Terminal (or skipped) fade-out completed: drop the sprites,
                        -- hold black, then play the movie.
                        setMode(c, 0, 0, true)
                        c.phase = 'wait'
                        c.wait = FADE_BLACK_HOLD
                        c.fade.mode = 'none'
                        c.fade.alpha = 255
                    else
                        -- An interstitial fade-out completed: clear the overlay so the
                        -- next slide is visible again (mirrors the original, where only
                        -- the terminal slide waits on the fade).
                        c.fade.mode = 'none'
                        c.fade.alpha = 0
                    end
                elseif not done and c.skipped and c.fade.mode == 'none' then
                    -- Skip pressed while already black: hold black, then movie.
                    c.phase = 'wait'
                    c.wait = FADE_BLACK_HOLD
                end
        if c.phase == 'slides' then
            updateSprites(c)
                    updateSurf(c)
                end
    elseif c.phase == 'wait' then
            updateFade(c)
            if c.wait ~= nil and c.wait > 0 then
                c.wait = c.wait - 1
            end
            if c.skipped or c.wait <= 0 then
                beginMovie(c)
                finish(c)
            end
        end

        -- Keep the overlay drawn every frame (it clears at the end of a frame).
        if c.fade and c.fade.alpha and c.fade.alpha > 0 then
            gfx.fade(0, 0, 0, c.fade.alpha)
        end
    end

    -- ---------------------------------------------------------------------------
    -- Public module API
    -- ---------------------------------------------------------------------------

    local prologue = {}

    --- Start a new-game intro. `opts.character` is "leon" or "claire",
    --- `opts.scenario` is "a" or "b"; an optional `opts.onFinished` callback runs
    --- once the slideshow + movie are over. A second start while one is already
    --- running is ignored.
    function prologue.start(opts)
        if active then return end
        opts = opts or {}
        local character = opts.character == 'claire' and 'claire' or 'leon'
        local scenario = opts.scenario == 'b' and 'b' or 'a'
        local def = VARIANTS[character .. '_' .. scenario]
        if not def then return end

        local caps = captionSheet(character == 'claire' and scenario == 'b')
        local c = {
            def = def,
            character = character,
            scenario = scenario,
            variant = character .. '_' .. scenario,
            onFinished = opts.onFinished,
            sub = 1,
            frame = 1,
            machine = nil,
            phase = 'slides',
            wait = 0,
            skipped = false,
            terminal = false,
            allowSkip = true,
            requestSkip = false,
            scrollTick = 0,
            scrollStep = 0,
            scaleTick = 0,
            scaleStep = 0,
                        backdropShown = false,
                                                backdropRetired = false,
                                                surf = nil,
                                                fade = { mode = 'none', n = 0, alpha = 255 },
            voices = {},
            movies = {},
            bgmStarted = false,
            finished = false,
        }
        c.def = def
        c.def.captions = caps

        buildScene(c)
        active = c

        re.subscribe(HookKind.tick, function()
            if active ~= c then return end
            if c.requestSkip then
                c.requestSkip = false
                skipPressed(c)
            end
            if c.phase ~= 'done' then
                -- A held skip key behaves like a single press each press.
                if c.allowSkip and (input.pressed('accept') or input.pressed('menu')) then
                    skipPressed(c)
                    if c.finished then return end
                end
                updateRun(c)
            end
        end)
    end

    --- Live test/monitor state (serializable) for the web tester.
    function prologue.status()
        if not active then
            return {
                active = false, finished = false, skipped = false,
                character = nil, scenario = nil, variant = nil,
                substate = nil, frame = nil, bgmStarted = false,
                voices = {}, movies = {},
            }
        end
        return {
            active = true,
            finished = active.finished,
            skipped = active.skipped,
            character = active.character,
            scenario = active.scenario,
            variant = active.variant,
            substate = active.sub,
            frame = active.frame,
            bgmStarted = active.bgmStarted,
            voices = active.voices,
            movies = active.movies,
        }
    end

    --- Web-tester hook: issue a skip as if the player pressed the skip key.
    function prologue.skip()
        if active then active.requestSkip = true end
    end

    return prologue