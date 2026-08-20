require('script/scheduler')
require('script/util')

local Menu = require('script/menu')

local splash = {}

local function createSplash()
    splash.entity = scene.createEntity()
    splash.sprite = scene.addComponent(splash.entity, 'core.sprite')
    splash.texture = gfx.loadTexture('common/data/gw2', 320, 240)
    scene.setField(splash.sprite, 'texture', splash.texture)
    scene.setField(splash.sprite, 'size.width', gfx.screenWidth())
    scene.setField(splash.sprite, 'size.height', gfx.screenHeight())
    scene.setField(splash.sprite, 'src.x', 0)
    scene.setField(splash.sprite, 'src.y', 0)
    scene.setField(splash.sprite, 'src.width', 0)
    scene.setField(splash.sprite, 'src.height', 0)
    scene.setField(splash.sprite, 'flags', SpriteFlags.stretchX | SpriteFlags.stretchY)
end

-- Hides the splash after the menu takes over so it doesn't linger behind it.
local function hideSplash()
    if splash.sprite then
        scene.setField(splash.sprite, 'size.width', 0)
        scene.setField(splash.sprite, 'size.height', 0)
    end
    splash.entity = nil
    splash.sprite = nil
    splash.texture = nil
end

local scheduler = Scheduler:new()

scheduler
    :exec(function()
        createSplash()
    end)
    :fadeIn(60)
    :wait(120)
    :fadeOut(60)
    :exec(function()
        hideSplash()
        Menu:new()
    end)
    :fadeIn(60)