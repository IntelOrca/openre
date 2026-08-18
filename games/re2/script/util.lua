-- util.lua — small helpers for game scripts.

local util = {}

--- Full-window crop rect, for layering content over the whole screen.
function util.getCropRect()
    return { x = 0, y = 0, width = gfx.screenWidth(), height = gfx.screenHeight() }
end

return util