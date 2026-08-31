-- dialog.lua - generic confirmation dialog (games/re2/script/dialog.lua).
--
-- A reusable two-choice dialog that can be opened from anywhere (the pause
-- menu, the title screen, ...). A black box 75% wide and 50% tall, a
-- message centred inside with white text, and two navigatable buttons
-- (Yes / No) flowed vertically at the bottom.
--
--   Dialog:new({ message = '...', onChoose = function(choice) end })
--
-- `choice` is 'yes' or 'no'; onChoose fires once when a button is chosen and
-- the dialog closes itself. cancel behaves exactly like choosing No.
--
-- Layout is authored in a 640x480 design space mapped onto the window by a
-- native scaled_frame ("contain"): the box is a UiFlex (vertical) container,
-- so the message and button rows are positioned/sized by UiFlexSystem every
-- frame. The dialog builds its own root under the overlay root (or a
-- caller-supplied parent), so it paints above the game and the pause menu.
--
-- The SFX emitter lives on the dialog root; close() defers destroying the
-- root to the next dispatch so core.Audio (which runs after app.Dialog in the
-- same managed dispatch) still sees the closing dialog's last trigger.

local ecs = require('ecs')

local Dialog        = {}
local dialogMethods = {}

-- Live dialog instances, driven by the registered app.Dialog managed system
-- below. Instances are removed once close()'s deferred root destroy runs.
local liveDialogs = {}

local DESIGN_W = 640
local DESIGN_H = 480

-- The modal box is 75% wide and 50% tall of the design space, centred.
local BOX_W = DESIGN_W * 0.75 -- 480
local BOX_H = DESIGN_H * 0.5 -- 240

-- Box content insets and the button row height for the vertical flex flow.
local BOX_PADDING = 24
local BOX_GAP = 12
local BUTTON_H = 30

local ALPHA_SELECTED = 1.0
local ALPHA_UNSELECTED = 0.5

-- Small builders (mirror options.lua).
local function addSize(handle, w, h, weight)
    local size = scene.addComponent(handle, 'core.UiSize')
    scene.setField(size, 'width', w)
    scene.setField(size, 'height', h)
    if weight ~= nil then
        scene.setField(size, 'weight', weight)
    end
end

local function addFlex(handle, orientation, gap, padding)
    local flex = scene.addComponent(handle, 'core.UiFlex')
    scene.setField(flex, 'orientation', orientation)
    scene.setField(flex, 'gap', gap)
    scene.setField(flex, 'padding', padding)
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

local function playSfx(self, sound)
    if sound == nil or self.emitter == nil then
        return
    end
    self.sfxTrigger = self.sfxTrigger + 1
    scene.setField(self.emitter, 'sound', sound)
    scene.setField(self.emitter, 'trigger', self.sfxTrigger)
end

local function refreshButtons(self)
    scene.setField(self.buttons[1], 'text', whiteSpan(self.choices[1], self.selected == 1 and ALPHA_SELECTED or ALPHA_UNSELECTED))
    scene.setField(self.buttons[2], 'text', whiteSpan(self.choices[2], self.selected == 2 and ALPHA_SELECTED or ALPHA_UNSELECTED))
end

local function close(self, choice)
    if self.closed then
        return
    end
    self.closed = true
    if self.onChoose then
        local ok, err = pcall(self.onChoose, choice)
        if not ok then
            print('[dialog] onChoose failed: ' .. tostring(err))
        end
    end
    -- The root (and its emitter) stays alive for one more dispatch so
    -- core.Audio scans the closing dialog's last trigger; the app.Dialog
    -- tick destroys it on the next frame.
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
    -- Per-instance sound handles; audio.loadAudio deduplicates across
    -- instances, so this is cheap, and audio keeps the decoded resources
    -- resident for the module lifetime.
    instance.sfx = {
        cursor = audio.loadAudio('common/sound/core/core22.sap', 0),
        back = audio.loadAudio('common/sound/core/core22.sap', 1),
        accept = audio.loadAudio('common/sound/core/core22.sap', 2),
        invalid = audio.loadAudio('common/sound/core/core22.sap', 3),
    }

    -- Root entity for the whole dialog; destroying it removes the entire tree.
    instance.root = scene.createEntity(instance.parent)
    scene.setField(instance.root, 'name', 'dialog')

    -- The SFX emitter lives on the dialog root; it is destroyed together
    -- with the root (see close / app.Dialog deferred destroy).
    instance.emitter = scene.addComponent(instance.root, 'core.AudioEmitter')

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

    -- The black box, centred on the design-space origin (0,0), is a vertical
    -- flex container: the message stretches, then the two buttons flow below.
    local box = scene.createEntity(instance.canvas)
    scene.setField(box, 'name', 'dialog.box')
    local boxSprite = scene.addComponent(box, 'core.Sprite')
    scene.setField(boxSprite, 'color', { 0, 0, 0, 255 })
    addSize(box, BOX_W, BOX_H)
    addFlex(box, 'vertical', BOX_GAP, BOX_PADDING)

    -- Message, centre aligned, stretches to fill the box width.
    local msg = scene.createEntity(box)
    scene.setField(msg, 'name', 'dialog.message')
    local msgText = scene.addComponent(msg, 'core.Text')
    scene.setField(msgText, 'font', require('ui.font').get('big'))
    scene.setField(msgText, 'text', whiteSpan(instance.message, 1.0))
    scene.setField(msgText, 'flags', TextFlags.xCenter | TextFlags.yMiddle)
    addSize(msg, 0, 0, 1) -- weight 1: fill the remaining flex space

    -- Two buttons at the bottom, flowed vertically.
    instance.buttons = {}
    for i = 1, 2 do
        local b = scene.createEntity(box)
        scene.setField(b, 'name', 'dialog.button.' .. i)
        local t = scene.addComponent(b, 'core.Text')
        scene.setField(t, 'font', require('ui.font').get('big'))
        scene.setField(t, 'flags', TextFlags.xCenter | TextFlags.yMiddle)
        addSize(b, 0, BUTTON_H)
        instance.buttons[i] = t
    end
    instance:refreshButtons()

    liveDialogs[#liveDialogs + 1] = instance

    return instance
end

-- app.Dialog: drive every live dialog instance once per frame and prune
-- closed ones on the next dispatch (the deferred destroy lets core.Audio see
-- a closing dialog's last SFX trigger).
ecs.registerSystem('app.Dialog', {
    after = { 'core.LegacyGame' },
}, function()
    local i = 1
    local destroyed = false
    while i <= #liveDialogs do
        local d = liveDialogs[i]
        if d.closed then
            -- Deferred destroy: the root (and its emitter) survived one
            -- dispatch so core.Audio picked up the last trigger.
            if d.root then
                scene.destroy(d.root)
                d.root = nil
                d.emitter = nil
                d.sfx = nil
            end
            table.remove(liveDialogs, i)
            destroyed = true
        else
            -- Advance the dialog here (single-frame edge handling).
            d:fitCanvas()
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
    if destroyed then
        re.collectGarbage()
    end
end)

return Dialog
