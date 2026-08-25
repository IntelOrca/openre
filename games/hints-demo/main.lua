-- Input hint component showcase: automatic command bindings.
--
-- Run with: OPENRE_GAME=hints-demo run.bat (plus OPENRE_SCRIPTS for web.lua)
-- Every registered input command gets one input_hint per style (kbm / ps /
-- x / n) whose `command` field is just the command name. The engine resolves
-- each command's live [input] bindings and renders one icon per binding in
-- that style - rebinding a key or button updates every hint automatically.

local stdFont
local smFont

local function span(color, text)
    return '<span color="' .. color .. '">' .. text .. '</span>'
end

local function addText(parent, x, y, bandW, text, small, flags)
    local e = scene.createEntity(parent)
    scene.setField(e, 'name', 'text')
    scene.setField(e, 'x', x)
    scene.setField(e, 'y', y)
    local t = scene.addComponent(e, 'core.text')
    scene.setField(t, 'font', small and smFont or stdFont)
    scene.setField(t, 'text', text)
    scene.setField(t, 'size.width', bandW)
    scene.setField(t, 'size.height', small and 14 or 20)
    scene.setField(t, 'flags', flags or (TextFlags.xLeft | TextFlags.yMiddle))
    return e
end

local styleDefs = {
    { device = 'kbm', label = 'Keyboard + Mouse', color = '#e8e8e8' },
    { device = 'ps', label = 'PlayStation', color = '#8ab4ff' },
    { device = 'x', label = 'Xbox', color = '#9adf9a' },
    { device = 'n', label = 'Nintendo', color = '#ff9a9a' },
}

local styleSlot = 150

-- One hint entity per style; the engine expands it to the command's
-- bindings on that device family.
local function addCommandRow(parent, x0, y, cmdName)
    addText(parent, x0, y, 96, span('#b0b0b0', cmdName), true)
    for i, s in ipairs(styleDefs) do
        local e = scene.createEntity(parent)
        scene.setField(e, 'name', 'hint.' .. cmdName .. '.' .. s.device)
        scene.setField(e, 'x', x0 + 104 + (i - 1) * styleSlot + styleSlot * 0.5)
        scene.setField(e, 'y', y)
        local h = scene.addComponent(e, 'core.input_hint')
        scene.setField(h, 'command', cmdName)
        scene.setField(h, 'device', s.device)
    end
end

local root = scene.getRoot('user')

stdFont = gfx.loadFont('fonts/std.fnt')
smFont = gfx.loadFont('fonts/sm.fnt')

local function build()
    local sw = gfx.screenWidth()
    local sh = gfx.screenHeight()

    addText(
        root, sw * 0.5, 28, sw,
        span('#ffffff', 'Input Hint Component') .. '  '
            .. span('#8a8a8a', 'one hint per command - bindings resolved automatically'),
        false, TextFlags.xCenter | TextFlags.yMiddle)

    local slot = styleSlot
    for i, hd in ipairs(styleDefs) do
        addText(root, 12 + 104 + (i - 1) * slot, 52, slot - 6, span(hd.color, hd.label), true)
    end

    local commands = input.commands()
    local top = 78
    local rowH = math.floor(math.min(34, (sh - top - 26) / #commands))
    for i, cmdName in ipairs(commands) do
        addCommandRow(root, 12, top + (i - 1) * rowH, cmdName)
    end

    addText(
        root, 12, sh - 16, sw,
        span('#666666', 'icons come straight from the [input] INI bindings per command'),
        true)
end

build()
