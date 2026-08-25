-- ui/config.lua - configuration list item controls built on the Lua ECS
-- (see games/core/script/ecs.lua).
--
-- There are two controls:
--
-- * ui.config_enum ('<' value row used by settings lists). A game creates
--   one entity and drives its fields:
--
--       local handle, id = config.addEnum(root, x, y, 200)
--       ecs.set(id, 'ui.config_enum', 'text', 'STEREO')
--       ecs.set(id, 'ui.config_enum', 'selected_index', 2)
--       ecs.set(id, 'ui.config_enum', 'count', 3)
--
--   Everything visual is produced by the registered 'ui.config' system: a
--   left/right arrow sprite pair hugging the width boundary box (from the
--   shared texture/ui_arrow atlas), the `text` field centred between them,
--   and an indicator bar row underneath - one equal-size bar per option,
--   white for selected_index, gray for the rest, so the player can see at a
--   glance how many options exist and which one is active.
--
-- * ui.config_list_item (a whole settings row wrapping one enum). The game
--   creates the row and gives it a sub entity carrying ui.config_enum:
--
--       local handle, id = config.addListItem(root, x, y, 320)
--       config.addEnum(handle, 0, 0)  -- position/width owned by the system
--
--   The 'ui.config_list_item' system lays the pair out: the label text fills
--   the left half, the enum sub entity always takes exactly 50% of the item
--   width on the right side, and a gray underline runs along the bottom of
--   everything except a small diamond that is rasterised after (to the right
--   of) the line whenever the enum's value differs from its default
--   (`changed` = true). `selected` draws the cursor chrome over the whole
--   row - an opaque background block (covering the underline) plus a border.
--
-- Both components are purely visual: no input handling lives here. Games
-- move the selection themselves by rewriting the fields; the systems only
-- mirror them into derived child entities for drawing.

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

-- List item geometry. The label band matches the enum's so both texts sit
-- on one line; the underline sits low enough to clear the enum's bar row.
local ITEM_TEXT_H = 14   -- shared label band height
local ITEM_LINE_Y = 18   -- underline centre Y (below the enum's bars)
local LINE_H = 1         -- underline thickness
local BORDER_W = 1       -- selection border thickness
local DIAMOND_SIZE = 5   -- square edge before the 45-degree rotation
local DIAMOND_GAP = 3    -- line end -> diamond left tip
local ITEM_PAD = 2       -- selection chrome padding around the content

local COLOR_TEXT = '#ffffff'
local COLOR_BAR_LIT = { 255, 255, 255, 255 }
local COLOR_BAR_OFF = { 120, 120, 120, 255 }
local COLOR_ARROW = { 230, 230, 230, 255 }
local COLOR_ITEM_ON = '#ffffff'   -- list item label, selected row
local COLOR_ITEM_OFF = '#9a9a9a'  -- list item label, unselected rows
local COLOR_LINE = { 120, 120, 120, 255 }
local COLOR_DIAMOND = { 255, 255, 255, 255 }
local COLOR_SEL_BG = { 36, 52, 100, 255 }
local COLOR_SEL_BORDER = { 230, 230, 230, 255 }

-- Decorative rects sit behind text glyph sprites (which render at z = 0);
-- higher z sorts further back / draws earlier. Back-to-front: underline,
-- selection chrome, enum internals, diamond, glyphs.
local Z_LINE = 80
local Z_ITEM_BG = 70
local Z_ITEM_BORDER = 65
local Z_ARROW = 50
local Z_BAR = 50
local Z_DIAMOND = 40

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
-- List item component --------------------------------------------------------
-------------------------------------------------------------------------------

ecs.registerComponent('ui.config_list_item', {
    { name = 'width', type = 'int', default = 240 },
    { name = 'text', type = 'string', default = '' },
    -- Cursor state: brightens the label and draws an opaque background block
    -- plus border over the whole row (covering the underline) while true.
    { name = 'selected', type = 'bool', default = false },
    -- true while the paired enum value differs from its default; shows the
    -- changed diamond after the underline.
    { name = 'changed', type = 'bool', default = false },
})

--- Create a list item under `parent` with its centre at the given
--- canvas-space offset. The item lays out its own label, underline and
--- changed diamond; pair it with a ui.config_enum sub entity (created with
--- config.addEnum(handle, 0, 0)) for the value control on its right side.
---@param parent SceneEntity
---@param x number Canvas-space centre X.
---@param y number Canvas-space centre Y.
---@param width? integer Total row width in design pixels.
---@return SceneEntity handle
---@return integer id
function config.addListItem(parent, x, y, width)
    local handle, id = ecs.spawn(parent)
    assert(id ~= nil, 'ui.config.addListItem: spawn failed')
    scene.setField(handle, 'name', 'ui.config_list_item')
    scene.setField(handle, 'x', x)
    scene.setField(handle, 'y', y)
    ecs.addComponent(id, 'ui.config_list_item')
    if width ~= nil then
        ecs.set(id, 'ui.config_list_item', 'width', width)
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
        local barHandle = scene.entityFromId(id)
        if barHandle ~= nil then
            ecs.destroyEntity(barHandle)
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

-------------------------------------------------------------------------------
-- List item system -----------------------------------------------------------
-------------------------------------------------------------------------------

-- Per-frame view state, kept out of the binary rows: these describe derived
-- entities (created by this system) rather than authored data.
--   itemViews[id] = { textId, lineId, diamond[], plus cached inputs }
local itemViews = {}

-- Half-diagonal of the rotated square; reserves its gutter on the right edge.
local DIAMOND_HALF_DIAG = DIAMOND_SIZE * 0.70710678

-- Returns the first direct child of `handle` carrying ui.config_enum (the
-- game attaches it; this system only positions it).
local function findEnumChild(handle)
    for _, childId in ipairs(scene.children(handle)) do
        local childHandle = scene.entityFromId(childId)
        if childHandle ~= nil and scene.component(childHandle, 'ui.config_enum') ~= nil then
            return childId
        end
    end
    return nil
end

-- Creates or refreshes one tracked solid quad under `key`, returning its
-- handle with the wanted size already applied.
local function trackedQuad(view, key, parentHandle, name, w, h, color, z)
    local id = liveChild(view, key)
    if id == nil then
        id = addQuadChild(parentHandle, name, w, h, color, z)
        view[key] = id
    end
    local quadHandle = scene.entityFromId(id)
    local sprite = scene.component(quadHandle, 'core.sprite')
    scene.setField(sprite, 'size.width', w)
    scene.setField(sprite, 'size.height', h)
    return quadHandle
end

-- Destroys every tracked quad whose key starts with `prefix`.
local function clearQuads(view, prefix)
    for key, id in pairs(view) do
        if type(key) == 'string' and key:sub(1, #prefix) == prefix then
            local quadHandle = scene.entityFromId(id)
            if quadHandle ~= nil then
                ecs.destroyEntity(quadHandle)
            end
            view[key] = nil
        end
    end
end

-- Creates or refreshes the left-aligned label child. Text entities anchor at
-- their bounding-box centre (see TextLayoutSystem), so callers position this
-- child on the band's centre, not its left edge.
local function ensureItemText(view, handle, text, boxW, selected)
    local markup = '<span color="'
        .. (selected and COLOR_ITEM_ON or COLOR_ITEM_OFF)
        .. '">' .. text .. '</span>'
    if liveChild(view, 'textId') == nil then
        local textHandle
        textHandle, view.textId = ecs.spawn(handle)
        scene.setField(textHandle, 'name', 'ui.config_list_item.label')
        local t = scene.addComponent(textHandle, 'core.text')
        scene.setField(t, 'font', uiFont)
        scene.setField(t, 'flags', TextFlags.xLeft | TextFlags.yMiddle)
        view.markup = nil
    end

    if view.markup ~= markup or view.boxW ~= boxW then
        local textHandle = scene.component(scene.entityFromId(view.textId), 'core.text')
        scene.setField(textHandle, 'text', markup)
        scene.setField(textHandle, 'size.width', boxW)
        scene.setField(textHandle, 'size.height', ITEM_TEXT_H)
        view.markup = markup
    end
    view.boxW = boxW
end

-- Creates or refreshes the bottom underline spanning [x0, x1].
local function ensureLine(view, handle, x0, x1)
    local lineHandle = trackedQuad(view, 'line', handle, 'ui.config_list_item.line',
        math.max(1, x1 - x0), LINE_H, COLOR_LINE, Z_LINE)
    scene.setField(lineHandle, 'x', (x0 + x1) * 0.5)
    scene.setField(lineHandle, 'y', ITEM_LINE_Y)
end

-- Rebuilds the rasterised diamond (one thin quad per pixel row of a square
-- turned 45 degrees), centred on `cx`.
local function ensureDiamond(view, handle, cx)
    local half = (DIAMOND_SIZE - 1) * 0.5
    for i = 0, DIAMOND_SIZE - 1 do
        local w = DIAMOND_SIZE - 2 * math.abs(i - half)
        local quadHandle = trackedQuad(view, 'diamond' .. i, handle,
            'ui.config_list_item.diamond' .. i, w, 1, COLOR_DIAMOND, Z_DIAMOND)
        scene.setField(quadHandle, 'x', cx)
        scene.setField(quadHandle, 'y', ITEM_LINE_Y + (i - half))
    end
end

-- Selection chrome: a background block plus border covering the whole item.
-- Its z sits below the underline's, so it draws on top of (covers) the line
-- while staying behind the diamond and glyphs.
local function ensureSelection(view, handle, x0, x1, yTop, yBottom)
    local w = math.max(1, x1 - x0)
    local h = math.max(1, yBottom - yTop)

    local bgHandle = trackedQuad(view, 'selbg', handle, 'ui.config_list_item.selbg',
        w, h, COLOR_SEL_BG, Z_ITEM_BG)
    scene.setField(bgHandle, 'x', (x0 + x1) * 0.5)
    scene.setField(bgHandle, 'y', (yTop + yBottom) * 0.5)

    -- Border outline: top, bottom, left, right strips inside the block edge.
    local borders = {
        { key = 'selbt', w = w, h = BORDER_W },
        { key = 'selbb', w = w, h = BORDER_W },
        { key = 'selbl', w = BORDER_W, h = h },
        { key = 'selbr', w = BORDER_W, h = h },
    }
    for i, border in ipairs(borders) do
        local quadHandle = trackedQuad(view, border.key, handle,
            'ui.config_list_item.' .. border.key,
            border.w, border.h, COLOR_SEL_BORDER, Z_ITEM_BORDER)
        local bx, by = (x0 + x1) * 0.5, (yTop + yBottom) * 0.5
        if i == 1 then -- top
            by = yTop + BORDER_W * 0.5
        elseif i == 2 then -- bottom
            by = yBottom - BORDER_W * 0.5
        elseif i == 3 then -- left
            bx = x0 + BORDER_W * 0.5
        else -- right
            bx = x1 - BORDER_W * 0.5
        end
        scene.setField(quadHandle, 'x', bx)
        scene.setField(quadHandle, 'y', by)
    end
end

-- Runs before 'ui.config' so the enum width forced here is what that system
-- draws from this frame.
ecs.registerSystem('ui.config_list_item', 900, function()
    -- Drop derived-view bookkeeping for entities that no longer exist.
    for id in pairs(itemViews) do
        if scene.entityFromId(id) == nil then
            itemViews[id] = nil
        end
    end

    ecs.each('ui.config_list_item', function(itemId)
        local handle = scene.entityFromId(itemId)
        if handle == nil then
            return
        end
        local view = itemViews[itemId]
        if view == nil then
            view = {}
            itemViews[itemId] = view
        end

        local width = math.max(1, ecs.get(itemId, 'ui.config_list_item', 'width'))
        local text = ecs.get(itemId, 'ui.config_list_item', 'text')
        local selected = ecs.get(itemId, 'ui.config_list_item', 'selected')
        local changed = ecs.get(itemId, 'ui.config_list_item', 'changed')

        local halfW = width * 0.5
        -- Right-edge gutter reserved for the changed diamond: the underline,
        -- label and config control stop short of it, the diamond does not.
        local gutter = DIAMOND_HALF_DIAG * 2 + DIAMOND_GAP
        local contentR = halfW - gutter
        local diamondCx = halfW - DIAMOND_HALF_DIAG

        -- Label fills the left half of the content area; the text child is
        -- positioned on its band centre because text boxes anchor centrally.
        local bandW = math.max(1, width * 0.5 - gutter)
        ensureItemText(view, handle, text, bandW, selected)
        local labelHandle = scene.entityFromId(view.textId)
        scene.setField(labelHandle, 'x', -halfW + bandW * 0.5)
        scene.setField(labelHandle, 'y', 0)

        -- Underline along the bottom, ending just before the diamond.
        ensureLine(view, handle, -halfW, contentR)

        -- Selection chrome covers the whole item (over the underline).
        if selected then
            ensureSelection(view, handle, -halfW, halfW,
                -(ITEM_TEXT_H * 0.5) - ITEM_PAD,
                ITEM_LINE_Y + (DIAMOND_SIZE - 1) * 0.5 + ITEM_PAD)
        else
            clearQuads(view, 'sel')
        end

        -- Diamond sits after (to the right of) the underline's end, but only
        -- while the value differs from the default.
        if changed then
            ensureDiamond(view, handle, diamondCx)
        else
            clearQuads(view, 'diamond')
        end

        -- The paired enum control owns the right side: exactly 50% of the
        -- item width, vertically centred on the shared label band. Both are
        -- forced every frame; the game's own values never survive layout.
        local enumId = findEnumChild(handle)
        if enumId ~= nil then
            local enumHandle = scene.entityFromId(enumId)
            scene.setField(enumHandle, 'x', contentR - width * 0.25)
            scene.setField(enumHandle, 'y', 0)
            ecs.set(enumId, 'ui.config_enum', 'width',
                math.max(1, math.floor(width * 0.5)))
        end
    end)
end)

return config
