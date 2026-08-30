-- options.lua - OPTIONS overlay (games/re2/script/options.lua).
--
-- Rendered with the shared Lua-ECS controls (games/core/script/ui). Exposes
-- open()/isOpen()/close() so the RE2 menu can show it as a popup and park
-- itself underneath.
--
-- Run model: open() builds the whole screen under the 'user' root and the tick
-- handler keeps it in sync; close() destroys it. The tick is subscribed once
-- (the engine has no unsubscribe) and is inert while the overlay is closed.
--
-- All content is authored in a fixed 640x480 design space mapped onto the
-- window by a native scaled_frame ("contain"); the frame target is refreshed
-- every tick so resizes are tracked.

local ecs = require('ecs')
local config = require('ui.config')
local tabs = require('ui.tabs')

local DESIGN_W = 640
local DESIGN_H = 480

-- Role fonts follow the [options] "font" mode. config/tabs store the role
-- names so controls built after a live font switch resolve the new font.
config.setFont('big')
tabs.setFont('big')

local root = scene.getRoot('user')

-- Popup module state.
local isOpenFlag = false
local optionsHandle = nil
local canvasHandle = nil
local backdrop = nil
local frame = nil

-- Per-frame navigation snapshot. The input handler below reads the real
-- presses during the input walk (which runs before the script tick), then
-- clears the frame so the menu behind and the game fan-out receive nothing.
-- The tick drives navigation off this snapshot instead of input.pressed().
local nav = {
    up = false,
    down = false,
    left = false,
    right = false,
    cancel = false,
}

-- Built on open(); read by the tick while the overlay is open.
local CATALOG = {}
local activePanel = nil
local mainStripId = nil
local paneHeading = nil
local paneBody = nil
local ui, content, left, right, backRow, topSpacer
local lastMainSel = 1
local lastSubSel = 1

-------------------------------------------------------------------------------
-- Small builders -------------------------------------------------------------
-------------------------------------------------------------------------------

local function span(color, text)
    return '<span color="' .. color .. '">' .. text .. '</span>'
end

local function addSize(handle, w, h, weight)
    local size = scene.addComponent(handle, 'core.UiSize')
    scene.setField(size, 'width', w)
    scene.setField(size, 'height', h)
    if weight ~= nil then
        scene.setField(size, 'weight', weight)
    end
end

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

-- A text child of a flex container; position is owned by the parent flex.
local function addText(parentHandle, w, h, markup, small, flags, name)
    local handle = ecs.spawn(parentHandle)
    if name ~= nil then
        scene.setField(handle, 'name', name)
    end
    local t = scene.addComponent(handle, 'core.Text')
    scene.setField(t, 'font', small and require('ui.font').get('small') or require('ui.font').get('big'))
    scene.setField(t, 'text', markup)
    scene.setField(t, 'flags', flags or TextFlags.xCenter | TextFlags.yMiddle)
    addSize(handle, w, h)
    return handle, t
end

-- A solid-colour quad child centred on its own origin. Coordinates are in
-- the parent's local space.
local function addQuad(parentHandle, cx, cy, w, h, color, z, name)
    local handle = ecs.spawn(parentHandle)
    if name ~= nil then
        scene.setField(handle, 'name', name)
    end
    scene.setField(handle, 'x', cx)
    scene.setField(handle, 'y', cy)
    local sprite = scene.addComponent(handle, 'core.Sprite')
    scene.setField(sprite, 'color', color)
    scene.setField(handle, 'z', z)
    addSize(handle, w, h)
    return handle
end

-------------------------------------------------------------------------------
-- The option catalog ---------------------------------------------------------
-------------------------------------------------------------------------------

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

-- Builds CATALOG (category / subcategory / group / row tree) from the native
-- engine options registry (options.list). Each row wraps one registered Def and
-- its displayed value tracks the live options.get()/options.set() state.
local function buildCatalog()
    CATALOG = {}
    local defs = options.list() or {}
    local seenCat = {}
    for _, def in ipairs(defs) do
        if not seenCat[def.category] then
            seenCat[def.category] = true
            CATALOG[#CATALOG + 1] = { label = def.category, subs = {} }
        end
    end
    for _, cat in ipairs(CATALOG) do
        local seenSub = {}
        for _, def in ipairs(defs) do
            if def.category == cat.label and not seenSub[def.subcategory] then
                seenSub[def.subcategory] = true
                cat.subs[#cat.subs + 1] = { label = def.subcategory, rows = {} }
            end
        end
        for _, sub in ipairs(cat.subs) do
            sub.entries = {}
            sub.rows = {}
            local prevGroup = nil
            for _, def in ipairs(defs) do
                if def.category == cat.label and def.subcategory == sub.label then
                    -- A group header is inserted whenever the option's group
                    -- differs from the previous one (and is non-empty). Group
                    -- headers and value rows share one uniform-height window.
                    if def.group ~= '' and def.group ~= prevGroup then
                        sub.entries[#sub.entries + 1] = { kind = 'group', text = def.group }
                        prevGroup = def.group
                    end
                    local row = makeRow(def)
                    row.kind = 'row'
                    sub.entries[#sub.entries + 1] = row
                    row.eindex = #sub.entries
                    sub.rows[#sub.rows + 1] = row
                end
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
    error('options: not enough vertical space for one item row')
end
local VISIBLE_ROWS = VIEW_H / config.ITEM_HEIGHT
local ITEM_WIDTH = 290
local SCROLLBAR_W = 7       -- width reserved for the scrollbar column
local TRACK_W = 3
local COLOR_TRACK = { 70, 70, 70, 255 }
local COLOR_THUMB = { 180, 180, 180, 255 }

local function fitCanvas()
    scene.setField(frame, 'target.width', gfx.screenWidth())
    scene.setField(frame, 'target.height', gfx.screenHeight())
    scene.setField(canvasHandle, 'x', gfx.screenWidth() * 0.5)
    scene.setField(canvasHandle, 'y', gfx.screenHeight() * 0.5)
end

-- Per-frame navigation snapshot updated by the onInput handler (defined below
-- in the "Per-frame input" section). Forward-declared here so buildTree can
-- attach it as the overlay's InputHandler: Lua locals are only in scope from
-- their declaration, so a reference before `local function onInput` would
-- resolve to the global (nil) and silently clear the callback.
local onInput

-- Builds the canvas + static flex tree under a fresh root entity.
local function buildTree()
    -- Owner node for the whole overlay; both the backdrop and the scaled
    -- canvas hang off it so the popup is a single destroyable subtree.
    optionsHandle = scene.createEntity(root)
    scene.setField(optionsHandle, 'name', 'options')

    -- Input handler: reads the real nav presses during the input walk (which
    -- runs before the script tick), snapshots them for the tick, then clears
    -- the frame so the menu behind and the game fan-out receive nothing.
    local handler = scene.addComponent(optionsHandle, 'core.InputHandler')
    scene.setField(handler, 'callback', onInput)

    -- 96%-opacity black backdrop covering the whole screen. It is a child of
    -- the options owner (not the scaled canvas), so it is never letterboxed,
    -- and it is created before the canvas at the default z (0): the engine's
    -- stable z-sort keeps it in front of the already-built menu but behind the
    -- options UI, dimming the menu to ~4% underneath the popup.
    backdrop = scene.createEntity(optionsHandle)
    scene.setField(backdrop, 'name', 'backdrop')
    local backdropSprite = scene.addComponent(backdrop, 'core.Sprite')
    scene.setField(backdropSprite, 'color', { 0, 0, 0, 245 })
    scene.setField(backdrop, 'x', gfx.screenWidth() * 0.5)
    scene.setField(backdrop, 'y', gfx.screenHeight() * 0.5)
    addSize(backdrop, gfx.screenWidth(), gfx.screenHeight())

    -- Scaled canvas holding the design-space layout.
    canvasHandle = scene.createEntity(optionsHandle)
    scene.setField(canvasHandle, 'name', 'canvas')
    frame = scene.addComponent(canvasHandle, 'core.ScaledFrame')
    scene.setField(frame, 'source.width', DESIGN_W)
    scene.setField(frame, 'source.height', DESIGN_H)
    scene.setField(frame, 'mode', 'contain')
    scene.setField(frame, 'increment', 0)
    fitCanvas()

    -- Top-level vertical container filling the design frame.
    ui = ecs.spawn(canvasHandle)
    scene.setField(ui, 'name', 'ui')
    addSize(ui, DESIGN_W, DESIGN_H)
    addFlex(ui, 'vertical', 8)

    addText(ui, DESIGN_W, HEADING_H, span('#ffffff', 'OPTIONS'), nil, nil, 'heading')

    -- Main category strip; resized through its authored ui_size so the native
    -- strip chrome rides inside the flex layout.
    local mainStripHandle
    mainStripHandle, mainStripId = tabs.addStrip(ui, 0, 0, 1, 'main_strip')
    do
        local size = scene.component(mainStripHandle, 'core.UiSize')
        scene.setField(size, 'width', DESIGN_W)
        scene.setField(size, 'height', STRIP_H)
    end
    for _, cat in ipairs(CATALOG) do
        tabs.addTab(mainStripId, cat.label)
    end

    -- Content splits into the left controls column and the right hint pane.
    content = ecs.spawn(ui)
    scene.setField(content, 'name', 'content')
    addSize(content, DESIGN_W, CONTENT_H)
    addFlex(content, 'horizontal')

    left = ecs.spawn(content)
    scene.setField(left, 'name', 'left')
    addSize(left, 0, 0, 2)
    addFlex(left, 'vertical', 8)

    right = ecs.spawn(content)
    scene.setField(right, 'name', 'right')
    addSize(right, 0, 0, 1)
    addFlex(right, 'vertical', 8, 16)

    local paneHeadingHandle
    paneHeadingHandle, paneHeading = addText(right, DESIGN_W * 0.5 - 32, 20,
        '', false, TextFlags.xLeft | TextFlags.yMiddle, 'pane_heading')
    -- Weighted: stretches down the pane; yTop keeps the glyphs anchored high.
    do
        local handle = ecs.spawn(right)
        scene.setField(handle, 'name', 'pane_body')
        local t = scene.addComponent(handle, 'core.Text')
        scene.setField(t, 'font', require('ui.font').get('small'))
        scene.setField(t, 'text', '')
        scene.setField(t, 'flags', TextFlags.xLeft | TextFlags.yTop)
        addSize(handle, DESIGN_W * 0.5 - 32, 0, 1)
        paneBody = t
    end

    -- Spacer pins everything below to the bottom.
    topSpacer = ecs.spawn(ui)
    scene.setField(topSpacer, 'name', 'spacer')
    addSize(topSpacer, 0, 0, 1)

    -- Bottom middle: one hint - Back.
    backRow = ecs.spawn(ui)
    scene.setField(backRow, 'name', 'back_row')
    addSize(backRow, 70, BACK_H)
    addFlex(backRow, 'horizontal', 6)
    do
        local hintHandle = ecs.spawn(backRow)
        scene.setField(hintHandle, 'name', 'back_hint')
        scene.setField(hintHandle, 'scale.x', 0.5)
        scene.setField(hintHandle, 'scale.y', 0.5)
        local hint = scene.addComponent(hintHandle, 'core.InputHint')
        scene.setField(hint, 'device', 'ps')
        scene.setField(hint, 'command', 'cancel')
        scene.setField(hint, 'text', 'Back')
        addSize(hintHandle, 16, 16)
    end
end

-------------------------------------------------------------------------------
-- Panel building (one panel per main category, created on demand) ------------
-------------------------------------------------------------------------------

local function pushRow(v)
    local row = v.src
    ecs.set(v.id, 'ui.config.ListItem', 'text', row.label)
    ecs.set(v.id, 'ui.config.ListItem', 'changed',
        row.editable and row.index ~= row.defaultIndex)
    if row.def.kind == 'range' then
        ecs.set(v.controlId, 'ui.config.Range', 'min', row.def.min)
        ecs.set(v.controlId, 'ui.config.Range', 'max', row.def.max)
        ecs.set(v.controlId, 'ui.config.Range', 'step', row.def.step)
        ecs.set(v.controlId, 'ui.config.Range', 'value', indexToValue(row.def, row.index))
        ecs.set(v.controlId, 'ui.config.Range', 'text', tostring(row.options[row.index]))
    elseif row.editable then
        ecs.set(v.controlId, 'ui.config.Enum', 'count', #row.options)
        ecs.set(v.controlId, 'ui.config.Enum', 'selected_index', row.index)
        ecs.set(v.controlId, 'ui.config.Enum', 'text', tostring(row.options[row.index]))
    else
        -- External action entry: not value-editable.
        ecs.set(v.controlId, 'ui.config.Enum', 'count', 0)
        ecs.set(v.controlId, 'ui.config.Enum', 'selected_index', 1)
        ecs.set(v.controlId, 'ui.config.Enum', 'text', '')
    end
end

local function refreshSelection(sub)
    local sel = sub.rows[sub.cursor]
    for _, v in ipairs(sub.view) do
        if v.src ~= nil then
            ecs.set(v.id, 'ui.config.ListItem', 'selected', v.src == sel)
        end
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
    local last = math.min(#sub.entries, first + VISIBLE_ROWS - 1)
    for i = first, last do
        local entry = sub.entries[i]
        if entry.kind == 'group' then
            -- Group header: same height as a list item, never selectable.
            local handle, id = config.addListGroup(activePanel.list, 0, 0, ITEM_WIDTH, 'list_group')
            ecs.set(id, 'ui.config.ListGroup', 'text', entry.text)
            sub.view[#sub.view + 1] = { entry = entry, handle = handle, id = id }
        else
            local src = entry
            local handle, id, rowHandle = config.addListItem(activePanel.list, 0, 0, ITEM_WIDTH, 'list_item')
            local controlId
            if src.def.kind == 'range' then
                _, controlId = config.addRange(rowHandle, ITEM_WIDTH * 0.5, 'range')
            else
                _, controlId = config.addEnum(rowHandle, ITEM_WIDTH * 0.5, 'enum')
            end
            local v = { src = src, handle = handle, id = id, controlId = controlId }
            sub.view[#sub.view + 1] = v
            pushRow(v)
            ecs.set(id, 'ui.config.ListItem', 'selected', src == sub.rows[sub.cursor])
        end
    end

    -- Trailing weighted spacer. With no weighted child the vertical flex would
    -- centre a short run of rows in the leftover space; this absorbs that space
    -- so the rows always start at the top of the list.
    activePanel.filler = ecs.spawn(activePanel.list)
    scene.setField(activePanel.filler, 'name', 'list_filler')
    addSize(activePanel.filler, 0, 0, 1)
end

-- Clamps offset/cursor, rebuilds the visible row window and moves the thumb.
-- The list itself never moves: it is pinned to the scroll area. Scrolling is in
-- entry space (group headers + value rows, all one item tall).
local function updateScroll(sub)
    local entryCount = #sub.entries
    local maxOffset = math.max(0, entryCount - VISIBLE_ROWS)
    local cursorEntry = sub.rows[sub.cursor]
    local e = cursorEntry ~= nil and cursorEntry.eindex or 1
    if e < sub.offset + 1 then
        sub.offset = e - 1
        -- If a group header sits directly above the pinned row, pull the window
        -- up one more so the header stays on screen.
        if sub.offset > 0 and sub.entries[sub.offset] ~= nil
            and sub.entries[sub.offset].kind == 'group' then
            sub.offset = sub.offset - 1
        end
    elseif e > sub.offset + VISIBLE_ROWS then
        sub.offset = e - VISIBLE_ROWS
    else
        -- Cursor already sits inside the window. If a group header is the entry
        -- immediately above the window and the cursor is the topmost visible
        -- row, pull the window up one so the heading is shown. Without this, the
        -- first row of a group can never reveal its own heading once scrolled
        -- down: the cursor (which can never rest on the non-selectable header)
        -- keeps offset pinned at 1, so the very first group heading is lost.
        if sub.offset > 0 and e == sub.offset + 1 and sub.entries[sub.offset] ~= nil
            and sub.entries[sub.offset].kind == 'group' then
            sub.offset = sub.offset - 1
        end
    end
    if sub.offset > maxOffset then
        sub.offset = maxOffset
    elseif sub.offset < 0 then
        sub.offset = 0
    end

    rebuildList(sub)

    local thumbH = math.max(14,
        math.floor(VIEW_H * math.min(entryCount, VISIBLE_ROWS) / entryCount))
    local frac = maxOffset > 0 and sub.offset / maxOffset or 0
    local size = scene.component(sub.thumb, 'core.UiSize')
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
    local subStripHandle, subStripId = tabs.addStrip(left, 0, 0, 2, 'sub_strip')
    local subStripSize = scene.component(subStripHandle, 'core.UiSize')
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
    local track = addQuad(scrollbar, 0, 0, TRACK_W, VIEW_H, COLOR_TRACK, 0, 'scroll_track')
    local thumb = addQuad(scrollbar, 0, 0, TRACK_W, VIEW_H, COLOR_THUMB, 0, 'scroll_thumb')

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

-------------------------------------------------------------------------------
-- Popup lifecycle ------------------------------------------------------------
-------------------------------------------------------------------------------

-- Opens the overlay. `restore` is accepted for call-site compatibility but the
-- overlay always opens at its default category/scroll position (no persisted
-- state is kept between opens).
local function open(restore)
    if isOpenFlag then
        return
    end
    isOpenFlag = true
    buildCatalog()
    buildTree()
    buildPanel(CATALOG[1])
    refreshPane()
    print('[options] ready')
end

local function close()
    if not isOpenFlag then
        return
    end
    isOpenFlag = false
    if optionsHandle ~= nil then
        -- Destroying the owner tears down the backdrop, canvas and the whole
        -- UI subtree with it.
        scene.destroy(optionsHandle)
        optionsHandle = nil
    end
    canvasHandle = nil
    backdrop = nil
    frame = nil
    activePanel = nil
end

local function isOpen()
    return isOpenFlag
end

local function containsCommand(list, name)
    for i = 1, #list do
        if list[i] == name then
            return true
        end
    end
    return false
end

-- Input handler for the overlay: reads the real nav presses out of the frame
-- context, snapshots them for the tick (which runs after the walk), then
-- clears the frame so the menu behind and the game fan-out receive nothing.
-- (`function onInput` rather than `local function onInput` so this assigns to
-- the local forward-declared above, which buildTree closes over.)
function onInput(frame)
    nav.up = containsCommand(frame.press, 'up')
    nav.down = containsCommand(frame.press, 'down')
    nav.left = containsCommand(frame.press, 'left')
    nav.right = containsCommand(frame.press, 'right')
    nav.cancel = containsCommand(frame.press, 'cancel')
    -- Null the input: everything downstream (menu, game fan-out, other
    -- handlers) sees an empty frame this frame.
    frame.state = {}
    frame.press = {}
end

-------------------------------------------------------------------------------
-- Per-frame input ------------------------------------------------------------
-------------------------------------------------------------------------------

re.subscribe(HookKind.tick, function()
    if not isOpenFlag or activePanel == nil then
        return
    end
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

    -- Back/cancel closes the popup, flushing any pending value changes.
    if nav.cancel then
        options.flush()
        close()
        return
    end

    if nav.up then
        sub.cursor = wrap(sub.cursor, -1, #sub.rows)
        refreshSelection(sub)
        updateScroll(sub)
        refreshPane()
    elseif nav.down then
        sub.cursor = wrap(sub.cursor, 1, #sub.rows)
        refreshSelection(sub)
        updateScroll(sub)
        refreshPane()
    end

    if nav.left or nav.right then
        local v = cursorView(sub)
        if v ~= nil and v.src.editable then
            -- Value selection never wraps: clamp to the first/last option.
            local delta = nav.right and 1 or -1
            v.src.index = math.max(1, math.min(#v.src.options, v.src.index + delta))
            -- Persist into the native options registry.
            options.set(v.src.def.id, indexToValue(v.src.def, v.src.index))
            -- Re-read in case the value was clamped or snapped to a step grid.
            local value = options.get(v.src.def.id) or v.src.def.default
            v.src.index = valueToIndex(v.src.def, value)
            options.flush()
            -- The font mode drives the whole UI font catalogue: re-pin the
            -- roles so the native swap walk remaps live text this frame.
            if v.src.def.id == 'font' then
                require('ui.font').refresh()
            end
            -- Widescreen swaps the Screen Resolution list for the matching
            -- aspect ratio (and snaps the value natively): rebuild the
            -- catalog and panel so the row labels follow, then drop the
            -- nav processing for this frame.
            if v.src.def.id == 'widescreen' then
                buildCatalog()
                destroyPanel()
                buildPanel(CATALOG[mainSel])
                lastSubSel = 1
                refreshPane()
                return
            end
            pushRow(v)
        end
    end
end)

return {
    open = open,
    close = close,
    isOpen = isOpen,
}
