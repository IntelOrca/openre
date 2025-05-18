local staticTime = 0.25             -- Duration of static pale red
local pulseTime = 0.25 + staticTime -- Total duration of the pulse (including static time)
local minPulseRed = 0.1             -- Minimum red value for the heartbeat
local timer = 0                     -- Timer to track the elapsed time
local isPulsing = false             -- State to track if we are in the pulsing phase

-- Linear interpolation function
function lerp(start, stop, t)
    return start + (stop - start) * t
end

function update()
    if state ~= STATE_MENU then
        return
    end

    timer = timer + (1 / 60) -- Increment timer by 1/60 seconds (assuming 60 FPS)

    if isPulsing then
        -- Calculate the factor for lerp based on the timer
        local factor = (math.sin((timer / (pulseTime - staticTime)) * math.pi) + 1) / 2 -- Normalized to [0, 1]

        -- Lerp for brightness and rumble intensity
        local brightness = lerp(minPulseRed, 1.0, factor) -- Lerp between minPulseRed and 1.0
        local rumbleIntensity = lerp(0, 1, factor)        -- Lerp between 0 and 0.5 for rumble

        -- Set LED color and rumble intensity
        input.led = { red = brightness, green = 0, blue = 0 } -- From pale red to bright red
        input.rumble = { low = rumbleIntensity, high = 0 }    -- Rumble intensity

        -- Check if the pulse duration has completed
        if timer >= (pulseTime - staticTime) then
            isPulsing = false
            timer = 0 -- Reset timer for static phase
        end
    else
        -- Static pale red phase
        input.led = { red = minPulseRed, green = 0, blue = 0 } -- Static pale red
        input.rumble = { low = 0, high = 0 }                   -- No rumble during static phase

        -- Check if the static time has completed
        if timer >= staticTime then
            isPulsing = true
            timer = 0 -- Reset timer for pulsing phase
        end
    end
end
