-- config-demo - demo game for the shared Lua-ECS config list controls.
--
-- Run with: OPENRE_GAME=config-demo run.bat (plus OPENRE_SCRIPTS for web.lua)
--
-- The game creates a native-flex list container and several
-- ui.config_list_item rows (games/core/script/ui/config.lua) inside it, each
-- given a ui.config_enum sub entity; every other pixel - row label,
-- underline, changed diamond, arrow sprites, centred value, indicator bar
-- row, selection chrome - is produced by the two registered systems on top
-- of declarative core.ui_flex trees laid out by the engine.
--
-- The components are purely visual, so the demo drives them by hand to show
-- live updates: W/S or dpad up/down pick a row, A/D or left/right cycle that
-- row's selected index, Return retargets the BRIGHTNESS row onto a longer
-- option list to exercise bars being created and destroyed, and Escape
-- restores the picked row to its default so the changed diamond disappears.
--
-- Input: W/S pick row, A/D change value, Return swap BRIGHTNESS options,
-- Escape restore default.

local ecs = require('ecs')
local config = require('ui.config')

local DESIGN_W = 640
local DESIGN_H = 480

local stdFont = gfx.loadFont('fonts/std.fnt')
config.setFont(stdFont)

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
-- Snap the fitted scale down one SCALE_STEP so art never renders at odd
-- fractions (see re2/script/menu.lua).
scene.setField(frame, 'increment', 1)

local function fitCanvas()
    scene.setField(frame, 'target.width', gfx.screenWidth())
    scene.setField(frame, 'target.height', gfx.screenHeight())
    scene.setField(canvasHandle, 'x', gfx.screenWidth() * 0.5)
    scene.setField(canvasHandle, 'y', gfx.screenHeight() * 0.5)
end
fitCanvas()

-- Canvas-space helpers: (dx, dy) are design-space offsets from the centre.
local function addText(parent, dx, dy, bandW, bandH, markup, flags)
    local handle = ecs.spawn(parent)
    scene.setField(handle, 'name', 'text')
    scene.setField(handle, 'x', dx - DESIGN_W * 0.5)
    scene.setField(handle, 'y', dy - DESIGN_H * 0.5)
    local size = scene.addComponent(handle, 'core.ui_size')
    scene.setField(size, 'width', bandW)
    scene.setField(size, 'height', bandH)
    local t = scene.addComponent(handle, 'core.text')
    scene.setField(t, 'font', stdFont)
    scene.setField(t, 'text', markup)
    scene.setField(t, 'flags', flags or TextFlags.xCenter | TextFlags.yMiddle)
    return t -- component handle: callers rewrite 'text' on it
end

local function span(color, text)
    return '<span color="' .. color .. '">' .. text .. '</span>'
end

-------------------------------------------------------------------------------
-- Static chrome --------------------------------------------------------------
-------------------------------------------------------------------------------

addText(canvasHandle, 0, 14, DESIGN_W, 20,
    span('#ffffff', 'Lua ECS Config List Item Demo'))

local footer = addText(canvasHandle, 0, 448, DESIGN_W, 16, '')

-------------------------------------------------------------------------------
-- The config list rows (the only control-specific code in the game) ----------
-------------------------------------------------------------------------------

local ROWS = {
    { label = 'SOUND',     options = { 'MONO', 'STEREO', 'SURROUND' }, index = 2 },
    { label = 'BRIGHTNESS', options = { '1', '2', '3', '4', '5' } },
    { label = 'SUBTITLES', options = { 'OFF', 'ON' } },
    { label = 'CAMERA',    options = { 'CLASSIC', 'SHOULDER', 'FIRST PERSON', 'DRONE' } },
}
-- Every option list starts from its first entry; SOUND is already moved off
-- its default so a changed diamond is visible without touching anything.
for _, row in ipairs(ROWS) do
    row.defaultIndex = 1
    row.index = row.index or 1
end

-- One list container laid out by the native flex: a vertical run of rows
-- packed with no gap. The container is sized to its exact content height,
-- so group centring keeps the rows top-packed at LIST_TOP. Each item gets
-- an enum sub entity on its row whose width is exactly half the item width.
local LIST_TOP = 120
local ITEM_WIDTH = 360

local listHandle = scene.createEntity(canvasHandle)
scene.setField(listHandle, 'name', 'list')
do
    local x = 160 + ITEM_WIDTH * 0.5 - DESIGN_W * 0.5
    local listH = #ROWS * config.ITEM_HEIGHT
    scene.setField(listHandle, 'x', x)
    scene.setField(listHandle, 'y', LIST_TOP + listH * 0.5 - DESIGN_H * 0.5)
    local size = scene.addComponent(listHandle, 'core.ui_size')
    scene.setField(size, 'width', ITEM_WIDTH)
    scene.setField(size, 'height', listH)
    local flex = scene.addComponent(listHandle, 'core.ui_flex')
    scene.setField(flex, 'orientation', 'vertical')
    scene.setField(flex, 'gap', 0)
end

for i, row in ipairs(ROWS) do
    local _, id, rowHandle = config.addListItem(listHandle, 0, 0, ITEM_WIDTH)
    local _, enumId = config.addEnum(rowHandle, ITEM_WIDTH * 0.5)
    row.id = id
    row.enumId = enumId
end

local cursorRow = 1

local function pushRow(row)
    ecs.set(row.id, 'ui.config_list_item', 'text', row.label)
    ecs.set(row.id, 'ui.config_list_item', 'changed', row.index ~= row.defaultIndex)
    ecs.set(row.enumId, 'ui.config_enum', 'count', #row.options)
    ecs.set(row.enumId, 'ui.config_enum', 'selected_index', row.index)
    ecs.set(row.enumId, 'ui.config_enum', 'text', tostring(row.options[row.index]))
end

local function refreshSelection()
    for i, row in ipairs(ROWS) do
        ecs.set(row.id, 'ui.config_list_item', 'selected', i == cursorRow)
    end
end

local function refreshFooter()
    local row = ROWS[cursorRow]
    scene.setField(footer, 'text',
        span('#8a8a8a', 'W/S or dpad select row') .. span('#666666', '   -   ')
        .. span('#8a8a8a', 'A/D or left/right change value') .. span('#666666', '   -   ')
        .. span('#8a8a8a', 'Return swap BRIGHTNESS options') .. span('#666666', '   -   ')
        .. span('#8a8a8a', 'Escape restore default')
        .. '   ' .. span('#ffffff', '[' .. row.label .. ']'))
end

re.subscribe(HookKind.tick, function()
    fitCanvas()

    local function wrap(value, delta, count)
        return ((value - 1 + delta) % count) + 1
    end

    if input.pressed('up') then
        cursorRow = wrap(cursorRow, -1, #ROWS)
        refreshSelection()
        refreshFooter()
    elseif input.pressed('down') then
        cursorRow = wrap(cursorRow, 1, #ROWS)
        refreshSelection()
        refreshFooter()
    end

    if input.pressed('left') or input.pressed('right') then
        local row = ROWS[cursorRow]
        row.index = wrap(row.index, input.pressed('right') and 1 or -1, #row.options)
        pushRow(row)
    end

    -- Retarget one row onto a different-length option list mid-run so the
    -- bar row's create/destroy path is exercised while watching.
    if input.pressed('accept') then
        local row = ROWS[2]
        row.options = #row.options == 5
            and { '10', '20', '30', '40', '50', '60', '70', '80', '90', '100' }
            or { '1', '2', '3', '4', '5' }
        row.index = math.min(row.index, #row.options)
        pushRow(row)
    end

    -- Restore the picked row to its default so the changed diamond goes.
    if input.pressed('cancel') then
        local row = ROWS[cursorRow]
        row.index = row.defaultIndex
        pushRow(row)
    end
end)

for _, row in ipairs(ROWS) do
    pushRow(row)
end
refreshSelection()
refreshFooter()

print('[config-demo] ready: ' .. #ROWS .. ' config_list_item rows')
