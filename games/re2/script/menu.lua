require('script/scene')
require('script/scheduler')
require('script/util')

local MENU_STATE_TITLE = 0
local MENU_STATE_TRANSITION_TO_MAIN = 1
local MENU_STATE_MAIN = 2
local MENU_STATE_TRANSITION_TO_TITLE = 3
local MENU_STATE_RESIDENT_EVIL = 4
local MENU_STATE_FADE_OUT = 5

Menu = {
    state = MENU_STATE_TITLE,
    logoSceneObject = nil,
    startMenuItem = nil,
    menuItems = {},
    flashTicks = 0
}
Menu.__index = Menu

local function getDifficultyMenu(gameMode, player)
    return {
        {
            text = "EASY",
            action = function()
            end
        },
        {
            text = "NORMAL",
            selected = true,
            action = function()
            end
        },
        {
            text = "HARD",
            action = function()
            end
        }
    }
end

local function getGameSubMenu(gameMode)
    return {
        {
            text = "LEON",
            children = getDifficultyMenu(gameMode, "leon")
        },
        {
            text = "CLAIRE",
            children = getDifficultyMenu(gameMode, "claire")
        }
    }
end

function Menu:getMenuDefinition()
    return {
        {
            text = "LOAD GAME",
            action = function()
                self.state = MENU_STATE_RESIDENT_EVIL
                self.flashTicks = 0
                sfx.playSound(self.resources.residentEvilSound)
            end
        },
        {
            text = "ORIGINAL MODE",
            children = getGameSubMenu("original")
        },
        {
            text = "ARRANGE MODE",
            children = getGameSubMenu("arrange")
        },
        {
            text = "SPECIAL",
            children = {
                {
                    text = "4TH SURVIVOR",
                    action = function()
                    end
                },
                {
                    text = "TO-FU",
                    action = function()
                    end
                },
                {
                    text = "EX.BATTLE",
                    action = function()
                    end
                },
                {
                    text = "GALLERY",
                    action = function()
                    end
                }
            }
        },
        {
            text = "OPTION",
            action = function()
            end
        }
    }
end

function Menu:new()
    local instance = setmetatable({}, Menu)
    instance:loadResources()
    instance:initializeScene()
    return instance
end

function Menu:loadResources()
    self.resources = {
        titleBgTexture = gfx.loadTexture("texture/titlebg", 320, 240),
        logo1Texture = gfx.loadTexture("texture/logo1", 128, 128),
        logo2Texture = gfx.loadTexture("texture/logo2", 128, 128),
        font1 = gfx.loadFont("font/font1"),
        font2 = gfx.loadFont("font/font2"),
        selectSound = sfx.loadSound("sound/select"),
        applySound = sfx.loadSound("sound/apply"),
        backSound = sfx.loadSound("sound/back"),
        residentEvilSound = sfx.loadSound("sound/residentevil")
    }
end

function Menu:initializeScene()
    self.scene = Scene:new()

    local logoSceneObject = Logo:new()
    logoSceneObject.texture1 = self.resources.logo1Texture
    logoSceneObject.texture2 = self.resources.logo2Texture
    logoSceneObject.x = gfx.screenWidth / 2
    logoSceneObject.y = gfx.screenHeight / 2
    logoSceneObject.z = 0.5
    logoSceneObject.scale = 2
    self.scene:add(logoSceneObject)
    self.logoSceneObject = logoSceneObject

    local versionInfo = TextBlock:new()
    versionInfo.font = self.resources.font1
    versionInfo.scale = 0.5 * 2
    local margin = versionInfo.scale * 4
    versionInfo.x = margin
    versionInfo.y = gfx.screenHeight - margin - 4
    versionInfo.z = 0.5
    versionInfo.text = 'OpenRE, Resident Evil 2, Version 1.0'
    self.scene:add(versionInfo)

    local startMenuItem = MenuItem:new()
    startMenuItem.x = gfx.screenWidth / 2
    startMenuItem.y = gfx.screenHeight * 3 / 4
    startMenuItem.text = 'PRESS START'
    startMenuItem.scale = 2
    startMenuItem.font = self.resources.font2
    startMenuItem.selected = true
    startMenuItem.action = function()
        startMenuItem.selected = false
        startMenuItem:fade(0)
        self.logoSceneObject:move(self.logoSceneObject.x, 160)
        self.state = MENU_STATE_TRANSITION_TO_MAIN
        sfx.playSound(self.resources.applySound)
    end
    self.scene:add(startMenuItem)
    self.startMenuItem = startMenuItem

    local x = gfx.screenWidth / 2
    local y = gfx.screenHeight / 2 + 16
    local lastMenuItem = nil
    for _, value in pairs(self:getMenuDefinition()) do
        local menuItem = MenuItem:new()
        menuItem.x = x
        menuItem.y = y
        menuItem.z = 0.5
        menuItem.text = value.text
        menuItem.scale = 2
        menuItem.font = self.resources.font2
        menuItem.selectSound = self.resources.selectSound
        menuItem.opacity = 0
        menuItem.action = value.action
        if lastMenuItem ~= nil then
            menuItem.up = lastMenuItem
            lastMenuItem.down = menuItem
        end
        self.scene:add(menuItem)
        table.insert(self.menuItems, menuItem)
        lastMenuItem = menuItem
        y = y + 32
    end
end

function Menu:update()
    if self.state ~= MENU_STATE_FADE_OUT then
        self.scene:update()
    end

    if self.state == MENU_STATE_TRANSITION_TO_MAIN then
        if not self.logoSceneObject.transitioning then
            self.state = MENU_STATE_MAIN
            for _, value in pairs(self.menuItems) do
                value:fade(1)
            end
            self.menuItems[1].selectReady = true
        end
    elseif self.state == MENU_STATE_MAIN then
        if input.isPressed(InputCommand.menuCancel) then
            self.state = MENU_STATE_TRANSITION_TO_TITLE
            for _, value in pairs(self.menuItems) do
                value.selected = false
                value:fade(0)
            end
            sfx.playSound(self.resources.backSound)
        end
    elseif self.state == MENU_STATE_TRANSITION_TO_TITLE then
        if not self.menuItems[1].transitioning then
            self.state = MENU_STATE_TITLE
            self.startMenuItem:fade(1)
            self.startMenuItem.selectReady = true
            self.logoSceneObject:move(self.logoSceneObject.x, gfx.screenHeight / 2)
        end
    elseif self.state == 4 then
        self.flashTicks = self.flashTicks + 1
        if self.flashTicks == 10 then
            self.state = MENU_STATE_FADE_OUT
        end
    end
end

function Menu:draw()
    local rect = util.getCropRect()
    gfx.drawTexture(self.resources.titleBgTexture, rect.x, rect.y, 0, rect.width, rect.height)
    self.scene:draw()
    if self.state == MENU_STATE_RESIDENT_EVIL then
        local alpha = util.lerp(1, 0, self.flashTicks / 10)
        gfx.drawSolid({
            red = 1,
            green = 1,
            blue = 1,
            alpha = alpha
        }, 0, 0, 1, gfx.screenWidth, gfx.screenHeight)
    end
end

---@param fadeIn boolean
function scheduleMenu(game, fadeIn)
    local menu = nil
    game.scheduler:exec(function()
        menu = Menu:new()
        game.routine = function()
            menu:update()
            menu:draw()
        end
    end)
    if fadeIn then
        game.scheduler:fadeIn(60)
    end
    game.scheduler:run(function()
        return menu ~= nil and menu.state ~= MENU_STATE_FADE_OUT
    end)
    game.scheduler:fadeOut(60 * 3)
    game.scheduler:exec(function()
        game.routine = nil
        menu = nil
    end)
    game.scheduler:run(function()
        return true
    end)
end
