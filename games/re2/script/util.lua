util = {}

function util.dump(value)
    if type(value) == "table" then
        for key, val in pairs(value) do
            if type(val) == "table" then
                print(key .. ":")
                util.dump(val) -- Recursively dump nested tables
            else
                print(key .. ": " .. tostring(val))
            end
        end
    else
        print(tostring(value)) -- Print as string if not a table
    end
end

function util.lerp(start, stop, t)
    return start + (stop - start) * t
end

function util.moveTowards(a, b, s)
    local delta = b - a
    if delta > 0 then
        return math.min(a + s, b)
    elseif delta < 0 then
        return math.max(a - s, b)
    else
        return b
    end
end

-- Gets the 4:3 rect for the game viewport.
-- This applies a zoom for classic to widescreen format,
-- or just 0,0,w,h for classic format.
function util.getCropRect()
    local renderSize = { width = gfx.screenWidth, height = gfx.screenHeight }
    local scale = renderSize.width / 320
    local width = renderSize.width
    local height = 240 * scale
    local x = 0
    local y = (renderSize.height / 2) - (height / 2)
    return {
        x = x,
        y = y,
        width = width,
        height = height
    }
end

return util
