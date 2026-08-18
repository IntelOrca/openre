require('script/scheduler')
require('script/util')

local app = {}
local scheduler
local splashTexture
local splashEntity
local splashSprite

local function createSplash()
    splashEntity = scene.createEntity()
    splashSprite = scene.addComponent(splashEntity, 'core.sprite')
    splashTexture = gfx.loadTexture('common/data/gw2', 320, 240)
    scene.setField(splashSprite, 'texture', splashTexture)
        scene.setField(splashSprite, 'size.width', gfx.screenWidth())
        scene.setField(splashSprite, 'size.height', gfx.screenHeight())
        scene.setField(splashSprite, 'src.x', 0)
        scene.setField(splashSprite, 'src.y', 0)
        scene.setField(splashSprite, 'src.width', 0)
        scene.setField(splashSprite, 'src.height', 0)
        scene.setField(splashSprite, 'flags', SpriteFlags.stretchX | SpriteFlags.stretchY)
end

scheduler = Scheduler:new()

local function main()
    scheduler
        :exec(function()
            createSplash()
        end)
        :fadeIn(60)
        :wait(120)
        :fadeOut(60)
        :exec(function()
            splashTexture = nil
            scene.exit()
        end)
end

main()