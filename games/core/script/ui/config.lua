-- ui/config.lua - configuration list item controls built on the Lua ECS
-- (see games/core/script/ecs.lua).
--
-- There are four controls:
--
-- * ui.config.Enum ('<' value row used by settings lists). A game creates
--   one entity inside a row and drives its fields:
--
--       local handle, id = config.addEnum(rowHandle, 180)
--       ecs.set(id, 'ui.config.Enum', 'text', 'STEREO')
--       ecs.set(id, 'ui.config.Enum', 'selected_index', 2)
--       ecs.set(id, 'ui.config.Enum', 'count', 3)
--
--   The control is a declarative flex tree: a horizontal root of
--   [arrow | weighted vertical stack | arrow] spaced by a flex gap, where the
--   stack holds the label band and the indicator bar row. The arrows are
--   top-level children cross-axis centred (alignment "center") on the whole
--   block height, not just the label band. The
--   native UiFlexSystem positions and sizes every box; the registered
--   'ui.config.Control' system only mirrors state into derived entities -
--   label markup, bar count and bar colours - and never writes geometry.
--
-- * ui.config.Range (a '< - |....| + > N' slider for ranged settings). A game
--   creates it like the enum and drives:
--
--       local handle, id = config.addRange(rowHandle, 180)
--       ecs.set(id, 'ui.config.Range', 'value', 8)
--       ecs.set(id, 'ui.config.Range', 'min', 0)
--       ecs.set(id, 'ui.config.Range', 'max', 20)
--       ecs.set(id, 'ui.config.Range', 'step', 1)
--
--   The control is a horizontal flex of [arrow | - | slider (weighted) | + |
--   arrow | value]. The slider holds a full-width rule with distributed
--   vertical ticks (the end ticks slightly taller) and a full-height marker on
--   the current value. The 'ui.config.Control' system builds the ticks from
--   (max - min) / step + 1 and never wraps.
--
-- * ui.config.ListGroup (a group header band the same height as a list item,
--   placed before the options of a group). A game creates it with
--   config.addListGroup(parent, x, y, width) and drives:
--
--       local handle, id = config.addListGroup(listHandle, 0, 0, 290)
--       ecs.set(id, 'ui.config.ListGroup', 'text', 'CHEATS')
--
--   The control is a vertical flex of [spacer | bottom band | rule]: the band is
--   a horizontal flex of [gray back box (weighted) | spacer], the gray box is an
--   overlay holding the gray sprite (behind) and the white category text, and the
--   rule is the 1px underline shared with the list items. The native
--   UiFlexSystem positions and sizes every box; the registered
--   'ui.config.ListGroup' system only mirrors the text field into the label
--   markup and never writes geometry.
--
-- * ui.config.ListItem (a whole settings row wrapping one control). A game
--   creates it with config.addListItem(parent, width), attaches one enum
--   sub entity with config.addEnum(rowHandle, width * 0.5) and drives:
--
--       ecs.set(id, 'ui.config.ListItem', 'text', 'SOUND')
--       ecs.set(id, 'ui.config.ListItem', 'selected', true)
--       ecs.set(id, 'ui.config.ListItem', 'changed', false)
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

-- List group header band.
local COLOR_GROUP_BG = { 50, 50, 50, 255 }   -- gray back box
local GROUP_PAD = 6        -- vertical padding of the gray back box around its text

-- Decorative rects sit behind text glyph sprites (which render at z = 0);
-- higher z sorts further back / draws earlier. Back-to-front: underline,
-- selection ring, blue interior fill, enum internals, diamond, glyphs.
local Z_LINE = 0
local Z_ARROW = 0
local Z_BAR = 0
local Z_DIAMOND = 0
local Z_GROUP_BG = 0  -- gray band sits behind the rule and text

-- ui.config.Range slider control constants.
local RANGE_BLOCK_H = ENUM_BLOCK_H   -- matches the enum control height
local RULE_H = 1                     -- horizontal rule thickness
local MARK_W = 1                     -- vertical tick width
local MARK_H = 3                     -- tick extent above/below the rule (middle)
local END_MARK_H = 4                 -- taller tick at either end
local RANGE_VALUE_W = 24             -- end value text block width
local RANGE_ICON_W = 8               -- "+"/"-" icon box width
local Z_RULE = 0                    -- rule line (behind the ticks)
local Z_MARK = 0                    -- slider ticks (in front of the rule)
local Z_ICON = 0                    -- range '-' / '+' white glyphs (front-most)

-------------------------------------------------------------------------------
-- Component ------------------------------------------------------------------
-------------------------------------------------------------------------------

ecs.registerComponent('ui.config.Enum', {
    { name = 'text', type = 'string', default = '' },
    -- Both 1-based; selected_index is clamped into [1, count] for drawing
    -- only (the system never writes fields back).
    { name = 'selected_index', type = 'int', default = 1 },
    { name = 'count', type = 'int', default = 0 },
})

ecs.registerComponent('ui.config.Range', {
    { name = 'text', type = 'string', default = '' },
    -- Live numeric value (drawn as the end text block and the full-height
    -- current marker). min/max/step describe the legal range, whose mark
    -- count is (max - min) / step + 1. The control never wraps.
    { name = 'value', type = 'int', default = 0 },
    { name = 'min', type = 'int', default = 0 },
    { name = 'max', type = 'int', default = 0 },
    { name = 'step', type = 'int', default = 1 },
})

ecs.registerComponent('ui.config.ListItem', {
    { name = 'text', type = 'string', default = '' },
    -- Cursor state: brightens the label and draws an opaque background block
    -- plus border over the whole row (covering the underline) while true.
    { name = 'selected', type = 'bool', default = false },
    -- true while the paired enum value differs from its default; shows the
    -- changed diamond after the underline.
    { name = 'changed', type = 'bool', default = false },
})

ecs.registerComponent('ui.config.ListGroup', {
    -- The group / category title drawn in white on the gray band.
    { name = 'text', type = 'string', default = '' },
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

-- Adds a core.UiSize component to `handle`: the visual size plus an
-- optional layout weight (> 0 marks the box as weighted / stretchable).
local function addSize(handle, w, h, weight)
    local size = scene.addComponent(handle, 'core.UiSize')
    scene.setField(size, 'width', w)
    scene.setField(size, 'height', h)
    if weight ~= nil then
        scene.setField(size, 'weight', weight)
    end
end

-- Adds a core.UiFlex component to `handle`.
local function addFlex(handle, orientation, gap, padding)
    local flex = scene.addComponent(handle, 'core.UiFlex')
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
    local sprite = scene.addComponent(handle, 'core.Sprite')
    scene.setField(sprite, 'color', color)
    scene.setField(handle, 'z', z)
    addSize(handle, w, h)
    return handle, id
end

-- An invisible child that absorbs remaining main-axis space (weight 1) or
-- reserves fixed space (weight omitted).
local function addSpacer(parent, name, w, h, weight)
    local handle, id = ecs.spawn(parent)
    scene.setField(handle, 'name', name)
    addSize(handle, w or 0, h or 0, weight)
    return handle, id
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

-- Left/right arrow cell of the shared ui_arrow atlas, as a top-level child of
-- `parent` (cross-axis centred on the full block height).
local function addArrow(parent, dir, name)
    local arrow = ecs.spawn(parent)
    scene.setField(arrow, 'name', name or ('ui.config_arrow' .. (dir < 0 and 'L' or 'R')))
    local sprite = scene.addComponent(arrow, 'core.Sprite')
    scene.setField(sprite, 'texture', arrowAtlas())
    scene.setField(sprite, 'src.x', dir < 0 and 0 or ARROW_W)
    scene.setField(sprite, 'src.y', 0)
    scene.setField(sprite, 'src.width', ARROW_W)
    scene.setField(sprite, 'src.height', ARROW_H)
    scene.setField(sprite, 'color', COLOR_ARROW)
    scene.setField(arrow, 'z', Z_ARROW)
    addSize(arrow, ARROW_W, ARROW_H)
    scene.setField(scene.component(arrow, 'core.UiSize'), 'alignment', 'center')
    return arrow
end

-- The '-' / '+' glyphs of the range slider, drawn as plain white rectangles
-- (not font glyphs). Each is a 'none' flex box (so the shape centres) holding
-- one (minus) or two (plus) white quads.
local function addIcon(parent, kind, name)
    local box, boxId = ecs.spawn(parent)
    scene.setField(box, 'name', name)
    addSize(box, RANGE_ICON_W, RANGE_BLOCK_H)
    addFlex(box, 'none')

    if kind == '-' then
        addQuad(box, name .. '.bar', RANGE_ICON_W - 4, 1, COLOR_DIAMOND, Z_ICON)
    else -- '+'
        addQuad(box, name .. '.h', RANGE_ICON_W - 4, 1, COLOR_DIAMOND, Z_ICON)
        local vh = addQuad(box, name .. '.v', 1, 4, COLOR_DIAMOND, Z_ICON)
        scene.setField(scene.component(vh, 'core.UiSize'), 'alignment', 'center')
    end
    return box, boxId
end

-- Per-frame view state, kept out of the binary rows: these describe derived
-- entities rather than authored data.
--   enumTree[id] = { labelId, barsRowId, bars[], markup }
--   itemTree[id] = { rowId, labelId, interiorId, boxId,
--                    ringId, fillId, diamondQuads{}, markup }
local enumTree = {}
local itemTree = {}
local rangeTree = {}
local groupTree = {}

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
    for id in pairs(rangeTree) do
        if scene.entityFromId(id) == nil then
            rangeTree[id] = nil
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
---@param name? string Override for the entity-name prefix (default `ui.config.Enum`).
---@return SceneEntity handle
---@return integer id
function config.addEnum(parent, width, name)
    local handle, id = ecs.spawn(parent)
    assert(id ~= nil, 'ui.config.addEnum: spawn failed')
    local prefix = name or 'ui.config.Enum'
    scene.setField(handle, 'name', prefix)
    ecs.addComponent(id, 'ui.config.Enum')
    local w = width or 120
    addSize(handle, w, ENUM_BLOCK_H, 1)

    -- Horizontal root: [arrowL | weighted stack | arrowR], spaced by a 2px
    -- flex gap. The arrows are top-level children; their weight-0 boxes are
    -- cross-axis centred (alignment "center") on the full block height.
    addFlex(handle, 'horizontal', 2)

    addArrow(handle, -1, prefix .. '.arrowL')

    -- Weighted vertical stack filling the space between the arrows; the
    -- label band and bar row below are authored to fill it exactly.
    local stack = ecs.spawn(handle)
    scene.setField(stack, 'name', prefix .. '.stack')
    addSize(stack, 0, 0, 1)
    addFlex(stack, 'vertical', BAR_TOP_GAP)

    local bandW = w - 2 * ARROW_W

    -- Centred value label; the system writes the markup here.
    local labelHandle, labelId = ecs.spawn(stack)
    scene.setField(labelHandle, 'name', prefix .. '.label')
    local t = scene.addComponent(labelHandle, 'core.Text')
    scene.setField(t, 'font', uiFont)
    scene.setField(t, 'flags', TextFlags.xCenter | TextFlags.yMiddle)
    addSize(labelHandle, bandW, TEXT_ROW_H)

    -- Indicator bar row underneath; bars are created/destroyed by the
    -- system as `count` changes and group-centre horizontally.
    local barsRowHandle, barsRowId = ecs.spawn(stack)
    scene.setField(barsRowHandle, 'name', prefix .. '.bars_row')
    addSize(barsRowHandle, bandW, BAR_H)
    addFlex(barsRowHandle, 'horizontal', BAR_GAP)

    addArrow(handle, 1, prefix .. '.arrowR')

    enumTree[id] = { labelId = labelId, barsRowId = barsRowId,     bars = {},
    prefix = prefix,
 markup = nil }

    return handle, id
end

--- Create a range slider control under `parent` (usually a config list item's
--- row). Its box is `width` wide; height matches the enum control. The value is
--- driven through the `value`, `min`, `max` and `step` fields; the system
--- renders a horizontal rule with distributed ticks, a taller tick at each
--- end, the current value as a full-height marker, and the value text block.
---@param parent SceneEntity Row/entity to attach to.
---@param width number Boundary box width in design pixels.
---@param name? string Override for the entity-name prefix (default `ui.config.Range`).
---@return SceneEntity handle
---@return integer id
function config.addRange(parent, width, name)
    local handle, id = ecs.spawn(parent)
    assert(id ~= nil, 'ui.config.addRange: spawn failed')
    local prefix = name or 'ui.config.Range'
    scene.setField(handle, 'name', prefix)
    ecs.addComponent(id, 'ui.config.Range')
    local w = width or 120
    addSize(handle, w, RANGE_BLOCK_H, 1)

    -- Horizontal root: [arrowL | - | slider (weighted) | + | arrowR | value].
    addFlex(handle, 'horizontal', 2)

    addArrow(handle, -1, prefix .. '.arrowL')
    addIcon(handle, '-', prefix .. '.subtract')

    -- Slider: a 'none' flex filling the weighted slot, holding the rule line
    -- (full width) behind a horizontal marks flex (full size). The marks flex
    -- owns the distributed ticks + spacers (see the ui.config system).
    local sliderHandle, sliderId = ecs.spawn(handle)
    scene.setField(sliderHandle, 'name', prefix .. '.slider')
    addSize(sliderHandle, 0, 0, 1)
    addFlex(sliderHandle, 'none')

    addQuad(sliderHandle, prefix .. '.rule', 1000, RULE_H, COLOR_LINE, Z_RULE)

    local marksHandle, marksId = ecs.spawn(sliderHandle)
    scene.setField(marksHandle, 'name', prefix .. '.marks')
    addSize(marksHandle, 0, 0, 1)
    addFlex(marksHandle, 'horizontal', 0)

    addIcon(handle, '+', prefix .. '.plus')
    addArrow(handle, 1, prefix .. '.arrowR')

    -- Value text block (right end): a fixed-width flex box so the text is
    -- centred within the allocated area rather than hugging the left edge.
    local valueHandle, valueId = ecs.spawn(handle)
    scene.setField(valueHandle, 'name', prefix .. '.value')
    addSize(valueHandle, RANGE_VALUE_W, RANGE_BLOCK_H)
    addFlex(valueHandle, 'none')

    local valueText, valueTextId = ecs.spawn(valueHandle)
    scene.setField(valueText, 'name', prefix .. '.value.text')
    local vt = scene.addComponent(valueText, 'core.Text')
    scene.setField(vt, 'font', uiFont)
    scene.setField(vt, 'flags', TextFlags.xCenter | TextFlags.yMiddle)
    addSize(valueText, 0, 0)

    rangeTree[id] = {
        marksId = marksId,
        textId = valueTextId,
        flexOrder = {},
        markHandles = {},
        count = nil,
        prefix = prefix,
        markup = nil,
    }

    return handle, id
end

--- Create a list item under `parent` with its centre at the given
--- canvas-space offset. The item builds its whole flex tree here; pair it
--- with a ui.config.Enum sub entity (config.addEnum(rowHandle, width * 0.5))
--- for the value control on its right side.
---@param parent SceneEntity
---@param x number Canvas-space centre X.
---@param y number Canvas-space centre Y.
---@param width? integer Total row width in design pixels.
---@return SceneEntity handle
---@return integer id
---@param name? string Override for the entity-name prefix (default `ui.config.ListItem`).
---@return SceneEntity row Row entity - attach a ui.config.Enum sub entity here.
function config.addListItem(parent, x, y, width, name)
    local handle, id = ecs.spawn(parent)
    assert(id ~= nil, 'ui.config.addListItem: spawn failed')
    local prefix = name or 'ui.config.ListItem'
    scene.setField(handle, 'name', prefix)
    scene.setField(handle, 'x', x)
    scene.setField(handle, 'y', y)
    ecs.addComponent(id, 'ui.config.ListItem')
    addSize(handle, width or 240, ITEM_H)

    -- Horizontal: [selection_box (weighted) | diamond], separated by a gap.
    -- The diamond sits outside the box so it never scales with it.
    addFlex(handle, 'horizontal', DIAMOND_GAP)

    -- Selection box: takes all the space left of the diamond and stacks its
    -- layers ('none') by z order.
    local boxHandle, boxId = ecs.spawn(handle)
    scene.setField(boxHandle, 'name', prefix .. '.selection_box')
    addSize(boxHandle, 0, 0, 1)
    addFlex(boxHandle, 'none')

    -- Selection ring: a white rect filling the whole selection box. It is the
    -- first child of the box, so tree-depth paint order puts it furthest back
    -- (behind the fill and the content). Hidden until selected (weight 0).
    local ring, ringId = addQuad(boxHandle, prefix .. '.sel_ring', 0, 0, COLOR_SEL_BORDER, 0)
    scene.setField(scene.component(ring, 'core.UiSize'), 'weight', 0)

    -- Underlay: a weighted wrapper whose vertical flex pins the bottom
    -- border (the row underline) to the bottom edge - a weighted spacer
    -- first, then the literal-thickness line. Always visible; covered by
    -- the selection fill while selected.
    local underlay = ecs.spawn(boxHandle)
    scene.setField(underlay, 'name', prefix .. '.underlay')
    addSize(underlay, 0, 0, 1)
    addFlex(underlay, 'vertical')
    addSpacer(underlay, prefix .. '.underlay.push', 0, 0, 1)
    addQuad(underlay, prefix .. '.bottom_border', width or 240, LINE_H, COLOR_LINE, Z_LINE)

    -- Interior: fills the box and insets its children by the border width,
    -- so the blue selection fill drawn inside it leaves the white ring
    -- visible around every edge.
    local interior, interiorId = ecs.spawn(boxHandle)
    scene.setField(interior, 'name', prefix .. '.interior')
    addSize(interior, 0, 0, 1)
    addFlex(interior, 'none', nil, BORDER_W)

    -- Selection fill: a blue rect inside the padded interior. It is the first
    -- child of the interior, so it paints between the ring and the content.
    -- Hidden until selected (weight 0).
    local fill, fillId = addQuad(interior, prefix .. '.sel_fill', 0, 0, COLOR_SEL_BG, 0)
    scene.setField(scene.component(fill, 'core.UiSize'), 'weight', 0)

    -- The row: weighted label | literal half-width enum, padded away from
    -- the selection ring by ITEM_PAD.
    local row, rowId = ecs.spawn(interior)
    scene.setField(row, 'name', prefix .. '.row')
    addSize(row, 0, 0, 1)
    addFlex(row, 'horizontal', nil, ITEM_PAD)

    local labelHandle, labelId = ecs.spawn(row)
    scene.setField(labelHandle, 'name', prefix .. '.label')
    local t = scene.addComponent(labelHandle, 'core.Text')
    scene.setField(t, 'font', uiFont)
    scene.setField(t, 'flags', TextFlags.xLeft | TextFlags.yMiddle)
    addSize(labelHandle, 0, 0, 1)

    -- The changed diamond: a literal-sized box at top level, weight-0, so
    -- it keeps its size and centres vertically in the item. Its single 5x5
    -- white square sprite is shown by the system while `changed`.
    local diamondBox, diamondBoxId = ecs.spawn(handle)
    scene.setField(diamondBox, 'name', prefix .. '.diamond')
    addSize(diamondBox, DIAMOND_SIZE, DIAMOND_SIZE)
    addFlex(diamondBox, 'none')

    itemTree[id] = {
        rowId = rowId,
        labelId = labelId,
        boxId = boxId,
        interiorId = interiorId,
        prefix = prefix,
        diamondBoxId = diamondBoxId,
        ringId = ringId,
        fillId = fillId,
        diamondQuads = {},
        markup = nil,
    }

    return handle, id, row
end

--- Create a group header row under `parent`. Its box is `width` wide and
--- `config.ITEM_HEIGHT` tall so it lines up with the list items around it. The
--- header is a vertical flex of [spacer | bottom band | rule]: the band is a
--- horizontal flex of [gray back box (weighted) | spacer (weighted)], the gray
--- box is a bottom-left tag exactly half the band's content width (so half the
--- row width at whatever width the flex system lays the row out at) holding the
--- gray sprite (behind) and the white category text, and the rule is the 1px
--- underline shared with the list items. The 'ui.config.ListGroup' system
--- mirrors the `text` field into the label markup.
---@param parent SceneEntity
---@param x number Canvas-space centre X.
---@param y number Canvas-space centre Y.
---@param width? integer Total row width in design pixels.
---@return SceneEntity handle
---@param name? string Override for the entity-name prefix (default `ui.config.ListGroup`).
---@return integer id
function config.addListGroup(parent, x, y, width, name)
    local handle, id = ecs.spawn(parent)
    assert(id ~= nil, 'ui.config.addListGroup: spawn failed')
    local prefix = name or 'ui.config.ListGroup'
    scene.setField(handle, 'name', prefix)
    scene.setField(handle, 'x', x)
    scene.setField(handle, 'y', y)
    ecs.addComponent(id, 'ui.config.ListGroup')
    addSize(handle, width or 240, ITEM_H)
    addFlex(handle, 'vertical', 0)

    -- Weighted top spacer; absorbs any rounding so the band + rule fill the row.
    addSpacer(handle, prefix .. '.spacer', 0, 0, 1)

    -- Bottom band: horizontal flex, no padding, so the gray box sits flush with
    -- the left edge of the row. The gray box is a left-aligned tag half the row
    -- width; a trailing weighted spacer pins it to the left edge. Weight 1 lets
    -- the band fill the row above the rule.
    local bottom, bottomId = ecs.spawn(handle)
    scene.setField(bottom, 'name', prefix .. '.bottom')
    addSize(bottom, 0, ITEM_H - LINE_H, 1)
    addFlex(bottom, 'horizontal', 0)

    -- Gray back box: fills the bottom band's height (stretch) and takes half the
    -- row width (weight 1:1 with the trailing spacer). It carries its own gray
    -- sprite (drawn behind the text) and the white category text; no dedicated
    -- background sub-entity is needed.
    local grayBack, grayBackId = ecs.spawn(bottom)
    scene.setField(grayBack, 'name', prefix .. '.gray_back')
    addSize(grayBack, 0, TEXT_ROW_H + GROUP_PAD * 2, 1)
    addFlex(grayBack, 'none', nil, 1)
    -- Stretch to the band height; the weight shares the row 50:50 with the spacer.
    scene.setField(scene.component(grayBack, 'core.UiSize'), 'alignment', 'stretch')
    local sprite = scene.addComponent(grayBack, 'core.Sprite')
    scene.setField(sprite, 'color', COLOR_GROUP_BG)
    scene.setField(grayBack, 'z', Z_GROUP_BG)

    local labelHandle, labelId = ecs.spawn(grayBack)
    scene.setField(labelHandle, 'name', prefix .. '.label')
    local t = scene.addComponent(labelHandle, 'core.Text')
    scene.setField(t, 'font', uiFont)
    scene.setField(t, 'flags', TextFlags.xLeft | TextFlags.yMiddle)
    addSize(labelHandle, 0, 0, 1)

    -- Trailing weighted spacer: shares the band 50:50 with the gray box and
    -- pins it to the left edge.
    addSpacer(bottom, prefix .. '.trailing', 0, 0, 1)

    -- Underline shared with the list items.
    addQuad(handle, prefix .. '.rule', width or 240, LINE_H, COLOR_LINE, Z_LINE)

    groupTree[id] = {
        labelId = labelId,
        grayBackId = grayBackId,
        markup = nil,
    }

    return handle, id
end

-- Natural item box height, for callers composing lists with flex gaps.
config.ITEM_HEIGHT = ITEM_H

-------------------------------------------------------------------------------
-- Derived-entity builders (used by the systems below) ------------------------
-------------------------------------------------------------------------------

-- Fills or empties the reserved diamond box with a single 5x5 white square
-- sprite, shown while the value differs from its default.
local function ensureDiamond(tree, changed)
    local boxHandle = scene.entityFromId(tree.diamondBoxId)
    if boxHandle == nil then
        return
    end

    while #tree.diamondQuads > (changed and 1 or 0) do
        local id = table.remove(tree.diamondQuads)
        local quadHandle = scene.entityFromId(id)
        if quadHandle ~= nil then
            ecs.destroyEntity(quadHandle)
        end
    end
    if changed then
        for _ = #tree.diamondQuads + 1, 1 do
            local _, id = addQuad(boxHandle, tree.prefix .. '.diamond.square',
                DIAMOND_SIZE, DIAMOND_SIZE, COLOR_DIAMOND, Z_DIAMOND)
            tree.diamondQuads[#tree.diamondQuads + 1] = id
        end
    end
end

-------------------------------------------------------------------------------
-- Systems --------------------------------------------------------------------
-------------------------------------------------------------------------------

-- List item system: mirrors text/selected/changed into the derived tree.
-- Runs before 'ui.config.Control' so a freshly attached enum draws this frame.
ecs.registerSystem('ui.config.ListItem', {
    after = { 'core.LegacyGame' },
    before = { 'ui.config.Control', 'core.ScaledFrame' },
}, function()
    pruneTrees()

    ecs.each('ui.config.ListItem', function(itemId)
        local tree = itemTree[itemId]
        if tree == nil then
            return
        end
        local handle = scene.entityFromId(itemId)
        if handle == nil then
            return
        end

        local text = ecs.get(itemId, 'ui.config.ListItem', 'text')
        local selected = ecs.get(itemId, 'ui.config.ListItem', 'selected')
        local changed = ecs.get(itemId, 'ui.config.ListItem', 'changed')

        -- Label markup follows the selection brightness.
        local markup = '<span color="'
            .. (selected and COLOR_ITEM_ON or COLOR_ITEM_OFF)
            .. '">' .. text .. '</span>'
        if tree.markup ~= markup then
            local labelHandle = scene.entityFromId(tree.labelId)
            if labelHandle ~= nil then
                scene.setField(scene.component(labelHandle, 'core.Text'), 'text', markup)
            end
            tree.markup = markup
        end

        -- Selection chrome is built eagerly in addListItem; show/hide it by
        -- toggling the rects' flex weight (weight 0 -> 0 size -> hidden, weight
        -- 1 -> fills the box). Tree-depth paint order already layers them
        -- ring (back) -> fill -> content (front), so no z is needed.
        local ringHandle = scene.entityFromId(tree.ringId)
        if ringHandle ~= nil then
            scene.setField(scene.component(ringHandle, 'core.UiSize'), 'weight', selected and 1 or 0)
        end
        local fillHandle = scene.entityFromId(tree.fillId)
        if fillHandle ~= nil then
            scene.setField(scene.component(fillHandle, 'core.UiSize'), 'weight', selected and 1 or 0)
        end

        -- Changed diamond fills its reserved slot while the value differs
        -- from the default.
        ensureDiamond(tree, changed)
    end)
end)

-- Group header system: mirrors the text field into the white category label.
-- Runs alongside the list item system so a freshly attached group draws this frame.
ecs.registerSystem('ui.config.ListGroup', { before = { 'ui.config.Control' } }, function()
    for gid in pairs(groupTree) do
        if scene.entityFromId(gid) == nil then
            groupTree[gid] = nil
        end
    end

    ecs.each('ui.config.ListGroup', function(gid)
        local tree = groupTree[gid]
        if tree == nil then
            return
        end
        local handle = scene.entityFromId(gid)
        if handle == nil then
            return
        end

        local text = ecs.get(gid, 'ui.config.ListGroup', 'text')
        local markup = '<span color="' .. COLOR_TEXT .. '">' .. text .. '</span>'
        if tree.markup ~= markup then
            local labelHandle = scene.entityFromId(tree.labelId)
            if labelHandle ~= nil then
                scene.setField(scene.component(labelHandle, 'core.Text'), 'text', markup)
            end
            tree.markup = markup
        end
    end)
end)

-- Enum system: mirrors text/count/selected_index into the enum's label and
-- indicator bars. Bar entities are reused in place as the count changes.
-- (Named ui.config.Control after the Enum + Range value controls it renders;
-- the bare 'ui.config' name belongs to the module/namespace.)
ecs.registerSystem('ui.config.Control', {}, function()
    pruneTrees()

    ecs.each('ui.config.Enum', function(enumId)
        local tree = enumTree[enumId]
        if tree == nil then
            return
        end

        local text = ecs.get(enumId, 'ui.config.Enum', 'text')
        local count = math.max(0, ecs.get(enumId, 'ui.config.Enum', 'count'))
        local selectedIndex = math.max(1, math.min(count, ecs.get(enumId, 'ui.config.Enum', 'selected_index')))

        -- Centred value label between the arrows.
        local markup = '<span color="' .. COLOR_TEXT .. '">' .. text .. '</span>'
        if tree.markup ~= markup then
            local labelHandle = scene.entityFromId(tree.labelId)
            if labelHandle ~= nil then
                scene.setField(scene.component(labelHandle, 'core.Text'), 'text', markup)
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
                local newHandle, newId = addQuad(barsRowHandle, tree.prefix .. '.bar' .. i,
                    BAR_W, BAR_H, lit and COLOR_BAR_LIT or COLOR_BAR_OFF, Z_BAR)
                tree.bars[i] = newId
                barHandle = newHandle
            else
                barHandle = scene.entityFromId(id)
            end
            if barHandle ~= nil then
                scene.setField(scene.component(barHandle, 'core.Sprite'), 'color',
                    lit and COLOR_BAR_LIT or COLOR_BAR_OFF)
            end
        end
    end)

    -- Range slider: rebuilds the distributed tick run when the mark count
    -- changes, then lights the current value's full-height marker.
    ecs.each('ui.config.Range', function(rangeId)
        local tree = rangeTree[rangeId]
        if tree == nil then
            return
        end

        local min = ecs.get(rangeId, 'ui.config.Range', 'min')
        local max = ecs.get(rangeId, 'ui.config.Range', 'max')
        local step = math.max(1, ecs.get(rangeId, 'ui.config.Range', 'step'))
        local value = ecs.get(rangeId, 'ui.config.Range', 'value')
        local count = math.floor((max - min) / step) + 1
        if count < 1 then
            count = 1
        end

        -- Rebuild the fixed mark + spacer run when the mark count changes
        -- (min/max/step differ). Children order: mark0, spacer1, mark1, ...,
        -- spacerN-1, markN-1. The leading mark's left edge sits on the slider
        -- left edge and the trailing mark's right edge on the right edge, so
        -- the ticks are distributed with a taller tick at each end. Heights
        -- are cross-axis and set later, so they never disturb x positions.
        if tree.count ~= count then
            for _, childId in ipairs(tree.flexOrder or {}) do
                local h = scene.entityFromId(childId)
                if h ~= nil then
                    ecs.destroyEntity(h)
                end
            end
            tree.flexOrder = {}
            tree.markHandles = {}
            local marksHandle = scene.entityFromId(tree.marksId)
            if marksHandle ~= nil then
                for i = 0, count - 1 do
                    if i > 0 then
                        local _, sid = addSpacer(marksHandle,
                            tree.prefix .. '.spacer' .. i, 0, 0, 1)
                        tree.flexOrder[#tree.flexOrder + 1] = sid
                    end
                    local mh, mid = addQuad(marksHandle,
                        tree.prefix .. '.mark' .. i, MARK_W, MARK_H * 2, COLOR_BAR_OFF, Z_MARK)
                    scene.setField(scene.component(mh, 'core.UiSize'), 'alignment', 'center')
                    tree.flexOrder[#tree.flexOrder + 1] = mid
                    tree.markHandles[i + 1] = mid
                end
            end
            tree.count = count
        end

        -- Light the current value's full-height marker; end ticks are taller
        -- than the middle ticks. Heights are cross-axis (vertical) so changing
        -- them does not move the horizontal tick positions.
        local currentPos = math.max(0, math.min(count - 1,
            math.floor((value - min) / step)))
        for i = 1, count do
            local pos = i - 1
            local h = scene.entityFromId(tree.markHandles[i])
            if h ~= nil then
                local isEnd = (pos == 0 or pos == count - 1)
                local isCurrent = (pos == currentPos)
                local hgt = isCurrent and RANGE_BLOCK_H
                    or (isEnd and END_MARK_H * 2 or MARK_H * 2)
                local size = scene.component(h, 'core.UiSize')
                if size ~= nil then
                    scene.setField(size, 'height', hgt)
                end
                local sprite = scene.component(h, 'core.Sprite')
                if sprite ~= nil then
                    scene.setField(sprite, 'color',
                        isCurrent and COLOR_BAR_LIT or COLOR_BAR_OFF)
                end
            end
        end

        -- End value text block.
        local text = ecs.get(rangeId, 'ui.config.Range', 'text') or ''
        local markup = '<span color="' .. COLOR_TEXT .. '">' .. text .. '</span>'
        if tree.markup ~= markup then
            local th = scene.entityFromId(tree.textId)
            if th ~= nil then
                scene.setField(scene.component(th, 'core.Text'), 'text', markup)
            end
            tree.markup = markup
        end
    end)
end)

return config