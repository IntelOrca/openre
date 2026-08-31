-- menu.lua — RE2-style title screen (logo + pulsing START GAME -> menu) built
-- on the ECS scene tree. The menu is a data-driven tree of Lua tables; submenus
-- are returned by factory functions which close over the navigation trail, so
-- leaf actions receive the full context (arranged mode, character, scenario,
-- difficulty) without any trail-tracking state.
--
-- States:
--   PRESS_START  logo + pulsing START GAME. accept/menu -> MENU. After a
--                timeout the screen fades out and the next recorded demo plays
--                (PDEMO00 -> PDEMO01 -> PDEMO02 -> PDEMO00 -> ...).
--   MENU         a stack of menu levels. up/down/left/right move the cursor;
--                accept drills one level in, cancel backs one out (or to
--                PRESS START at the root). Level changes fade out then the new
--                level fades in; input is locked during the transition.
--   DEMO         fade out to black, then core.loadDemo + core.runGame() drive
--                the legacy game replaying the demo until it returns to the
--                menu (native demo timeout/cancel), then fade back in to the
--                PRESS START screen.
--   LEAVING      difficulty start sequence: play the 'RESIDENT EVIL 2' voice
--                bite behind a white flash, fade to black, hold solid black,
--                destroy the menu and hand over to the prologue.
--   OPTIONS      the options overlay (script/options.lua) is open on top of
--                the root menu; the menu suspends all input polling until the
--                overlay closes itself, then returns to MENU.

-- The sandboxed VM removes setmetatable, so each Title instance carries its
-- methods directly instead of sharing them through a class metatable.
local prologue          = require("script/prologue")
local LoadScreen        = require("script/load")
require("script/scheduler") -- Scheduler (global) for the handoff chains
local options           = require("script/options")
local game              = require("script/game")
local ecs               = require("ecs")

local Title             = {}
local titleMethods      = {}

-- Live Title instances, driven by the registered app.Menu managed system
-- below (one frame of update() per instance, mirroring the old per-instance
-- tick subscription). Instances are removed when closeMenu() closes them.
local liveTitles = {}

-- Design space for the title screen: content is authored in this fixed
-- 320x240 coordinate space. Two sibling layers under the user root map it
-- onto the window via native ScaledFrameComponents (see Title:new): "back"
-- holds only the background and contains-fits it exactly; "front" holds the
-- UI (logo + text) and snaps its scale down to whole SCALE_STEP increments.
local CANVAS_W          = 320
local CANVAS_H          = 240
local SCALE_STEP        = 0.5                   -- UI scale quantum (front layer)

local STATE_PRESS_START = 0
local STATE_MENU        = 1
local STATE_DEMO        = 2
local STATE_LEAVING     = 3
local STATE_OPTIONS     = 4

-- Ticks to wait on the PRESS START screen before the demo plays (~15s @ 60fps).
local DEMO_TIMEOUT      = 60 * 15

-- Recorded demos, cycled by the idle timeout (Pl0\Pld\PDEMO0N.DAT in the game
-- data). The native core.loadDemo replays them through the legacy game.
local DEMOS             = { 'PDEMO00', 'PDEMO01', 'PDEMO02' }

-- Handoff timing when the menu hands over to another screen: fade out to
-- black, destroy, hold solid black (~1s), then let the new screen fade in.
local SCREEN_FADE_FRAMES = 60
local SCREEN_HOLD_FRAMES = 60

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

local function containsCommand(list, name)
    for i = 1, #list do
        if list[i] == name then
            return true
        end
    end
    return false
end

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
-- UI sound effects -----------------------------------------------------------
-------------------------------------------------------------------------------

-- The menu sounds, loaded the first time the title screen is created (not at
-- script load). core22.sap packs four menu blips (sub 0 cursor, 1 back,
-- 2 accept, 3 invalid); core17.sap sub 0 is the 'RESIDENT EVIL 2' voice bite
-- played when a new game starts. audio.loadAudio deduplicates, so later
-- Title instances share the same decoded sounds.

-- Plays a resident sound through the menu's audio_emitter: point it at the
-- sound, then write a fresh trigger value so AudioSystem fires the one-shot
-- from its first sample. Safe for rapid repeated input.
local function playSfx(self, sound)
    if sound == nil or self.emitter == nil then
        return
    end
    self.sfxTrigger = self.sfxTrigger + 1
    scene.setField(self.emitter, 'sound', sound)
    scene.setField(self.emitter, 'trigger', self.sfxTrigger)
end

-------------------------------------------------------------------------------
-- Screen teardown / handoffs ---------------------------------------------------
-------------------------------------------------------------------------------

-- Destroys every entity this menu created, drops all resource handles and
-- collects so they unload before the next screen takes over.
local function closeMenu(self)
    self.closed = true
    if self.front then
        scene.destroy(self.front)
        self.front = nil
        self.frontFrame = nil
    end
    if self.back then
        scene.destroy(self.back)
        self.back = nil
        self.backFrame = nil
    end
    if self.backdrop then
        scene.destroy(self.backdrop)
        self.backdrop = nil
        self.backdropSprite = nil
    end
    -- The menu's resources (sounds, fonts, textures) belong to this
    -- screen: drop every handle and collect so they unload.
    if self.emitterEntity then
        scene.destroy(self.emitterEntity)
        self.emitterEntity = nil
    end
    self.emitter = nil
    self.sfx = nil
    self.assets = nil
    re.collectGarbage()
end

-- Load Game handoff: fade the menu out to black, destroy it, hold black for
-- about a second, then create the load screen behind a fade in.
local function openLoadScreen(self)
    if self.handoff then
        return
    end
    self.handoff = true
    playSfx(self, self.sfx.accept)
    Scheduler:new()
        :fadeOut(SCREEN_FADE_FRAMES)
        :exec(function()
            closeMenu(self)
            LoadScreen:new()
        end)
        :holdBlack(SCREEN_HOLD_FRAMES)
        :fadeIn(SCREEN_FADE_FRAMES)
end

-------------------------------------------------------------------------------
-- Difficulty / start helpers -------------------------------------------------
-------------------------------------------------------------------------------

-- Start the game at the chosen difficulty: play the 'RESIDENT EVIL 2' voice
-- bite behind a white flash, fade to black, destroy the menu, then the prologue.
local function startGame(self)
    local ctx = self.gameCtx
    self.state = STATE_LEAVING
    self.leavePhase = 0
    self.leaveTimer = 0
    self.gameCtx = ctx

    -- Play the voice bite once (non-looping).
    playSfx(self, self.sfx.startVoice)
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
        { label = 'Load Game', tooltip = 'Load a previously saved game.',
          action = function(self) openLoadScreen(self) end },
        { label = 'New Game (JPN)', tooltip = 'Start a new game in the original Japanese arrangement.',
          sub = createCharacterMenu('jpn') },
        { label = 'New Game (UE)', tooltip = 'Start a new game in the USA/Europe arrangement.',
          sub = createCharacterMenu('ue') },
    },
}

-- 4th survivor and To-fu are locked by default: they display as greyed-out
-- '???' rows (see the locked handling in itemLabel/refreshItem) until the
-- game is completed, which is exactly what the tooltip tells the player.
local EXTRA_GAMES_MENU = {
    items = {
        { label = '4th Survivor', locked = true, tooltip = 'Complete the game to unlock.', action = noop },
        { label = 'To-fu',        locked = true, tooltip = 'Complete the game to unlock.', action = noop },
        { label = 'Ex. Battle',           tooltip = 'Battle mode against enemy squads.',   action = noop },
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
        { label = 'Options', tooltip = 'Adjust game settings.',
          -- Enter STATE_OPTIONS: the overlay takes over input until it closes
          -- itself on cancel (see the STATE_OPTIONS branch in update()).
          -- If opening fails, fall straight back to the menu.
          action = function(self)
              self.state = STATE_OPTIONS
              local ok, err
              if options ~= nil and type(options.open) == 'function' then
                  ok, err = pcall(options.open)
              else
                  ok, err = false, 'options module unavailable'
              end
              if not ok then
                  print('[menu] failed to open options overlay: '
                      .. tostring(err))
                  self.state = STATE_MENU
              else
                  -- The overlay paints its own 80%-opacity backdrop, so the menu
                  -- stays visible, dimmed, underneath the popup.
              end
          end },
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

-- Create one line of text parented under the front layer (320x240 space). `centerX` is the
-- desired CENTRE of the text box; with TextFlags.xLeft the glyphs are flush to
-- the box's left edge, so pass centerX = (desired left edge) + bandW/2.
local function createLine(parent, font, centerX, centerY, bandW, bandH, text, name, flags)
    local e = scene.createEntity(parent)
    scene.setField(e, 'name', name)
    scene.setField(e, 'x', centerX)
    scene.setField(e, 'y', centerY)
    local t = scene.addComponent(e, 'core.Text')
    scene.setField(t, 'font', font)
    scene.setField(t, 'text', text)
    local size = scene.addComponent(e, 'core.UiSize')
    scene.setField(size, 'width', bandW)
    scene.setField(size, 'height', bandH)
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
    local previous = self.selected
    self.selected = ((self.selected - 1 + delta) % n) + 1
    if self.selected ~= previous then
        playSfx(self, self.sfx.cursor)
    end
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
    playSfx(self, self.sfx.back)
    transitionTo(self, function()
        local top = table.remove(self.stack)
        self.menu = top.menu
        self.selected = top.selected
    end)
end

-- Item chosen: drill into a submenu, fire a leaf action, or buzz when the
-- entry cannot do anything (locked '???' or an unimplemented placeholder).
local function selectItem(self)
    local item = self.menu.items[self.selected]
    if item == nil then
        return
    end
    if (item.locked and not DEBUG_UNLOCK) or item.action == noop then
        playSfx(self, self.sfx.invalid)
        return -- locked '???': cursor lands, accept is a no-op
    end
    if item.sub then
        playSfx(self, self.sfx.accept)
        enterSubmenu(self)
    elseif item.action then
        playSfx(self, self.sfx.accept)
        item.action(self)
    else
        playSfx(self, self.sfx.invalid)
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
    self.demoIndex = self.demoIndex or 0 -- next index into DEMOS
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
            self:playDemo()
        end
    elseif self.demoPhase == 1 then
        -- Demo playback (core.runGame is non-blocking). The menu stays alive
        -- behind the black; when the session returns to the menu the overlay
        -- is below 255 and phase 2 fades back in to the PRESS START screen.
        if not game.inGame() then
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

-- Difficulty start sequence: white flash in, hold (over the voice bite), white
-- flash out, then fade to black. After a second of solid black, destroy the
-- menu and hand over to the prologue.
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
    elseif self.leavePhase == 3 then -- fade to black
        local t = math.min(self.leaveTimer / 240, 1)
        gfx.fade(0, 0, 0, math.floor(t * 255))
        if t >= 1 then
            gfx.fade(0, 0, 0, 255)
            self.leavePhase = 4
            self.leaveTimer = 0
        end
    else -- hold solid black for a second, then hand over
        gfx.fade(0, 0, 0, 255)
        if self.leaveTimer >= 60 then
            closeMenu(self)
            self:launchPrologue()
        end
    end
end

local function launchPrologue(self)
    local ctx = self.gameCtx or {}
    prologue.start({
        character = ctx.player or 'leon',
        scenario = ctx.scenario or 'a',
        onFinished = function()
            -- Hand over to the legacy game exactly like load/demo: latch a
            -- fresh game from the chosen character/scenario/difficulty, then
            -- run it non-blocking. When the session returns to the menu,
            -- recreate the title so the player lands back on Main Story.
            if core.startNewGame(ctx.player or 'leon', ctx.scenario or 'a', ctx.difficulty or 'standard') then
                core.runGame()
                game.resumeAfterGame(function()
                    Title:new({ mainStory = true })
                end)
            else
                print('[title] new game failed to start')
                Title:new({ mainStory = true })
            end
        end,
    })
end

-- Demo playback: load the next recorded demo into the legacy game and run it
-- non-blocking (core.runGame enables the LegacyGame node and returns; the
-- game's native demo-end/cancel path signals a return to the menu). The menu
-- stays alive behind the black overlay; updateDemo's phase 1 waits for the
-- session to end, then phase 2 fades back in to the PRESS START screen.
local function playDemo(self)
    local name = DEMOS[((self.demoIndex or 0) % #DEMOS) + 1]
    self.demoIndex = ((self.demoIndex or 0) + 1) % #DEMOS
    if not core.loadDemo(name) then
        print('[title] demo ' .. name .. ' failed to load; skipping')
        self.demoPhase = 2
        self.demoTimer = 0
        return
    end
    print('[title] playing demo ' .. name)
    core.runGame()
end

-- Keep one scaled-frame layer tracking the window: recentre it and refresh
-- its target to the current window size so ScaledFrameSystem recomputes the
-- scale (exact fit for back, snapped to SCALE_STEP steps for front).
local function fitLayer(entity, frame)
    local w = gfx.screenWidth()
    local h = gfx.screenHeight()
    scene.setField(entity, 'x', w * 0.5)
    scene.setField(entity, 'y', h * 0.5)
    scene.setField(frame, 'target.width', w)
    scene.setField(frame, 'target.height', h)
end

local function update(self)
    if self.closed then
        return
    end
    -- A handoff chain (e.g. Load Game) owns everything from here on; freeze
    -- input and redraws until the screen is destroyed.
    if self.handoff then
        return
    end
    self.ticks = self.ticks + 1

    -- While the options overlay is open the menu stays in place underneath the
    -- overlay's 80%-opacity backdrop: skip all input polling. The overlay's
    -- tick runs BEFORE ours within each frame. When cancel closes the overlay
    -- on frame N, our tick that same frame sees isOpen() == false and returns
    -- to STATE_MENU having polled nothing; the cancel edge expires with frame
    -- N, so STATE_MENU polling resumes clean on frame N+1 with no
    -- double-handling. (The menu's InputHandler snapshot is also empty while
    -- the overlay is open: the overlay's handler runs earlier in the walk and
    -- nulls the frame.)
    if self.state == STATE_OPTIONS then
        if not options.isOpen() then
            self.state = STATE_MENU
        end
        return
    end

    -- Re-centre the layers and refresh their targets every frame so they
    -- track window resizes.
    fitLayer(self.back, self.backFrame)
    fitLayer(self.front, self.frontFrame)

    -- Keep the solid backdrop covering the whole window behind everything.
    if self.backdrop then
        local w = gfx.screenWidth()
        local h = gfx.screenHeight()
        scene.setField(self.backdrop, 'x', w * 0.5)
        scene.setField(self.backdrop, 'y', h * 0.5)
        scene.setField(self.backdropSize, 'width', w)
        scene.setField(self.backdropSize, 'height', h)
    end

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
        if self.nav.accept or self.nav.menu then
            playSfx(self, self.sfx.accept)
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
        if self.nav.up or self.nav.left then
            moveCursor(self, -1)
        elseif self.nav.down or self.nav.right then
            moveCursor(self, 1)
        end
        if self.nav.accept then
            selectItem(self)
        elseif self.nav.cancel then
            if #self.stack > 0 then
                goBack(self)
            else
                playSfx(self, self.sfx.back)
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

function Title:new(opts)
    opts = opts or {}
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
    instance.handoff = false

    -- Logo vertical positions, expressed as offsets from the layer centre
    -- (each layer's local origin is its centre, which maps to the window
    -- centre). Negative = above centre.
    instance.pressLogoY = CANVAS_H * (0.34 - 0.5)
    instance.menuLogoY = CANVAS_H * (0.18 - 0.5)

    -- Root entity for things that must outlive the menu layers.
    instance.userRoot = scene.getRoot("user")

    -- Menu sounds, loaded on first open (audio.loadAudio deduplicates, so
    -- later Title instances share the same decoded sounds). The handles are
    -- kept on the instance: resource handles are garbage collected, and the
    -- emitter's `sound` field only borrows its cookie.
    instance.sfx = {
        cursor = audio.loadAudio('common/sound/core/core22.sap', 0),
        back = audio.loadAudio('common/sound/core/core22.sap', 1),
        accept = audio.loadAudio('common/sound/core/core22.sap', 2),
        invalid = audio.loadAudio('common/sound/core/core22.sap', 3),
        startVoice = audio.loadAudio('common/sound/core/core17.sap', 0),
    }

    -- Two sibling layers under the user root, each mapping the 320x240 design
    -- space onto the window through a native scaled-frame component (the
    -- ScaledFrameSystem applies the actual scale every frame):
    --   back  — the background only; contains-fits the window exactly.
    --   front — the UI (logo + text); same fit, but the scale snaps DOWN to
    --           whole SCALE_STEP increments so art never renders at odd
    --           fractions. Both roots are placed at the window centre by
    --           fitLayer(); children live in a coordinate space centred on
    --           (0,0) and each piece of content is centred on its own origin.
    local function createLayer(name, increment)
        local entity = scene.createEntity(instance.userRoot)
        scene.setField(entity, 'name', name)
        local frame = scene.addComponent(entity, 'core.ScaledFrame')
        scene.setField(frame, 'source.width', CANVAS_W)
        scene.setField(frame, 'source.height', CANVAS_H)
        scene.setField(frame, 'mode', 'contain')
        scene.setField(frame, 'increment', increment)
        return entity, frame
    end

    -- One persistent audio emitter for every UI sound: playSfx points it at a
    -- resident sound and bumps `trigger` to fire it as a one-shot. The
    -- component handle (not the entity) is kept, as emitter fields live on
    -- the component.
    local emitter = scene.createEntity(instance.userRoot)
    scene.setField(emitter, 'name', 'title.sfx')
    local emitterComponent = scene.addComponent(emitter, 'core.AudioEmitter')
    instance.emitterEntity = emitter
    instance.emitter = emitterComponent
    instance.sfxTrigger = 0

    -- Solid dark-red backdrop filling the whole window. Created before the
    -- layers so it paints behind them (equal z keeps creation order); a
    -- texture-less sprite draws as a solid colour quad (see render2d).
    local backdrop = scene.createEntity(instance.userRoot)
    scene.setField(backdrop, 'name', 'menu.backdrop')
    local backdropSprite = scene.addComponent(backdrop, 'core.Sprite')
    scene.setField(backdropSprite, 'color', { 8, 0, 0, 255 })
    local backdropSize = scene.addComponent(backdrop, 'core.UiSize')
    scene.setField(backdropSize, 'width', gfx.screenWidth())
    scene.setField(backdropSize, 'height', gfx.screenHeight())
    scene.setField(backdrop, 'x', gfx.screenWidth() * 0.5)
    scene.setField(backdrop, 'y', gfx.screenHeight() * 0.5)
    instance.backdrop = backdrop
    instance.backdropSprite = backdropSprite
    instance.backdropSize = backdropSize

    -- Input handler: snapshots the menu's nav presses out of the frame during
    -- the input walk (which runs before the script tick), so update() reads
    -- the snapshot rather than the removed input.pressed(). The frame is not
    -- touched, so the game fan-out still receives the press. While the options
    -- overlay is open, its handler (a later-created sibling of this menu's
    -- entities under the user root) runs first and nulls the frame, so this
    -- handler sees nothing until the overlay closes - matching the old
    -- STATE_OPTIONS self.state gating below.
    instance.nav = { accept = false, menu = false, up = false, down = false, left = false, right = false, cancel = false }
    local menuHandler = scene.addComponent(backdrop, 'core.InputHandler')
    scene.setField(menuHandler, 'callback', function(frame)
        local nav = instance.nav
        nav.accept = containsCommand(frame.press, 'accept')
        nav.menu = containsCommand(frame.press, 'menu')
        nav.up = containsCommand(frame.press, 'up')
        nav.down = containsCommand(frame.press, 'down')
        nav.left = containsCommand(frame.press, 'left')
        nav.right = containsCommand(frame.press, 'right')
        nav.cancel = containsCommand(frame.press, 'cancel')
    end)

    instance.back, instance.backFrame = createLayer('menu.back', 0)
    instance.front, instance.frontFrame = createLayer('menu.front', SCALE_STEP)

    -- Fonts and textures borrowed by this menu's components. Resource
    -- handles unload on garbage collection, so the instance must keep every
    -- handle alive for as long as its components reference the cookies.
    -- Role fonts follow the [options] "font" mode; the swap walk remaps
    -- already-built scene text, and re-opening re-resolves the roles.
    local uiFont = require('ui.font')
    local bigFont = uiFont.get('big')
    local smallFont = uiFont.get('small')
    local bgTex = gfx.loadTexture('common/data/Title_bg', 320, 240)
    instance.assets = { bigFont, smallFont, bgTex }

    -- Original RE2 title background (data://), authored at 320x240. It lives
    -- under the back layer, centred on the layer origin (0,0) = window centre,
    -- so it tracks the window smoothly while the front UI snaps between steps.
    local bgTex = instance.assets[3]
    if bgTex then
        local e = scene.createEntity(instance.back)
        scene.setField(e, 'name', 'menu.background')
        scene.setField(e, 'x', 0)
        scene.setField(e, 'y', 0)
        local s = scene.addComponent(e, 'core.Sprite')
        scene.setField(s, 'texture', bgTex)
        local size = scene.addComponent(e, 'core.UiSize')
        scene.setField(size, 'width', CANVAS_W)
        scene.setField(size, 'height', CANVAS_H)
        scene.setField(s, 'flags', SpriteFlags.stretchX | SpriteFlags.stretchY)
    end

    -- RE2 title logo: assembled from TIM index 1 and TIM index 2 of tmoji.bin.
    -- The logo root sits at the front layer centre (0,0); each tile is a static
    -- relative offset from that root, chosen so the assembled logo is centred.
    -- (The right-hand TIM is mostly empty, so the offsets are biased left of the
    -- raw source-rectangle centre to keep the visible logo centred.)
    local LOGO_TILES = {
        { 1, 0, 0,  128, 80, -76,   0 },
        { 1, 0, 80, 128, 48,  52, -16 },
        { 2, 0, 0,  128, 32,  52,  24 },
        { 2, 0, 32, 128, 80, 180,   0 },
    }

    local logoRoot = scene.createEntity(instance.front)
    scene.setField(logoRoot, 'name', 'menu.logo')
    -- Centred on the front layer origin (0,0) = window centre; the tiles below are
    -- static offsets from this root already chosen to centre the assembled logo.
    scene.setField(logoRoot, 'x', 0)
    scene.setField(logoRoot, 'y', instance.pressLogoY)
    instance.logoRoot = logoRoot
    instance.logoY = instance.pressLogoY
    instance.logoTargetY = instance.pressLogoY

    for _, t in ipairs(LOGO_TILES) do
        local tex = gfx.loadTexture('common/data/tmoji.bin', 0, 0, t[1])
        if tex then
            instance.assets[#instance.assets + 1] = tex
            local e = scene.createEntity(logoRoot)
            scene.setField(e, 'name', 'menu.logo.tile')
            scene.setField(e, 'x', t[6])
            scene.setField(e, 'y', t[7])
            local s = scene.addComponent(e, 'core.Sprite')
            scene.setField(s, 'texture', tex)
            scene.setField(s, 'src.x', t[2])
            scene.setField(s, 'src.y', t[3])
            scene.setField(s, 'src.width', t[4])
            scene.setField(s, 'src.height', t[5])
            local size = scene.addComponent(e, 'core.UiSize')
            scene.setField(size, 'width', t[4])
            scene.setField(size, 'height', t[5])
            scene.setField(s, 'flags', SpriteFlags.stretchX | SpriteFlags.stretchY)
        end
    end

    -- START GAME prompt (just above the menu band).
        local _, pt = createLine(
            instance.front,
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
                instance.front,
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
            instance.front,
                smallFont or bigFont,
            4, bandCenterY(TOOLTIP_FRAC, 0.06), CANVAS_W, CANVAS_H * 0.06,
            '',
            'menu.tooltip',
            TextFlags.xLeft | TextFlags.yMiddle)
        instance.tooltipComponent = tt

    -- Begin on the PRESS START screen (default), or straight in the Main
    -- Story menu when returning from a sub-screen (e.g. the load screen).
    instance.state = STATE_PRESS_START
    instance.fade = 1
    instance.menu = ROOT_MENU
    instance.stack = {}
    instance.transition = nil
    instance.gameCtx = nil
    instance.closed = false
if opts.mainStory then
        enterMenu(instance)
        instance.menu = MAIN_STORY_MENU
        instance.selected = 2 -- Load Game
        refreshItems(instance)
        refreshTooltip(instance)
    end

    liveTitles[#liveTitles + 1] = instance

    return instance
end

-- app.Menu: drive every live title instance once per frame and prune closed
-- ones (the instance's update() no-ops once closed, but dropping it also lets
-- the instance be collected). Replaces the old per-instance tick hook.
ecs.registerSystem('app.Menu', {
    after = { 'core.LegacyGame' },
}, function()
    local i = 1
    while i <= #liveTitles do
        local t = liveTitles[i]
        if t.closed then
            table.remove(liveTitles, i)
        else
            t:update()
            if t.closed then
                -- closeMenu() ran during update (e.g. the LEAVING handover).
                table.remove(liveTitles, i)
            else
                i = i + 1
            end
        end
    end
end)

return Title
