-- scheduler.lua — cooperative per-frame task queue for game scripts.
-- Each Scheduler instance owns its queue and is driven from the registered
-- `app.Scheduler` managed system (no per-instance tick hooks), so
-- :wait/:exec/:run chains run in order across frames. Finished schedulers are
-- dropped from the live list so they stop running and can be collected. The
-- sandboxed VM removes setmetatable, so instances carry their methods directly
-- instead of sharing them through a class metatable.

---@class Scheduler
Scheduler = {}

local ecs = require('ecs')

local SCHEDULER_KIND_WAIT = 0
local SCHEDULER_KIND_EXEC = 1
local SCHEDULER_KIND_RUN = 2

local SCHEDULER_METHODS = {}

-- Live scheduler instances driven by the managed system below.
local live = {}
local function schedule(instance)
    live[#live + 1] = instance
end

local function update(self)
    while true do
        local curr = self.curr
        if curr == nil then
            if #self.q == 0 then break end
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

local function wait(self, time)
    table.insert(self.q, { kind = SCHEDULER_KIND_WAIT, time = time })
    return self
end

local function exec(self, cb)
    table.insert(self.q, { kind = SCHEDULER_KIND_EXEC, cb = cb })
    return self
end

local function run(self, cb)
    table.insert(self.q, { kind = SCHEDULER_KIND_RUN, cb = cb })
    return self
end

local function fadeIn(self, time)
    local timer = time + 1
    self:run(function()
        timer = timer - 1
        local fade = timer / time
        gfx.fade(0, 0, 0, math.floor(fade * 255))
        return timer > 0
    end)
    return self
end

local function fadeOut(self, time)
    local timer = time + 1
    self:run(function()
        timer = timer - 1
        local fade = 1 - (timer / time)
        gfx.fade(0, 0, 0, math.floor(fade * 255))
        return timer > 0
    end)
    return self
end

-- Holds a solid black screen for `time` ticks after a fade out. gfx.fade only
-- lasts a single frame, so the overlay must be re-issued every tick while
-- waiting or the faded-out scene pops back visible.
local function holdBlack(self, time)
    local timer = time
    self:run(function()
        gfx.fade(0, 0, 0, 255)
        timer = timer - 1
        return timer > 0
    end)
    return self
end

SCHEDULER_METHODS = {
    update = update,
    wait = wait,
    exec = exec,
    run = run,
    fadeIn = fadeIn,
    fadeOut = fadeOut,
    holdBlack = holdBlack,
}

function Scheduler:new()
    local instance = { curr = nil, q = {} }
    for name, fn in pairs(SCHEDULER_METHODS) do
        instance[name] = fn
    end
    schedule(instance)
    return instance
end

-- app.Scheduler: drives every live scheduler instance once per frame and
-- drops the ones whose queue has fully drained (no work left). A freshly
-- created instance keeps its first pass so work that is appended after
-- construction (rather than chained in the constructor) is still driven.
ecs.registerSystem('app.Scheduler', {
    after = { 'core.LegacyGame' },
    before = { 'core.ScaledFrame' },
}, function()
    local i = 1
    while i <= #live do
        local self = live[i]
        if self == nil then
            table.remove(live, i)
        elseif self.started and #self.q == 0 and self.curr == nil then
            -- Finished (a previous update drained the queue): remove it so
            -- the instance can be collected (its captured upvalues otherwise
            -- keep it alive).
            table.remove(live, i)
        else
            self.started = true
            self:update()
            i = i + 1
        end
    end
end)

return Scheduler
