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
local util = require('script/util')

local SCHEDULER_KIND_WAIT = 0
local SCHEDULER_KIND_EXEC = 1
local SCHEDULER_KIND_RUN = 2

local SCHEDULER_METHODS = {}

-- Live scheduler instances driven by the managed system below.
local live = {}
local function schedule(instance)
    live[#live + 1] = instance
end

-- Full-window fade overlay (replaces the removed gfx.fade). Recreated as the
-- newest child of the user root every time it is touched, so it always draws
-- above any screen entity — including a new screen created mid-fade by an
-- :exec step (which would otherwise paint over a persisting overlay).
local overlayEntity = nil
local overlaySprite = nil

local function setOverlayColor(r, g, b, a)
    if overlayEntity then
        scene.destroy(overlayEntity)
        overlayEntity = nil
    end
    if a <= 0 then
        overlaySprite = nil
        return
    end
    overlayEntity, overlaySprite = util.spawnOverlay(nil, { r, g, b, math.max(0, math.min(255, a)) })
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
        setOverlayColor(0, 0, 0, math.floor(fade * 255))
        return timer > 0
    end)
    return self
end

local function fadeOut(self, time)
    local timer = time + 1
    self:run(function()
        timer = timer - 1
        local fade = 1 - (timer / time)
        setOverlayColor(0, 0, 0, math.floor(fade * 255))
        return timer > 0
    end)
    return self
end

-- Holds a solid black screen for `time` ticks after a fade out. The overlay
-- is an entity (not a one-shot gfx.fade), so it persists for the whole wait —
-- but it is recreated each tick to stay the newest user-root child, in case a
-- new screen landed on top after the fade-out step.
local function holdBlack(self, time)
    local timer = time
    self:run(function()
        setOverlayColor(0, 0, 0, 255)
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
