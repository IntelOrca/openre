require('script/menu')
require('script/scheduler')
require('script/util')

local game = {
    scheduler = Scheduler:new(),
    enableSplash = true,
    enableVideo = true
}

function game:launch()
    re.subscribe(HookKind.tick, function()
        self:update()
    end)

    self.scheduler:wait(30)
    if self.enableSplash then
        self:scheduleSplash()
    end
    if self.enableVideo then
        self:scheduleMovie()
        self:scheduleMenu(false)
    else
        self:scheduleMenu(true)
    end
end

function game:update()
    local routine = self.routine
    if routine then
        routine()
    end
end

function game:scheduleSplash()
    local splashTexture
    self.scheduler
        :exec(function()
            splashTexture = gfx.loadTexture("texture/splashbg", 320, 240)
            self.routine = function()
                local rect = util.getCropRect()
                gfx.drawTexture(splashTexture, rect.x, rect.y, 0, rect.width, rect.height)
            end
        end)
        :fadeIn(60)
        :wait(120)
        :fadeOut(60)
        :exec(function()
            splashTexture = nil
        end)
end

function game:scheduleMovie()
    local introMovie
    self.scheduler
        :exec(function()
            introMovie = gfx.loadMovie("movie/intro")
            introMovie:play()
            self.routine = function()
                local rect = util.getCropRect()
                gfx.drawTexture(introMovie, rect.x, rect.y, 0, rect.width, rect.height)
            end
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

function game:scheduleMenu(fadeIn)
    scheduleMenu(self, fadeIn)
end

game:launch()
