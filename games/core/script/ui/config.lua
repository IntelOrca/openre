-- ui/config.lua - configuration list item controls built on the Lua ECS
-- (see games/core/script/ecs.lua).
--
-- There are two controls:
--
-- * ui.config_enum ('<' value row used by settings lists). A game creates
--   one entity inside a row and drives its fields:
--
--       local handle, id = config.addEnum(rowHandle, 180)
--       ecs.set(id, 'ui.config_enum', 'text', 'STEREO')
--       ecs.set(id, 'ui.config_enum', 'selected_index', 2)
--       ecs.set(id, 'ui.config_enum', 'count', 3)
--
--   The control is a declarative flex tree: a horizontal root of
--   [arrow | weighted vertical stack | arrow] spaced by a flex gap, where the
--   stack holds the label band and the indicator bar row. The arrows are
--   top-level children cross-axis centred (alignment "center") on the whole
--   block height, not just the label band. The
--   native UiFlexSystem positions and sizes every box; the registered
--   'ui.config' system only mirrors state into derived entities - label
--   markup, bar count and bar colours - and never writes geometry.
--
-- * ui.config_list_item (a whole settings row wrapping one enum). A game
--   creates it with config.addListItem(parent, width), attaches one enum
--   sub entity with config.addEnum(rowHandle, width * 0.5) and drives:
--
--       ecs.set(id, 'ui.config_list_item', 'text', 'SOUND')
--       ecs.set(id, 'ui.config_list_item', 'selected', true)
--       ecs.set(id, 'ui.config_list_item', 'changed', false)
--
--   The item is a horizontal flex of [selection_box (weighted) | diamond]
--   separated by a gap. The selection box is an overlay stack ('none'
--   orientation): an underlay wrapper pinning the bottom border (the
--   underline) to the bottom edge, a white rect filling the whole box while
--   `selected`, and a padded interior holding the blue selection fill plus
--   the content row. The interior's padding insets the blue fill by the
--   border width, so the white rect shows through as a 1px ring around every
--   edge. The row lays out [label (weighted) | enum (weighted)], so the two
--   share the item width. The diamond lives outside the selection box,
--   vertically centred in the leftover space, whenever `changed` is true.
--
-- Both components are purely visual: no input handling lives here, and no
-- code writes positions or sizes - geometry is owned entirely by the native
-- UiFlexSystem from the structure created below.

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

local ENUM_BLOCK_H = TEXT_ROW_H + BAR_TOP_GAP + BAR_H -- enum content height
local LINE_H = 1         -- underline thickness
local BORDER_W = 1       -- selection border thickness
local ITEM_PAD = 4       -- breathing room around the row's content
-- Item box height: the enum block plus the interior's border inset and the
-- row's own padding, counted once per vertical edge.
local ITEM_H = ENUM_BLOCK_H + (BORDER_W + ITEM_PAD) * 2
local DIAMOND_SIZE = 5   -- square edge before the 45-degree rotation
local DIAMOND_GAP = 8    -- flex gap between the selection box and the diamond

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
-- selection ring, blue interior fill, enum internals, diamond, glyphs.
local Z_LINE = 80
local Z_SEL_RING = 70
local Z_SEL_FILL = 65
local Z_ARROW = 50
local Z_BAR = 50
local Z_DIAMOND = 40

-------------------------------------------------------------------------------
-- Component ------------------------------------------------------------------
-------------------------------------------------------------------------------

ecs.registerComponent('ui.config_enum', {
    { name = 'text', type = 'string', default = '' },
    -- Both 1-based; selected_index is clamped into [1, count] for drawing
    -- only (the system never writes fields back).
    { name = 'selected_index', type = 'int', default = 1 },
    { name = 'count', type = 'int', default = 0 },
})

ecs.registerComponent('ui.config_list_item', {
    { name = 'text', type = 'string', default = '' },
    -- Cursor state: brightens the label and draws an opaque background block
    -- plus border over the whole row (covering the underline) while true.
    { name = 'selected', type = 'bool', default = false },
    -- true while the paired enum value differs from its default; shows the
    -- changed diamond after the underline.
    { name = 'changed', type = 'bool', default = false },
})

-------------------------------------------------------------------------------
-- Small builders -------------------------------------------------------------
-------------------------------------------------------------------------------

-- The one font used by every control; set before creating any.
local uiFont = nil

---@param font Font A gfx.loadFont handle.
function config.setFont(font)
    uiFont = font
end

-- Adds a core.ui_size component to `handle`: the visual size plus an
-- optional layout weight (> 0 marks the box as weighted / stretchable).
local function addSize(handle, w, h, weight)
    local size = scene.addComponent(handle, 'core.ui_size')
    scene.setField(size, 'width', w)
    scene.setField(size, 'height', h)
    if weight ~= nil then
        scene.setField(size, 'weight', weight)
    end
end

-- Adds a core.ui_flex component to `handle`.
local function addFlex(handle, orientation, gap, padding)
    local flex = scene.addComponent(handle, 'core.ui_flex')
    scene.setField(flex, 'orientation', orientation)
    if gap ~= nil then
        scene.setField(flex, 'gap', gap)
    end
    if padding ~= nil then
        scene.setField(flex, 'padding', padding)
    end
end

-- A solid-colour quad child with a fixed size.
local function addQuad(parent, name, w, h, color, z)
    local handle, id = ecs.spawn(parent)
    scene.setField(handle, 'name', name)
    local sprite = scene.addComponent(handle, 'core.sprite')
    scene.setField(sprite, 'color', color)
    scene.setField(sprite, 'z', z)
    addSize(handle, w, h)
    return handle, id
end

-- An invisible child that absorbs remaining main-axis space (weight 1) or
-- reserves fixed space (weight omitted).
local function addSpacer(parent, name, w, h, weight)
    local handle = ecs.spawn(parent)
    scene.setField(handle, 'name', name)
    addSize(handle, w or 0, h or 0, weight)
    return handle
end

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
-- entities rather than authored data.
--   enumTree[id] = { labelId, barsRowId, bars[], markup }
--   itemTree[id] = { rowId, labelId, interiorId,
--                    chromeIds[]|nil, diamondQuads{}, markup }
local enumTree = {}
local itemTree = {}

-- Drops bookkeeping for entities that no longer exist.
local function pruneTrees()
    for id in pairs(enumTree) do
        if scene.entityFromId(id) == nil then
            enumTree[id] = nil
        end
    end
    for id in pairs(itemTree) do
        if scene.entityFromId(id) == nil then
            itemTree[id] = nil
        end
    end
end

-------------------------------------------------------------------------------
-- Creation helpers (what a game calls) ---------------------------------------
-------------------------------------------------------------------------------

--- Create an enum list control under `parent` (usually a config list item's
--- row). Its box is `width` wide; height is the natural content block.
---@param parent SceneEntity Row/entity to attach to.
---@param width number Boundary box width in design pixels.
---@return SceneEntity handle
---@return integer id
function config.addEnum(parent, width)
    local handle, id = ecs.spawn(parent)
    assert(id ~= nil, 'ui.config.addEnum: spawn failed')
    scene.setField(handle, 'name', 'ui.config_enum')
    ecs.addComponent(id, 'ui.config_enum')
    local w = width or 120
    addSize(handle, w, ENUM_BLOCK_H, 1)

    -- Horizontal root: [arrowL | weighted stack | arrowR], spaced by a 2px
    -- flex gap. The arrows are top-level children; their weight-0 boxes are
    -- cross-axis centred (alignment "center") on the full block height.
    addFlex(handle, 'horizontal', 2)

    local atlas = arrowAtlas()

    local function addArrow(dir)
        local arrow = ecs.spawn(handle)
        scene.setField(arrow, 'name', 'ui.config_enum.arrow' .. (dir < 0 and 'L' or 'R'))
        local sprite = scene.addComponent(arrow, 'core.sprite')
        scene.setField(sprite, 'texture', atlas)
        scene.setField(sprite, 'src.x', dir < 0 and 0 or ARROW_W)
        scene.setField(sprite, 'src.y', 0)
        scene.setField(sprite, 'src.width', ARROW_W)
        scene.setField(sprite, 'src.height', ARROW_H)
        scene.setField(sprite, 'color', COLOR_ARROW)
        scene.setField(sprite, 'z', Z_ARROW)
        addSize(arrow, ARROW_W, ARROW_H)
        scene.setField(scene.component(arrow, 'core.ui_size'), 'alignment', 'center')
    end

    addArrow(-1)

    -- Weighted vertical stack filling the space between the arrows; the
    -- label band and bar row below are authored to fill it exactly.
    local stack = ecs.spawn(handle)
    scene.setField(stack, 'name', 'ui.config_enum.stack')
    addSize(stack, 0, 0, 1)
    addFlex(stack, 'vertical', BAR_TOP_GAP)

    local bandW = w - 2 * ARROW_W

    -- Centred value label; the system writes the markup here.
    local labelHandle, labelId = ecs.spawn(stack)
    scene.setField(labelHandle, 'name', 'ui.config_enum.label')
    local t = scene.addComponent(labelHandle, 'core.text')
    scene.setField(t, 'font', uiFont)
    scene.setField(t, 'flags', TextFlags.xCenter | TextFlags.yMiddle)
    addSize(labelHandle, bandW, TEXT_ROW_H)

    -- Indicator bar row underneath; bars are created/destroyed by the
    -- system as `count` changes and group-centre horizontally.
    local barsRowHandle, barsRowId = ecs.spawn(stack)
    scene.setField(barsRowHandle, 'name', 'ui.config_enum.bars_row')
    addSize(barsRowHandle, bandW, BAR_H)
    addFlex(barsRowHandle, 'horizontal', BAR_GAP)

    addArrow(1)

    enumTree[id] = { labelId = labelId, barsRowId = barsRowId, bars = {}, markup = nil }

    return handle, id
end

--- Create a list item under `parent` with its centre at the given
--- canvas-space offset. The item builds its whole flex tree here; pair it
--- with a ui.config_enum sub entity (config.addEnum(rowHandle, width * 0.5))
--- for the value control on its right side.
---@param parent SceneEntity
---@param x number Canvas-space centre X.
---@param y number Canvas-space centre Y.
---@param width? integer Total row width in design pixels.
---@return SceneEntity handle
---@return integer id
---@return SceneEntity row Row entity - attach a ui.config_enum sub entity here.
function config.addListItem(parent, x, y, width)
    local handle, id = ecs.spawn(parent)
    assert(id ~= nil, 'ui.config.addListItem: spawn failed')
    scene.setField(handle, 'name', 'ui.config_list_item')
    scene.setField(handle, 'x', x)
    scene.setField(handle, 'y', y)
    ecs.addComponent(id, 'ui.config_list_item')
    addSize(handle, width or 240, ITEM_H)

    -- Horizontal: [selection_box (weighted) | diamond], separated by a gap.
    -- The diamond sits outside the box so it never scales with it.
    addFlex(handle, 'horizontal', DIAMOND_GAP)

    -- Selection box: takes all the space left of the diamond and stacks its
    -- layers ('none') by z order.
    local boxHandle, boxId = ecs.spawn(handle)
    scene.setField(boxHandle, 'name', 'ui.config_list_item.selection_box')
    addSize(boxHandle, 0, 0, 1)
    addFlex(boxHandle, 'none')

    -- Underlay: a weighted wrapper whose vertical flex pins the bottom
    -- border (the row underline) to the bottom edge - a weighted spacer
    -- first, then the literal-thickness line. Always visible; covered by
    -- the selection fill while selected.
    local underlay = ecs.spawn(boxHandle)
    scene.setField(underlay, 'name', 'ui.config_list_item.underlay')
    addSize(underlay, 0, 0, 1)
    addFlex(underlay, 'vertical')
    addSpacer(underlay, 'ui.config_list_item.underlay.push', 0, 0, 1)
    addQuad(underlay, 'ui.config_list_item.bottom_border', width or 240, LINE_H, COLOR_LINE, Z_LINE)

    -- Interior: fills the box and insets its children by the border width,
    -- so the blue selection fill drawn inside it leaves the white ring
    -- visible around every edge.
    local interior, interiorId = ecs.spawn(boxHandle)
    scene.setField(interior, 'name', 'ui.config_list_item.interior')
    addSize(interior, 0, 0, 1)
    addFlex(interior, 'none', nil, BORDER_W)

    -- The row: weighted label | literal half-width enum, padded away from
    -- the selection ring by ITEM_PAD.
    local row, rowId = ecs.spawn(interior)
    scene.setField(row, 'name', 'ui.config_list_item.row')
    addSize(row, 0, 0, 1)
    addFlex(row, 'horizontal', nil, ITEM_PAD)

    local labelHandle, labelId = ecs.spawn(row)
    scene.setField(labelHandle, 'name', 'ui.config_list_item.label')
    local t = scene.addComponent(labelHandle, 'core.text')
    scene.setField(t, 'font', uiFont)
    scene.setField(t, 'flags', TextFlags.xLeft | TextFlags.yMiddle)
    addSize(labelHandle, 0, 0, 1)

    -- The changed diamond: a literal-sized box at top level, weight-0, so
    -- it keeps its size and centres vertically in the item. Its rasterised
    -- rows are filled in by the system while `changed`.
    local diamondBox, diamondBoxId = ecs.spawn(handle)
    scene.setField(diamondBox, 'name', 'ui.config_list_item.diamond')
    addSize(diamondBox, DIAMOND_SIZE, DIAMOND_SIZE)
    addFlex(diamondBox, 'vertical')

    itemTree[id] = {
        rowId = rowId,
        labelId = labelId,
        boxId = boxId,
        interiorId = interiorId,
        diamondBoxId = diamondBoxId,
        chromeIds = nil,
        diamondQuads = {},
        markup = nil,
    }

    return handle, id, row
end

-- Natural item box height, for callers composing lists with flex gaps.
config.ITEM_HEIGHT = ITEM_H

-------------------------------------------------------------------------------
-- Derived-entity builders (used by the systems below) ------------------------
-------------------------------------------------------------------------------

-- Selection chrome as two rects: an unpadded white rect filling the whole
-- selection box (the ring) plus a blue rect inside the padded interior (the
-- fill). The interior's padding insets the blue rect by the border width,
-- so the white shows through as a 1px ring around every edge. Returns the
-- ids of the two subtree roots so the caller can destroy them again.
local function buildSelection(boxHandle, interiorId)
    local chromeIds = {}

    local ring, ringId = addQuad(boxHandle, 'ui.config_list_item.sel_ring', 0, 0, COLOR_SEL_BORDER, Z_SEL_RING)
    scene.setField(scene.component(ring, 'core.ui_size'), 'weight', 1)
    chromeIds[#chromeIds + 1] = ringId

    local interiorHandle = scene.entityFromId(interiorId)
    if interiorHandle ~= nil then
        local fill, fillId = addQuad(interiorHandle, 'ui.config_list_item.sel_fill', 0, 0, COLOR_SEL_BG, Z_SEL_FILL)
        scene.setField(scene.component(fill, 'core.ui_size'), 'weight', 1)
        chromeIds[#chromeIds + 1] = fillId
    end

    return chromeIds
end

local function destroyChrome(tree)
    for _, childId in ipairs(tree.chromeIds or {}) do
        local childHandle = scene.entityFromId(childId)
        if childHandle ~= nil then
            ecs.destroyEntity(childHandle)
        end
    end
    tree.chromeIds = nil
end

-- Fills or empties the reserved diamond box with the rasterised changed
-- diamond: one thin quad per pixel row of a square turned 45 degrees. The
-- box is a vertical flex with no weights, so the rows group-centre on both
-- axes and every row width centres horizontally.
local function ensureDiamond(tree, changed)
    local boxHandle = scene.entityFromId(tree.diamondBoxId)
    if boxHandle == nil then
        return
    end

    while #tree.diamondQuads > (changed and DIAMOND_SIZE or 0) do
        local id = table.remove(tree.diamondQuads)
        local quadHandle = scene.entityFromId(id)
        if quadHandle ~= nil then
            ecs.destroyEntity(quadHandle)
        end
    end
    if changed then
        local half = (DIAMOND_SIZE - 1) * 0.5
        for i = #tree.diamondQuads + 1, DIAMOND_SIZE do
            local w = DIAMOND_SIZE - 2 * math.abs(i - 1 - half)
            local _, id = addQuad(boxHandle, 'ui.config_list_item.diamond.row' .. i,
                w, 1, COLOR_DIAMOND, Z_DIAMOND)
            tree.diamondQuads[i] = id
        end
    end
end

-------------------------------------------------------------------------------
-- Systems --------------------------------------------------------------------
-------------------------------------------------------------------------------

-- List item system: mirrors text/selected/changed into the derived tree.
-- Runs before 'ui.config' so a freshly attached enum draws this frame.
ecs.registerSystem('ui.config_list_item', 900, function()
    pruneTrees()

    ecs.each('ui.config_list_item', function(itemId)
        local tree = itemTree[itemId]
        if tree == nil then
            return
        end
        local handle = scene.entityFromId(itemId)
        if handle == nil then
            return
        end

        local text = ecs.get(itemId, 'ui.config_list_item', 'text')
        local selected = ecs.get(itemId, 'ui.config_list_item', 'selected')
        local changed = ecs.get(itemId, 'ui.config_list_item', 'changed')

        -- Label markup follows the selection brightness.
        local markup = '<span color="'
            .. (selected and COLOR_ITEM_ON or COLOR_ITEM_OFF)
            .. '">' .. text .. '</span>'
        if tree.markup ~= markup then
            local labelHandle = scene.entityFromId(tree.labelId)
            if labelHandle ~= nil then
                scene.setField(scene.component(labelHandle, 'core.text'), 'text', markup)
            end
            tree.markup = markup
        end

        -- Selection chrome overlay: built while selected, destroyed after.
        if selected and tree.chromeIds == nil then
            tree.chromeIds = buildSelection(scene.entityFromId(tree.boxId), tree.interiorId)
        elseif not selected and tree.chromeIds ~= nil then
            destroyChrome(tree)
        end

        -- Changed diamond fills its reserved slot while the value differs
        -- from the default.
        ensureDiamond(tree, changed)
    end)
end)

-- Enum system: mirrors text/count/selected_index into the enum's label and
-- indicator bars. Bar entities are reused in place as the count changes.
ecs.registerSystem('ui.config', 1000, function()
    pruneTrees()

    ecs.each('ui.config_enum', function(enumId)
        local tree = enumTree[enumId]
        if tree == nil then
            return
        end

        local text = ecs.get(enumId, 'ui.config_enum', 'text')
        local count = math.max(0, ecs.get(enumId, 'ui.config_enum', 'count'))
        local selectedIndex = math.max(1, math.min(count, ecs.get(enumId, 'ui.config_enum', 'selected_index')))

        -- Centred value label between the arrows.
        local markup = '<span color="' .. COLOR_TEXT .. '">' .. text .. '</span>'
        if tree.markup ~= markup then
            local labelHandle = scene.entityFromId(tree.labelId)
            if labelHandle ~= nil then
                scene.setField(scene.component(labelHandle, 'core.text'), 'text', markup)
            end
            tree.markup = markup
        end

        -- Indicator bar row: one equal-size bar per option, white for
        -- selected_index, gray for the rest. The native flex centres them
        -- horizontally no matter how many there are.
        local barsRowHandle = scene.entityFromId(tree.barsRowId)
        if barsRowHandle == nil then
            return
        end
        while #tree.bars > count do
            local id = table.remove(tree.bars)
            local barHandle = scene.entityFromId(id)
            if barHandle ~= nil then
                ecs.destroyEntity(barHandle)
            end
        end
        for i = 1, count do
            local lit = i == selectedIndex
            local id = tree.bars[i]
            local barHandle = nil
            if id == nil or scene.entityFromId(id) == nil then
                local newHandle, newId = addQuad(barsRowHandle, 'ui.config_enum.bar' .. i,
                    BAR_W, BAR_H, lit and COLOR_BAR_LIT or COLOR_BAR_OFF, Z_BAR)
                tree.bars[i] = newId
                barHandle = newHandle
            else
                barHandle = scene.entityFromId(id)
            end
            if barHandle ~= nil then
                scene.setField(scene.component(barHandle, 'core.sprite'), 'color',
                    lit and COLOR_BAR_LIT or COLOR_BAR_OFF)
            end
        end
    end)
end)

return config