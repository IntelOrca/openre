-- app.lua - the RE2 boot flow as registered ECS systems.
--
-- The module boot sequence (splash -> intro movie -> title menu) used to be
-- driven by a scheduler plus a re.subscribe(HookKind.tick) closure in
-- main.lua - that per-frame hook API is gone. The old per-frame imperative
-- state (timers, gfx.fade alpha) now lives in components on entities, driven
-- by two registered systems:
--
--   app.GameFlow - the single transition authority. Reads/writes the `state`
--                  field of the persistent app.GameFlow component on the app
--                  entity created by app.init() (call it from main.lua).
--   app.Splash   - drives the splash parent's sub-entities (canvas, logo,
--                  black fade overlay). It only mutates its own app.Splash
--                  component and the splash subtree; GameFlow polls its
--                  `done` flag for the transition.
--
-- State numbers follow the boot narrative the user described:
--   0 = SPLASH  - GameFlow spawns the splash parent; Splash animates it.
--   1 = DECIDE  - splash finished (or none): pick cmdline / skip / movie.
--   2 = MOVIE   - the intro movie plays async via the native core.Video
--                 entity path; destroying the entity is the teardown.
--   3 = TITLE   - create Title:new() behind a black overlay child of the
--                 title root that fades out exactly like the splash fade-in,
--                 then the overlay is removed.
--   4 = GAMEPLAY - a non-blocking core.runGame() session (cmdline start) is
--                 live; a game.resumeAfterGame callback lands on the title
--                 menu when it returns.
--
-- Components and systems use the reserved `app.*` namespace.

local ecs = require('ecs')
local util = require('script/util')
local game = require('script/game')
local Title = require('script/menu')

local app = {}

-- GameFlow states (see header above).
local STATE_SPLASH = 0
local STATE_DECIDE = 1
local STATE_MOVIE = 2
local STATE_TITLE = 3
local STATE_GAMEPLAY = 4 -- a non-blocking core.runGame() session is live

-- Splash phases: fade in (overlay alpha 255 -> 0), hold, fade out (0 -> 255),
-- hold black, done. The old scheduler fadeIn(60)/wait(120)/fadeOut(60) /
-- holdBlack(60) beat, with the fade now drawn by a black sprite overlay child
-- instead of gfx.fade (which is a single-frame global, unusable from a
-- system that runs every frame).
local FADE_IN_TICKS = 60
local HOLD_TICKS = 120
local FADE_OUT_TICKS = 60
local BLACK_HOLD_TICKS = 60

local PHASE_FADE_IN = 0
local PHASE_HOLD = 1
local PHASE_FADE_OUT = 2
local PHASE_BLACK_HOLD = 3
local PHASE_DONE = 4

-- The title intro movie. Matches the old playIntroMovie path.
local MOVIE_PATH = "pl0\\zmovie\\title_l.bin"

-- Boot overrides: OPENRE_SKIP_INTRO (test boot override) and
-- OPENRE_FAST_RELOAD (set by the engine when the game module reloads at
-- runtime) skip the splash and movie and drop straight on the menu.
local fastReload = re.getEnv('OPENRE_FAST_RELOAD')
local skipIntro = re.getEnv('OPENRE_SKIP_INTRO')

-- Transient boot state. None of this needs to survive a scene clear (hot
-- reload recreates the whole VM and main.lua re-runs app.init).
local boot = {
    splash = nil, -- { id, root, logo, fade, fadeSprite, texture } while alive
    movie = nil,  -- { entity, handle, busy, skip } while a movie plays
    title = nil,  -- { overlay, sprite, timer } while the menu is being revealed
}

-- The app entity's numeric scene id (set by app.init).
local flowId = nil

--------------------------------------------------------------------------------
-- Components ---------------------------------------------------------------
--------------------------------------------------------------------------------

ecs.registerComponent('app.GameFlow', {
    { name = 'state', type = 'int', default = 0 },
})

ecs.registerComponent('app.Splash', {
    { name = 'phase', type = 'int', default = 0 },
    { name = 'timer', type = 'int', default = 0 },
    { name = 'done', type = 'bool', default = false },
})

--------------------------------------------------------------------------------
-- Splash ---------------------------------------------------------------------
--------------------------------------------------------------------------------

-- Creates the splash: a fitted 320x240 canvas parent holding the logo sprite
-- and a textureless black fade overlay child (drawn above the logo as the
-- later sibling). GameFlow owns the parent; this only builds the subtree.
local function spawnSplash()
    local root, id = ecs.spawn()
    if root == nil then
        return
    end
    scene.setField(root, 'name', 'splash')
    ecs.addComponent(id, 'app.Splash')

    local texture = gfx.loadTexture('common/data/gw2', 320, 240)

    -- Logo: the 320x240 splash card. Authored in the canvas design space, so
    -- a 320x240 sprite at the canvas origin (0,0) fills the fitted canvas.
    local logo = scene.createEntity(root)
    scene.setField(logo, 'name', 'splash.logo')
    local sprite = scene.addComponent(logo, 'core.Sprite')
    scene.setField(sprite, 'texture', texture)
    scene.setField(sprite, 'src.x', 0)
    scene.setField(sprite, 'src.y', 0)
    scene.setField(sprite, 'src.width', 0)
    scene.setField(sprite, 'src.height', 0)
    scene.setField(sprite, 'flags', SpriteFlags.stretchX | SpriteFlags.stretchY)
    local size = scene.addComponent(logo, 'core.UiSize')
    scene.setField(size, 'width', 320)
    scene.setField(size, 'height', 240)

    -- Fade overlay: a textureless full-canvas black sprite (the renderer
    -- draws sprites without a texture through its solid pipeline). Its alpha
    -- is animated below; the old gfx.fade (single-frame global) is not used.
    local fade = scene.createEntity(root)
    scene.setField(fade, 'name', 'splash.fade')
    local fadeSprite = scene.addComponent(fade, 'core.Sprite')
    scene.setField(fadeSprite, 'color', { 0, 0, 0, 255 })
    scene.setField(fadeSprite, 'flags', SpriteFlags.stretchX | SpriteFlags.stretchY)
    local fadeSize = scene.addComponent(fade, 'core.UiSize')
    scene.setField(fadeSize, 'width', 320)
    scene.setField(fadeSize, 'height', 240)

    -- Centre + fit the canvas now so the very first rendered frame is correct.
    util.fitCanvas(root)

    boot.splash = {
        id = id,
        root = root,
        logo = logo,
        fade = fade,
        fadeSprite = fadeSprite,
        texture = texture,
    }
end

-- True once the splash has no more work (no splash yet, or done).
local function splashDone()
    local s = boot.splash
    if s == nil then
        return true
    end
    if scene.entityFromId(s.id) == nil then
        return true
    end
    return ecs.get(s.id, 'app.Splash', 'done')
end

-- Removes the splash parent (its children are already gone). No-op when no
-- splash exists (e.g. skip-intro boot). A full collection unloads the splash
-- texture through the handle's __gc so it isn't resident while the menu loads.
local function destroySplash()
    local s = boot.splash
    if s == nil then
        return
    end
    if s.root ~= nil and scene.entityFromId(s.id) ~= nil then
        scene.destroy(s.root)
    end
    boot.splash = nil
    re.collectGarbage()
end

-- app.Splash: drives the splash parent's sub-entities each frame. Mutates
-- only the app.Splash component on the splash root plus the splash subtree;
-- GameFlow reads the fresh `done` flag after this runs.
local function splashSystem()
    local s = boot.splash
    if s == nil then
        return
    end
    if scene.entityFromId(s.id) == nil then
        boot.splash = nil
        return
    end
    if ecs.get(s.id, 'app.Splash', 'done') then
        return
    end

    -- Keep the canvas centred and fit-scaled so it tracks window resizes.
    util.fitCanvas(s.root)

    local phase = ecs.get(s.id, 'app.Splash', 'phase')
    local timer = ecs.get(s.id, 'app.Splash', 'timer') + 1
    local alpha = 0

    if phase == PHASE_FADE_IN then
        alpha = math.floor(255 * (FADE_IN_TICKS - timer) / FADE_IN_TICKS)
        if timer >= FADE_IN_TICKS then
            phase = PHASE_HOLD
            timer = 0
            alpha = 0
        end
    elseif phase == PHASE_HOLD then
        alpha = 0
        if timer >= HOLD_TICKS then
            phase = PHASE_FADE_OUT
            timer = 0
        end
    elseif phase == PHASE_FADE_OUT then
        alpha = math.floor(255 * timer / FADE_OUT_TICKS)
        if timer >= FADE_OUT_TICKS then
            phase = PHASE_BLACK_HOLD
            timer = 0
            alpha = 255
        end
    elseif phase == PHASE_BLACK_HOLD then
        alpha = 255
        if timer >= BLACK_HOLD_TICKS then
            phase = PHASE_DONE
            timer = 0
        end
    end

    scene.setField(s.fadeSprite, 'color', { 0, 0, 0, alpha })
    ecs.set(s.id, 'app.Splash', 'phase', phase)
    ecs.set(s.id, 'app.Splash', 'timer', timer)

    if phase == PHASE_DONE then
            -- Remove the sub-entities; GameFlow destroys the parent itself.
            scene.destroy(s.logo)
            scene.destroy(s.fade)
            s.logo = nil
            s.fade = nil
            s.fadeSprite = nil
            s.texture = nil
            ecs.set(s.id, 'app.Splash', 'done', true)
        end
end

--------------------------------------------------------------------------------
-- Title movie ----------------------------------------------------------------
--------------------------------------------------------------------------------

-- Spawns the intro movie as an entity under the user layer. The path string
-- is handed to the native VideoSystem, which auto-starts playback when it sees
-- a core.Video component whose `movie` path is set and status is Playing;
-- destroying this entity is the whole teardown (VideoSystem mark-and-sweeps
-- orphaned decoders/textures). Returns false when the movie could not be
-- created.
local function startMovie()
    local w = gfx.screenWidth()
    local h = gfx.screenHeight()

    local entity = scene.createEntity(scene.getRoot('user'))
    scene.setField(entity, 'name', 'title_movie')
    -- Sprites are centred on their transform, so anchor the full-window movie
    -- on the screen centre (same as video_play).
    scene.setField(entity, 'x', w * 0.5)
    scene.setField(entity, 'y', h * 0.5)

    local video = scene.addComponent(entity, 'core.Video')
    scene.setField(video, 'movie', MOVIE_PATH)
    scene.setField(video, 'status', 1) -- VideoStatus.Playing

    local size = scene.addComponent(entity, 'core.UiSize')
    scene.setField(size, 'width', w)
    scene.setField(size, 'height', h)

    local m = {
        entity = entity,
        busy = true,
        skip = false,
    }

    -- Any press skips the movie (mirroring the blocking movie loop, which
    -- breaks on any frame.press left after the input handler chain). The
    -- frame.press field is an array of command names, so test its length, not
    -- a raw zero. The input walk runs before the managed systems, so GameFlow
    -- sees the flag the same frame the key went down.
    local handler = scene.addComponent(entity, 'core.InputHandler')
    scene.setField(handler, 'callback', function(frame)
        if #frame.press > 0 then
            m.skip = true
        end
    end)

    boot.movie = m
    return true
end

-- True when the movie is over (finished or skipped or gone).
local function movieFinished()
    local m = boot.movie
    if m == nil or not m.busy then
        return true
    end
    if m.skip then
        return true
    end
    local video = scene.component(m.entity, 'core.Video')
    if video == nil then
        return true
    end
    local st = scene.getField(video, 'status')
    return st ~= 1
end

-- Ends the movie: destroying the entity lets VideoSystem's mark-and-sweep
-- release the decoder and texture on its next pass. A full collection runs
-- any __gc so the resources are gone before the menu loads its textures.
local function destroyMovie()
    local m = boot.movie
    if m == nil then
        return
    end
    if m.entity ~= nil then
        scene.destroy(m.entity)
    end
    m.entity = nil
    m.busy = false
    m.skip = false
    boot.movie = nil
    re.collectGarbage()
end

--------------------------------------------------------------------------------
-- Title ----------------------------------------------------------------------
--------------------------------------------------------------------------------

-- Title fade length: the menu is revealed from black over the same beat as
-- the splash's fade-in (a black overlay child of the title root animated to
-- alpha 0), instead of the old hold-then-pop.
local TITLE_FADE_TICKS = 60

-- Creates the title menu behind a black overlay that fades out exactly like
-- the splash: the overlay is a textureless black sprite child of the title
-- root (the newest child, so it paints above every menu entity), and GameFlow
-- animates its alpha 255 -> 0 over TITLE_FADE_TICKS. `noHold` (skip-intro /
-- fast-reload boots) skips the overlay entirely.
local function spawnTitle(noHold)
    local title = Title:new()
    local overlay = nil
    local sprite = nil
    if not noHold then
        overlay, sprite = util.spawnOverlay(title.root, { 0, 0, 0, 255 })
        scene.setField(overlay, 'name', 'title.black')
    end

    boot.title = {
        overlay = overlay,
        sprite = sprite,
        timer = 0,
    }
end

--------------------------------------------------------------------------------
-- GameFlow -------------------------------------------------------------------
--------------------------------------------------------------------------------

-- Command-line start (openre.exe <savefile> -p <player> -r <roomcode>
-- -s <scenario>): reach gameplay through the module-driven setup APIs +
-- core.runGame, then fall back to the boot. core.runGame is non-blocking, so
-- the caller registers a resume callback that lands on the menu when the
-- session returns. Returns true once gameplay ran.
local function cmdlineStart()
    local cmd = core.cmdline()
    if not cmd or not cmd.startRequested then
        return false
    end

    if cmd.saveRequested then
        if core.loadSave(cmd.savePath) then
            core.runGame()
            return true
        end
        print('[app] cmdline save "' .. cmd.savePath .. '" could not be loaded; entering menu')
    elseif cmd.stage and cmd.stage >= 0 then
        local player = cmd.player >= 0 and cmd.player or 0
        local room = cmd.room >= 0 and cmd.room or 0
        if core.startRoomWarp(player, cmd.stage, room) then
            core.runGame()
            return true
        end
    end
    return false
end

local function getState()
    if flowId == nil or scene.entityFromId(flowId) == nil then
        return nil
    end
    return ecs.get(flowId, 'app.GameFlow', 'state')
end

local function setState(value)
    if flowId == nil or scene.entityFromId(flowId) == nil then
        return
    end
    ecs.set(flowId, 'app.GameFlow', 'state', value)
end

-- app.GameFlow: the single transition authority. It spawns/removes the
-- splash and movie entities and creates the title; the phase systems only
-- report through their own components.
local function gameFlowSystem()
    -- Edge-watch core.inGame() every frame (game.lua): fires any pending
    -- resume callbacks when a non-blocking core.runGame() session returns to
    -- the menu. Runs before the state machine below so a cmdline session that
    -- ended this frame can transition its state same-frame.
    game.track()

    local state = getState()
    if state == nil then
        return
    end

    if state == STATE_SPLASH then
        spawnSplash()
        setState(STATE_DECIDE)
    elseif state == STATE_DECIDE then
        if not splashDone() then
            return
        end
        destroySplash()
        if cmdlineStart() then
            -- Non-blocking: the session plays in the app loop; spawn the menu
            -- when it returns to the module.
            game.resumeAfterGame(function()
                spawnTitle(false)
                setState(STATE_TITLE)
            end)
            setState(STATE_GAMEPLAY)
            return
        end
        if skipIntro or fastReload then
            spawnTitle(true)
            setState(STATE_TITLE)
            return
        end
        if not startMovie() then
            -- Movie unavailable (dev run without assets): menu directly.
            spawnTitle(false)
            setState(STATE_TITLE)
            return
        end
        setState(STATE_MOVIE)
    elseif state == STATE_MOVIE then
        local m = boot.movie
        if m == nil then
            if not startMovie() then
                spawnTitle(false)
                setState(STATE_TITLE)
            end
            return
        end
        if not movieFinished() then
            return
        end
        destroyMovie()
        spawnTitle(false)
        setState(STATE_TITLE)
    elseif state == STATE_GAMEPLAY then
        -- Idle: the resumed game session owns the frame. The resume callback
        -- registered in DECIDE lands on the title menu when it returns.
    elseif state == STATE_TITLE then
        local t = boot.title
        if t == nil then
            return
        end
        if t.overlay ~= nil then
            -- Fade the overlay out exactly like the splash fade-in: alpha
            -- 255 -> 0 over TITLE_FADE_TICKS, then remove it.
            t.timer = t.timer + 1
            local alpha = math.floor(255 * (TITLE_FADE_TICKS - t.timer) / TITLE_FADE_TICKS)
            scene.setField(t.sprite, 'color', { 0, 0, 0, alpha })
            if t.timer < TITLE_FADE_TICKS then
                return
            end
            scene.destroy(t.overlay)
            t.overlay = nil
        end
        boot.title = nil
    end
end

--------------------------------------------------------------------------------
-- Registration / init ---------------------------------------------------------
--------------------------------------------------------------------------------

-- app.Splash mutates the splash; GameFlow must run after it so it sees the
-- same frame's `done` flag. Managed systems default to the slot between
-- core.LegacyGame and core.ScaledFrame, which is before the native core.Video
-- — so a movie entity created here auto-starts playback that same frame.
ecs.registerSystem('app.Splash', { after = { 'core.LegacyGame' } }, splashSystem)
ecs.registerSystem('app.GameFlow', { after = { 'app.Splash' } }, gameFlowSystem)

--- Create the persistent app entity carrying the app.GameFlow component.
--- Call from main.lua. A skip-intro / fast-reload boot starts in the decide
--- state (no splash), so the cmd-line start is still honoured before the
--- module drops on the menu. Returns the app entity id.
function app.init()
    local handle, id = ecs.spawn()
    if handle == nil then
        return nil
    end
    scene.setField(handle, 'name', 'app.flow')
    ecs.addComponent(id, 'app.GameFlow')
    if fastReload or skipIntro then
        ecs.set(id, 'app.GameFlow', 'state', STATE_DECIDE)
    end
    flowId = id
    return id
end

return app