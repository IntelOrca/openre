-- menu.lua — a self-contained RE2-style title menu built on the ECS scene
-- tree. No input bindings exist yet, so the highlighted menu item auto-cycles
-- to demonstrate the menu structure; wiring real navigation can slot in later.
--
-- Layout is described in fractions of the current window size (gfx.screenWidth
-- / gfx.screenHeight) so the menu scales to any window the game is launched in.

-- The main-menu entries shown once the title is up. Actions are placeholders
-- for now; when input arrives each item can carry a cb() to run.
local MENU_ITEMS = {
    'LOAD GAME',
    'ORIGINAL MODE',
    'ARRANGE MODE',
    'SPECIAL',
    'OPTION',
}

-- How many ticks the highlight rests on one item before advancing.
local CYCLE_TICKS = 45

-- The sandboxed VM removes setmetatable, so each Menu instance carries its
-- methods directly instead of sharing them through a class metatable.
local menuMethods = {}
Menu = {}

local function update(self)
    self.ticks = self.ticks + 1

    -- Auto-cycle the highlighted item.
    if self.ticks % CYCLE_TICKS == 0 then
        self.selected = (self.selected % #MENU_ITEMS) + 1
        for i, e in ipairs(self.itemEntities) do
            self:refreshItem(i, e)
        end
    end

    -- Pulse the PRESS START prompt.
    if self.pressStart then
        local on = math.floor(self.ticks / 20) % 2 == 0
        scene.setField(
            self.pressStart,
            'text',
            on and '<span color="#ffd700">PRESS START</span>' or '<span color="#888888">PRESS START</span>'
        )
    end
end

local function refreshItem(self, index, component)
    local text = MENU_ITEMS[index]
    if index == self.selected then
        text = '<span color="#ffd700">' .. text .. '</span>'
    else
        text = '<span color="#bbbbbb">' .. text .. '</span>'
    end
    scene.setField(component, 'text', text)
end

menuMethods = {
    update = update,
    refreshItem = refreshItem,
}

--- Creates one centred line of text occupying the given vertical band.
--- Returns the core.text component handle (used to update the string later).
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
    return t
end

function Menu:new()
    local instance = {}
    for name, fn in pairs(menuMethods) do
        instance[name] = fn
    end
    instance.selected = 1
    instance.ticks = 0
    instance.itemEntities = {}

    local h = gfx.screenHeight()
    local w = gfx.screenWidth()

    local bigFont = gfx.loadFont('common/data/font0')
    local smallFont = gfx.loadFont('common/data/font0:small')

    -- Original RE2 title artwork, loaded from the game data (data://).
    -- The background ADT decodes to a 320x240 full-frame image; the logo is
    -- paged TIMs carved out of tmoji.bin (indices 1 & 2). Pure-black pixels
    -- are transparent, so the logo overlays the background. Both are created
    -- before any text so they draw first (behind the text).
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

    -- RE2 title logo: two TIMs packed inside tmoji.bin (index 1 & 2), each an
    -- 8bpp 128x128 texture with a 256-colour CLUT. The original
    -- (move_and_display_title_image, 0x005065C0) draws the logo as four tiles
    -- that assemble in a "11/.2" pattern. Each tile is a child of a central
    -- "logo root" entity so they share its transform; offsets are relative to
    -- that centre (1:1, src pixels == screen pixels).
    local logoRoot = scene.createEntity(scene.getRoot("user"))
    scene.setField(logoRoot, 'name', 'menu.logo')
    scene.setField(logoRoot, 'x', w * 0.5)
    scene.setField(logoRoot, 'y', h * 0.26)
    -- Scale the logo up 2x in x and y; child tiles inherit the parent's scale.
    scene.setField(logoRoot, 'scale.x', 2)
    scene.setField(logoRoot, 'scale.y', 2)

    -- { timIndex, srcX, srcY, srcW, srcH, offsetX, offsetY from centre }
    -- Assembles as "11/.2": index1 supplies the top row (top 80 left, bottom 48
    -- right), index2 the bottom-right column; the bottom-left quadrant is empty.
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

        -- PRESS START prompt just above the menu.
        local bandTop = h * 0.56
        local bandHeight = h * 0.07
        instance.pressStart = createLine(
            bigFont,
            bandTop, bandHeight,
            '<span color="#ffd700">PRESS START</span>',
            'menu.pressStart',
            TextFlags.xCenter | TextFlags.yMiddle)

        -- Stack the main-menu entries.
        local top = h * 0.66
        local step = h * 0.075
        local lineHeight = h * 0.07
        for i, _ in ipairs(MENU_ITEMS) do
            local t = createLine(
                bigFont,
                top + (i - 1) * step, lineHeight,
                MENU_ITEMS[i],
                'menu.item.' .. i,
                TextFlags.xCenter | TextFlags.yMiddle)
            instance:refreshItem(i, t)
            instance.itemEntities[i] = t
        end
    end

    re.subscribe(HookKind.tick, function()
        update(instance)
    end)

    return instance
end

return Menu
