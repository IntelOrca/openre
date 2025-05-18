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
        self.x = util.moveTowards(self.x, self.targetX, speed)
        if self.x == self.targetX then
            self.targetX = nil
        else
            self.transitioning = true
        end
    end
    if type(self.targetY) == 'number' then
        self.y = util.moveTowards(self.y, self.targetY, speed)
        if self.y == self.targetY then
            self.targetY = nil
        else
            self.transitioning = true
        end
    end
    if type(self.targetOpacity) == 'number' then
        self.opacity = util.moveTowards(self.opacity, self.targetOpacity, speedOpacity)
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
    selectSound = nil,
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
                    sfx.playSound(self.selectSound)
                end
            end
        elseif input.isDown(InputCommand.down) then
            if not self.actioned then
                self.actioned = true
                if self.down ~= nil then
                    self.down.selectReady = true
                    self.selected = false
                    sfx.playSound(self.selectSound)
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
    local highlightFactor = util.lerp(0.5, 1.0, highlightT)
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
