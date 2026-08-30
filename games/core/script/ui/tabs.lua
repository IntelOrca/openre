-- ui/tabs.lua - shared tab strip control built on the Lua ECS (see
-- games/core/script/ecs.lua).
--
-- A game only creates the entities and components:
--
--     local strip = tabs.addStrip(root, x, y)   -- ui.TabStrip
--     local tab   = tabs.addTab(stripId, 'HUD') -- ui.Tab
--
-- Everything else is produced by the registered 'ui.TabStrip' system: the LB/RB
-- input hints flanking the strip, the grey rule under the whole row, each
-- tab's label text and the white underline segment marking the selected tab.
-- State lives in the binary component rows: ui.TabStrip carries
-- selected_index plus the two hint command names, ui.Tab carries the label
-- and its mirrored `selected` bool.
--
-- Layout model (pure flex): addStrip builds a declarative tree -
--
--     strip (vertical)
--     |- row (weight 25, horizontal): hint | band (weighted) | hint
--     `- bottom (weight 3, vertical)
--        |- spacer
--        `- rule area ('none'): grey rule + cell band overlaid
--
-- addTab appends a weighted tab into the band and a matching weighted
-- underline cell into the cell band, so the native UiFlexSystem gives
-- every tab an equal share of the band and places each cell exactly under
-- its tab. The selected tab's label is white, the rest grey, and only the
-- selected cell is opaque white - a thicker underline drawn centred over
-- the grey rule. Level 1 strips draw slot separators: the first tab owns a
-- left edge too, every other boundary is covered by exactly one separator.
-- Level 2 (sub-row) strips draw none and are driven by
-- subtab_left/subtab_right.
--
-- All text uses the shared UI font set once via tabs.setFont(); the standard
-- font everywhere is deliberate.

local ecs = require('ecs')

local tabs = {}

-- Per-frame selection input snapshot. Each strip entity gets a tiny
-- core.InputHandler that runs during the input walk (deepest-first, so any
-- strip is visited before an overlay owner nulls the frame) and records
-- whether the strip's left/right select commands were pressed. The system
-- consumes this snapshot instead of the removed global input.pressed().
--   stripNav[stripId] = { left = bool, right = bool }
local stripNav = {}

local function containsCommand(list, name)
    for i = 1, #list do
        if list[i] == name then
            return true
        end
    end
    return false
end

-------------------------------------------------------------------------------
-- Layout / style constants ---------------------------------------------------
-------------------------------------------------------------------------------

local STRIP_W = 560      -- default strip width; creators may resize
local STRIP_H = 28       -- strip band height (authored)
local HINT_SIZE = 16     -- LB/RB hint icons at 0.5 scale
local ROW_GAP = 4        -- gap between hints and the tab band
-- The tab band is inset from the strip edges by a hint plus one gap on each
-- side; the underline cell band must use the same inset to stay aligned.
local SIDE_INSET = HINT_SIZE + ROW_GAP
local SEP_W = 1
local SEP_H = 18

local COLOR_SEPARATOR = { 150, 150, 150, 200 }
local COLOR_RULE = { 132, 132, 132, 220 }
local COLOR_UNDERLINE = { 255, 255, 255, 255 }
local COLOR_HIDDEN = { 0, 0, 0, 0 }

local SELECTED_COLOR = '#ffffff'
local DIM_COLOR = '#9a9a9a'

-- Decorative rects sit behind text glyph sprites (which render at z = 0);
-- a higher z renders further back. The underline cell draws above the rule
-- so the selected segment reads as a brighter, thicker part of it.
local Z_RULE = 0
local Z_SEPARATOR = 0
local Z_UNDERLINE = 0

-------------------------------------------------------------------------------
-- Components -----------------------------------------------------------------
-------------------------------------------------------------------------------

ecs.registerComponent('ui.TabStrip', {
    { name = 'selected_index', type = 'int', default = 1 },
    -- 1 = outer tab row: separators drawn, tab_left/tab_right commands.
    -- 2 = sub-row: no separators, operated with the subtab_left/subtab_right
    -- commands (addStrip presets the command fields accordingly).
    { name = 'level', type = 'int', default = 1 },
    { name = 'hint_command_l', type = 'string', default = 'tab_left' },
    { name = 'hint_command_r', type = 'string', default = 'tab_right' },
})

ecs.registerComponent('ui.Tab', {
    { name = 'label', type = 'string', default = '' },
    { name = 'selected', type = 'bool', default = false },
})

-------------------------------------------------------------------------------
-- Small builders -------------------------------------------------------------
-------------------------------------------------------------------------------

-- The one font used by every strip; set before creating strips. Either a font
-- handle or a role name ('big'/'small') re-resolved per strip (see
-- resolveUiFont below) so strips created after a live font switch track it.
local uiFont = nil

---@param font Font|string A gfx.loadFont handle, or a role name ('big'/'small').
function tabs.setFont(font)
    uiFont = font
end

-- Role names map onto the current mode's font via ui.font (the core role
-- catalogue pins it into the native registry); plain handles are used as-is.
local function resolveUiFont()
    if type(uiFont) == 'string' then
        return require('ui.font').get(uiFont)
    end
    return uiFont
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

local function addQuad(parentHandle, name, w, h, color, z, weight)
    local handle = ecs.spawn(parentHandle)
    scene.setField(handle, 'name', name)
    local sprite = scene.addComponent(handle, 'core.Sprite')
    scene.setField(sprite, 'color', color)
    scene.setField(handle, 'z', z)
    addSize(handle, w, h, weight)
    return handle
end

-------------------------------------------------------------------------------
-- Creation helpers (what a game calls) ---------------------------------------
-------------------------------------------------------------------------------

-- Per-frame view state, kept out of the binary rows: these describe derived
-- entities rather than authored data.
--   stripViews[stripId] = { bandId, cellsId, cellIds{}, hintL, hintR }
--   tabViews[tabId]     = { labelId, markup }
local stripViews = {}
local tabViews = {}

--- Create a tab strip entity under `parent` at the given canvas-space
--- centre. `level` 1 (default) is an outer tab row: separators are drawn
--- and it is operated with the tab_left/tab_right commands. Level 2 is a
--- sub-row: no separators, operated with subtab_left/subtab_right. Returns
--- the native handle and numeric id. The strip authors a default 560x28
--- box; resize it through its core.UiSize afterwards if needed.
---@param parent SceneEntity
---@param x number Canvas-space centre X (offset from the parent origin).
---@param y number Canvas-space centre Y.
---@param level? integer 1 (default) or 2.
---@param name? string Override for the entity-name prefix (default `ui.TabStrip`).
---@return SceneEntity handle
---@return integer id
function tabs.addStrip(parent, x, y, level, name)
    if level ~= 2 then
        level = 1
    end
    local prefix = name or 'ui.TabStrip'
    local handle, id = ecs.spawn(parent)
    assert(id ~= nil, 'ui.tabs.addStrip: spawn failed')
    scene.setField(handle, 'name', prefix)
    scene.setField(handle, 'x', x)
    scene.setField(handle, 'y', y)
    ecs.addComponent(id, 'ui.TabStrip')
    ecs.set(id, 'ui.TabStrip', 'level', level)
    local cmdL, cmdR = 'tab_left', 'tab_right'
    if level == 2 then
        cmdL, cmdR = 'subtab_left', 'subtab_right'
    end
    ecs.set(id, 'ui.TabStrip', 'hint_command_l', cmdL)
    ecs.set(id, 'ui.TabStrip', 'hint_command_r', cmdR)
    addSize(handle, STRIP_W, STRIP_H)
    addFlex(handle, 'vertical')

    -- Tab row: hints hug the ends, the weighted band between them holds the
    -- tabs and stretches to fill.
    local row = ecs.spawn(handle)
    scene.setField(row, 'name', prefix .. '.row')
    addSize(row, 0, 0, 25)
    addFlex(row, 'horizontal', ROW_GAP)

    local function addHint(key, command)
        local hintHandle, hintId = ecs.spawn(row)
        scene.setField(hintHandle, 'name', prefix .. '.' .. key)
        scene.setField(hintHandle, 'scale.x', 0.5)
        scene.setField(hintHandle, 'scale.y', 0.5)
        local hint = scene.addComponent(hintHandle, 'core.InputHint')
        scene.setField(hint, 'device', 'ps')
        scene.setField(hint, 'command', command)
        addSize(hintHandle, HINT_SIZE, HINT_SIZE)
        return hintHandle, hintId
    end
    local hintLHandle, hintLId = addHint('hintL', cmdL)

    -- Band between the hints: creation order defines the flex order.
    local band, bandId = ecs.spawn(row)
    scene.setField(band, 'name', prefix .. '.band')
    addSize(band, 0, 0, 1)
    addFlex(band, 'horizontal')

    local hintRHandle, hintRId = addHint('hintR', cmdR)

    -- Bottom zone: an overlay area holding the grey rule and, on top of it,
    -- the per-tab selection underline. A spacer pushes the overlay low so it
    -- reads as sitting under the tabs. Both layers are weighted children of
    -- the 'none' container, so they fill the same box and stack by z order.
    local bottom = ecs.spawn(handle)
    scene.setField(bottom, 'name', prefix .. '.bottom')
    addSize(bottom, 0, 0, 3)
    addFlex(bottom, 'vertical')
    local bottomSpacer = ecs.spawn(bottom)
    scene.setField(bottomSpacer, 'name', prefix .. '.bottom_spacer')
    addSize(bottomSpacer, 0, 0, 1)
    local ruleArea = ecs.spawn(bottom)
    scene.setField(ruleArea, 'name', prefix .. '.rule_area')
    addSize(ruleArea, 0, 0, 1)
    addFlex(ruleArea, 'none')
    addQuad(ruleArea, prefix .. '.rule', 0, 0, COLOR_RULE, Z_RULE, 1)

    -- Underline layer: weighted so it fills the same box as the rule and
    -- stacks on top of it. Side insets mirror the tab band's insets (hint +
    -- row gap per side), so one weighted cell per tab lines up exactly
    -- under its tab.
    local underlay = ecs.spawn(ruleArea)
    scene.setField(underlay, 'name', prefix .. '.underlay')
    addSize(underlay, 0, 0, 1)
    addFlex(underlay, 'horizontal')
    local underlayPadL = ecs.spawn(underlay)
    scene.setField(underlayPadL, 'name', prefix .. '.underlay_pad_l')
    addSize(underlayPadL, SIDE_INSET, 0)
    local cells, cellsId = ecs.spawn(underlay)
    scene.setField(cells, 'name', prefix .. '.cell_band')
    addSize(cells, 0, 0, 1)
    addFlex(cells, 'horizontal')
    local underlayPadR = ecs.spawn(underlay)
    scene.setField(underlayPadR, 'name', prefix .. '.underlay_pad_r')
    addSize(underlayPadR, SIDE_INSET, 0)

    stripViews[id] = {
        bandId = bandId,
        cellsId = cellsId,
        cellIds = {},
        hintL = hintLHandle,
        hintR = hintRHandle,
        prefix = prefix,
    }

    -- Input handler: snapshots the strip's select presses out of the frame
    -- each turn of the input walk (which runs before the script tick, so the
    -- snapshot is fresh when the system reads it). The handler runs during
    -- the walk even when an overlay owner (e.g. options) nulls the frame
    -- afterwards, keeping tab navigation working underneath modals.
    local nav = { left = false, right = false }
    stripNav[id] = nav
    local stripHandler = scene.addComponent(handle, 'core.InputHandler')
    scene.setField(stripHandler, 'callback', function(frame)
        nav.left = containsCommand(frame.press, cmdL)
        nav.right = containsCommand(frame.press, cmdR)
    end)
    return handle, id
end

--- Append a tab to a strip. Order of addTab calls defines the tab order.
---@param stripId number Numeric id returned by addStrip.
---@param label string
---@return SceneEntity handle
---@return integer id
function tabs.addTab(stripId, label)
    local view = stripViews[stripId]
    assert(view ~= nil, 'ui.tabs.addTab: unknown strip')
    local level = ecs.get(stripId, 'ui.TabStrip', 'level')
    local prefix = view.prefix

    local tabHandle, tabId = ecs.spawn(scene.entityFromId(view.bandId))
    scene.setField(tabHandle, 'name', prefix .. '.tab.' .. label)

    -- Weighted: the band splits its width evenly across every tab.
    addSize(tabHandle, 0, 0, 1)

    -- Label: a direct text on the tab for level 2; wrapped in a horizontal
    -- flex with this tab's slot-edge separators for level 1. Only the first
    -- tab draws a left edge - every other boundary is covered by exactly one
    -- separator, so no line is drawn twice.
    local labelId = tabId
    if level ~= 2 then
        addFlex(tabHandle, 'horizontal')
        if #view.cellIds == 0 then
            addQuad(tabHandle, prefix .. '.sepL', SEP_W, SEP_H, COLOR_SEPARATOR, Z_SEPARATOR)
        end
        local labelHandle, labelChildId = ecs.spawn(tabHandle)
        scene.setField(labelHandle, 'name', prefix .. '.label')
        local t = scene.addComponent(labelHandle, 'core.Text')
        scene.setField(t, 'font', resolveUiFont())
        scene.setField(t, 'flags', TextFlags.xCenter | TextFlags.yMiddle)
        addSize(labelHandle, 0, 0, 1)
        labelId = labelChildId
        addQuad(tabHandle, prefix .. '.sepR', SEP_W, SEP_H, COLOR_SEPARATOR, Z_SEPARATOR)
    else
        local t = scene.addComponent(tabHandle, 'core.Text')
        scene.setField(t, 'font', resolveUiFont())
        scene.setField(t, 'flags', TextFlags.xCenter | TextFlags.yMiddle)
    end

    ecs.addComponent(tabId, 'ui.Tab')
    ecs.set(tabId, 'ui.Tab', 'label', label)

    -- Matching underline cell, transparent until the tab is selected.
    local cellHandle, cellId = ecs.spawn(scene.entityFromId(view.cellsId))
    scene.setField(cellHandle, 'name', prefix .. '.tab.underline.' .. label)
    local sprite = scene.addComponent(cellHandle, 'core.Sprite')
    scene.setField(sprite, 'color', COLOR_HIDDEN)
    scene.setField(cellHandle, 'z', Z_UNDERLINE)
    addSize(cellHandle, 0, 0, 1)
    view.cellIds[#view.cellIds + 1] = cellId

    tabViews[tabId] = { labelId = labelId, markup = nil }
    return tabHandle, tabId
end

--- Read/write the current selection index (1-based) through the component.
---@param stripId number
---@return integer
function tabs.selectedIndex(stripId)
    return ecs.get(stripId, 'ui.TabStrip', 'selected_index')
end

function tabs.setSelectedIndex(stripId, index)
    ecs.set(stripId, 'ui.TabStrip', 'selected_index', index)
end

-------------------------------------------------------------------------------
-- System ---------------------------------------------------------------------
-------------------------------------------------------------------------------

-- Tab strip system: keeps the per-strip overlay state and hint commands in
-- sync with the binary component rows.
ecs.registerSystem('ui.TabStrip', {
    after = { 'core.LegacyGame' },
    before = { 'core.ScaledFrame' },
}, function()
    -- Drop derived-view bookkeeping for entities that no longer exist.
    local function sweep(views)
        for id in pairs(views) do
            if scene.entityFromId(id) == nil then
                views[id] = nil
            end
        end
    end
    sweep(stripViews)
    sweep(tabViews)
    sweep(stripNav)

    ecs.each('ui.TabStrip', function(stripId)
        local view = stripViews[stripId]
        local stripHandle = scene.entityFromId(stripId)
        if view == nil or stripHandle == nil then
            return
        end

        -- Tabs parented to this strip's band, in creation order.
        local tabsInBand = {}
        ecs.each('ui.Tab', function(tabId)
            local tabHandle = scene.entityFromId(tabId)
            if tabHandle ~= nil and scene.parent(tabHandle) == view.bandId then
                tabsInBand[#tabsInBand + 1] = tabId
            end
        end)
        local count = #tabsInBand

        -- Selection input: wrap in both directions.
        local selectedIndex = count > 0
            and math.max(1, math.min(count, ecs.get(stripId, 'ui.TabStrip', 'selected_index')))
            or 1
        if count > 0 then
            local cmdL = ecs.get(stripId, 'ui.TabStrip', 'hint_command_l')
            local cmdR = ecs.get(stripId, 'ui.TabStrip', 'hint_command_r')
            -- Presses come from the strip's own InputHandler snapshot (see
            -- addStrip); the frame itself may already be nulled by an overlay
            -- owner further up the tree, so read the snapshot, not input.*.
            local nav = stripNav[stripId]
            if nav ~= nil and nav.left then
                selectedIndex = ((selectedIndex - 2 + count) % count) + 1
            elseif nav ~= nil and nav.right then
                selectedIndex = (selectedIndex % count) + 1
            end
            ecs.set(stripId, 'ui.TabStrip', 'selected_index', selectedIndex)

            scene.setField(
                scene.component(view.hintL, 'core.InputHint'), 'command', cmdL)
            scene.setField(
                scene.component(view.hintR, 'core.InputHint'), 'command', cmdR)
        end

        -- Mirror state into the labels and underline cells.
        for i, tabId in ipairs(tabsInBand) do
            local selected = i == selectedIndex
            ecs.set(tabId, 'ui.Tab', 'selected', selected)

            local tv = tabViews[tabId]
            if tv ~= nil then
                local label = ecs.get(tabId, 'ui.Tab', 'label')
                local markup = '<span color="'
                    .. (selected and SELECTED_COLOR or DIM_COLOR) .. '">' .. label .. '</span>'
                if markup ~= tv.markup then
                    local labelHandle = scene.entityFromId(tv.labelId)
                    if labelHandle ~= nil then
                        scene.setField(scene.component(labelHandle, 'core.Text'), 'text', markup)
                    end
                    tv.markup = markup
                end
            end

            local cellId = view.cellIds[i]
            if cellId ~= nil then
                local cellHandle = scene.entityFromId(cellId)
                if cellHandle ~= nil then
                    scene.setField(scene.component(cellHandle, 'core.Sprite'), 'color',
                        selected and COLOR_UNDERLINE or COLOR_HIDDEN)
                end
            end
        end
    end)
end)

return tabs
