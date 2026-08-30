-- ui/font.lua - role-based UI font catalogue (core policy).
--
-- The native text system understands font *roles* ('big' and 'small'), not
-- concrete files: engine text (input hints, movie captions), the swap walk
-- and core.setRoleFont all operate on roles. This module is the single place
-- that decides which concrete font each role uses for the current [options]
-- "font" mode, and pins those choices into the native registry so every
-- consumer follows them.
--
--   role   retro (font = 0)        modern (font = 1)
--   -----  ----------------------  -------------------------------------
--   big    fonts/std.fnt           fonts/Barlow-Regular.ttf @ 12px
--   small  fonts/sm.fnt            fonts/Barlow-Regular.ttf @ 7px
--
-- The TTF sizes are tuned so cap height / line height roughly match the
-- 14px / 8px sprite line boxes. Per-codepoint fallback (bundled core TTF ->
-- system font -> tofu box) is handled natively in resolveGlyph, so the
-- catalogue only picks the primary family and size.
--
-- Handles are cached per (role, mode). The cache table keeps every font
-- handle alive for the process lifetime: the scene stores its cookie number,
-- so a collected handle would recycle the cookie onto an unrelated resource
-- and already-built text would silently vanish. Never drop an entry.

-- Games call ui.font.get('big' | 'small') to fetch (and pin) the current
-- mode's font handle, and ui.font.refresh() after the [options] "font" value
-- changes so the native swap walk remaps live text.
local M = {}

local CACHE = {} -- [role][mode] = FontHandle (kept alive on purpose)

-- Concrete fonts per role and mode. Retro uses the sprite fonts; modern
-- uses the bundled Barlow TTF at the tuned sizes.
local SPEC = {
    big = {
        [0] = { path = 'fonts/std.fnt', size = 0 },
        [1] = { path = 'fonts/Barlow-Regular.ttf', size = 12 },
    },
    small = {
        [0] = { path = 'fonts/sm.fnt', size = 0 },
        [1] = { path = 'fonts/Barlow-Regular.ttf', size = 7 },
    },
}

-- Per-role glyph fallback fonts, registered alongside the primaries. The
-- engine only consults the fallback when a sprite (.fnt) primary lacks a
-- codepoint (accents, kana, kanji); it sizes each fallback to the sprite's
-- line box so the glyph metrics line up. Modern TTF primaries already cover
-- Unicode, so they register no fallback (the engine skips straight to the
-- system font for those).
local FALLBACK = {
    big = {
        [0] = { path = 'fonts/Barlow-Regular.ttf', size = 14 },
        [1] = nil,
    },
    small = {
        [0] = { path = 'fonts/Barlow-Regular.ttf', size = 8 },
        [1] = nil,
    },
}

-- Lifetime cache for fallback handles: the engine reads the registered
-- fallback cookie when laying out text, so these must never be collected.
local FALLBACK_CACHE = {} -- [role][mode] = FontHandle

-- Loads (or fetches from the lifetime cache) the font handle for a role in
-- a mode. Falls back to the role's retro sprite font if a TTF load fails.
local function handleFor(role, mode)
    local byMode = CACHE[role]
    if byMode == nil then
        byMode = {}
        CACHE[role] = byMode
    end
    local handle = byMode[mode]
    if handle == nil then
        local spec = SPEC[role][mode]
        handle = gfx.loadFont(spec.path, spec.size)
        if handle == nil and mode ~= 0 then
            -- TTF unavailable: fall back to the role's sprite font.
            local retro = SPEC[role][0]
            handle = gfx.loadFont(retro.path, retro.size)
        end
        if handle == nil then
            error("ui.font: cannot load any font for role '" .. role .. "' (mode " .. mode .. ')')
        end
        byMode[mode] = handle
    end
    return handle
end

-- The fallback handle (kept alive) for a role in a mode, or nil when the
-- mode's primary is a TTF font that needs no sprite fallback.
local function fallbackFor(role, mode)
    local spec = FALLBACK[role][mode]
    if spec == nil then
        return nil
    end
    local byMode = FALLBACK_CACHE[role]
    if byMode == nil then
        byMode = {}
        FALLBACK_CACHE[role] = byMode
    end
    local handle = byMode[mode]
    if handle == nil then
        handle = gfx.loadFont(spec.path, spec.size)
        if handle ~= nil then
            byMode[mode] = handle
        end
    end
    return handle
end

--- Returns the current mode's font handle for a role and pins it into the
--- native registry (with its glyph fallback) so engine text follows. Safe to
--- call repeatedly; the handle is cached and the pin is a no-op when the
--- font is unchanged.
---@param role string 'big' or 'small'
---@return FontHandle
function M.get(role)
    local mode = options.get('font') or 0
    local handle = handleFor(role, mode)
    core.setRoleFont(role, handle, fallbackFor(role, mode))
    return handle
end

--- Re-resolves both roles against the current [options] "font" mode and pins
--- them into the native registry. Call this after the font option changes so
--- the swap walk (native FontSwapSystem) remaps already-built scene text onto
--- the new fonts.
function M.refresh()
    M.get('big')
    M.get('small')
end

return M