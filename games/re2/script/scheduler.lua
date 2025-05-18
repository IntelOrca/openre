---@class Scheduler
Scheduler = {
    curr = nil,
    q = {}
}
Scheduler.__index = Scheduler

local SCHEDULER_KIND_WAIT = 0
local SCHEDULER_KIND_EXEC = 1
local SCHEDULER_KIND_RUN = 2

function Scheduler:new()
    local instance = setmetatable({}, Scheduler)
    re.subscribe(HookKind.tick, function()
        instance:update()
    end)
    return instance
end

function Scheduler:update()
    while true do
        local curr = self.curr
        if curr == nil then
            if #self.q == 0 then
                break
            end
            curr = table.remove(self.q, 1)
            self.curr = curr
        end
        if curr.kind == SCHEDULER_KIND_WAIT then
            if curr.time <= 0 then
                self.curr = nil
            else
                curr.time = curr.time - 1
                break
            end
        elseif curr.kind == SCHEDULER_KIND_EXEC then
            curr.cb()
            self.curr = nil
        elseif curr.kind == SCHEDULER_KIND_RUN then
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

function Scheduler:wait(time)
    table.insert(self.q, { kind = SCHEDULER_KIND_WAIT, time = time })
    return self
end

function Scheduler:exec(cb)
    table.insert(self.q, { kind = SCHEDULER_KIND_EXEC, cb = cb })
    return self
end

function Scheduler:run(cb)
    table.insert(self.q, { kind = SCHEDULER_KIND_RUN, cb = cb })
    return self
end

function Scheduler:fadeIn(time)
    local timer = time + 1
    self:run(function()
        timer = timer - 1
        local fade = timer / time
        gfx.fade(0, 0, 0, fade)
        return timer > 0
    end)
    return self
end

function Scheduler:fadeOut(time)
    local timer = time + 1
    self:run(function()
        timer = timer - 1
        local fade = 1 - (timer / time)
        gfx.fade(0, 0, 0, fade)
        return timer > 0
    end)
    return self
end
