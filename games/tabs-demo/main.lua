-- tabs-demo - demo game for the shared Lua-ECS tab control.
--
-- Run with: OPENRE_GAME=tabs-demo run.bat (plus OPENRE_SCRIPTS for web.lua)
--
-- The game itself does almost nothing: it creates two ui.tab_strips - a
-- level 1 outer row (separators, tab_left/tab_right) and a level 2 sub-row
-- (no separators, subtab_left/subtab_right) - plus a few text blocks.
-- Everything else - the LB/RB input hints, separators, label texts,
-- selection highlight, layout, scrolling and the input handling - is
-- produced by the ui.tabs system (games/core/script/ui/tabs.lua) running on
-- the shared Lua ECS (games/core/script/ecs.lua).
--
-- Each tick reads both strips' selected_index out of their binary component
-- rows and rewrites the heading/body/footer text blocks, showing content
-- switching driven purely by component state.
--
-- Input: Q/E or pad LB/RB switch the outer tab; 1/3 or LT/RT the subtab.

local ecs = require('ecs')
local tabs = require('ui.tabs')

local DESIGN_W = 640
local DESIGN_H = 480

local stdFont = gfx.loadFont('fonts/std.fnt')
tabs.setFont(stdFont)

local root = scene.getRoot('user')

-------------------------------------------------------------------------------
-- Canvas: authored in 640x480, centred and fit-scaled to the window.
-------------------------------------------------------------------------------

local canvasHandle = scene.createEntity(root)
scene.setField(canvasHandle, 'name', 'canvas')
local frame = scene.addComponent(canvasHandle, 'core.scaled_frame')
scene.setField(frame, 'source.width', DESIGN_W)
scene.setField(frame, 'source.height', DESIGN_H)
scene.setField(frame, 'mode', 'contain')

local function fitCanvas()
    scene.setField(frame, 'target.width', gfx.screenWidth())
    scene.setField(frame, 'target.height', gfx.screenHeight())
    scene.setField(canvasHandle, 'x', gfx.screenWidth() * 0.5)
    scene.setField(canvasHandle, 'y', gfx.screenHeight() * 0.5)
end
fitCanvas()

-- Canvas-space helpers: (dx, dy) are design-space offsets from the centre.
local function spawnAt(parent, dx, dy, name)
    local handle, id = ecs.spawn(parent)
    scene.setField(handle, 'name', name)
    scene.setField(handle, 'x', dx - DESIGN_W * 0.5)
    scene.setField(handle, 'y', dy - DESIGN_H * 0.5)
    return handle
end

local function addText(parent, dx, dy, bandW, bandH, markup, flags)
    local handle = spawnAt(parent, dx + bandW * 0.5, dy + bandH * 0.5, 'text')
    local t = scene.addComponent(handle, 'core.text')
    scene.setField(t, 'font', stdFont)
    scene.setField(t, 'text', markup)
    scene.setField(t, 'size.width', bandW)
    scene.setField(t, 'size.height', bandH)
    scene.setField(t, 'flags', flags or TextFlags.xCenter | TextFlags.yMiddle)
    return t
end

-------------------------------------------------------------------------------
-- Static chrome ---------------------------------------------------------------
-------------------------------------------------------------------------------

addText(canvasHandle, 0, 14, DESIGN_W, 20,
    '<span color="#ffffff">Lua ECS Tab Control Demo</span>')

addText(canvasHandle, 0, 448, DESIGN_W, 16,
    '<span color="#8a8a8a">Q/E or LB/RB change tab</span>  '
    .. '<span color="#666666">-   </span>'
    .. '<span color="#8a8a8a">1/3 or LT/RT change subtab</span>')

-------------------------------------------------------------------------------
-- The tab strips (the only control-specific code in the game) ----------------
-------------------------------------------------------------------------------

local stripHandle, stripId = tabs.addStrip(canvasHandle, 0, 64 - DESIGN_H * 0.5, 1)
local subHandle, subStripId = tabs.addStrip(canvasHandle, 0, 102 - DESIGN_H * 0.5, 2)

local TABS = {
    { label = 'STATUS' },
    { label = 'ITEMS' },
    { label = 'MAP' },
    { label = 'FILES' },
    { label = 'OPTIONS' },
    { label = 'CONTROLS' },
    { label = 'SAVE' },
    { label = 'LOAD' },
}
for _, tab in ipairs(TABS) do
    tabs.addTab(stripId, tab.label)
end

local SUBTABS = {
    { label = 'CONTROLS' },
    { label = 'CAMERA' },
    { label = 'HUD' },
    { label = 'AUDIO' },
}
for _, tab in ipairs(SUBTABS) do
    tabs.addTab(subStripId, tab.label)
end

-------------------------------------------------------------------------------
-- Content blocks rewritten from selected_index ------------------------------
-------------------------------------------------------------------------------

local heading = addText(canvasHandle, 60, 130, 520, 24, '',
    TextFlags.xLeft | TextFlags.yMiddle)
local body1 = addText(canvasHandle, 60, 180, 520, 18, '',
    TextFlags.xLeft | TextFlags.yMiddle)
local body2 = addText(canvasHandle, 60, 210, 520, 18, '',
    TextFlags.xLeft | TextFlags.yMiddle)
local body3 = addText(canvasHandle, 60, 240, 520, 18, '',
    TextFlags.xLeft | TextFlags.yMiddle)

local function span(color, text)
    return '<span color="' .. color .. '">' .. text .. '</span>'
end

for i, tab in ipairs(TABS) do
    tab.heading = span('#ffffff', tab.label)
        .. ' ' .. span('#8a8a8a', '- outer tab ' .. i .. ' of ' .. #TABS)
end
for i, tab in ipairs(SUBTABS) do
    tab.subLine = span('#c8c8c8', 'Subtab: ' .. string.lower(tab.label))
        .. ' ' .. span('#9a9a9a', '(' .. i .. ' of ' .. #SUBTABS
            .. ', level 2 - no separators, subtab_left/subtab_right)')
end

local lastShown = nil
re.subscribe(HookKind.tick, function()
    fitCanvas()

    local oi = tabs.selectedIndex(stripId)
    local si = tabs.selectedIndex(subStripId)
    local key = oi * 16 + si
    if key ~= lastShown then
        local tab = TABS[oi]
        local sub = SUBTABS[si]
        if tab ~= nil and sub ~= nil then
            scene.setField(heading, 'text', tab.heading)
            scene.setField(body1, 'text', sub.subLine)
            scene.setField(body2, 'text', span('#9a9a9a',
                'Content for the ' .. string.lower(tab.label) .. '/'
                .. string.lower(sub.label) .. ' panel.'))
            scene.setField(body3, 'text', span('#9a9a9a',
                'Both strips are built and driven by Lua components and systems.'))
            lastShown = key
        end
    end
end)

print('[tabs-demo] ready: strip id '
    .. tostring(stripId) .. ', sub strip id ' .. tostring(subStripId))
