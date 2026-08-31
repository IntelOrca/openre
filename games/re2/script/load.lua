-- load.lua — RE2-style Load Game screen (type00.adt background + save list)
-- built on the ECS scene tree, mirroring the layer structure of menu.lua.
--
-- Layout (authored in the 320x240 design space; layer children live in a
-- space where (0,0) is the canvas centre):
--   * "Load" header centred above the panel.
--   * The save-list panel is baked into the type00.adt background, so only the
--     rows and highlight need to line up with it.
--   * 10 centred rows distributed evenly over the panel; empty slots draw
--     nothing. The highlighted row gets a white 50% quad on top. With more
--     than 10 saves the visible window scrolls to follow the cursor; the
--     cursor wraps at both ends.
--   * Info line centred below the panel: "<Character> <Scenario>/<save
--     count>/<room>", with the character part coloured blue for Leon / red
--     for Claire, the shade darkening with difficulty.
--
-- The screen tracks selection and loads the chosen save: accept calls
-- core.loadGame (restores the save image into the game) and blocks inside
-- core.runGame(), which drives the legacy game until it returns to the menu.
-- Cancel returns to the title menu (recreated at PRESS START). A `mode`
-- parameter is reserved so a future save variant can reuse the component.
--
-- The pause menu (script/pause.lua) reuses this screen in a `fromPause` mode:
-- cancel then returns to the still-open pause menu (game keeps its Paused
-- state), accept hands the chosen save to `onLoaded` instead of running the
-- default continuation, and the input frame is nulled so the pause menu
-- underneath never reacts to the navigation.

local Scheduler = require('script/scheduler')
local game = require('script/game')
local util = require('script/util')
local ecs = require('ecs')

local LoadScreen         = {}
local loadMethods        = {}

-- Live load-screen instances, driven by the registered app.Load managed
-- system below (mirrors the old per-instance tick hook).
local liveLoadScreens = {}

-- Forward declarations: these are assigned further down but referenced from
-- LoadScreen:new.
local refresh
local update

local function containsCommand(list, name)
    for i = 1, #list do
        if list[i] == name then
            return true
        end
    end
    return false
end

local CANVAS_W           = 320
local CANVAS_H           = 240
local SCALE_STEP         = 0.5                   -- UI scale quantum (front layer)

-- The baked-in save-list panel of type00.adt, in layer-local coordinates
-- ((0,0) = canvas centre). PANEL_Y is the CENTRE of the panel.
local PANEL_W            = 308
local PANEL_H            = 166
local PANEL_Y            = 3
local ROW_COUNT          = 10
local ROW_PITCH          = PANEL_H / ROW_COUNT -- 16.6
local PANEL_TOP          = PANEL_Y - PANEL_H * 0.5

local HEADER_Y           = -104                -- above the panel
local INFO_Y             = 110                 -- below the panel

-- Transition timing (frames). Matches the handoff chains that create/close
-- this screen; INPUT_DELAY covers the fade out -> hold black -> fade in so
-- input is ignored while the screen is still covered by the black overlay.
local FADE_FRAMES        = 60
local HOLD_FRAMES        = 60
local INPUT_DELAY        = FADE_FRAMES + HOLD_FRAMES

-- Character label colours keyed by difficulty. Casual is lighter, hardcore
-- darker; standard is the base shade.
local COLOR_LEON         = {
    casual   = '#8080ff',
    standard = '#5050ff',
    hardcore = '#2828c0',
}
local COLOR_CLAIRE       = {
    casual   = '#ff8080',
    standard = '#ff5050',
    hardcore = '#c02828',
}
local COLOR_WHITE        = '#ffffffff'

local ALPHA_HIGHLIGHT    = 0.5

-------------------------------------------------------------------------------
-- Markup helpers --------------------------------------------------------------
-------------------------------------------------------------------------------

local function hexByte(n)
    return string.format('%02x', n)
end

local function span(text, color, alphaFrac)
    local a = math.floor(math.max(0, math.min(1, alphaFrac or 1)) * 255 + 0.5)
    return '<span color="' .. color .. hexByte(a) .. '">' .. text .. '</span>'
end

local function whiteSpan(text, alphaFrac)
    return span(text, '#ffffff', alphaFrac)
end

-------------------------------------------------------------------------------
-- Save entries ----------------------------------------------------------------
-------------------------------------------------------------------------------

local function characterLabel(entry)
    local name = entry.character == 'claire' and 'Claire' or 'Leon'
    return name .. ' ' .. string.upper(entry.scenario or 'a')
end

local function characterColor(entry)
    local colors = entry.character == 'claire' and COLOR_CLAIRE or COLOR_LEON
    return colors[entry.difficulty] or colors.standard
end

-- Composes the info line for one save:
--   "<Character> <Scenario>" coloured by character/difficulty, followed by
--   "/<numSaves>/<room>" in white.
local function composeInfo(entry)
    if entry == nil then
        return ''
    end
    local label = characterLabel(entry)
    local rest = '/' .. string.format('%02d', entry.numSaves or 0) .. '/'
        .. (entry.room or '')
    return span(label, characterColor(entry)) .. whiteSpan(rest)
end

-------------------------------------------------------------------------------
-- Creation --------------------------------------------------------------------
-------------------------------------------------------------------------------

-- One scaled-frame layer mapping the 320x240 design space onto the window
-- (same recipe as menu.lua): `increment` 0 fits exactly, SCALE_STEP snaps the
-- UI scale down to whole half steps.
local function createLayer(parent, name, increment)
    local entity = scene.createEntity(parent)
    scene.setField(entity, 'name', name)
    local frame = scene.addComponent(entity, 'core.ScaledFrame')
    scene.setField(frame, 'source.width', CANVAS_W)
    scene.setField(frame, 'source.height', CANVAS_H)
    scene.setField(frame, 'mode', 'contain')
    scene.setField(frame, 'increment', increment)
    return entity, frame
end

-- Keep one layer tracking the window: recentre it and refresh its target so
-- ScaledFrameSystem recomputes the scale every frame.
local function fitLayer(entity, frame)
    local w = gfx.screenWidth()
    local h = gfx.screenHeight()
    scene.setField(entity, 'x', w * 0.5)
    scene.setField(entity, 'y', h * 0.5)
    scene.setField(frame, 'target.width', w)
    scene.setField(frame, 'target.height', h)
end

-- Create one line of text under the front layer. Layer-local (lx, ly) is the
-- CENTRE of the text box.
local function createLine(parent, font, lx, ly, bandW, bandH, text, name, flags)
    local e = scene.createEntity(parent)
    scene.setField(e, 'name', name)
    scene.setField(e, 'x', lx)
    scene.setField(e, 'y', ly)
    local t = scene.addComponent(e, 'core.Text')
    scene.setField(t, 'font', font)
    scene.setField(t, 'text', text)
    local size = scene.addComponent(e, 'core.UiSize')
    scene.setField(size, 'width', bandW)
    scene.setField(size, 'height', bandH)
    scene.setField(t, 'flags', flags)
    return e, t
end

function LoadScreen:new(opts)
    opts = opts or {}
    local instance = {}
    for name, fn in pairs(loadMethods) do
        instance[name] = fn
    end

    instance.mode = opts.mode or 'load'
    instance.age = 0
    instance.closing = false
    instance.closed = false
    instance.enteringGame = false
    instance.fadeRemaining = 0
    instance.entries = save.list() or {}

    -- Pause-mode opts (opened from the pause menu): fromPause makes cancel
    -- return to the (still open) pause menu instead of recreating the title,
    -- onLoaded replaces the default load-and-run continuation, and the input
    -- frame is nulled so the pause menu underneath never reacts.
    instance.fromPause = opts.fromPause or false
    instance.onLoaded = opts.onLoaded
    instance.onCancelled = opts.onCancelled

    -- Cursor over the FULL list (1-based); `offset` is the first visible row.
    instance.cursor = 1
    instance.offset = 1

    -- Root entity for the whole screen, under the user layer. Everything this
    -- screen creates (emitter, layers, text) hangs under it, so destroy()
    -- removes the entire tree with one scene.destroy.
    instance.root = scene.createEntity(scene.getRoot('user'))
    scene.setField(instance.root, 'name', 'load')
    instance.userRoot = instance.root

    -- UI sounds (audio.loadAudio deduplicates across instances).
    instance.sfx = {
        cursor = audio.loadAudio('common/sound/core/core22.sap', 0),
        back = audio.loadAudio('common/sound/core/core22.sap', 1),
        accept = audio.loadAudio('common/sound/core/core22.sap', 2),
        invalid = audio.loadAudio('common/sound/core/core22.sap', 3),
    }
    local emitter = scene.createEntity(instance.userRoot)
    scene.setField(emitter, 'name', 'loadscreen.sfx')
    instance.emitterEntity = emitter
    instance.emitter = scene.addComponent(emitter, 'core.AudioEmitter')
    instance.sfxTrigger = 0

    -- Layers: back carries the type00.adt background (exact contain fit),
    -- front carries the UI (half-step snapped scale).
    instance.back, instance.backFrame = createLayer(instance.userRoot, 'load.back', 0)
    instance.front, instance.frontFrame = createLayer(instance.userRoot, 'load.front', SCALE_STEP)

    -- Input handler: snapshots the nav presses out of the frame during the
    -- input walk (which runs before the script tick), so update() reads the
    -- snapshot rather than the removed input.pressed(). The frame is not
    -- touched, so the game fan-out still receives the press; the load screen
    -- is the only screen alive here so nothing downstream reacts anyway.
    instance.nav = { up = false, down = false, accept = false, cancel = false }
    local loadHandler = scene.addComponent(instance.front, 'core.InputHandler')
    scene.setField(loadHandler, 'callback', function(frame)
        local nav = instance.nav
        nav.up = containsCommand(frame.press, 'up')
        nav.down = containsCommand(frame.press, 'down')
        nav.accept = containsCommand(frame.press, 'accept')
        nav.cancel = containsCommand(frame.press, 'cancel')
        if instance.fromPause then
            -- Stop the presses reaching the pause menu underneath.
            frame.state = {}
            frame.press = {}
        end
    end)

    -- Fonts and textures must stay referenced or they unload on GC. Role
    -- fonts follow the [options] "font" mode (swapped live by the engine).
    instance.font = require('ui.font').get('big')
    instance.bgTexture = gfx.loadTexture('common/data/type00', CANVAS_W, CANVAS_H)
    if instance.bgTexture then
        local e = scene.createEntity(instance.back)
        scene.setField(e, 'name', 'load.background')
        scene.setField(e, 'x', 0)
        scene.setField(e, 'y', 0)
        local s = scene.addComponent(e, 'core.Sprite')
        scene.setField(s, 'texture', instance.bgTexture)
        local size = scene.addComponent(e, 'core.UiSize')
        scene.setField(size, 'width', CANVAS_W)
        scene.setField(size, 'height', CANVAS_H)
        scene.setField(s, 'flags', SpriteFlags.stretchX | SpriteFlags.stretchY)
    end

    -- Header ("Load" — "Save" once the save variant exists), above the panel.
    local _, header = createLine(
        instance.front,
        instance.font,
        0, HEADER_Y, CANVAS_W, 30,
        whiteSpan(instance.mode == 'save' and 'Save' or 'Load'),
        'load.header',
        TextFlags.xCenter | TextFlags.yMiddle)
    instance.headerComponent = header

    -- Highlight quad drawn over the selected row (above the row text via
    -- creation order). Sized to match a row band of the baked-in panel.
    instance.highlight = nil
    do
        local e = scene.createEntity(instance.front)
        scene.setField(e, 'name', 'load.highlight')
        scene.setField(e, 'x', 0)
        local s = scene.addComponent(e, 'core.Sprite')
        scene.setField(s, 'color', { 255, 255, 255, math.floor(ALPHA_HIGHLIGHT * 255 + 0.5) })
        local size = scene.addComponent(e, 'core.UiSize')
        scene.setField(size, 'width', PANEL_W)
        scene.setField(size, 'height', ROW_PITCH)
        instance.highlight = e
        instance.highlightSprite = s
    end

    -- Row lines, distributed evenly over the panel.
    instance.rowComponents = {}
    for i = 1, ROW_COUNT do
        local _, t = createLine(
            instance.front,
            instance.font,
            0, PANEL_TOP + ROW_PITCH * (i - 0.5), PANEL_W, ROW_PITCH,
            '',
            'load.row.' .. i,
            TextFlags.xCenter | TextFlags.yMiddle)
        instance.rowComponents[i] = t
    end

    -- Info line below the panel.
    local _, info = createLine(
        instance.front,
        instance.font,
        0, INFO_Y, CANVAS_W, 20,
        '',
        'load.info',
        TextFlags.xCenter | TextFlags.yMiddle)
    instance.infoComponent = info

    instance:refresh()

    liveLoadScreens[#liveLoadScreens + 1] = instance

    return instance
end

-------------------------------------------------------------------------------
-- Refresh ---------------------------------------------------------------------
-------------------------------------------------------------------------------

local function playSfx(self, sound)
    if sound == nil then
        return
    end
    self.sfxTrigger = self.sfxTrigger + 1
    scene.setField(self.emitter, 'sound', sound)
    scene.setField(self.emitter, 'trigger', self.sfxTrigger)
end

local function clampScroll(self)
    local n = #self.entries
    if n <= ROW_COUNT then
        self.offset = 1
        return
    end
    while self.cursor < self.offset do
        self.offset = self.offset - 1
    end
    while self.cursor > self.offset + ROW_COUNT - 1 do
        self.offset = self.offset + 1
    end
end

-- Redraws the visible rows, highlight position and info line from the current
-- cursor / scroll offset.
refresh = function(self)
    clampScroll(self)
    local n = #self.entries
    for vi = 1, ROW_COUNT do
        local comp = self.rowComponents[vi]
        local index = self.offset + vi - 1
        local entry = self.entries[index]
        if entry then
            scene.setField(comp, 'text', whiteSpan(entry.name or ''))
        else
            scene.setField(comp, 'text', '')
        end
    end

    -- Highlight only when there is something to highlight; hide it otherwise.
    if n > 0 and self.cursor >= self.offset and self.cursor < self.offset + ROW_COUNT then
        local vi = self.cursor - self.offset + 1
        scene.setField(self.highlight, 'y', PANEL_TOP + ROW_PITCH * (vi - 0.5))
        scene.setField(self.highlightSprite, 'color', { 255, 255, 255, math.floor(ALPHA_HIGHLIGHT * 255 + 0.5) })
    else
        scene.setField(self.highlightSprite, 'color', { 255, 255, 255, 0 })
    end

    scene.setField(self.infoComponent, 'text', composeInfo(self.entries[self.cursor]))
end

-------------------------------------------------------------------------------
-- Input -----------------------------------------------------------------------
-------------------------------------------------------------------------------

local function moveCursor(self, delta)
    local n = #self.entries
    if n == 0 then
        return
    end
    local previous = self.cursor
    self.cursor = ((self.cursor - 1 + delta) % n) + 1
    if self.cursor ~= previous then
        playSfx(self, self.sfx.cursor)
        refresh(self)
    end
end

-- Cancel: leave the screen and return to the title menu (PRESS START).
local function beginClose(self)
    if self.closing then
        return
    end
    self.closing = true
    playSfx(self, self.sfx.back)
    if self.fromPause then
        -- Return to the pause menu: the pause overlay is still alive
        -- underneath, so just destroy this screen (no fade, no Title) and
        -- let the caller dismiss us.
        self:destroy()
        if self.onCancelled then
            local ok, err = pcall(self.onCancelled)
            if not ok then
                print('[load] onCancelled failed: ' .. tostring(err))
            end
        end
        return
    end
    local Title = require('script/menu')
    local scheduler = Scheduler:new()
    scheduler
        :fadeOut(FADE_FRAMES)
        :exec(function()
            self:destroy()
            Title:new()
        end)
        :holdBlack(HOLD_FRAMES)
        :fadeIn(FADE_FRAMES)
end

-- Starts the selected save: fade to black (driven frame-by-frame here through
-- an overlay sprite under the user layer), then destroy the screen, restore
-- the save via core.loadGame and run the session non-blocking (core.runGame
-- returns immediately). When it returns to the menu, the resume callback
-- recreates the title so the player lands back at PRESS START.
local function startLoad(self)
    if self.closing or self.enteringGame then
        return
    end
    self.enteringGame = true
    self.fadeRemaining = FADE_FRAMES
    -- Full-window fade overlay (replaces the removed gfx.fade). Created as
    -- the newest child of the user root so it paints above the load screen;
    -- destroyed once the fade completes.
    self.fadeOv, self.fadeOvSprite = util.spawnOverlay(nil, { 0, 0, 0, 0 })
end

-------------------------------------------------------------------------------
-- Tick ------------------------------------------------------------------------
-------------------------------------------------------------------------------

update = function(self)
    if self.closed then
        return
    end
    self.age = self.age + 1

    fitLayer(self.back, self.backFrame)
    fitLayer(self.front, self.frontFrame)

    -- Ignore input while the handoff black overlay still covers the screen or
    -- while closing. In pause mode the screen pops straight in (no fade
    -- handoff), so there is no overlay to wait out and input is live at once.
    if self.closing or (self.age <= INPUT_DELAY and not self.fromPause) then
        return
    end

    -- Enter-the-game fade: accept was pressed, so fade to black over
    -- FADE_FRAMES through the overlay, then hand over to the legacy game.
    -- core.runGame returns immediately; the resume callback below recreates
    -- the menu once the session returns to it.
    if self.enteringGame then
        self.fadeRemaining = self.fadeRemaining - 1
        local progress = math.max(0, math.min(1, 1 - self.fadeRemaining / FADE_FRAMES))
        if self.fadeOvSprite then
            scene.setField(self.fadeOvSprite, 'color', { 0, 0, 0, math.floor(progress * 255) })
        end
        if self.fadeRemaining > 0 then
            return
        end
        if self.fadeOv then
            scene.destroy(self.fadeOv)
            self.fadeOv = nil
        end
        self.enteringGame = false
        local entry = self.entries[self.cursor]
        self:destroy()
        if self.onLoaded then
            -- Pause mode: the caller (pause.lua) owns the load-and-continue
            -- handoff for the still-paused session.
            local ok, err = pcall(self.onLoaded, entry)
            if not ok then
                print('[load] onLoaded failed: ' .. tostring(err))
            end
            return
        end
        local ok = entry ~= nil and core.loadGame(entry.name) or false
        if not ok then
            print('[load] failed to load save ' .. tostring(entry and entry.name or '?'))
            local Title = require('script/menu')
            Title:new()
        else
            core.runGame()
            game.resumeAfterGame(function()
                local Title = require('script/menu')
                Title:new()
            end)
        end
        return
    end

    if self.nav.up then
        moveCursor(self, -1)
    elseif self.nav.down then
        moveCursor(self, 1)
    elseif self.nav.accept then
        if #self.entries > 0 then
            playSfx(self, self.sfx.accept)
            self:startLoad()
        else
            playSfx(self, self.sfx.invalid)
        end
    elseif self.nav.cancel then
        beginClose(self)
    end
end

-- Tears the screen down: destroy every entity this screen created (the whole
-- tree hangs under instance.root), drop all resource handles and collect so
-- they unload before the next screen takes over.
local function destroy(self)
    self.closed = true
    if self.fadeOv then
        scene.destroy(self.fadeOv)
        self.fadeOv = nil
    end
    if self.root then
        scene.destroy(self.root)
        self.root = nil
    end
    self.userRoot = nil
    self.front = nil
    self.back = nil
    self.frontFrame = nil
    self.backFrame = nil
    self.emitterEntity = nil
    self.emitter = nil
    self.sfx = nil
    self.font = nil
    self.bgTexture = nil
    self.rowComponents = nil
    self.highlight = nil
    self.highlightSprite = nil
    re.collectGarbage()
end

loadMethods = {
    update = update,
    refresh = refresh,
    moveCursor = moveCursor,
    beginClose = beginClose,
    startLoad = startLoad,
    destroy = destroy,
}

-- app.Load: drive every live load-screen instance once per frame and prune
-- closed ones (replaces the old per-instance tick hook).
ecs.registerSystem('app.Load', {
    after = { 'core.LegacyGame' },
}, function()
    local i = 1
    while i <= #liveLoadScreens do
        local s = liveLoadScreens[i]
        if s.closed then
            table.remove(liveLoadScreens, i)
        else
            s:update()
            if s.closed then
                table.remove(liveLoadScreens, i)
            else
                i = i + 1
            end
        end
    end
end)

return LoadScreen
