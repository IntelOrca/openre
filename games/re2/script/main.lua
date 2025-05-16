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

local STATE_START = 0
local STATE_SPLASH = 1
local STATE_MOVIE = 2
local STATE_MENU = 3

local initialized = false
local splashTexture
local introMovie
local titleBgTexture
local logo1Texture
local logo2Texture
local logo1TextureA
local logo1TextureB
local logo1TextureC
local logo1TextureD
local font1
local font2
local state = STATE_START

local enableSplash = false
local enableVideo = false

function init()
    splashTexture = gfx.loadTexture("texture/splashbg", 320, 240);
    titleBgTexture = gfx.loadTexture("texture/titlebg", 320, 240);
    logo1Texture = gfx.loadTexture("texture/logo1", 128, 128);
    logo2Texture = gfx.loadTexture("texture/logo2", 128, 128);
    logo1TextureA = gfx.getTextureRect(logo1Texture, 0, 0, 128, 80)
    logo1TextureB = gfx.getTextureRect(logo1Texture, 0, 80, 128, 128)
    logo1TextureC = gfx.getTextureRect(logo2Texture, 0, 0, 128, 32)
    logo1TextureD = gfx.getTextureRect(logo2Texture, 0, 32, 128, 128)
    font1 = gfx.loadFont("font/font1")
    font2 = gfx.loadFont("font/font2")

    if enableSplash then
        scheduler
            :wait(60)
            :exec(function()
                state = STATE_SPLASH
            end)
            :fadeIn(60)
            :wait(120)
            :fadeOut(60)
    end
    if enableVideo then
        scheduler
            :exec(function()
                state = STATE_MOVIE
                introMovie = gfx.loadMovie("movie/intro")
                introMovie:play()
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
    scheduler
        :exec(function()
            state = STATE_MENU
        end)
        :fadeIn(60)
        :run(function()
            return true
        end)
end

function update()

end

function draw()
    local renderSize = { width = gfx.screenWidth, height = gfx.screenHeight };
    local scale = renderSize.width / 320
    local width = renderSize.width
    local height = 240 * scale
    local x = 0
    local y = (renderSize.height / 2) - (height / 2)
    if state == STATE_SPLASH then
        gfx.drawTexture(splashTexture, x, y, 0, width, height);
    elseif state == STATE_MOVIE then
        gfx.drawTexture(introMovie, x, y, 0, width, height)
    elseif state == STATE_MENU then
        gfx.drawTexture(titleBgTexture, x, y, 0, width, height);
        drawLogo(scale)
    end

    gfx.drawText(font1, "OpenRE, Resident Evil 2, Version 1.0", 4 * scale, renderSize.height - (8 * scale), 0,
        renderSize.width,
        renderSize.height)
    gfx.drawText(font2,
        '<span scale="2"><span color="rgba(255, 255, 255, 0.5)">ORIGINAL MODE</span>\nLEON CLAIRE\n<span color="rgb(255, 0, 0)">EXIT</span></span>',
        32,
        164, 0,
        renderSize.width, renderSize.height)
end

function drawLogo(scale)
    local left = (gfx.screenWidth / 2) - ((288 * scale) / 2)
    local top = (gfx.screenHeight / 5) - ((96 * scale) / 2)

    gfx.drawTexture(logo1TextureA, left, top, 0, 128 * scale, 80 * scale);
    gfx.drawTexture(logo1TextureB, left + (128 * scale), top, 0, 128 * scale, 48 * scale);
    gfx.drawTexture(logo1TextureC, left + (128 * scale), top + (48 * scale), 0, 128 * scale, 32 * scale);
    gfx.drawTexture(logo1TextureD, left + (256 * scale), top, 0, 128 * scale, 96 * scale);
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
