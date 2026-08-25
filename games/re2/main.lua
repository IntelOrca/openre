require('script/scheduler')
local util = require('script/util')
local Title = require('script/menu')

local splash = {}

local function createSplash()
    splash.entity = scene.createEntity(scene.getRoot("user"))
    scene.setField(splash.entity, 'name', 'splash')
    splash.texture = gfx.loadTexture('common/data/gw2', 320, 240)
    splash.sprite = scene.addComponent(splash.entity, 'core.sprite')
    splash.size = scene.addComponent(splash.entity, 'core.ui_size')
    scene.setField(splash.sprite, 'texture', splash.texture)
    -- Authored in the 320x240 design space. The canvas entity (splash.entity)
    -- is centred in the window by util.fitCanvas, and the engine renders the
    -- sprite centred on the entity's transform, so a 320x240 sprite at the
    -- canvas origin (0,0) fills the window-centred canvas.
    scene.setField(splash.size, 'width', 320)
    scene.setField(splash.size, 'height', 240)
    scene.setField(splash.sprite, 'src.x', 0)
    scene.setField(splash.sprite, 'src.y', 0)
    scene.setField(splash.sprite, 'src.width', 0)
    scene.setField(splash.sprite, 'src.height', 0)
    scene.setField(splash.sprite, 'flags', SpriteFlags.stretchX | SpriteFlags.stretchY)
    -- Keep the splash canvas centred and fit-scaled every frame so it tracks
    -- any window resize while it is on screen.
    util.fitCanvas(splash.entity)
    re.subscribe(HookKind.tick, function()
        if splash.entity then
            util.fitCanvas(splash.entity)
        end
    end)
end

-- Hides the splash after the menu takes over so it doesn't linger behind it.
-- The canvas entity is destroyed and the Lua references dropped, then a full
-- collection unloads the texture through the handles' __gc.
local function hideSplash()
    if splash.entity then
        scene.destroy(splash.entity)
    end
    splash.entity = nil
    splash.sprite = nil
    splash.texture = nil
    re.collectGarbage()
end

-- Optional boot override for testing: set OPENRE_PROLOGUE="leon,a" (or
-- claire/b etc.) to skip the title screen and start straight into the prologue
-- once the splash has faded in. Used by automated testing so no menu navigation
-- or movie skipping is required to exercise the intro.
local prologue = require('script/prologue')
local bootPrologue = re.getEnv('OPENRE_PROLOGUE')

local scheduler = Scheduler:new()

-- Shows the title menu after a movie or the prologue: hold solid black for a
-- second, create the menu behind a full-screen overlay, then lift the overlay
-- so the menu fades in.
local function showTitle()
    scheduler
        :holdBlack(60)
        :exec(function()
            Title:new()
        end)
        :fadeIn(60)
end

-- Hot reload (OPENRE_FAST_RELOAD, set by the engine when the game module is
-- reloaded at runtime): skip the splash and title movie entirely and drop
-- straight back on the title menu. menu.lua then reopens the options overlay
-- from the cross-VM `shared` table if it was open before the reload.
local fastReload = re.getEnv('OPENRE_FAST_RELOAD')
if fastReload then
    print('[main] fast reload: skipping splash/movie')
    scheduler:exec(function()
        Title:new()
    end)
else
    scheduler
        :exec(function()
            createSplash()
        end)
        :fadeIn(60)
        :wait(120)
        :fadeOut(60)
        :exec(hideSplash)
        :holdBlack(60)
        :exec(function()
            if bootPrologue then
                local ch, sc = bootPrologue:match('(%w+),(%w+)')
                if ch and sc then
                    prologue.start({
                        character = ch,
                        scenario = sc,
                        onFinished = showTitle,
                    })
                    return
                end
            end

        -- Own function frame so the intro movie's handle dies the moment the
        -- movie is over; the collect below unloads its resource before the
        -- menu loads.
        local function playIntroMovie()
            local intro = core.loadMovie("pl0\\zmovie\\title_l.bin")
            if intro ~= nil then
                core.playMovie(intro)
            end
        end
        playIntroMovie()
        re.collectGarbage()
        showTitle()
    end)
end
