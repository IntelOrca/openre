-- config-demo - options screen skeleton built from the shared Lua-ECS
-- controls (games/core/script/ui).
--
-- Run with: OPENRE_GAME=config-demo run.bat (plus OPENRE_SCRIPTS for web.lua)
--
-- The whole screen is one declarative flex tree:
--
--   ui (vertical)
--   |- heading
--   |- main category tab strip
--   |- content (horizontal)
--   |  |- left (vertical): sub strip, scroll area (list | scrollbar), spacer
--   |  `- right (vertical, padding 16): hint text for the selected option
--   |- spacer
--   `- back hint row
--
-- Nothing is pre-created per tab panel: when the main tab changes the old
-- panel entities are destroyed and the new category's sub strip + scroll
-- area are built on demand. Switching a sub tab rebuilds the single shared
-- list with that sub's rows. The demo never computes control geometry - it
-- mirrors state into components, rebuilds the visible rows on scroll and
-- moves the scroll thumb.
--
-- Input: tab_left/tab_right switch main category, subtab_left/subtab_right
-- switch sub category, W/S or dpad pick a row (the list scrolls to keep the
-- cursor visible), A/D or left/right change value (written to the native
-- options registry), Escape restores the picked row to its default.

local ecs = require('ecs')
local config = require('ui.config')
local tabs = require('ui.tabs')

local DESIGN_W = 640
local DESIGN_H = 480

local stdFont = gfx.loadFont('fonts/std.fnt')
local smFont = gfx.loadFont('fonts/sm.fnt')
config.setFont(stdFont)
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

-------------------------------------------------------------------------------
-- Small builders -------------------------------------------------------------
-------------------------------------------------------------------------------

local function span(color, text)
    return '<span color="' .. color .. '">' .. text .. '</span>'
end

local function addSize(handle, w, h, weight)
    local size = scene.addComponent(handle, 'core.ui_size')
    scene.setField(size, 'width', w)
    scene.setField(size, 'height', h)
    if weight ~= nil then
        scene.setField(size, 'weight', weight)
    end
end

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

-- A text child of a flex container; position is owned by the parent flex.
local function addText(parentHandle, w, h, markup, small, flags)
    local handle = ecs.spawn(parentHandle)
    local t = scene.addComponent(handle, 'core.text')
    scene.setField(t, 'font', small and smFont or stdFont)
    scene.setField(t, 'text', markup)
    scene.setField(t, 'flags', flags or TextFlags.xCenter | TextFlags.yMiddle)
    addSize(handle, w, h)
    return handle, t
end

-- A solid-colour quad child centred on its own origin. Coordinates are in
-- the parent's local space.
local function addQuad(parentHandle, cx, cy, w, h, color, z)
    local handle = ecs.spawn(parentHandle)
    scene.setField(handle, 'x', cx)
    scene.setField(handle, 'y', cy)
    local sprite = scene.addComponent(handle, 'core.sprite')
    scene.setField(sprite, 'color', color)
    scene.setField(sprite, 'z', z)
    addSize(handle, w, h)
    return handle
end

-------------------------------------------------------------------------------
-- The option catalog ---------------------------------------------------------
-------------------------------------------------------------------------------

-- The category / subcategory / row tree is built from the native engine
-- options registry (options.list). Each row wraps one registered Def and its
-- displayed value is kept in sync with the live options.get()/options.set()
-- state.
--
-- A row carries:
--   * def          - the OptionsDef returned by options.list()
--   * label/desc   - display title / tooltip (def.title / def.tooltip)
--   * options      - display strings for the value cycle (the label list for
--                    toggle/enum, or a ranged numeric list for range)
--   * index        - 1-based position within `options` for the current value
--   * defaultIndex - 1-based position of the default value
--   * editable     - false for "external" entries (actions like "Reset HUD")

-- Map a native option value onto its 1-based option-list index.
local function valueToIndex(def, value)
    if def.kind == 'range' then
        return math.floor((value - def.min) / def.step) + 1
    end
    -- toggle / enum: value is the 0-based label index.
    return value + 1
end

-- Map a row's 1-based option-list index back onto its native option value.
local function indexToValue(def, index)
    if def.kind == 'range' then
        return def.min + (index - 1) * def.step
    end
    return index - 1
end

local function makeRow(def)
    local value = options.get(def.id) or def.default
    local row = {
        def = def,
        label = def.title,
        desc = def.tooltip,
    }
    if def.kind == 'toggle' or def.kind == 'enum' then
        row.options = {}
        for i, label in ipairs(def.labels) do
            row.options[i] = label
        end
        row.editable = true
    elseif def.kind == 'range' then
        row.options = {}
        local n = 0
        for v = def.min, def.max, def.step do
            n = n + 1
            row.options[n] = tostring(v)
        end
        row.editable = true
    else
        -- external action entry: not value-editable.
        row.options = {}
        row.editable = false
    end
    row.index = valueToIndex(def, value)
    row.defaultIndex = valueToIndex(def, def.default)
    return row
end

local defs = options.list() or {}

-- Unique main categories in registration order.
local CATALOG = {}
local seenCat = {}
for _, def in ipairs(defs) do
    if not seenCat[def.category] then
        seenCat[def.category] = true
        CATALOG[#CATALOG + 1] = { label = def.category, subs = {} }
    end
end

-- Unique subcategories per category (registration order), each populated with
-- its rows in registration order.
for _, cat in ipairs(CATALOG) do
    local seenSub = {}
    for _, def in ipairs(defs) do
        if def.category == cat.label and not seenSub[def.subcategory] then
            seenSub[def.subcategory] = true
            cat.subs[#cat.subs + 1] = { label = def.subcategory, rows = {} }
        end
    end
    for _, sub in ipairs(cat.subs) do
        for _, def in ipairs(defs) do
            if def.category == cat.label and def.subcategory == sub.label then
                sub.rows[#sub.rows + 1] = makeRow(def)
            end
        end
    end
end

-------------------------------------------------------------------------------
-- Static flex tree -----------------------------------------------------------
-------------------------------------------------------------------------------

local HEADING_H = 24
local STRIP_H = 28          -- main + sub tab strip band height
local CONTENT_H = 372
local LIST_GAP = 8          -- vertical flex gap of the left column (see addFlex(left))
local BACK_H = 20

-- Fill the left column vertically: take the space left after the sub strip and
-- the flex gaps, then drop any partial row so the scroll area height is an
-- exact multiple of the item height.
local usableH = CONTENT_H - STRIP_H - 2 * LIST_GAP
local VIEW_H = math.floor(usableH / config.ITEM_HEIGHT) * config.ITEM_HEIGHT
if VIEW_H <= 0 then
    error('config-demo: not enough vertical space for one item row')
end
local VISIBLE_ROWS = VIEW_H / config.ITEM_HEIGHT
local ITEM_WIDTH = 290
local SCROLLBAR_W = 7       -- width reserved for the scrollbar column
local TRACK_W = 3
local COLOR_TRACK = { 70, 70, 70, 255 }
local COLOR_THUMB = { 180, 180, 180, 255 }

-- Top-level vertical container filling the design frame.
local ui = ecs.spawn(canvasHandle)
scene.setField(ui, 'name', 'ui')
addSize(ui, DESIGN_W, DESIGN_H)
addFlex(ui, 'vertical', 8)

addText(ui, DESIGN_W, HEADING_H, span('#ffffff', 'OPTIONS'))

-- Main category strip; resized through its authored ui_size so the native
-- strip chrome rides inside the flex layout.
local mainStripHandle, mainStripId = tabs.addStrip(ui, 0, 0, 1)
do
    local size = scene.component(mainStripHandle, 'core.ui_size')
    scene.setField(size, 'width', DESIGN_W)
    scene.setField(size, 'height', STRIP_H)
end
for _, cat in ipairs(CATALOG) do
    tabs.addTab(mainStripId, cat.label)
end

-- Content splits into the left controls column and the right hint pane.
local content = ecs.spawn(ui)
scene.setField(content, 'name', 'content')
addSize(content, DESIGN_W, CONTENT_H)
addFlex(content, 'horizontal')

local left = ecs.spawn(content)
scene.setField(left, 'name', 'left')
addSize(left, 0, 0, 2)
addFlex(left, 'vertical', 8)

local right = ecs.spawn(content)
scene.setField(right, 'name', 'right')
addSize(right, 0, 0, 1)
addFlex(right, 'vertical', 8, 16)

local paneHeadingHandle, paneHeading = addText(right, DESIGN_W * 0.5 - 32, 20,
    '', false, TextFlags.xLeft | TextFlags.yMiddle)
-- Weighted: stretches down the pane; yTop keeps the glyphs anchored high.
local paneBody = nil
do
    local handle = ecs.spawn(right)
    local t = scene.addComponent(handle, 'core.text')
    scene.setField(t, 'font', smFont)
    scene.setField(t, 'text', '')
    scene.setField(t, 'flags', TextFlags.xLeft | TextFlags.yTop)
    addSize(handle, DESIGN_W * 0.5 - 32, 0, 1)
    paneBody = t
end

-- Spacer pins everything below to the bottom.
local topSpacer = ecs.spawn(ui)
scene.setField(topSpacer, 'name', 'spacer')
addSize(topSpacer, 0, 0, 1)

-- Bottom middle: one hint - Back.
local backRow = ecs.spawn(ui)
scene.setField(backRow, 'name', 'back_row')
addSize(backRow, 70, BACK_H)
addFlex(backRow, 'horizontal', 6)
do
    local hintHandle = ecs.spawn(backRow)
    scene.setField(hintHandle, 'scale.x', 0.5)
    scene.setField(hintHandle, 'scale.y', 0.5)
    local hint = scene.addComponent(hintHandle, 'core.input_hint')
    scene.setField(hint, 'device', 'ps')
    scene.setField(hint, 'command', 'cancel')
    scene.setField(hint, 'text', 'Back')
    addSize(hintHandle, 16, 16)
end

-------------------------------------------------------------------------------
-- Panel building (one panel per main category, created on demand) ------------
-------------------------------------------------------------------------------

local activePanel = nil

local function pushRow(v)
    local row = v.src
    ecs.set(v.id, 'ui.config_list_item', 'text', row.label)
    ecs.set(v.id, 'ui.config_list_item', 'changed',
        row.editable and row.index ~= row.defaultIndex)
    if row.def.kind == 'range' then
        ecs.set(v.controlId, 'ui.config_range', 'min', row.def.min)
        ecs.set(v.controlId, 'ui.config_range', 'max', row.def.max)
        ecs.set(v.controlId, 'ui.config_range', 'step', row.def.step)
        ecs.set(v.controlId, 'ui.config_range', 'value', indexToValue(row.def, row.index))
        ecs.set(v.controlId, 'ui.config_range', 'text', tostring(row.options[row.index]))
    elseif row.editable then
        ecs.set(v.controlId, 'ui.config_enum', 'count', #row.options)
        ecs.set(v.controlId, 'ui.config_enum', 'selected_index', row.index)
        ecs.set(v.controlId, 'ui.config_enum', 'text', tostring(row.options[row.index]))
    else
        -- External action entry: not value-editable.
        ecs.set(v.controlId, 'ui.config_enum', 'count', 0)
        ecs.set(v.controlId, 'ui.config_enum', 'selected_index', 1)
        ecs.set(v.controlId, 'ui.config_enum', 'text', '')
    end
end

local function refreshSelection(sub)
    local sel = sub.rows[sub.cursor]
    for _, v in ipairs(sub.view) do
        ecs.set(v.id, 'ui.config_list_item', 'selected', v.src == sel)
    end
end

-- The view entry backing the cursor, if that row is currently materialised.
local function cursorView(sub)
    local src = sub.rows[sub.cursor]
    for _, v in ipairs(sub.view) do
        if v.src == src then
            return v
        end
    end
    return nil
end

-- Rebuilds the shared list so it holds exactly the VISIBLE_ROWS window
-- starting at sub.offset. There is no clipping, so scrolled-out rows must
-- not exist at all rather than being shifted outside the viewport.
local function rebuildList(sub)
    for _, v in ipairs(sub.view or {}) do
        ecs.destroyEntity(v.handle)
    end
    if activePanel.filler ~= nil then
        ecs.destroyEntity(activePanel.filler)
        activePanel.filler = nil
    end
    sub.view = {}

    local first = sub.offset + 1
    local last = math.min(#sub.rows, first + VISIBLE_ROWS - 1)
    for i = first, last do
        local src = sub.rows[i]
        local handle, id, rowHandle = config.addListItem(activePanel.list, 0, 0, ITEM_WIDTH)
        local controlId
        if src.def.kind == 'range' then
            _, controlId = config.addRange(rowHandle, ITEM_WIDTH * 0.5)
        else
            _, controlId = config.addEnum(rowHandle, ITEM_WIDTH * 0.5)
        end
        local v = { src = src, handle = handle, id = id, controlId = controlId }
        sub.view[#sub.view + 1] = v
        pushRow(v)
        ecs.set(id, 'ui.config_list_item', 'selected', src == sub.rows[sub.cursor])
    end

    -- Trailing weighted spacer. With no weighted child the vertical flex would
    -- centre a short run of rows in the leftover space; this absorbs that space
    -- so the rows always start at the top of the list.
    activePanel.filler = ecs.spawn(activePanel.list)
    addSize(activePanel.filler, 0, 0, 1)
end

-- Clamps offset/cursor, rebuilds the visible row window and moves the thumb.
-- The list itself never moves: it is pinned to the scroll area.
local function updateScroll(sub)
    local rows = #sub.rows
    local maxOffset = math.max(0, rows - VISIBLE_ROWS)
    if sub.offset > maxOffset then
        sub.offset = maxOffset
    elseif sub.offset < 0 then
        sub.offset = 0
    end
    if sub.cursor < sub.offset + 1 then
        sub.offset = sub.cursor - 1
    elseif sub.cursor > sub.offset + VISIBLE_ROWS then
        sub.offset = sub.cursor - VISIBLE_ROWS
    end

    rebuildList(sub)

    local thumbH = math.max(14,
        math.floor(VIEW_H * math.min(rows, VISIBLE_ROWS) / rows))
    local frac = maxOffset > 0 and sub.offset / maxOffset or 0
    local size = scene.component(sub.thumb, 'core.ui_size')
    scene.setField(size, 'height', thumbH)
    scene.setField(sub.thumb, 'y', frac * (VIEW_H - thumbH) - VIEW_H * 0.5 + thumbH * 0.5)
end

local function refreshPane()
    local sub = activePanel.subs[tabs.selectedIndex(activePanel.subStripId)]
    local row = sub.rows[sub.cursor]
    scene.setField(paneHeading, 'text', span('#ffffff', row.label))
    scene.setField(paneBody, 'text', span('#9a9a9a', row.desc))
end

-- Shows `sub`'s rows in the single shared list. The previously shown sub's
-- rows are destroyed first (they live in the same list), then `sub` is
-- rebuilt and scrolled into view.
local function activateSub(sub)
    local prev = activePanel.activeSub
    if prev ~= nil and prev ~= sub then
        for _, v in ipairs(prev.view or {}) do
            ecs.destroyEntity(v.handle)
        end
        prev.view = {}
    end
    activePanel.activeSub = sub
    updateScroll(sub)
    refreshSelection(sub)
end

-- Builds one main category's entities under the static tree. Everything is
-- parented to two roots so switching categories destroys exactly two nodes.
local function buildPanel(cat)
    local subStripHandle, subStripId = tabs.addStrip(left, 0, 0, 2)
    local subStripSize = scene.component(subStripHandle, 'core.ui_size')
    scene.setField(subStripSize, 'width', DESIGN_W * 0.5)
    scene.setField(subStripSize, 'height', STRIP_H)
    for _, sub in ipairs(cat.subs) do
        tabs.addTab(subStripId, sub.label)
    end

    -- Scroll area: a horizontal flex of [list (weight 1) | scrollbar
    -- (weight 0)]. A single list is rebuilt per sub category; the scrollbar
    -- holds the shared track and thumb.
    local scrollArea = ecs.spawn(left)
    scene.setField(scrollArea, 'name', 'scroll_area')
    addSize(scrollArea, DESIGN_W * 0.5, VIEW_H)
    addFlex(scrollArea, 'horizontal', 2)

    -- Single list: a vertical flex (weight 1) rebuilt for the active sub.
    local list = ecs.spawn(scrollArea)
    scene.setField(list, 'name', 'list')
    addSize(list, 0, 0, 1)
    addFlex(list, 'vertical', 0)

    -- Scrollbar (weight 0): an overlay box holding the track and thumb.
    local scrollbar = ecs.spawn(scrollArea)
    scene.setField(scrollbar, 'name', 'scrollbar')
    addSize(scrollbar, SCROLLBAR_W, 0)
    local track = addQuad(scrollbar, 0, 0, TRACK_W, VIEW_H, COLOR_TRACK, 50)
    local thumb = addQuad(scrollbar, 0, 0, TRACK_W, VIEW_H, COLOR_THUMB, 40)

    -- Trailing weighted spacer keeps the fixed-height children top-packed.
    local filler = ecs.spawn(left)
    scene.setField(filler, 'name', 'filler')
    addSize(filler, 0, 0, 1)
    for _, sub in ipairs(cat.subs) do
        sub.track = track
        sub.thumb = thumb
        sub.cursor = 1
        sub.offset = 0
        sub.view = {}
    end

    tabs.setSelectedIndex(subStripId, 1)

    activePanel = {
        cat = cat,
        subStripId = subStripId,
        subs = cat.subs,
        list = list,
        scrollbar = scrollbar,
        activeSub = nil,
        filler = nil,
        roots = { scene.entityFromId(subStripId), scrollArea, filler },
    }
    activateSub(cat.subs[1])
end

local function destroyPanel()
    if activePanel == nil then
        return
    end
    for _, handle in ipairs(activePanel.roots) do
        scene.destroy(handle)
    end
    for _, sub in ipairs(activePanel.cat.subs) do
        sub.track = nil
        sub.thumb = nil
        sub.view = nil
    end
    activePanel = nil
end

buildPanel(CATALOG[1])
refreshPane()

print('[config-demo] ready')

-------------------------------------------------------------------------------
-- Per-frame input ------------------------------------------------------------
-------------------------------------------------------------------------------

local lastMainSel = 1
local lastSubSel = 1

re.subscribe(HookKind.tick, function()
    fitCanvas()

    -- Tab selection is written by the native systems after this hook, so
    -- these reads lag one frame; fine for a demo.
    local mainSel = tabs.selectedIndex(mainStripId)
    if mainSel ~= lastMainSel then
        lastMainSel = mainSel
        destroyPanel()
        buildPanel(CATALOG[mainSel])
        lastSubSel = 1
        refreshPane()
    end

    local subSel = tabs.selectedIndex(activePanel.subStripId)
    if subSel ~= lastSubSel then
        lastSubSel = subSel
        activateSub(activePanel.subs[subSel])
        refreshPane()
    end

    local sub = activePanel.subs[subSel]

    local function wrap(value, delta, count)
        return ((value - 1 + delta) % count) + 1
    end

    if input.pressed('up') then
        sub.cursor = wrap(sub.cursor, -1, #sub.rows)
        refreshSelection(sub)
        updateScroll(sub)
        refreshPane()
    elseif input.pressed('down') then
        sub.cursor = wrap(sub.cursor, 1, #sub.rows)
        refreshSelection(sub)
        updateScroll(sub)
        refreshPane()
    end

    if input.pressed('left') or input.pressed('right') then
        local v = cursorView(sub)
        if v ~= nil and v.src.editable then
            -- Value selection never wraps: clamp to the first/last option.
            local delta = input.pressed('right') and 1 or -1
            v.src.index = math.max(1, math.min(#v.src.options, v.src.index + delta))
            -- Persist into the native options registry.
            options.set(v.src.def.id, indexToValue(v.src.def, v.src.index))
            -- Re-read in case the value was clamped or snapped to a step grid.
            local value = options.get(v.src.def.id) or v.src.def.default
            v.src.index = valueToIndex(v.src.def, value)
            options.flush()
            pushRow(v)
        end
    end

    -- Restore the picked row to its default so the changed diamond goes.
    if input.pressed('cancel') then
        local v = cursorView(sub)
        if v ~= nil and v.src.editable then
            v.src.index = v.src.defaultIndex
            options.set(v.src.def.id, indexToValue(v.src.def, v.src.index))
            options.flush()
            pushRow(v)
        end
        refreshPane()
    end
end)
