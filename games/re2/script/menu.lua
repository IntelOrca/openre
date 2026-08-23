-- menu.lua — RE2-style title screen (logo + pulsing START GAME -> menu) built
-- on the ECS scene tree. The menu is a data-driven tree of Lua tables; submenus
-- are returned by factory functions which close over the navigation trail, so
-- leaf actions receive the full context (arranged mode, character, scenario,
-- difficulty) without any trail-tracking state.
--
-- States:
--   PRESS_START  logo + pulsing START GAME. accept/menu -> MENU. After a
--                timeout the screen fades out and the demo plays (stub).
--   MENU         a stack of menu levels. up/down/left/right move the cursor;
--                accept drills one level in, cancel backs one out (or to
--                PRESS START at the root). Level changes fade out then the new
--                level fades in; input is locked during the transition.
--   DEMO         fade out to black, run the demo stub, then fade back in to the
--                PRESS START screen.
--   LEAVING      difficulty start sequence: play the BIOHAZARD soundbite
--                behind a white flash, fade to black, destroy the menu and
--                hand over to the prologue.

-- The sandboxed VM removes setmetatable, so each Title instance carries its
-- methods directly instead of sharing them through a class metatable.
local util              = require("script/util")
local prologue          = require("script/prologue")

local Title             = {}
local titleMethods      = {}

-- Design space for the title screen: all content (background, logo, text) is
-- laid out in this fixed 320x240 coordinate space, then a single "canvas"
-- entity is scaled to fit and centred in the window (preserving aspect ratio).
local CANVAS_W          = 320
local CANVAS_H          = 240

local STATE_PRESS_START = 0
local STATE_MENU        = 1
local STATE_DEMO        = 2
local STATE_LEAVING     = 3

-- Ticks to wait on the PRESS START screen before the demo plays (~15s @ 60fps).
local DEMO_TIMEOUT      = 60 * 15

-- Item layout: the menu is left-aligned to the screen. All item text shares a
-- single x position; each row gets a y band centre from the item top + step.
-- The cursor item shifts left by CURSOR_SHIFT px, eased each frame.
local ITEM_LEFT         = -CANVAS_W * 0.5 + 24 -- x of the text's left edge
local ITEM_TOP          = 0.42                  -- top of the first row (band frac)
local ITEM_STEP         = 0.075                 -- row pitch (band frac)
local CURSOR_SHIFT      = 8                     -- px the cursor item moves left
local CURSOR_EASE       = 0.25                  -- per-frame ease toward shift target
local TOOLTIP_FRAC      = 0.92                  -- tooltip band centre (band frac)

-- Opacity model: items sit at 50% white; the cursor item pulses smoothly
-- between 50% and 100% over ~1s via a sine wave.
local ALPHA_BASE        = 0.50
local ALPHA_PEAK        = 1.00
local PULSE_PERIOD      = 60                    -- ~1s @ 60fps
local ALPHA_TOOLTIP     = 0.80
local ALPHA_GREY        = 0.40                  -- locked '???' items

-- Cross-fade transition length (frames).
local FADE_FRAMES       = 10

-- Locked-slot cheat for testing: set OPENRE_MENU_UNLOCK to force-unlock the
-- ??? scenario slots (Leon B / Claire B).
local DEBUG_UNLOCK      = re.getEnv('OPENRE_MENU_UNLOCK') ~= nil

-------------------------------------------------------------------------------
-- Colour helpers -------------------------------------------------------------
-- <span color="#rrggbbaa"> markup: the last two hex digits are the alpha
-- channel (00-ff). This is how items get their pulsing opacity.
-------------------------------------------------------------------------------

local function hexByte(n)
    return string.format('%02x', n)
end

local function whiteSpan(text, alphaFrac)
    local a = math.floor(math.max(0, math.min(1, alphaFrac)) * 255 + 0.5)
    return '<span color="#ffffff' .. hexByte(a) .. '">' .. text .. '</span>'
end

local function greySpan(text, alphaFrac)
    local a = math.floor(math.max(0, math.min(1, alphaFrac)) * 255 + 0.5)
    return '<span color="#9a9a9a' .. hexByte(a) .. '">' .. text .. '</span>'
end

-------------------------------------------------------------------------------
-- Difficulty / start helpers -------------------------------------------------
-------------------------------------------------------------------------------

-- Start the game at the chosen difficulty: play the BIOHAZARD soundbite
-- behind a white flash, fade to black, destroy the menu, then the prologue.
local function startGame(self)
    local ctx = self.gameCtx
    self.state = STATE_LEAVING
    self.leavePhase = 0
    self.leaveTimer = 0
    self.gameCtx = ctx

    -- Play BIOHAZARD soundbite once (non-looping).
    local e = scene.createEntity(self.userRoot)
    scene.setField(e, 'name', 'title.soundbite')
    local a = scene.addComponent(e, 'core.audio_emitter')
    scene.setField(a, 'category', 'sfx')
    scene.setField(a, 'volume', 100)
    scene.setField(a, 'pan', 0)
    scene.setField(a, 'loop', false)
    scene.setField(a, 'source', 'common/sound/core/core16.sap')
    scene.setField(a, 'state', 'playing')
    self.soundbite = a
end

-------------------------------------------------------------------------------
-- Menu tree ------------------------------------------------------------------
-- A menu is a table `{ items = {...} }`. Each item is a table:
--   { label, tooltip,
--     sub    = table | function() -> table     -- drills into a submenu
--     action = function(self) ... end          -- leaf action
--     locked = true, unlockLabel = 'Leon B'    -- ???
--   }
-- The menu may also carry `defaultSelected` (1-based index) to override the
-- cursor position when the submenu is first entered.
-- Submenus can be nested tables or factory functions that take the traversal
-- arguments already chosen and return the submenu, closing over them.
-------------------------------------------------------------------------------

local function startGameWith(self, arranged, scenario, difficulty)
    self.gameCtx = {
        arranged   = arranged,                  -- 'jpn' | 'ue'
        player     = scenario.player,            -- 'leon' | 'claire'
        scenario   = scenario.scenario,          -- 'a' | 'b'
        difficulty = difficulty,                 -- 'casual' | 'standard' | 'hardcore'
    }
    startGame(self)
end

local function noop() end

-- Difficulty submenu for one (arranged, scenario) trail. Defaults the cursor
-- to Standard so a plain accept starts on the normal difficulty.
local function createDifficultyMenu(arranged, scenario)
    return {
        defaultSelected = 2, -- Standard
        items = {
            { label = 'Casual',   tooltip = 'Play on Easy difficulty.',
              action = function(self) startGameWith(self, arranged, scenario, 'casual') end },
            { label = 'Standard', tooltip = 'Play on Normal difficulty.',
              action = function(self) startGameWith(self, arranged, scenario, 'standard') end },
            { label = 'Hardcore', tooltip = 'Play on Hard difficulty.',
              action = function(self) startGameWith(self, arranged, scenario, 'hardcore') end },
        },
    }
end

-- Character/scenario submenu shared by New Game (JPN) and New Game (UE). The
-- arranged mode is passed in so every difficulty leaf below gets it as well.
local function createCharacterMenu(arranged)
    local function scenarioItem(label, tooltip, player, scenario)
        return {
            label = label,
            tooltip = tooltip,
            sub = function()
                return createDifficultyMenu(arranged, { player = player, scenario = scenario })
            end,
        }
    end
    -- Locked slots stay '???' and dead until DEBUG_UNLOCK unlocks them; the
    -- sub is still attached so an unlocked slot drills into the difficulty
    -- menu exactly like the A scenarios.
    local function lockedItem(unlockLabel, tooltip, player, scenario)
        return {
            label = '???',
            locked = true,
            unlockLabel = unlockLabel,
            tooltip = tooltip,
            sub = function()
                return createDifficultyMenu(arranged, { player = player, scenario = scenario })
            end,
        }
    end
    return {
        items = {
            scenarioItem('Leon A',   'Play the A scenario as Leon S. Kennedy.', 'leon',   'a'),
            scenarioItem('Claire A', 'Play the A scenario as Claire Redfield.', 'claire', 'a'),
            lockedItem('Leon B',   'Locked — complete Leon A to unlock Leon B.',   'leon',   'b'),
            lockedItem('Claire B', 'Locked — complete Claire A to unlock Claire B.', 'claire', 'b'),
        },
    }
end

-- Main Story: Continue / Load Game / the two shared New Game submenus.
local MAIN_STORY_MENU = {
    items = {
        { label = 'Continue',  tooltip = 'Continue from your most recent save.', action = noop },
        { label = 'Load Game', tooltip = 'Load a previously saved game.',        action = noop },
        { label = 'New Game (JPN)', tooltip = 'Start a new game in the original Japanese arrangement.',
          sub = createCharacterMenu('jpn') },
        { label = 'New Game (UE)', tooltip = 'Start a new game in the USA/Europe arrangement.',
          sub = createCharacterMenu('ue') },
    },
}

local EXTRA_GAMES_MENU = {
    items = {
        { label = '4th Survivor', tooltip = 'Escape the city as HUNK.',       action = noop },
        { label = 'To-fu',        tooltip = 'Escape the city as Tofu.',       action = noop },
        { label = 'Ex. Battle',   tooltip = 'Battle mode against enemy squads.', action = noop },
    },
}

local BONUSES_MENU = {
    items = {
        { label = 'Concept Art',    tooltip = 'View concept artwork.',         action = noop },
        { label = 'Movies',         tooltip = "Watch the game's movies.",      action = noop },
        { label = 'Challenges',     tooltip = 'View challenge records.',       action = noop },
        { label = 'Bonus Settings', tooltip = 'Configure bonus mode settings.', action = noop },
    },
}

local ROOT_MENU = {
    items = {
        { label = 'Main Story', tooltip = 'Start or resume the main scenario.',
          sub = MAIN_STORY_MENU },
        { label = 'Extra Games', tooltip = 'Unlockable bonus mini-games.',
          sub = EXTRA_GAMES_MENU },
        { label = 'Bonuses', tooltip = 'Unlockable gallery content.',
          sub = BONUSES_MENU },
        { label = 'Options', tooltip = 'Adjust game settings.', action = noop },
        { label = 'Exit', tooltip = 'Quit to the desktop.',
          action = function() scene.exit() end },
    },
}

-------------------------------------------------------------------------------
-- Rendering ------------------------------------------------------------------
-------------------------------------------------------------------------------

-- Pulse scalar 0..1 over PULSE_PERIOD, shared by cursor items and START GAME.
local function pulseFactor(self)
    local p = (self.pulseTicks % PULSE_PERIOD) / PULSE_PERIOD
    return (math.sin(p * 2 * math.pi) + 1) * 0.5 -- 0..1
end

-- Opacity of a menu item. The cursor item pulses ALPHA_BASE..ALPHA_PEAK with a
-- sine wave; everything else sits at ALPHA_BASE; locked slots are greyed (and
-- pulse in grey when cursor lands, so the selection is still visible).
local function itemAlpha(self, item, index)
    local pulse = 0
    if index == self.selected then
        pulse = pulseFactor(self)
    end
    if item.locked and not DEBUG_UNLOCK then
        return ALPHA_GREY + (ALPHA_BASE - ALPHA_GREY) * pulse
    end
    return ALPHA_BASE + (ALPHA_PEAK - ALPHA_BASE) * pulse
end

local function cursorShift(self, index)
    if index == self.selected then
        return -CURSOR_SHIFT
    end
    return 0
end

local function itemLabel(item)
    if item.locked and not DEBUG_UNLOCK then
        return '???'
    end
    return item.label
end

-- Target x for one item row (base position, shifted when the cursor is on it).
local function itemTargetX(self, index)
    return ITEM_LEFT + CANVAS_W * 0.5 + cursorShift(self, index)
end

-- Ease one item entity toward its target x (smooth cursor shift).
local function easeItemX(self, index)
    local e = self.itemEntities[index]
    if e == nil then
        return
    end
    local cur = self.itemX[index] or itemTargetX(self, index)
    local tgt = self.itemXTarget[index] or itemTargetX(self, index)
    local nxt = cur + (tgt - cur) * CURSOR_EASE
    if math.abs(nxt - tgt) < 0.5 then
        nxt = tgt
    end
    self.itemX[index] = nxt
    scene.setField(e, 'x', nxt)
end

local function easeItems(self)
    for i = 1, self.itemCount do
        easeItemX(self, i)
    end
end

-- (Re)draw one item line with its current opacity; the cursor item's x target
-- is updated so the shift left animates smoothly.
local function refreshItem(self, index)
    local comp = self.itemComponents[index]
    local item = self.menu.items[index]
    if item == nil then
        scene.setField(comp, 'text', '')
        return
    end
    local locked = item.locked and not DEBUG_UNLOCK
    local alpha = itemAlpha(self, item, index) * self.fade
    local text = locked and greySpan(itemLabel(item), alpha) or whiteSpan(itemLabel(item), alpha)
    scene.setField(comp, 'text', text)
    self.itemXTarget[index] = itemTargetX(self, index)
end

local function refreshItems(self)
    for i = 1, self.itemCount do
        refreshItem(self, i)
    end
end

-- Tooltip: the current item's description, drawn at the bottom of the screen.
local function refreshTooltip(self)
    local item = self.menu.items[self.selected]
    local text = ''
    if item then
        text = whiteSpan(item.tooltip or '', ALPHA_TOOLTIP * self.fade)
    end
    scene.setField(self.tooltipComponent, 'text', text)
end

-- Create one line of text parented under the 320x240 canvas. `centerX` is the
-- desired CENTRE of the text box; with TextFlags.xLeft the glyphs are flush to
-- the box's left edge, so pass centerX = (desired left edge) + bandW/2.
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

local function bandCenterY(topFrac, heightFrac)
    return CANVAS_H * (topFrac - 0.5) + CANVAS_H * heightFrac * 0.5
end

-------------------------------------------------------------------------------
-- Menu navigation ------------------------------------------------------------
-------------------------------------------------------------------------------

local function moveCursor(self, delta)
    local n = #self.menu.items
    self.selected = ((self.selected - 1 + delta) % n) + 1
    self.pulseTicks = 0
    refreshItems(self)
    refreshTooltip(self)
end

-- Begin a level cross-fade. `swapFn` runs halfway (at black-ish midpoint): it
-- points self.menu at the new submenu (or moves back) before fading back in.
local function transitionTo(self, swapFn)
    self.transition = { phase = 'out', timer = 0, swapFn = swapFn }
end

local function updateTransition(self)
    local tr = self.transition
    tr.timer = tr.timer + 1
    local t = tr.timer / FADE_FRAMES
    if tr.phase == 'out' then
        -- Fade out to black, then swap.
        self.fade = math.max(0, 1 - t)
        if t >= 1 then
            tr.phase = 'in'
            tr.timer = 0
            self.fade = 0
            tr.swapFn()
            refreshItems(self)
            refreshTooltip(self)
        end
    else
        -- Fade the new level in.
        self.fade = math.min(1, t)
        if t >= 1 then
            self.transition = nil
            self.fade = 1
        end
    end
    refreshItems(self)
    refreshTooltip(self)
end

local function enterSubmenu(self)
    local item = self.menu.items[self.selected]
    -- Resolve sub: table or factory function closing over the trail.
    local sub = item.sub
    if type(sub) == 'function' then
        sub = sub()
    end
    if sub == nil then
        return
    end
    local previous = self.selected
    transitionTo(self, function()
        self.stack[#self.stack + 1] = { menu = self.menu, selected = previous }
        self.menu = sub
            self.selected = sub.defaultSelected or 1
    end)
end

local function goBack(self)
    transitionTo(self, function()
        local top = table.remove(self.stack)
        self.menu = top.menu
        self.selected = top.selected
    end)
end

-- Item chosen: drill into a submenu, fire a leaf action, or (locked) nothing.
local function selectItem(self)
    local item = self.menu.items[self.selected]
    if item == nil then
        return
    end
    if item.locked and not DEBUG_UNLOCK then
        return -- locked '???': cursor lands, accept is a no-op
    end
    if item.sub then
        enterSubmenu(self)
    elseif item.action then
        item.action(self)
    end
end

-------------------------------------------------------------------------------
-- States ---------------------------------------------------------------------
-------------------------------------------------------------------------------

local function enterPressStart(self)
    self.state = STATE_PRESS_START
    self.ticks = 0
    self.pulseTicks = 0
    self.logoTargetY = self.pressLogoY
    self.stack = {}
    self.menu = ROOT_MENU
    self.selected = 1
    self.fade = 1
    self.transition = nil
    for i = 1, self.itemCount do
        scene.setField(self.itemComponents[i], 'text', '')
    end
    scene.setField(self.tooltipComponent, 'text', '')
    scene.setField(self.pressStart, 'text',
        whiteSpan('START GAME', ALPHA_PEAK))
end

local function enterMenu(self)
    self.state = STATE_MENU
    self.logoTargetY = self.menuLogoY
    self.stack = {}
    self.menu = ROOT_MENU
    self.selected = 1
    self.pulseTicks = 0
    scene.setField(self.pressStart, 'text', '')
    self.fade = 0
    self.transition = { phase = 'in', timer = 0, swapFn = nil } -- fade root in
end

local function enterDemo(self)
    self.state = STATE_DEMO
    self.demoPhase = 0
    self.demoTimer = 0
    scene.setField(self.pressStart, 'text', '')
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

-- Difficulty start sequence: white flash in, hold (over the soundbite), white
-- flash out, then fade to black. On full black, destroy the menu and hand over
-- to the prologue.
local function updateLeaving(self)
    self.leaveTimer = self.leaveTimer + 1
    if self.leavePhase == 0 then -- flash in
        local t = math.min(self.leaveTimer / 12, 1)
        gfx.fade(255, 255, 255, math.floor(t * 255))
        if t >= 1 then
            self.leavePhase = 1
            self.leaveTimer = 0
        end
    elseif self.leavePhase == 1 then -- hold white briefly
        gfx.fade(255, 255, 255, 255)
        if self.leaveTimer >= 10 then
            self.leavePhase = 2
            self.leaveTimer = 0
        end
    elseif self.leavePhase == 2 then -- flash out
        local t = math.min(self.leaveTimer / 12, 1)
        gfx.fade(255, 255, 255, math.floor((1 - t) * 255))
        if t >= 1 then
            self.leavePhase = 3
            self.leaveTimer = 0
        end
    else -- fade to black, then hand over
        local t = math.min(self.leaveTimer / 30, 1)
        gfx.fade(0, 0, 0, math.floor(t * 255))
        if t >= 1 then
            gfx.fade(0, 0, 0, 255)
            self.closed = true
            local canvas = self.canvas
            self.canvas = nil
            if canvas then
                scene.destroy(canvas)
            end
            self:launchPrologue()
        end
    end
end

local function launchPrologue(self)
    local ctx = self.gameCtx or {}
    prologue.start({
        character = ctx.player or 'leon',
        scenario = ctx.scenario or 'a',
        onFinished = function() Title:new() end,
    })
end

-- Demo playback placeholder. Real demo replays a recorded gameplay movie; for
-- now we just log so the flow is observable and return immediately.
local function playDemo(self)
    print('[title] playing demo (stub)')
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
            -- Pulse START GAME with the same 50%..100% sine fade as the menu items.
            self.pulseTicks = self.pulseTicks + 1
            local alpha = ALPHA_BASE + (ALPHA_PEAK - ALPHA_BASE) * pulseFactor(self)
            scene.setField(self.pressStart, 'text', whiteSpan('START GAME', alpha))

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
        -- Smooth up/down pulse of the cursor item, plus cross-fades.
        self.pulseTicks = self.pulseTicks + 1
            easeItems(self)
            if self.transition then
                updateTransition(self)
                return
            end
        refreshItem(self, self.selected)
        if input.pressed('up') or input.pressed('left') then
            moveCursor(self, -1)
        elseif input.pressed('down') or input.pressed('right') then
            moveCursor(self, 1)
        end
        if input.pressed('accept') then
            selectItem(self)
        elseif input.pressed('cancel') then
            if #self.stack > 0 then
                goBack(self)
            else
                enterPressStart(self)
            end
        end
    elseif self.state == STATE_DEMO then
        updateDemo(self)
    elseif self.state == STATE_LEAVING then
        updateLeaving(self)
    end
end

titleMethods = {
    update = update,
    refreshItem = refreshItem,
    refreshItems = refreshItems,
    refreshTooltip = refreshTooltip,
    easeItemX = easeItemX,
    easeItems = easeItems,
    moveCursor = moveCursor,
    selectItem = selectItem,
    updateTransition = updateTransition,
    enterSubmenu = enterSubmenu,
    goBack = goBack,
    enterPressStart = enterPressStart,
    enterMenu = enterMenu,
    enterDemo = enterDemo,
    updateDemo = updateDemo,
    playDemo = playDemo,
    updateLeaving = updateLeaving,
    launchPrologue = launchPrologue,
}

function Title:new()
    local instance = {}
    for name, fn in pairs(titleMethods) do
        instance[name] = fn
    end
    instance.selected = 1
    instance.ticks = 0
    instance.pulseTicks = 0
    instance.itemComponents = {}
    instance.itemEntities = {}
        instance.itemX = {}
        instance.itemXTarget = {}
    instance.itemCount = 10 -- enough rows for the deepest list (character menu)

    -- Logo vertical positions, expressed as offsets from the canvas centre
    -- (the canvas's local origin is its centre, which maps to the window
    -- centre). Negative = above centre.
    instance.pressLogoY = CANVAS_H * (0.34 - 0.5)
    instance.menuLogoY = CANVAS_H * (0.18 - 0.5)

    -- Root entity for things that must outlive the menu canvas.
    instance.userRoot = scene.getRoot("user")

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

    -- START GAME prompt (just above the menu band).
        local _, pt = createLine(
            canvas,
            bigFont,
            0 + 0, bandCenterY(0.56, 0.07), CANVAS_W, CANVAS_H * 0.07,
            whiteSpan('START GAME', ALPHA_PEAK),
            'menu.pressStart',
            TextFlags.xCenter | TextFlags.yMiddle)
        instance.pressStart = pt

        -- Menu item lines, left-aligned to the screen.
        instance.itemTopY = bandCenterY(ITEM_TOP, 0.05)
        instance.itemStepY = CANVAS_H * ITEM_STEP
        for i = 1, instance.itemCount do
            local e, t = createLine(
                canvas,
                bigFont,
                ITEM_LEFT + CANVAS_W * 0.5, instance.itemTopY + (i - 1) * instance.itemStepY,
                CANVAS_W, CANVAS_H * 0.05,
                '',
                'menu.item.' .. i,
                TextFlags.xLeft | TextFlags.yMiddle)
            instance.itemEntities[i] = e
            instance.itemComponents[i] = t
            instance.itemX[i] = ITEM_LEFT + CANVAS_W * 0.5
            instance.itemXTarget[i] = instance.itemX[i]
        end

        -- Tooltip line along the bottom edge (replaces the version disclaimer).
        local _, tt = createLine(
            canvas,
                smallFont or bigFont,
            4, bandCenterY(TOOLTIP_FRAC, 0.06), CANVAS_W, CANVAS_H * 0.06,
            '',
            'menu.tooltip',
            TextFlags.xLeft | TextFlags.yMiddle)
        instance.tooltipComponent = tt

    -- Begin on the PRESS START screen.
    instance.state = STATE_PRESS_START
    instance.fade = 1
    instance.menu = ROOT_MENU
    instance.stack = {}
    instance.transition = nil
    instance.gameCtx = nil
    instance.soundbite = nil
    instance.closed = false

    re.subscribe(HookKind.tick, function()
        update(instance)
    end)

    return instance
end

return Title