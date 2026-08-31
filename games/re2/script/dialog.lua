-- dialog.lua - generic confirmation dialog (games/re2/script/dialog.lua).
--
-- A reusable two-choice dialog that can be opened from anywhere (the pause
-- menu, the title screen, ...). Black box centred on the screen taking up
-- 50% of the window, a message centred inside with white text, and two
-- navigatable buttons (Yes / No) at the bottom.
--
--   Dialog:new({ message = '...', onChoose = function(choice) end })
--
-- `choice` is 'yes' or 'no'; onChoose fires once when a button is chosen and
-- the dialog closes itself. cancel behaves exactly like choosing No.
--
-- Layout is authored in a 640x480 design space mapped onto the window by a
-- native scaled_frame ("contain"), the same recipe as options.lua; the frame
-- target is refreshed every tick so resizes are tracked. The dialog builds
-- its own root under the overlay root (or a caller-supplied parent), so it
-- paints above the game and the pause menu.

local ecs = require('ecs')

local Dialog        = {}
local dialogMethods = {}

-- Live dialog instances, driven by the registered app.Dialog managed system
-- below. Instances are removed by :close().
local liveDialogs = {}

-- Shared persistent SFX emitter. app.Dialog and core.Audio run in the same
-- managed dispatch, with Audio AFTER Dialog; a per-instance emitter destroyed
-- by close() in the same dispatch would drop its last trigger before the audio
-- system scans live emitters, so accept/back sounds would never play. Sharing
-- one emitter that lives for the module lifetime sidesteps that: a trigger
-- bump on it is always seen. The sound handles are kept module-level too so
-- audio.loadAudio's decoded resources stay resident.
local sharedEmitter = nil
local sharedEmitterEntity = nil
local sharedSfx = nil
local sharedSfxTrigger = 0

local DESIGN_W = 640
local DESIGN_H = 480

-- The modal box is 50% of the design space, centred.
local BOX_W = DESIGN_W * 0.5 -- 320
local BOX_H = DESIGN_H * 0.5 -- 240

-- Buttons sit side by side near the bottom of the box.
local BUTTON_Y = 0.5 * BOX_H - 28
local BUTTON_SPACING_X = 88

-- Message spans the box width minus padding, centred a little above the
-- box centre so multi-line messages read cleanly.
local MSG_W = BOX_W - 48
local MSG_H = BOX_H - 64
local MSG_Y = -24

local ALPHA_SELECTED = 1.0
local ALPHA_UNSELECTED = 0.5

-- Small builders (mirror options.lua).
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

-- Fit the scaled canvas: recentre it and refresh the frame target so the
-- native ScaledFrameSystem recomputes the scale every frame.
local function fitCanvas(self)
    local w = gfx.screenWidth()
    local h = gfx.screenHeight()
    scene.setField(self.canvas, 'x', w * 0.5)
    scene.setField(self.canvas, 'y', h * 0.5)
    scene.setField(self.frame, 'target.width', w)
    scene.setField(self.frame, 'target.height', h)
end

-- Creates the module-level shared SFX emitter + sound handles on first use.
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
    -- The shared emitter hangs directly off the root of the scene (not any
    -- dialog subtree) so no close() ever destroys it.
    sharedEmitterEntity = scene.createEntity(scene.getRoot('overlay'))
    scene.setField(sharedEmitterEntity, 'name', 'dialog.sfx')
    sharedEmitter = scene.addComponent(sharedEmitterEntity, 'core.AudioEmitter')
end

local function playSfx(self, sound)
    if sound == nil then
        return
    end
    sharedSfxTrigger = sharedSfxTrigger + 1
    scene.setField(sharedEmitter, 'sound', sound)
    scene.setField(sharedEmitter, 'trigger', sharedSfxTrigger)
end

local function refreshButtons(self)
    local selectedText = self.choices[self.selected]
    local unselectedText = self.choices[self.selected == 1 and 2 or 1]
    scene.setField(self.buttons[1], 'text', whiteSpan(self.choices[1], self.selected == 1 and ALPHA_SELECTED or ALPHA_UNSELECTED))
    scene.setField(self.buttons[2], 'text', whiteSpan(self.choices[2], self.selected == 2 and ALPHA_SELECTED or ALPHA_UNSELECTED))
end

local function close(self, choice)
    if self.closed then
        return
    end
    self.closed = true
    -- Unsubscribe from the live-instance list.
    for i = 1, #liveDialogs do
        if liveDialogs[i] == self then
            table.remove(liveDialogs, i)
            break
        end
    end
    if self.onChoose then
        local ok, err = pcall(self.onChoose, choice)
        if not ok then
            print('[dialog] onChoose failed: ' .. tostring(err))
        end
    end
    if self.root then
        scene.destroy(self.root)
        self.root = nil
    end
    -- The SFX emitter is shared module state (see playSfx), so it survives
    -- this close; only the per-instance references are dropped.
    self.emitterEntity = nil
    self.emitter = nil
    self.sfx = nil
    re.collectGarbage()
end

dialogMethods = {
    close = close,
    refreshButtons = refreshButtons,
    playSfx = playSfx,
    fitCanvas = fitCanvas,
}

--- Create a modal confirmation dialog.
---@param opts table { message = string, onChoose = fun(choice: string),
---                     choices = table|nil (default {'Yes','No'}), parent = handle|nil }
---@return Dialog
function Dialog:new(opts)
    opts = opts or {}
    local instance = {}
    for name, fn in pairs(dialogMethods) do
        instance[name] = fn
    end
    instance.message = opts.message or ''
    instance.onChoose = opts.onChoose
    instance.choices = opts.choices or { 'Yes', 'No' }
    instance.selected = 1
    instance.closed = false
    instance.parent = opts.parent or scene.getRoot('overlay')
    instance.sfxTrigger = 0
        instance.emitter = nil
        instance.emitterEntity = nil

    -- Root entity for the whole dialog; destroying it removes the entire tree.
    instance.root = scene.createEntity(instance.parent)
    scene.setField(instance.root, 'name', 'dialog')

    -- Input handler: reads the real nav presses during the input walk (which
    -- runs before the script tick), snapshots them for close(), then clears
    -- the frame so anything underneath (the pause menu) receives nothing.
    instance.nav = { left = false, right = false, up = false, down = false, accept = false, cancel = false }
    local handler = scene.addComponent(instance.root, 'core.InputHandler')
    scene.setField(handler, 'callback', function(frame)
        local nav = instance.nav
        nav.left = containsCommand(frame.press, 'left')
        nav.right = containsCommand(frame.press, 'right')
        nav.up = containsCommand(frame.press, 'up')
        nav.down = containsCommand(frame.press, 'down')
        nav.accept = containsCommand(frame.press, 'accept')
        nav.cancel = containsCommand(frame.press, 'cancel')
        -- Null the input: the pause menu / game fan-out sees an empty frame.
        frame.state = {}
        frame.press = {}
    end)

    -- UI sounds (audio.loadAudio deduplicates across instances). Loaded once
        -- and cached module-level; the emitter is shared module state so the
        -- last trigger of a closing dialog is still seen by core.Audio.
        ensureSharedAudio()
        instance.sfx = sharedSfx

        -- Dim the whole window behind the box so the dialog reads over the pause
        -- menu / game (48% black).
    local dim = scene.createEntity(instance.root)
    scene.setField(dim, 'name', 'dialog.dim')
    local dimSprite = scene.addComponent(dim, 'core.Sprite')
    scene.setField(dimSprite, 'color', { 0, 0, 0, 122 })
    scene.setField(dim, 'x', gfx.screenWidth() * 0.5)
    scene.setField(dim, 'y', gfx.screenHeight() * 0.5)
    addSize(dim, gfx.screenWidth(), gfx.screenHeight())

    -- Scaled canvas holding the design-space layout.
    instance.canvas = ecs.spawn(instance.root)
    scene.setField(instance.canvas, 'name', 'dialog.canvas')
    instance.frame = scene.addComponent(instance.canvas, 'core.ScaledFrame')
    scene.setField(instance.frame, 'source.width', DESIGN_W)
    scene.setField(instance.frame, 'source.height', DESIGN_H)
    scene.setField(instance.frame, 'mode', 'contain')
    scene.setField(instance.frame, 'increment', 0)
    instance:fitCanvas()

    -- The black box, centred on the design-space origin (0,0).
    local box = scene.createEntity(instance.canvas)
    scene.setField(box, 'name', 'dialog.box')
    local boxSprite = scene.addComponent(box, 'core.Sprite')
    scene.setField(boxSprite, 'color', { 0, 0, 0, 255 })
    addSize(box, BOX_W, BOX_H)

    -- Message, centred inside the box.
    local msg = scene.createEntity(instance.canvas)
    scene.setField(msg, 'name', 'dialog.message')
        scene.setField(msg, 'y', MSG_Y)
    local msgText = scene.addComponent(msg, 'core.Text')
    scene.setField(msgText, 'font', require('ui.font').get('big'))
    scene.setField(msgText, 'text', whiteSpan(instance.message, 1.0))
    scene.setField(msgText, 'flags', TextFlags.xCenter | TextFlags.yMiddle)
    addSize(msg, MSG_W, MSG_H)

    -- Two buttons at the bottom, side by side.
    instance.buttons = {}
    for i = 1, 2 do
        local b = scene.createEntity(instance.canvas)
        scene.setField(b, 'name', 'dialog.button.' .. i)
        scene.setField(b, 'x', (i == 1 and -1 or 1) * BUTTON_SPACING_X)
        scene.setField(b, 'y', BUTTON_Y)
        local t = scene.addComponent(b, 'core.Text')
        scene.setField(t, 'font', require('ui.font').get('big'))
        scene.setField(t, 'flags', TextFlags.xCenter | TextFlags.yMiddle)
        addSize(b, 96, 28)
        instance.buttons[i] = t
    end
    instance:refreshButtons()

    liveDialogs[#liveDialogs + 1] = instance

    return instance
end

-- app.Dialog: drive every live dialog instance once per frame and prune
-- closed ones (close() removes it from the list; this loop is a safety net).
ecs.registerSystem('app.Dialog', {
    after = { 'core.LegacyGame' },
}, function()
    local i = 1
    while i <= #liveDialogs do
        local d = liveDialogs[i]
        if d.closed then
            table.remove(liveDialogs, i)
        else
            -- Advance the dialog here (single-frame edge handling).
            if d.nav.left or d.nav.right or d.nav.up or d.nav.down then
                local delta = (d.nav.left or d.nav.up) and -1 or 1
                d.selected = ((d.selected - 1 + delta) % #d.choices) + 1
                d:playSfx(d.sfx.cursor)
                d:refreshButtons()
            elseif d.nav.accept then
                d:playSfx(d.sfx.accept)
                d:close(d.choices[d.selected]:lower())
            elseif d.nav.cancel then
                d:playSfx(d.sfx.back)
                d:close('no')
            end
            i = i + 1
        end
    end
end)

return Dialog