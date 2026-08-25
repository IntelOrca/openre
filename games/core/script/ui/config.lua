-- ui/config.lua - configuration list item controls built on the Lua ECS
-- (see games/core/script/ecs.lua).
--
-- The first control is the enum item (ui.config_enum): the '<' value row
-- used by settings lists. A game creates one entity and drives its fields:
--
--     local handle, id = config.addEnum(root, x, y, 200)  -- ui.config_enum
--     ecs.set(id, 'ui.config_enum', 'text', 'STEREO')
--     ecs.set(id, 'ui.config_enum', 'selected_index', 2)
--     ecs.set(id, 'ui.config_enum', 'count', 3)
--
-- Everything visual is produced by the registered 'ui.config' system: a
-- left/right arrow sprite pair hugging the width boundary box (from the
-- shared texture/ui_arrow atlas), the `text` field centred between them,
-- and an indicator bar row underneath - one equal-size bar per option,
-- white for selected_index, gray for the rest, so the player can see at a
-- glance how many options exist and which one is active.
--
-- The component is purely visual: no input handling lives here. Games move
-- the selection themselves by rewriting selected_index/text; the system
-- only mirrors those fields into child entities for drawing.

local ecs = require('ecs')

local config = {}

-------------------------------------------------------------------------------
-- Layout / style constants ---------------------------------------------------
-------------------------------------------------------------------------------

local TEXT_ROW_H = 14    -- height of the centred label band
local BAR_W = 16         -- indicator bar width
local BAR_H = 4          -- indicator bar thickness
local BAR_GAP = 3        -- horizontal gap between bars
local BAR_TOP_GAP = 4    -- label band bottom -> bar top
local ARROW_W = 5        -- ui_arrow.bmp natural cell size
local ARROW_H = 9
local ARROW_PAD = 3      -- arrow centre inset from the boundary box edge

local COLOR_TEXT = '#ffffff'
local COLOR_BAR_LIT = { 255, 255, 255, 255 }
local COLOR_BAR_OFF = { 120, 120, 120, 255 }
local COLOR_ARROW = { 230, 230, 230, 255 }

-- Decorative rects sit behind text glyph sprites (which render at z = 0).
local Z_ARROW = 50
local Z_BAR = 50

-------------------------------------------------------------------------------
-- Component ------------------------------------------------------------------
-------------------------------------------------------------------------------

ecs.registerComponent('ui.config_enum', {
    { name = 'width', type = 'int', default = 120 },
    { name = 'text', type = 'string', default = '' },
    -- Both 1-based; selected_index is clamped into [1, count] for drawing
    -- only (the system never writes fields back).
    { name = 'selected_index', type = 'int', default = 1 },
    { name = 'count', type = 'int', default = 0 },
})

-------------------------------------------------------------------------------
-- Creation helper (what a game calls) ----------------------------------------
-------------------------------------------------------------------------------

-- The one font used by every control; set before creating any.
local uiFont = nil

---@param font Font A gfx.loadFont handle.
function config.setFont(font)
    uiFont = font
end

--- Create an enum list item under `parent` with its centre at the given
--- canvas-space offset. Returns the native handle and numeric id.
---@param parent SceneEntity
---@param x number Canvas-space centre X.
---@param y number Canvas-space centre Y.
---@param width? integer Boundary box width in design pixels.
---@return SceneEntity handle
---@return integer id
function config.addEnum(parent, x, y, width)
    local handle, id = ecs.spawn(parent)
    assert(id ~= nil, 'ui.config.addEnum: spawn failed')
    scene.setField(handle, 'name', 'ui.config_enum')
    scene.setField(handle, 'x', x)
    scene.setField(handle, 'y', y)
    ecs.addComponent(id, 'ui.config_enum')
    if width ~= nil then
        ecs.set(id, 'ui.config_enum', 'width', width)
    end
    return handle, id
end

-------------------------------------------------------------------------------
-- System ---------------------------------------------------------------------
-------------------------------------------------------------------------------

-- Left/right arrow cells of the shared atlas (games/core/texture/
-- ui_arrow.bmp), loaded once per VM. The module keeps the handle referenced
-- because sprites borrowing a texture do not own it.
local arrowTexture = nil

local function arrowAtlas()
    if arrowTexture == nil then
        arrowTexture = gfx.loadTexture('texture/ui_arrow')
        assert(arrowTexture ~= nil, "ui.config: missing core asset 'texture/ui_arrow'")
    end
    return arrowTexture
end

-- Per-frame view state, kept out of the binary rows: these describe derived
-- entities (created by this system) rather than authored data.
--   enumViews[id] = { arrowL, arrowR, textId, bars[], plus cached inputs }
local enumViews = {}

-- Returns the live entity id for a tracked child, recreating nothing; nil
-- when never created or destroyed since.
local function liveChild(state, key)
    local id = state[key]
    if id ~= nil and scene.entityFromId(id) == nil then
        state[key] = nil
        return nil
    end
    return id
end

-- Adds a solid-colour quad child of `parent` centred on its own origin.
local function addQuadChild(parentHandle, name, w, h, color, z)
    local handle, id = ecs.spawn(parentHandle)
    scene.setField(handle, 'name', name)
    local sprite = scene.addComponent(handle, 'core.sprite')
    scene.setField(sprite, 'color', color)
    scene.setField(sprite, 'size.width', w)
    scene.setField(sprite, 'size.height', h)
    scene.setField(sprite, 'z', z)
    return id
end

-- Creates or refreshes one flanking arrow sprite. `dir` is -1 / +1 and
-- `cx` is the arrow centre relative to the control origin.
local function ensureArrow(view, parentHandle, dir, cx)
    local key = dir < 0 and 'arrowL' or 'arrowR'
    local id = liveChild(view, key)
    if id == nil then
        local handle
        handle, id = ecs.spawn(parentHandle)
        scene.setField(handle, 'name', 'ui.config_enum.' .. key)
        local sprite = scene.addComponent(handle, 'core.sprite')
        scene.setField(sprite, 'texture', arrowAtlas())
        scene.setField(sprite, 'src.x', dir < 0 and 0 or ARROW_W)
        scene.setField(sprite, 'src.y', 0)
        scene.setField(sprite, 'src.width', ARROW_W)
        scene.setField(sprite, 'src.height', ARROW_H)
        scene.setField(sprite, 'size.width', ARROW_W)
        scene.setField(sprite, 'size.height', ARROW_H)
        scene.setField(sprite, 'color', COLOR_ARROW)
        scene.setField(sprite, 'z', Z_ARROW)
        view[key] = id
    end
    local handle = scene.entityFromId(id)
    scene.setField(handle, 'x', cx)
    scene.setField(handle, 'y', 0)
end

-- Creates or refreshes the centred label text child.
local function ensureText(view, parentHandle, text, boxW)
    local markup = '<span color="' .. COLOR_TEXT .. '">' .. text .. '</span>'
    if liveChild(view, 'textId') == nil then
        local handle
        handle, view.textId = ecs.spawn(parentHandle)
        scene.setField(handle, 'name', 'ui.config_enum.label')
        local t = scene.addComponent(handle, 'core.text')
        scene.setField(t, 'font', uiFont)
        scene.setField(t, 'flags', TextFlags.xCenter | TextFlags.yMiddle)
        view.markup = nil
    end

    if view.markup ~= markup or view.boxW ~= boxW then
        local textHandle = scene.component(scene.entityFromId(view.textId), 'core.text')
        scene.setField(textHandle, 'text', markup)
        scene.setField(textHandle, 'size.width', boxW)
        scene.setField(textHandle, 'size.height', TEXT_ROW_H)
        view.markup = markup
    end
    view.boxW = boxW
end

-- Syncs the indicator bar row to count/selectedIndex, reusing bar entities
-- in place and destroying extras when the option count shrinks.
local function ensureBars(view, parentHandle, count, selectedIndex, cy)
    while #view.bars > count do
        local id = table.remove(view.bars)
        if scene.entityFromId(id) ~= nil then
            ecs.destroyEntity(id)
        end
    end

    local totalW = count * BAR_W + math.max(0, count - 1) * BAR_GAP
    for i = 1, count do
        local lit = i == selectedIndex
        local id = view.bars[i]
        if id == nil or scene.entityFromId(id) == nil then
            id = addQuadChild(parentHandle, 'ui.config_enum.bar' .. i,
                BAR_W, BAR_H, lit and COLOR_BAR_LIT or COLOR_BAR_OFF, Z_BAR)
            view.bars[i] = id
        end
        local handle = scene.entityFromId(id)
        scene.setField(handle, 'x', -totalW * 0.5 + (i - 1) * (BAR_W + BAR_GAP) + BAR_W * 0.5)
        scene.setField(handle, 'y', cy)
        local sprite = scene.component(handle, 'core.sprite')
        scene.setField(sprite, 'color', lit and COLOR_BAR_LIT or COLOR_BAR_OFF)
    end
end

ecs.registerSystem('ui.config', 1000, function()
    -- Drop derived-view bookkeeping for entities that no longer exist.
    for id in pairs(enumViews) do
        if scene.entityFromId(id) == nil then
            enumViews[id] = nil
        end
    end

    ecs.each('ui.config_enum', function(enumId)
        local enumHandle = scene.entityFromId(enumId)
        if enumHandle == nil then
            return
        end
        local view = enumViews[enumId]
        if view == nil then
            view = { bars = {} }
            enumViews[enumId] = view
        end

        local width = math.max(1, ecs.get(enumId, 'ui.config_enum', 'width'))
        local text = ecs.get(enumId, 'ui.config_enum', 'text')
        local count = math.max(0, ecs.get(enumId, 'ui.config_enum', 'count'))
        local selectedIndex = math.max(1, math.min(count,
            ecs.get(enumId, 'ui.config_enum', 'selected_index')))

        -- Arrows hug the boundary box edges, centred on the label row.
        ensureArrow(view, enumHandle, -1, -(width * 0.5 - ARROW_PAD))
        ensureArrow(view, enumHandle, 1, width * 0.5 - ARROW_PAD)

        -- Label band is the box minus the arrow zones, so long labels stop
        -- short of the glyphs instead of running under them.
        ensureText(view, enumHandle, text,
            math.max(1, width - (ARROW_W + ARROW_PAD * 2) * 2))

        -- Indicator bars sit below the label band.
        ensureBars(view, enumHandle, count, selectedIndex,
            TEXT_ROW_H * 0.5 + BAR_TOP_GAP + BAR_H * 0.5)
    end)
end)

return config
