-- pause.lua - in-game pause menu (games/re2/script/pause.lua).
--
-- The legacy in-game PAUSE overlay is gone. Native only *requests* a pause by
-- writing `paused = 1` (PauseRequested) on the legacy game component when the
-- pause key fires; this module watches that field, spawns its own overlay on
-- top of the frozen game and confirms by writing `paused = 2` (Paused), at
-- which point native freezes the frame until this module writes 0 again.
--
--   pause.State machine on LegacyGameComponent.paused (see ecs_components.h):
--     0 Unpaused        game running freely
--     1 PauseRequested  native saw the pause key; NOT yet frozen this frame
--     2 Paused          module confirmed; native freezes and silences audio
--
-- Menu layout (authored in a 640x480 design space, "contain" scaled like
-- dialog.lua / options.lua):
--   * full-window backdrop, 90% black (alpha 230)
--   * "PAUSE MENU" heading at the top
--   * Restart / Load Game / Options / Quit rows, up/down navigable with the
--     selected row highlighted (full alpha, the rest dimmed)
--
-- Restart and Quit raise a confirmation dialog (script/dialog.lua); Load Game
-- opens the Load screen in pause mode; Options opens the options popup.
--
-- Root placement: the overlay lives under the *user* root rather than the
-- overlay root. Everything the ECS renders (any root) composites on top of the
-- guest game framebuffer while paused, so the menu still *paints* over the
-- game; and the popups opened from here (options / load) are later siblings
-- under the user root, so they are walked first by the input system and paint
-- above the pause menu. Their handlers null the input frame, which is what
-- stops the pause menu from reacting underneath.

local ecs = require('ecs')
local game = require('script/game')

local Pause = {}

local DESIGN_W = 640
local DESIGN_H = 480

-- Full-window dim behind the menu (90% black).
local BACKDROP_ALPHA = math.floor(0.9 * 255 + 0.5) -- 230

local ITEMS = { 'Restart', 'Load Game', 'Options', 'Quit' }

local HEADING_Y = -176
local ITEM_Y0 = -66
local ITEM_PITCH = 44

local ALPHA_SELECTED = 1.0
local ALPHA_UNSELECTED = 0.5

-- Shared persistent SFX emitter (same rationale as dialog.lua): the pause
-- overlay may be destroyed in the same managed dispatch that plays a sound,
-- and core.Audio runs after app.Pause, so a per-instance emitter would drop
-- its last trigger before the audio system scans live emitters.
local sharedEmitter = nil
local sharedEmitterEntity = nil
local sharedSfx = nil
local sharedSfxTrigger = 0

-- Live pause-state (module-local singleton, like options.lua).
local pause = {
    open = false,
    selected = 1,
    root = nil,
    canvas = nil,
    frame = nil,
    itemTexts = {},
    nav = nil,
}

-- Cached id of the legacy game entity underneath the game root.
local legacyId = nil

--------------------------------------------------------------------------------
-- Small builders (mirror dialog.lua / options.lua) ----------------------------
--------------------------------------------------------------------------------

local function addSize(handle, w, h)
    local size = scene.addComponent(handle, 'core.UiSize')
    scene.setField(size, 'width', w)
    scene.setField(size, 'height', h)
end

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

local function containsCommand(list, name)
    for i = 1, #list do
        if list[i] == name then
            return true
        end
    end
    return false
end

local function wrap(index, delta)
    return ((index - 1 + delta) % #ITEMS) + 1
end

--------------------------------------------------------------------------------
-- Legacy game component --------------------------------------------------------
--------------------------------------------------------------------------------

-- The legacy component is a *runtime* component on the node created by
-- core.runGame, so scene.entitiesWithComponent cannot see it: resolve it from
-- the game root's children instead.
local function findLegacyId()
    local root = scene.getRoot('game')
    if root == nil then
        return nil
    end
    local list = scene.children(root)
    for i = 1, #list do
        local e = scene.entityFromId(list[i])
        if e ~= nil and scene.component(e, 'core.Legacy') ~= nil then
            return list[i]
        end
    end
    return nil
end

local function legacyComp()
    local id = legacyId
    if id ~= nil then
        local e = scene.entityFromId(id)
        if e ~= nil then
            return scene.component(e, 'core.Legacy')
        end
        legacyId = nil
    end
    id = findLegacyId()
    legacyId = id
    local e = id ~= nil and scene.entityFromId(id) or nil
    return e ~= nil and scene.component(e, 'core.Legacy') or nil
end

--------------------------------------------------------------------------------
-- SFX -------------------------------------------------------------------------
--------------------------------------------------------------------------------

local function ensureSharedAudio()
    if sharedEmitter ~= nil then
        return
    end
    sharedSfx = {
        cursor = audio.loadAudio('common/sound/core/core22.sap', 0),
        back = audio.loadAudio('common/sound/core/core22.sap', 1),
        accept = audio.loadAudio('common/sound/core/core22.sap', 2),
        invalid = audio.loadAudio('common/sound/core/core22.sap', 3),
    }
    -- Hangs directly off the user root so no close() ever destroys it.
    sharedEmitterEntity = scene.createEntity(scene.getRoot('user'))
    scene.setField(sharedEmitterEntity, 'name', 'pause.sfx')
    sharedEmitter = scene.addComponent(sharedEmitterEntity, 'core.AudioEmitter')
end

local function playSfx(sound)
    if sound == nil then
        return
    end
    sharedSfxTrigger = sharedSfxTrigger + 1
    scene.setField(sharedEmitter, 'sound', sound)
    scene.setField(sharedEmitter, 'trigger', sharedSfxTrigger)
end

--------------------------------------------------------------------------------
-- Menu rendering ---------------------------------------------------------------
--------------------------------------------------------------------------------

-- Recentre the scaled canvas and refresh the frame target so the native
-- ScaledFrameSystem recomputes the scale every frame.
local function fitCanvas()
    local w = gfx.screenWidth()
    local h = gfx.screenHeight()
    scene.setField(pause.canvas, 'x', w * 0.5)
    scene.setField(pause.canvas, 'y', h * 0.5)
    scene.setField(pause.frame, 'target.width', w)
    scene.setField(pause.frame, 'target.height', h)
end

local function refreshItems()
    for i = 1, #pause.itemTexts do
        local comp = pause.itemTexts[i]
        local alpha = i == pause.selected and ALPHA_SELECTED or ALPHA_UNSELECTED
        scene.setField(comp, 'text', whiteSpan(ITEMS[i], alpha))
    end
end

local function buildPause()
    if pause.open then
        return
    end
    ensureSharedAudio()

    pause.root = scene.createEntity(scene.getRoot('user'))
    scene.setField(pause.root, 'name', 'pause')

    -- Input handler: snapshots the nav presses out of the frame during the
    -- input walk (which runs before the script tick and on every frozen
    -- frame), then clears the frame so the game fan-out receives nothing.
    pause.nav = { up = false, down = false, accept = false, cancel = false, menu = false }
    local nav = pause.nav
    local handler = scene.addComponent(pause.root, 'core.InputHandler')
    scene.setField(handler, 'callback', function(frame)
        nav.up = containsCommand(frame.press, 'up')
        nav.down = containsCommand(frame.press, 'down')
        nav.accept = containsCommand(frame.press, 'accept')
        nav.cancel = containsCommand(frame.press, 'cancel')
        nav.menu = containsCommand(frame.press, 'menu')
        frame.state = {}
        frame.press = {}
    end)

    -- Full-window backdrop (90% black), over the frozen game frame.
    local backdrop = scene.createEntity(pause.root)
    scene.setField(backdrop, 'name', 'pause.backdrop')
    local bs = scene.addComponent(backdrop, 'core.Sprite')
    scene.setField(bs, 'color', { 0, 0, 0, BACKDROP_ALPHA })
    scene.setField(backdrop, 'x', gfx.screenWidth() * 0.5)
    scene.setField(backdrop, 'y', gfx.screenHeight() * 0.5)
    addSize(backdrop, gfx.screenWidth(), gfx.screenHeight())

    -- Scaled canvas holding the design-space layout.
    pause.canvas = ecs.spawn(pause.root)
    scene.setField(pause.canvas, 'name', 'pause.canvas')
    pause.frame = scene.addComponent(pause.canvas, 'core.ScaledFrame')
    scene.setField(pause.frame, 'source.width', DESIGN_W)
    scene.setField(pause.frame, 'source.height', DESIGN_H)
    scene.setField(pause.frame, 'mode', 'contain')
    scene.setField(pause.frame, 'increment', 0)
    fitCanvas()

    local bigFont = require('ui.font').get('big')

    -- Heading at the top.
    local heading = scene.createEntity(pause.canvas)
    scene.setField(heading, 'name', 'pause.heading')
    scene.setField(heading, 'y', HEADING_Y)
    local ht = scene.addComponent(heading, 'core.Text')
    scene.setField(ht, 'font', bigFont)
    scene.setField(ht, 'text', whiteSpan('PAUSE MENU', 1.0))
    scene.setField(ht, 'flags', TextFlags.xCenter | TextFlags.yMiddle)
    addSize(heading, DESIGN_W, 40)

    -- Menu rows below the heading.
    pause.itemTexts = {}
    for i = 1, #ITEMS do
        local e = scene.createEntity(pause.canvas)
        scene.setField(e, 'name', 'pause.item.' .. i)
        scene.setField(e, 'y', ITEM_Y0 + ITEM_PITCH * (i - 1))
        local t = scene.addComponent(e, 'core.Text')
        scene.setField(t, 'font', bigFont)
        scene.setField(t, 'flags', TextFlags.xCenter | TextFlags.yMiddle)
        addSize(e, DESIGN_W, 32)
        pause.itemTexts[i] = t
    end
    refreshItems()

    pause.selected = 1
    pause.open = true
end

-- Destroys the pause overlay and releases the pause (writes `paused = 0` so
-- native resumes the game next frame). Safe to call when nothing is open;
-- that path is also the recovery for a game left frozen by a script reload.
local function closePause()
    local comp = legacyComp()
    if pause.open then
        pause.open = false
        pause.selected = 1
        if pause.root ~= nil then
            scene.destroy(pause.root)
            pause.root = nil
        end
        pause.canvas = nil
        pause.frame = nil
        pause.itemTexts = {}
        pause.nav = nil
    end
    if comp ~= nil then
        scene.setField(comp, 'paused', 0)
    end
    re.collectGarbage()
end

--------------------------------------------------------------------------------
-- Menu actions -----------------------------------------------------------------
--------------------------------------------------------------------------------

-- Restart-and-continue / load-and-continue: end the current session and
-- immediately re-enter it with a freshly applied save image (no visible
-- menu). The registered continuation re-kicks task 0 into game_loop so the
-- boot chain re-applies the save; the inner callback parks the *next* session
-- end back on the title.
local function restartAndContinue(saveName)
    closePause()
    game.clearResumeCallbacks()
    if saveName == nil or core.loadGame(saveName) == false then
        print('[pause] failed to load save ' .. tostring(saveName))
        core.requestReturnToMenu()
        game.resumeAfterGame(function()
            local Title = require('script/menu')
            Title:new()
        end)
        return
    end
    core.requestReturnToMenu()
    game.resumeAfterGame(function()
        core.runGame()
        game.resumeAfterGame(function()
            local Title = require('script/menu')
            Title:new()
        end)
    end)
end

-- Quit: end the session and let the resume callbacks the boot path registered
-- (load.lua / prologue / cmdline) recreate the title.
local function quitToMenu()
    closePause()
    core.requestReturnToMenu()
end

local function onRestart()
    local saves = save.list() or {}
    local entry = saves[1]
    if entry == nil or entry.name == nil then
        print('[pause] restart: no save to load')
        playSfx(sharedSfx.invalid)
        return
    end
    playSfx(sharedSfx.accept)
    local Dialog = require('script/dialog')
    Dialog:new({
        message = 'Restart from previous save?\n(Any unsaved data will be lost.)',
        onChoose = function(choice)
            if choice == 'yes' then
                restartAndContinue(entry.name)
            end
        end,
    })
end

local function onLoadGame()
    playSfx(sharedSfx.accept)
    local ok, err = pcall(function()
        local LoadScreen = require('script/load')
        LoadScreen:new({
            fromPause = true,
            onLoaded = function(entry)
                if entry ~= nil and entry.name ~= nil then
                    restartAndContinue(entry.name)
                end
            end,
            onCancelled = function()
                -- The pause overlay is still alive underneath: just return
                -- to it. The game stays Paused.
            end,
        })
    end)
    if not ok then
        print('[pause] failed to open load screen: ' .. tostring(err))
    end
end

local function onOptions()
    playSfx(sharedSfx.accept)
    local ok, err = pcall(require('script/options').open)
    if not ok then
        print('[pause] failed to open options: ' .. tostring(err))
    end
end

local function onQuit()
    playSfx(sharedSfx.accept)
    local Dialog = require('script/dialog')
    Dialog:new({
        message = 'Quit the game and return to the main menu?\n(Any unsaved data will be lost.)',
        onChoose = function(choice)
            if choice == 'yes' then
                quitToMenu()
            end
        end,
    })
end

local function activateItem()
    local item = ITEMS[pause.selected]
    if item == 'Restart' then
        onRestart()
    elseif item == 'Load Game' then
        onLoadGame()
    elseif item == 'Options' then
        onOptions()
    elseif item == 'Quit' then
        onQuit()
    end
end

--------------------------------------------------------------------------------
-- Per-frame tick ---------------------------------------------------------------
--------------------------------------------------------------------------------

local function tickPause()
    if not pause.open then
        return
    end
    fitCanvas()
    local nav = pause.nav
    if nav.up then
        pause.selected = wrap(pause.selected, -1)
        refreshItems()
        playSfx(sharedSfx.cursor)
    elseif nav.down then
        pause.selected = wrap(pause.selected, 1)
        refreshItems()
        playSfx(sharedSfx.cursor)
    elseif nav.accept then
        activateItem()
    elseif nav.cancel or nav.menu then
        playSfx(sharedSfx.back)
        closePause()
    end
end

-- app.Pause: watch LegacyGame.paused and drive the pause flow. Runs after
-- core.LegacyGame so the field reflects this frame's psx_main().
ecs.registerSystem('app.Pause', {
    after = { 'core.LegacyGame' },
}, function()
    local comp = legacyComp()
    if comp == nil then
        -- e.g. module mode with no live legacy node: just clean up.
        if pause.open then
            closePause()
        end
        return
    end

    local paused = scene.getField(comp, 'paused')

    if paused == 1 then
        -- PauseRequested: spawn the overlay and confirm the pause. The game
        -- is NOT frozen this frame yet (native freezes on 2 next frame).
        if not pause.open then
            buildPause()
        end
        scene.setField(comp, 'paused', 2)
        return
    end

    if paused == 2 then
        -- Paused (frozen): drive the menu. When the overlay is missing (e.g.
        -- a script reload mid-pause) release the freeze rather than leave
        -- the game locked.
        if pause.open then
            tickPause()
        else
            scene.setField(comp, 'paused', 0)
        end
        return
    end

    -- Unpaused: tear down a leftover overlay if any.
    if pause.open then
        closePause()
    end
end)

return Pause