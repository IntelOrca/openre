-- Scheduler API
local scheduler = {
    curr = nil,
    q = {}
}

function scheduler:update()
    while true do
        local curr = self.curr
        if curr == nil then
            if #self.q == 0 then
                break
            end
            curr = table.remove(self.q, 1)
            self.curr = curr
        end
        if curr.kind == 0 then
            if curr.time <= 0 then
                self.curr = nil
            else
                curr.time = curr.time - 1
                break
            end
        elseif curr.kind == 1 then
            curr.cb()
            self.curr = nil
        elseif self.curr.kind == 2 then
            if curr.cb() then
                break
            else
                self.curr = nil
            end
        else
            self.curr = nil
        end
    end
end

function scheduler:wait(time)
    table.insert(self.q, { kind = 0, time = time })
    return self
end

function scheduler:exec(cb)
    table.insert(self.q, { kind = 1, cb = cb })
    return self
end

function scheduler:run(cb)
    table.insert(self.q, { kind = 2, cb = cb })
    return self
end

function scheduler:fadeIn(time)
    local timer = time + 1
    self:run(function()
        timer = timer - 1
        local fade = timer / time
        gfx.fade(0, 0, 0, fade)
        return timer > 0
    end)
    return self
end

function scheduler:fadeOut(time)
    local timer = time + 1
    self:run(function()
        timer = timer - 1
        local fade = 1 - (timer / time)
        gfx.fade(0, 0, 0, fade)
        return timer > 0
    end)
    return self
end

function dump(value)
    if type(value) == "table" then
        for key, val in pairs(value) do
            if type(val) == "table" then
                print(key .. ":")
                dump(val) -- Recursively dump nested tables
            else
                print(key .. ": " .. tostring(val))
            end
        end
    else
        print(tostring(value)) -- Print as string if not a table
    end
end

local STATE_START = 0
local STATE_SPLASH = 1
local STATE_MOVIE = 2
local STATE_MENU = 3
local STATE_NOTHING = 4

local splashTexture
local introMovie
local titleBgTexture
local logo1Texture
local logo2Texture
local font1
local font2
local selectSound
local applySound
local backSound
local residentEvilSound

local initialized = false
local state = STATE_START
local enableSplash = false
local enableVideo = false

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

local menuState = 0
local logoSceneObject
local startMenuItem
local menuItems = {}
local flashTicks = 0

local menu = {
    {
        text = "LOAD GAME",
        action = function()
            menuState = 4
            flashTicks = 0
            sfx.playSound(residentEvilSound)
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

function lerp(start, stop, t)
    return start + (stop - start) * t
end

function moveTowards(a, b, s)
    local delta = b - a
    if delta > 0 then
        return math.min(a + s, b)
    elseif delta < 0 then
        return math.max(a - s, b)
    else
        return b
    end
end

local function updateTransition(t)
    local delta = t.target - t.current
    if delta > 0 then
        t.current = math.min(t.current + t.speed, t.target)
    elseif delta < 0 then
        t.current = math.max(t.current - t.speed, t.target)
    end
end

local function setTransition(t, value)
    t.current = value
    t.target = value
end

---@class SceneObject
SceneObject = {
    x = 0,
    y = 0,
    z = 0,
    opacity = 1,
    transitioning = false
}
SceneObject.__index = SceneObject

function SceneObject:new()
    return setmetatable({}, SceneObject)
end

function SceneObject:move(x, y)
    if self.x ~= x then
        self.targetX = x
        self.transitioning = true
    end
    if self.y ~= y then
        self.targetY = y
        self.transitioning = true
    end
end

function SceneObject:fade(opacity)
    if self.opacity ~= opacity then
        self.targetOpacity = opacity
        self.transitioning = true
    end
end

function SceneObject:update()
    local speed = 12
    local speedOpacity = 0.1
    self.transitioning = false
    if type(self.targetX) == 'number' then
        self.x = moveTowards(self.x, self.targetX, speed)
        if self.x == self.targetX then
            self.targetX = nil
        else
            self.transitioning = true
        end
    end
    if type(self.targetY) == 'number' then
        self.y = moveTowards(self.y, self.targetY, speed)
        if self.y == self.targetY then
            self.targetY = nil
        else
            self.transitioning = true
        end
    end
    if type(self.targetOpacity) == 'number' then
        self.opacity = moveTowards(self.opacity, self.targetOpacity, speedOpacity)
        if self.opacity == self.targetOpacity then
            self.targetOpacity = nil
        else
            self.transitioning = true
        end
    end
end

function SceneObject:draw()
end

---@class Logo: SceneObject
Logo = setmetatable({
    texture1 = nil,
    texture2 = nil,
    scale = 1
}, SceneObject)
Logo.__index = Logo

function Logo:new()
    return setmetatable({}, Logo)
end

function Logo:draw()
    if self.rects == nil then
        self.rects = {
            gfx.getTextureRect(self.texture1, 0, 0, 128, 80),
            gfx.getTextureRect(self.texture1, 0, 80, 128, 128),
            gfx.getTextureRect(self.texture2, 0, 0, 128, 32),
            gfx.getTextureRect(self.texture2, 0, 32, 128, 128)
        }
    end

    local logoOriginalWidth = 288
    local logoOriginalHeight = 96
    local scaleX = self.scale
    local scaleY = self.scale
    local left = self.x - (logoOriginalWidth * scaleX / 2)
    local top = self.y - (logoOriginalHeight * scaleY / 2)
    gfx.drawTexture(self.rects[1], left, top, self.z, 128 * scaleX, 80 * scaleY)
    gfx.drawTexture(self.rects[2], left + (128 * scaleX), top, self.z, 128 * scaleX, 48 * scaleY)
    gfx.drawTexture(self.rects[3], left + (128 * scaleX), top + (48 * scaleX), self.z, 128 * scaleX, 32 * scaleY)
    gfx.drawTexture(self.rects[4], left + (256 * scaleX), top, self.z, 128 * scaleX, 96 * scaleY)
end

---@class TextBlock: SceneObject
TextBlock = setmetatable({
    font = nil,
    text = '',
    scale = 1
}, SceneObject)
TextBlock.__index = TextBlock

function TextBlock:new()
    return setmetatable({}, TextBlock)
end

function TextBlock:draw()
    local text = string.format(
        '<span halign="left" valign="center" color="rgba(255, 255, 255, %f)" scale="%f">%s</span>',
        self.opacity,
        self.scale,
        self.text)
    gfx.drawText(self.font, text, self.x, self.y, self.z, self.x, self.y)
end

---@class MenuItem: SceneObject
MenuItem = setmetatable({
    ticks = 0,
    scale = 1,
    font = nil,
    text = '',
    selected = false,
    selectReady = false,
    actioned = false,
    action = nil,
    ---@type MenuItem
    up = nil,
    ---@type MenuItem
    down = nil
}, SceneObject)
MenuItem.__index = MenuItem

function MenuItem:new()
    return setmetatable({}, MenuItem)
end

function MenuItem:update()
    SceneObject.update(self)
    self.ticks = self.ticks + 1

    if self.selected then
        if input.isDown(InputCommand.menuStart, InputCommand.menuApply) then
            if not self.actioned then
                self.actioned = true
                if self.action then
                    self.action()
                end
            end
        elseif input.isDown(InputCommand.up) then
            if not self.actioned then
                self.actioned = true
                if self.up ~= nil then
                    self.up.selectReady = true
                    self.selected = false
                    sfx.playSound(selectSound)
                end
            end
        elseif input.isDown(InputCommand.down) then
            if not self.actioned then
                self.actioned = true
                if self.down ~= nil then
                    self.down.selectReady = true
                    self.selected = false
                    sfx.playSound(selectSound)
                end
            end
        else
            self.actioned = false
        end
    elseif self.selectReady then
        self.selectReady = false
        self.selected = true
        self.actioned = true
    end
end

function MenuItem:draw()
    if self.opacity <= 0 then
        return
    end

    local highlightT = 0
    if self.selected then
        highlightT = (math.sin(self.ticks / 8) + 1) / 2
    end
    local highlightFactor = lerp(0.5, 1.0, highlightT)
    local opacity = highlightFactor * self.opacity
    local text = string.format(
        '<span halign="center" valign="center" color="rgba(255, 255, 255, %f)" scale="%f">%s</span>',
        opacity,
        self.scale,
        self.text)
    gfx.drawText(self.font, text, self.x, self.y, self.z, self.x, self.y)
end

Scene = {}
Scene.__index = Scene

function Scene:new()
    local instance = setmetatable({}, Scene)
    instance.items = {}
    return instance
end

function Scene:add(obj)
    table.insert(self.items, obj)
end

function Scene:update()
    for _, value in pairs(self.items) do
        value:update()
    end
end

function Scene:draw()
    for _, value in pairs(self.items) do
        value:draw()
    end
end

local scene = nil

function init()
    scheduler
        :wait(30)
        :exec(function()
            titleBgTexture = gfx.loadTexture("texture/titlebg", 320, 240)
            logo1Texture = gfx.loadTexture("texture/logo1", 128, 128)
            logo2Texture = gfx.loadTexture("texture/logo2", 128, 128)
            font1 = gfx.loadFont("font/font1")
            font2 = gfx.loadFont("font/font2")
            selectSound = sfx.loadSound("sound/select")
            applySound = sfx.loadSound("sound/apply")
            backSound = sfx.loadSound("sound/back")
            residentEvilSound = sfx.loadSound("sound/residentevil")
        end)

    if enableSplash then
        scheduleSplash()
    end
    if enableVideo then
        scheduleMovie()
        scheduleMenu(false)
    else
        scheduleMenu(true)
    end
end

function scheduleSplash()
    scheduler
        :exec(function()
            splashTexture = gfx.loadTexture("texture/splashbg", 320, 240)
            state = STATE_SPLASH
        end)
        :fadeIn(60)
        :wait(120)
        :fadeOut(60)
        :exec(function()
            splashTexture = nil
        end)
end

function scheduleMovie()
    scheduler
        :exec(function()
            introMovie = gfx.loadMovie("movie/intro")
            introMovie:play()
            state = STATE_MOVIE
        end)
        :run(function()
            if input.isPressed(InputCommand.menuStart, InputCommand.menuApply, InputCommand.menuCancel) then
                introMovie:stop()
            end
            return introMovie.state == MovieState.playing
        end)
        :exec(function()
            introMovie = nil
        end)
end

function scheduleMenu(fadeIn)
    scheduler:exec(function()
        state = STATE_MENU
    end)
    if fadeIn then
        scheduler:fadeIn(60)
    end
    scheduler:run(function()
        return menuState ~= 5
    end)
end

function initializeScene()
    scene = Scene:new()

    logoSceneObject = Logo:new()
    logoSceneObject.texture1 = logo1Texture
    logoSceneObject.texture2 = logo2Texture
    logoSceneObject.x = gfx.screenWidth / 2
    logoSceneObject.y = gfx.screenHeight / 2
    logoSceneObject.z = 0.5
    logoSceneObject.scale = 2
    scene:add(logoSceneObject)

    local versionInfo = TextBlock:new()
    versionInfo.font = font1
    versionInfo.scale = 0.5 * 2
    local margin = versionInfo.scale * 4
    versionInfo.x = margin
    versionInfo.y = gfx.screenHeight - margin - 4
    versionInfo.z = 0.5
    versionInfo.text = 'OpenRE, Resident Evil 2, Version 1.0'
    scene:add(versionInfo)

    startMenuItem = MenuItem:new()
    startMenuItem.x = gfx.screenWidth / 2
    startMenuItem.y = gfx.screenHeight * 3 / 4
    startMenuItem.text = 'PRESS START'
    startMenuItem.scale = 2
    startMenuItem.font = font2
    startMenuItem.selected = true
    startMenuItem.action = function()
        startMenuItem.selected = false
        startMenuItem:fade(0)
        logoSceneObject:move(logoSceneObject.x, 160)
        menuState = 1
        sfx.playSound(applySound)
    end
    scene:add(startMenuItem)

    local x = gfx.screenWidth / 2
    local y = gfx.screenHeight / 2 + 16
    local lastMenuItem = nil
    for _, value in pairs(menu) do
        local menuItem = MenuItem:new()
        menuItem.x = x
        menuItem.y = y
        menuItem.z = 0.5
        menuItem.text = value.text
        menuItem.scale = 2
        menuItem.font = font2
        menuItem.opacity = 0
        menuItem.action = value.action
        if lastMenuItem ~= nil then
            menuItem.up = lastMenuItem
            lastMenuItem.down = menuItem
        end
        scene:add(menuItem)
        table.insert(menuItems, menuItem)
        lastMenuItem = menuItem
        y = y + 32
    end
end

function update()
    if state ~= STATE_MENU then
        return
    end

    if scene == nil then
        initializeScene()
    end
    if menuState < 4 then
        scene:update()
    end

    if menuState == 1 then
        if not logoSceneObject.transitioning then
            menuState = 2
            for _, value in pairs(menuItems) do
                value:fade(1)
            end
            menuItems[1].selectReady = true
        end
    elseif menuState == 2 then
        if input.isPressed(InputCommand.menuCancel) then
            menuState = 3
            for _, value in pairs(menuItems) do
                value.selected = false
                value:fade(0)
            end
            sfx.playSound(backSound)
        end
    elseif menuState == 3 then
        if not menuItems[1].transitioning then
            menuState = 0
            startMenuItem:fade(1)
            startMenuItem.selectReady = true
            logoSceneObject:move(logoSceneObject.x, gfx.screenHeight / 2)
        end
    elseif menuState == 4 then
        flashTicks = flashTicks + 1
        if flashTicks == 10 then
            menuState = 5
            scheduler:fadeOut(60 * 3)
            scheduler:exec(function()
                state = STATE_NOTHING
            end)
            scheduler:run(function()
                return true
            end)
        end
    end
end

function draw()
    local renderSize = { width = gfx.screenWidth, height = gfx.screenHeight }
    local scale = renderSize.width / 320
    local width = renderSize.width
    local height = 240 * scale
    local x = 0
    local y = (renderSize.height / 2) - (height / 2)
    if state == STATE_SPLASH then
        gfx.drawTexture(splashTexture, x, y, 0, width, height)
    elseif state == STATE_MOVIE then
        gfx.drawTexture(introMovie, x, y, 0, width, height)
    elseif state == STATE_MENU then
        gfx.drawTexture(titleBgTexture, x, y, 0, width, height)
        if scene ~= nil then
            scene:draw()
        end
    end
    if menuState == 4 then
        local alpha = lerp(1, 0, flashTicks / 10)
        gfx.drawSolid({
            red = 1,
            green = 1,
            blue = 1,
            alpha = alpha
        }, 0, 0, 1, gfx.screenWidth, gfx.screenHeight)
    end
end

re.subscribe(HookKind.tick, function()
    if not initialized then
        initialized = true
        init()
    end
    scheduler:update()
    update()
    draw()
end)
