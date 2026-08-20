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
local Title = {}
local titleMethods = {}

local STATE_PRESS_START = 0
local STATE_MENU = 1
local STATE_DEMO = 2

-- Ticks to wait on the PRESS START screen before the demo plays (~15s @ 60fps).
local DEMO_TIMEOUT = 60 * 15

-- Colours, kept consistent with the original RE2 title / sdl2 branch look.
local COLOR_GOLD = "#ffd700" -- selected item / highlighted prompt
local COLOR_GREY = "#bbbbbb" -- unselected menu item
local COLOR_DIM  = "#888888" -- PRESS START when not lit

-- Main-menu entries shown once the title is up. Actions are placeholders for
-- now; each can later carry a callback.
local MENU_ITEMS = {
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

local function moveCursor(self, delta)
    local n = #MENU_ITEMS
    self.selected = ((self.selected - 1 + delta) % n) + 1
    for i = 1, n do
        refreshItem(self, i)
    end
    -- Place the cursor beside the selected item.
    local y = self.menuTop + (self.selected - 1) * self.menuStep + self.menuLineHeight * 0.5
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
    print('[title] selected: ' .. MENU_ITEMS[self.selected])
end

local function update(self)
    self.ticks = self.ticks + 1

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

-- Creates one centred line of text occupying the given vertical band.
-- Returns the entity handle and the core.text component handle.
local function createLine(font, bandTop, bandHeight, text, name, flags)
    local e = scene.createEntity(scene.getRoot("user"))
    scene.setField(e, 'name', name)
    scene.setField(e, 'x', 0)
    scene.setField(e, 'y', bandTop)
    local t = scene.addComponent(e, 'core.text')
    scene.setField(t, 'font', font)
    scene.setField(t, 'text', text)
    scene.setField(t, 'size.width', gfx.screenWidth())
    scene.setField(t, 'size.height', bandHeight)
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

    local h = gfx.screenHeight()
    local w = gfx.screenWidth()

    instance.pressLogoY = h * 0.34
    instance.menuLogoY = h * 0.18

    local bigFont = gfx.loadFont('common/data/font0')
    local smallFont = gfx.loadFont('common/data/font0:small')

    -- Original RE2 title background (data://).
    local bgTex = gfx.loadTexture('common/data/Title_bg', 320, 240)
    if bgTex then
        local e = scene.createEntity(scene.getRoot("user"))
        scene.setField(e, 'name', 'menu.background')
        scene.setField(e, 'x', 0)
        scene.setField(e, 'y', 0)
        local s = scene.addComponent(e, 'core.sprite')
        scene.setField(s, 'texture', bgTex)
        scene.setField(s, 'size.width', w)
        scene.setField(s, 'size.height', h)
        scene.setField(s, 'flags', SpriteFlags.stretchX | SpriteFlags.stretchY)
    end

    -- RE2 title logo: two TIMs packed inside tmoji.bin (index 1 & 2). Each tile
    -- is a child of a central "logo root" so they share its transform and the
    -- whole logo slides together when it moves up.
    local logoRoot = scene.createEntity(scene.getRoot("user"))
    scene.setField(logoRoot, 'name', 'menu.logo')
    scene.setField(logoRoot, 'x', w * 0.5)
    scene.setField(logoRoot, 'y', instance.pressLogoY)
    scene.setField(logoRoot, 'scale.x', 2)
    scene.setField(logoRoot, 'scale.y', 2)
    instance.logoRoot = logoRoot
    instance.logoY = instance.pressLogoY
    instance.logoTargetY = instance.pressLogoY

    local LOGO_TILES = {
        { 1, 0, 0,  128, 80, -144 + 0,   -40 },
        { 1, 0, 80, 128, 48, -144 + 128, -40 },
        { 2, 0, 0,  128, 32, -144 + 128, 8 },
        { 2, 0, 32, 128, 80, -144 + 256, -40 },
    }
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
            smallFont,
            h * 0.92, h * 0.06,
            '<span color="#707070">OpenRE, Resident Evil 2, Version 1.0</span>',
            'menu.disclaimer',
            TextFlags.xLeft | TextFlags.yMiddle)
    end

    -- PRESS START prompt (just above the menu band).
    local _, pt = createLine(
        bigFont,
        h * 0.56, h * 0.07,
        '<span color="' .. COLOR_GOLD .. '">PRESS START</span>',
        'menu.pressStart',
        TextFlags.xCenter | TextFlags.yMiddle)
    instance.pressStart = pt

    -- Menu items (hidden until the menu opens).
    instance.menuTop = h * 0.60
    instance.menuStep = h * 0.075
    instance.menuLineHeight = h * 0.07
    for i, _ in ipairs(MENU_ITEMS) do
        local e, t = createLine(
            bigFont,
            instance.menuTop + (i - 1) * instance.menuStep, instance.menuLineHeight,
            '',
            'menu.item.' .. i,
            TextFlags.xCenter | TextFlags.yMiddle)
        instance.itemEntities[i] = e
        instance.itemComponents[i] = t
    end

    -- Menu cursor, placed to the left of the list.
    local ce, ct = createLine(
        bigFont,
        instance.menuTop + instance.menuLineHeight * 0.5, instance.menuLineHeight,
        '',
        'menu.cursor',
        TextFlags.xCenter | TextFlags.yMiddle)
    scene.setField(ce, 'x', -w * 0.22)
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
