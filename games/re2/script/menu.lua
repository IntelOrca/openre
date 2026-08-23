-- menu.lua — RE2-style title screen (logo + PRESS START -> menu) built on the
-- ECS scene tree. Navigation uses the runtime input command state
-- (input.down / input.pressed) with the menu commands (up/down/left/right,
-- accept, cancel).
--
-- States:
--   PRESS_START  logo + blinking PRESS START. accept/menu -> MENU. After a
--                timeout the screen fades out and the demo plays (stub).
--   MENU         logo slid up, menu list with a cursor. up/down/left/right move
--                the cursor; accept selects (placeholder); cancel -> PRESS_START.
--   DEMO         fade out to black, run the demo stub, then fade back in to the
--                PRESS START screen.

-- The sandboxed VM removes setmetatable, so each Title instance carries its
-- methods directly instead of sharing them through a class metatable.
local util              = require("script/util")
local prologue          = require("script/prologue")

local Title             = {}
local titleMethods      = {}

-- Design space for the title screen: all content (background, logo, text) is
-- laid out in this fixed 320x240 coordinate space, then a single "canvas"
-- entity is scaled to fit and centred in the window (preserving aspect ratio).
-- This replaces the old approach of stretching the background to the screen
-- and drawing the logo at a fixed 2x.
local CANVAS_W          = 320
local CANVAS_H          = 240

local STATE_PRESS_START = 0
local STATE_MENU        = 1
local STATE_DEMO        = 2

-- Ticks to wait on the PRESS START screen before the demo plays (~15s @ 60fps).
local DEMO_TIMEOUT      = 60 * 15

-- Colours, kept consistent with the original RE2 title / sdl2 branch look.
local COLOR_GOLD        = "#ffd700" -- selected item / highlighted prompt
local COLOR_GREY        = "#bbbbbb" -- unselected menu item
local COLOR_DIM         = "#888888" -- PRESS START when not lit

-- Main-menu entries shown once the title is up. Actions are placeholders for
-- now; each can later carry a callback.
local MENU_ITEMS        = {
    'LOAD GAME',
    'ORIGINAL MODE',
    'ARRANGE MODE',
    'SPECIAL',
    'OPTION',
}

local function setLineColor(component, color, text)
    scene.setField(component, 'text', '<span color="' .. color .. '">' .. text .. '</span>')
end

local function refreshItem(self, index)
    local text = '<span color="'
        .. (index == self.selected and COLOR_GOLD or COLOR_GREY)
        .. '">' .. MENU_ITEMS[index] .. '</span>'
    scene.setField(self.itemComponents[index], 'text', text)
end

-- Convert a top-left-style vertical band (top fraction of the canvas height,
-- height fraction) into the band's centre in the canvas's centred coordinate
-- space, where (0,0) is the canvas centre.
local function bandCenterY(topFrac, heightFrac)
    return CANVAS_H * (topFrac - 0.5) + CANVAS_H * heightFrac * 0.5
end

local function moveCursor(self, delta)
    local n = #MENU_ITEMS
    self.selected = ((self.selected - 1 + delta) % n) + 1
    for i = 1, n do
        refreshItem(self, i)
    end
    -- Place the cursor beside the selected item (band centre in the canvas's
    -- centred coordinate space; the engine renders sprites centred on the box).
    local y = bandCenterY(self.menuTop + (self.selected - 1) * self.menuStep, self.menuLineHeight)
    scene.setField(self.cursorEntity, 'y', y)
end

local function enterMenu(self)
    self.state = STATE_MENU
    self.logoTargetY = self.menuLogoY
    scene.setField(self.pressStart, 'text', '')
    for i = 1, #MENU_ITEMS do
        refreshItem(self, i)
    end
    scene.setField(self.cursorComponent, 'text', '<span color="' .. COLOR_GOLD .. '">▶</span>')
    moveCursor(self, 0)
end

local function enterPressStart(self)
    self.state = STATE_PRESS_START
    self.ticks = 0
    self.logoTargetY = self.pressLogoY
    for i = 1, #MENU_ITEMS do
        scene.setField(self.itemComponents[i], 'text', '')
    end
    scene.setField(self.cursorComponent, 'text', '')
    setLineColor(self.pressStart, COLOR_GOLD, 'PRESS START')
end

local function enterDemo(self)
    self.state = STATE_DEMO
    self.demoPhase = 0
    self.demoTimer = 0
    -- Hide everything during the fade.
    scene.setField(self.pressStart, 'text', '')
    for i = 1, #MENU_ITEMS do
        scene.setField(self.itemComponents[i], 'text', '')
    end
    scene.setField(self.cursorComponent, 'text', '')
end

local function updateDemo(self)
    self.demoTimer = self.demoTimer + 1
    if self.demoPhase == 0 then
        -- Fade out to black.
        local t = math.min(self.demoTimer, 60)
        gfx.fade(0, 0, 0, math.floor((t / 60) * 255))
        if self.demoTimer >= 60 then
            self.demoPhase = 1
            self.demoTimer = 0
            self:playDemo() -- stub
        end
    elseif self.demoPhase == 1 then
        -- Demo playback stub: hold a moment, then fade back in.
        if self.demoTimer >= 180 then
            self.demoPhase = 2
            self.demoTimer = 0
        end
    else
        -- Fade back in to the title screen.
        local t = math.min(self.demoTimer, 60)
        gfx.fade(0, 0, 0, math.floor((1 - t / 60) * 255))
        if self.demoTimer >= 60 then
            enterPressStart(self)
        end
    end
end

-- Demo playback placeholder. Real demo replays a recorded gameplay movie; for
-- now we just log so the flow is observable and return immediately.
local function playDemo(self)
    print('[title] playing demo (stub)')
end

local function selectItem(self)
    local item = MENU_ITEMS[self.selected]
    print('[title] selected: ' .. item)
    self.closed = true
    local canvas = self.canvas
    self.canvas = nil
    if canvas then
        scene.destroy(canvas)
    end
    if item == 'ORIGINAL MODE' then
        -- Leon A: play the prologue, then return to the title.
        prologue.start({
            character = 'leon', scenario = 'a',
            onFinished = function() Title:new() end,
        })
    elseif item == 'ARRANGE MODE' then
        -- Claire B: play the prologue, then return to the title.
        prologue.start({
            character = 'claire', scenario = 'b',
            onFinished = function() Title:new() end,
        })
    end
end

local function update(self)
    if self.closed then
        return
    end
    self.ticks = self.ticks + 1

    -- Re-fit and re-centre the canvas every frame so it tracks window resizes.
    util.fitCanvas(self.canvas)

    -- Smoothly animate the logo toward its target position.
    local ly = self.logoY + (self.logoTargetY - self.logoY) * 0.15
    self.logoY = ly
    scene.setField(self.logoRoot, 'y', ly)

    if self.state == STATE_PRESS_START then
        -- Blink PRESS START on/off (classic RE2 title behaviour), not a colour
        -- fade — the sdl2 branch showed it appearing and disappearing.
        local on = math.floor(self.ticks / 30) % 2 == 0
        scene.setField(self.pressStart, 'text', on and '<span color="' .. COLOR_GOLD .. '">PRESS START</span>' or '')

        -- Timeout -> demo.
        if self.ticks >= DEMO_TIMEOUT then
            enterDemo(self)
            return
        end
        -- Start -> menu.
        if input.pressed('accept') or input.pressed('menu') then
            enterMenu(self)
            return
        end
    elseif self.state == STATE_MENU then
        if input.pressed('up') or input.pressed('left') then
            moveCursor(self, -1)
        elseif input.pressed('down') or input.pressed('right') then
            moveCursor(self, 1)
        end
        if input.pressed('accept') then
            selectItem(self)
        elseif input.pressed('cancel') then
            enterPressStart(self)
        end
    elseif self.state == STATE_DEMO then
        updateDemo(self)
    end
end

titleMethods = {
    update = update,
    refreshItem = refreshItem,
    moveCursor = moveCursor,
    enterMenu = enterMenu,
    enterPressStart = enterPressStart,
    enterDemo = enterDemo,
    updateDemo = updateDemo,
    playDemo = playDemo,
    selectItem = selectItem,
}

-- Creates one line of text parented under the 320x240 canvas so it scales and
-- centres with everything else. `centerX`/`centerY` are the desired CENTRE of
-- the text box in the canvas's centred coordinate space (origin = canvas centre
-- = window centre); `bandW`/`bandH` are the text box size. The engine renders
-- sprites centred on their transform, and text_layout centres the glyphs within
-- a box centred on the entity, so a text entity placed at (centerX, centerY)
-- sits exactly there. The text flags align the glyphs within that centred box
-- (xCenter / yMiddle centre them on centerX/Y, xLeft aligns them to the box's
-- left edge, etc.).
local function createLine(canvas, font, centerX, centerY, bandW, bandH, text, name, flags)
    local e = scene.createEntity(canvas)
    scene.setField(e, 'name', name)
    scene.setField(e, 'x', centerX)
    scene.setField(e, 'y', centerY)
    local t = scene.addComponent(e, 'core.text')
    scene.setField(t, 'font', font)
    scene.setField(t, 'text', text)
    scene.setField(t, 'size.width', bandW)
    scene.setField(t, 'size.height', bandH)
    scene.setField(t, 'flags', flags)
    return e, t
end

function Title:new()
    local instance = {}
    for name, fn in pairs(titleMethods) do
        instance[name] = fn
    end
    instance.selected = 1
    instance.ticks = 0
    instance.itemEntities = {}
    instance.itemComponents = {}

    -- Logo vertical positions, expressed as offsets from the canvas centre
    -- (the canvas's local origin is its centre, which maps to the window
    -- centre). Negative = above centre.
    instance.pressLogoY = CANVAS_H * (0.34 - 0.5)
    instance.menuLogoY = CANVAS_H * (0.18 - 0.5)

    -- Everything (background, logo, text) lives inside a single 320x240
    -- "canvas" entity. The canvas root is placed at the window centre and
    -- scaled to fit each frame (see update()); its children are laid out in a
    -- coordinate space centred on (0,0), and each piece of content is centred
    -- on its own local origin.
    local canvas = scene.createEntity(scene.getRoot("user"))
    scene.setField(canvas, 'name', 'menu.canvas')
    instance.canvas = canvas
    util.fitCanvas(canvas)

    local bigFont = gfx.loadFont('common/data/font0')
    local smallFont = gfx.loadFont('common/data/font0:small')

    -- Original RE2 title background (data://), authored at 320x240. It is
    -- centred on the canvas origin (0,0) so it fills the window-centred canvas.
    local bgTex = gfx.loadTexture('common/data/Title_bg', 320, 240)
    if bgTex then
        local e = scene.createEntity(canvas)
        scene.setField(e, 'name', 'menu.background')
        scene.setField(e, 'x', 0)
        scene.setField(e, 'y', 0)
        local s = scene.addComponent(e, 'core.sprite')
        scene.setField(s, 'texture', bgTex)
        scene.setField(s, 'size.width', CANVAS_W)
        scene.setField(s, 'size.height', CANVAS_H)
        scene.setField(s, 'flags', SpriteFlags.stretchX | SpriteFlags.stretchY)
    end

    -- RE2 title logo: assembled from TIM index 1 and TIM index 2 of tmoji.bin.
    -- The logo root sits at the canvas centre (0,0); each tile is a static
    -- relative offset from that root, chosen so the assembled logo is centred.
    -- (The right-hand TIM is mostly empty, so the offsets are biased left of the
    -- raw source-rectangle centre to keep the visible logo centred.)
    local LOGO_TILES = {
        { 1, 0, 0,  128, 80, -76,   0 },
        { 1, 0, 80, 128, 48,  52, -16 },
        { 2, 0, 0,  128, 32,  52,  24 },
        { 2, 0, 32, 128, 80, 180,   0 },
    }

    local logoRoot = scene.createEntity(canvas)
    scene.setField(logoRoot, 'name', 'menu.logo')
    -- Centred on the canvas origin (0,0) = window centre; the tiles below are
    -- static offsets from this root already chosen to centre the assembled logo.
    scene.setField(logoRoot, 'x', 0)
    scene.setField(logoRoot, 'y', instance.pressLogoY)
    instance.logoRoot = logoRoot
    instance.logoY = instance.pressLogoY
    instance.logoTargetY = instance.pressLogoY

    for _, t in ipairs(LOGO_TILES) do
        local tex = gfx.loadTexture('common/data/tmoji.bin', 0, 0, t[1])
        if tex then
            local e = scene.createEntity(logoRoot)
            scene.setField(e, 'name', 'menu.logo.tile')
            scene.setField(e, 'x', t[6])
            scene.setField(e, 'y', t[7])
            local s = scene.addComponent(e, 'core.sprite')
            scene.setField(s, 'texture', tex)
            scene.setField(s, 'src.x', t[2])
            scene.setField(s, 'src.y', t[3])
            scene.setField(s, 'src.width', t[4])
            scene.setField(s, 'src.height', t[5])
            scene.setField(s, 'size.width', t[4])
            scene.setField(s, 'size.height', t[5])
            scene.setField(s, 'flags', SpriteFlags.stretchX | SpriteFlags.stretchY)
        end
    end

    if smallFont then
        -- Version disclaimer along the bottom edge.
        createLine(
            canvas,
            smallFont,
            4, bandCenterY(0.92, 0.06), CANVAS_W, CANVAS_H * 0.06,
            '<span color="#707070">OpenRE, Resident Evil 2, Version 1.0</span>',
            'menu.disclaimer',
            TextFlags.xLeft | TextFlags.yMiddle)
    end

    -- PRESS START prompt (just above the menu band).
    local _, pt = createLine(
        canvas,
        bigFont,
        0, bandCenterY(0.56, 0.07), CANVAS_W, CANVAS_H * 0.07,
        '<span color="' .. COLOR_GOLD .. '">PRESS START</span>',
        'menu.pressStart',
        TextFlags.xCenter | TextFlags.yMiddle)
    instance.pressStart = pt

    -- Menu items (hidden until the menu opens).
    instance.menuTop = 0.50
    instance.menuStep = 0.075
    instance.menuLineHeight = 0.07
    for i, _ in ipairs(MENU_ITEMS) do
        local e, t = createLine(
            canvas,
            bigFont,
            0, bandCenterY(instance.menuTop + (i - 1) * instance.menuStep, instance.menuLineHeight),
            CANVAS_W, CANVAS_H * instance.menuLineHeight,
            '',
            'menu.item.' .. i,
            TextFlags.xCenter | TextFlags.yMiddle)
        instance.itemEntities[i] = e
        instance.itemComponents[i] = t
    end

    -- Menu cursor, placed to the left of the list.
    local ce, ct = createLine(
        canvas,
        bigFont,
        CANVAS_W * -0.22, bandCenterY(instance.menuTop, instance.menuLineHeight),
        CANVAS_W, CANVAS_H * instance.menuLineHeight,
        '',
        'menu.cursor',
        TextFlags.xCenter | TextFlags.yMiddle)
    instance.cursorEntity = ce
    instance.cursorComponent = ct

    -- Begin on the PRESS START screen.
    instance.state = STATE_PRESS_START

    re.subscribe(HookKind.tick, function()
        update(instance)
    end)

    return instance
end

return Title
