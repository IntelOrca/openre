-- scheduler.lua — cooperative per-frame task queue for game scripts.
-- Each Scheduler instance owns its queue and is driven from the tick hook,
-- so :wait/:exec/:run chains run in order across frames. The sandboxed VM
-- removes setmetatable, so instances carry their methods directly instead
-- of sharing them through a class metatable.

---@class Scheduler
Scheduler = {}

local SCHEDULER_KIND_WAIT = 0
local SCHEDULER_KIND_EXEC = 1
local SCHEDULER_KIND_RUN = 2

local SCHEDULER_METHODS = {}

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
    re.subscribe(HookKind.tick, function()
        instance:update()
    end)
    return instance
end

return Scheduler