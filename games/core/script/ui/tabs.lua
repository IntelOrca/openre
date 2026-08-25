-- ui/tabs.lua - shared tab strip control built on the Lua ECS (see
-- games/core/script/ecs.lua).
--
-- A game only creates the entities and components:
--
--     local strip = tabs.addStrip(root, x, y)   -- ui.tab_strip
--     local tab   = tabs.addTab(stripId, 'HUD') -- ui.tab
--
-- Everything else is produced by the registered 'ui.tabs' system: the LB/RB
-- input hints flanking the strip, a full-width horizontal rule under the
-- whole row, equal-width slot separators, each tab's label text and
-- selection highlight, and the tab_left / tab_right (or whatever commands
-- the strip's fields name) input handling. State lives in the binary
-- component rows: ui.tab_strip carries selected_index plus the two hint
-- command names, ui.tab carries the label and its mirrored `selected` bool.
--
-- Layout model (fixed slots): the band between the two hints is divided
-- into N equal-width slots that NEVER move or resize. The visible count
-- adapts to a preferred slot width; scrolling moves whole slots through the
-- band so the selected tab stays inside it - visually the labels change in
-- place while out-of-window tabs park offscreen.
--
-- Level 1 strips draw slot separators and answer to tab_left/tab_right;
-- level 2 (sub-row) strips draw no separators and are driven by
-- subtab_left/subtab_right instead.
--
-- All text uses the shared UI font set once via tabs.setFont(); the standard
-- font everywhere is deliberate.

local ecs = require('ecs')

local tabs = {}

-------------------------------------------------------------------------------
-- Layout / style constants ---------------------------------------------------
-------------------------------------------------------------------------------

-- Fixed-slot model: every tab occupies an equal-width slot inside a band
-- that sits at a FIXED place between the LB/RB input hints. Scrolling never
-- moves the band or its separators; it only changes which tabs occupy the
-- slots (tab entities slide between slots, extras park offscreen).
local STRIP_HALF = 280   -- strip spans 560 design px incl. the hint icons
local HINT_CX = 262      -- input-hint icon centre offset from the origin
local BAND_HALF = 238    -- half-width of the fixed slot band
local PREFERRED_SLOT_W = 118 -- target slot width; picks the visible count
local ROW_H = 24         -- slot/text box height
local RULE_DY = 15       -- rule line offset below the row centre
local SEP_W = 1
local SEP_H = 18

local COLOR_HIGHLIGHT_OUTER = { 255, 255, 255, 36 }
local COLOR_HIGHLIGHT_MED = { 255, 255, 255, 72 }
local COLOR_HIGHLIGHT_CORE = { 255, 255, 255, 128 }
local COLOR_SEPARATOR = { 150, 150, 150, 200 }
local COLOR_RULE = { 132, 132, 132, 220 }

-- Highlight insets relative to the slot box.
local HL_PAD_X = 5
local HL_PAD_OUTER = 3
local HL_PAD_MED = 1
local HL_PAD_CORE = -2

-- Decorative rects sit behind text glyph sprites (which render at z = 0);
-- a higher z renders further back.
local Z_RULE = 70
local Z_HIGHLIGHT = 60
local Z_SEPARATOR = 50

local SELECTED_COLOR = '#ffffff'
local DIM_COLOR = '#9a9a9a'

local OFFSCREEN_Y = -10000 -- parked here while outside the scroll window

-------------------------------------------------------------------------------
-- Components -----------------------------------------------------------------
-------------------------------------------------------------------------------

ecs.registerComponent('ui.tab_strip', {
    { name = 'selected_index', type = 'int', default = 1 },
    -- 1 = outer tab row: separators drawn, tab_left/tab_right commands.
    -- 2 = sub-row: no separators, operated with the subtab_left/subtab_right
    -- commands (addStrip presets the command fields accordingly).
    { name = 'level', type = 'int', default = 1 },
    { name = 'hint_command_l', type = 'string', default = 'tab_left' },
    { name = 'hint_command_r', type = 'string', default = 'tab_right' },
})

ecs.registerComponent('ui.tab', {
    { name = 'label', type = 'string', default = '' },
    { name = 'selected', type = 'bool', default = false },
})

-------------------------------------------------------------------------------
-- Creation helpers (what a game calls) ---------------------------------------
-------------------------------------------------------------------------------

-- The one font used by every strip; set before creating strips.
local uiFont = nil

---@param font Font A gfx.loadFont handle.
function tabs.setFont(font)
    uiFont = font
end

--- Create a tab strip entity under `parent` at the given canvas-space
--- centre. `level` 1 (default) is an outer tab row: separators are drawn
--- and it is operated with the tab_left/tab_right commands. Level 2 is a
--- sub-row: no separators, operated with subtab_left/subtab_right. Returns
--- the native handle and numeric id.
---@param parent SceneEntity
---@param x number Canvas-space centre X (offset from the parent origin).
---@param y number Canvas-space centre Y.
---@param level? integer 1 (default) or 2.
---@return SceneEntity handle
---@return integer id
function tabs.addStrip(parent, x, y, level)
    if level ~= 2 then
        level = 1
    end
    local handle, id = ecs.spawn(parent)
    assert(id ~= nil, 'ui.tabs.addStrip: spawn failed')
    scene.setField(handle, 'name', 'ui.tab_strip')
    scene.setField(handle, 'x', x)
    scene.setField(handle, 'y', y)
    ecs.addComponent(id, 'ui.tab_strip')
    ecs.set(id, 'ui.tab_strip', 'level', level)
    local cmdL, cmdR = 'tab_left', 'tab_right'
    if level == 2 then
        cmdL, cmdR = 'subtab_left', 'subtab_right'
    end
    ecs.set(id, 'ui.tab_strip', 'hint_command_l', cmdL)
    ecs.set(id, 'ui.tab_strip', 'hint_command_r', cmdR)
    return handle, id
end

--- Append a tab to a strip. Order of addTab calls defines the tab order.
---@param stripId number Numeric id returned by addStrip.
---@param label string
---@return SceneEntity handle
---@return integer id
function tabs.addTab(stripId, label)
    local handle, id = ecs.spawn(scene.entityFromId(stripId))
    assert(id ~= nil, 'ui.tabs.addTab: spawn failed')
    scene.setField(handle, 'name', 'ui.tab.' .. label)
    ecs.addComponent(id, 'ui.tab')
    ecs.set(id, 'ui.tab', 'label', label)
    return handle, id
end

--- Read/write the current selection index (1-based) through the component.
---@param stripId number
---@return integer
function tabs.selectedIndex(stripId)
    return ecs.get(stripId, 'ui.tab_strip', 'selected_index')
end

function tabs.setSelectedIndex(stripId, index)
    ecs.set(stripId, 'ui.tab_strip', 'selected_index', index)
end

-------------------------------------------------------------------------------
-- System ---------------------------------------------------------------------
-------------------------------------------------------------------------------

-- Per-frame view state, kept out of the binary rows: these describe derived
-- entities (created by this system) rather than authored data.
--   stripViews[stripId] = { hintL, hintR, hlWide, hlMed, hlCore, seps[] }
--   tabViews[tabId]     = { textId, label, width, height }
local stripViews = {}
local tabViews = {}

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
    local size = scene.addComponent(handle, 'core.ui_size')
    scene.setField(size, 'width', w)
    scene.setField(size, 'height', h)
    scene.setField(sprite, 'z', z)
    return id
end

local function ensureStripChrome(parentHandle, parentId)
    local view = stripViews[parentId]
    if view == nil then
        view = { seps = {}, first = 0 }
        stripViews[parentId] = view
    end

    -- LB/RB input hints hugging the strip ends. The engine expands each into
    -- one icon per binding of the named command; scale shrinks the fixed
    -- 32px icons down to strip size.
    local function ensureHint(key, cx)
        local id = liveChild(view, key)
        if id == nil then
            local handle
            handle, id = ecs.spawn(parentHandle)
            scene.setField(handle, 'name', 'ui.tab_strip.' .. key)
            scene.setField(handle, 'x', cx)
            scene.setField(handle, 'y', 0)
            scene.setField(handle, 'scale.x', 0.5)
            scene.setField(handle, 'scale.y', 0.5)
            local hint = scene.addComponent(handle, 'core.input_hint')
            scene.setField(hint, 'device', 'ps')
            view[key] = id
        end
        return id
    end
    view.hintL = ensureHint('hintL', -HINT_CX)
    view.hintR = ensureHint('hintR', HINT_CX)

    -- Full-width horizontal rule under the whole strip (hints included).
    local ruleId = liveChild(view, 'rule')
    if ruleId == nil then
        ruleId = addQuadChild(parentHandle, 'ui.tab_strip.rule',
            STRIP_HALF * 2, 1, COLOR_RULE, Z_RULE)
        scene.setField(scene.entityFromId(ruleId), 'y', RULE_DY)
        view.rule = ruleId
    end

    return view
end

local function ensureSeparator(view, parentHandle, index)
    local id = view.seps[index]
    if id ~= nil and scene.entityFromId(id) == nil then
        id = nil
        view.seps[index] = nil
    end
    if id == nil then
        local handle
        handle, id = ecs.spawn(parentHandle)
        scene.setField(handle, 'name', 'ui.tab_strip.sep' .. index)
        local sprite = scene.addComponent(handle, 'core.sprite')
        scene.setField(sprite, 'color', COLOR_SEPARATOR)
        local size = scene.addComponent(handle, 'core.ui_size')
        scene.setField(size, 'width', SEP_W)
        scene.setField(size, 'height', SEP_H)
        scene.setField(sprite, 'z', Z_SEPARATOR)
        view.seps[index] = id
    end
    return id
end

-- Park/unpark a child entity vertically far away instead of destroying it,
-- so toggling visibility never churns entities or text layouts.
local function park(id, hidden, x)
    local handle = scene.entityFromId(id)
    if handle == nil then
        return
    end
    scene.setField(handle, 'y', hidden and OFFSCREEN_Y or 0)
    if not hidden then
        scene.setField(handle, 'x', x)
    end
end

-- Creates or refreshes a tab's label text child, sized to its fixed slot.
local function ensureTabText(tabView, textParentHandle, label, selected, boxW, boxH)
    if tabView.textId ~= nil and scene.entityFromId(tabView.textId) == nil then
        tabView.textId = nil
        tabView.label = nil
    end
    if tabView.textId == nil then
        local handle
        handle, tabView.textId = ecs.spawn(textParentHandle)
        scene.setField(handle, 'name', 'ui.tab.label')
        local text = scene.addComponent(handle, 'core.text')
        scene.setField(text, 'font', uiFont)
        scene.setField(text, 'flags', TextFlags.xCenter | TextFlags.yMiddle)
        scene.addComponent(handle, 'core.ui_size')
        tabView.label = nil
    end

    if tabView.label ~= label then
        tabView.label = label
        tabView.markup = nil
    end

    local markup = '<span color="'
        .. (selected and SELECTED_COLOR or DIM_COLOR) .. '">' .. label .. '</span>'
    if markup ~= tabView.markup then
        local textHandle = scene.component(scene.entityFromId(tabView.textId), 'core.text')
        scene.setField(textHandle, 'text', markup)
        local sizeHandle = scene.component(scene.entityFromId(tabView.textId), 'core.ui_size')
        scene.setField(sizeHandle, 'width', boxW)
        scene.setField(sizeHandle, 'height', boxH)
        tabView.markup = markup
    end
end

local function moveHighlight(view, parentHandle, cx, w, h)
    local pads = {
        { key = 'hlWide', pad = HL_PAD_OUTER, color = COLOR_HIGHLIGHT_OUTER, z = Z_HIGHLIGHT },
        { key = 'hlMed', pad = HL_PAD_MED, color = COLOR_HIGHLIGHT_MED, z = Z_HIGHLIGHT },
        { key = 'hlCore', pad = HL_PAD_CORE, color = COLOR_HIGHLIGHT_CORE, z = Z_HIGHLIGHT },
    }
    for _, layer in ipairs(pads) do
        local id = liveChild(view, layer.key)
        if id == nil then
            id = addQuadChild(parentHandle, 'ui.tab_strip.highlight.' .. layer.key,
                2, 2, layer.color, layer.z)
            view[layer.key] = id
        end
        local handle = scene.entityFromId(id)
        scene.setField(handle, 'x', cx)
        scene.setField(handle, 'y', 0)
        local sprite = scene.component(handle, 'core.sprite')
        local size = scene.component(handle, 'core.ui_size')
        scene.setField(size, 'width', w + layer.pad * 2)
        scene.setField(size, 'height', h + layer.pad * 2)
    end
end

ecs.registerSystem('ui.tabs', 1000, function()
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

    -- Collect live strips in creation order.
    local strips = {}
    ecs.each('ui.tab_strip', function(stripId)
        strips[#strips + 1] = stripId
    end)

    for _, stripId in ipairs(strips) do
        local stripHandle = scene.entityFromId(stripId)
        if stripHandle ~= nil then
            local view = ensureStripChrome(stripHandle, stripId)

            -- Tabs parented to this strip, in creation order.
            local tabs = {}
            ecs.each('ui.tab', function(tabId)
                local tabHandle = scene.entityFromId(tabId)
                if tabHandle ~= nil and scene.parent(tabHandle) == stripId then
                    tabs[#tabs + 1] = tabId
                end
            end)
            local count = #tabs

            -- Selection input: wrap in both directions.
            local selectedIndex = count > 0
                and math.max(1, math.min(count, ecs.get(stripId, 'ui.tab_strip', 'selected_index')))
                or 1
            if count > 0 then
                local cmdL = ecs.get(stripId, 'ui.tab_strip', 'hint_command_l')
                local cmdR = ecs.get(stripId, 'ui.tab_strip', 'hint_command_r')
                if input.pressed(cmdL) then
                    selectedIndex = ((selectedIndex - 2 + count) % count) + 1
                elseif input.pressed(cmdR) then
                    selectedIndex = (selectedIndex % count) + 1
                end
                ecs.set(stripId, 'ui.tab_strip', 'selected_index', selectedIndex)

                scene.setField(
                    scene.component(scene.entityFromId(view.hintL), 'core.input_hint'), 'command', cmdL)
                scene.setField(
                    scene.component(scene.entityFromId(view.hintR), 'core.input_hint'), 'command', cmdR)
            end

            -- Fixed-slot geometry: the visible slot count adapts to a
            -- preferred slot width, then every slot stretches equally to
            -- fill the band. The band never moves or resizes between frames.
            local bandW = BAND_HALF * 2
            local visibleSlots = count > 0
                and math.max(1, math.min(count, math.floor(bandW / PREFERRED_SLOT_W)))
                or 0
            local slotW = visibleSlots > 0 and bandW / visibleSlots or bandW

            -- Scroll by whole slots so the cursor stays inside the window;
            -- the slots themselves never move (only which tab occupies them).
            if visibleSlots > 0 then
                local maxFirst = count - visibleSlots
                if view.first > maxFirst then
                    view.first = maxFirst
                end
                if view.first < 0 then
                    view.first = 0
                end
                local cursor0 = selectedIndex - 1 -- 0-based
                if cursor0 < view.first then
                    view.first = cursor0
                elseif cursor0 >= view.first + visibleSlots then
                    view.first = cursor0 - visibleSlots + 1
                end
            end

            -- Slot assignment: tab i occupies slot i-1-first when in range;
            -- everything else parks offscreen. Slot centres are constant.
            for i, tabId in ipairs(tabs) do
                if tabViews[tabId] == nil then
                    tabViews[tabId] = {}
                end
                local slot = i - 1 - view.first
                local inWindow = slot >= 0 and slot < visibleSlots

                ensureTabText(tabViews[tabId], scene.entityFromId(tabId),
                    ecs.get(tabId, 'ui.tab', 'label'), i == selectedIndex, slotW, ROW_H)
                ecs.set(tabId, 'ui.tab', 'selected', i == selectedIndex)

                local tabHandle = scene.entityFromId(tabId)
                if inWindow then
                    scene.setField(tabHandle, 'x', -BAND_HALF + slotW * (slot + 0.5))
                    scene.setField(tabHandle, 'y', 0)
                else
                    scene.setField(tabHandle, 'y', OFFSCREEN_Y)
                end
            end

            -- Separators on every slot boundary, including the two edges
            -- where the band meets the hints (N+1 lines) - level 1 only.
            -- Level 2 rows never draw them; any existing ones stay parked
            -- (covers a strip whose level is changed at runtime).
            for b = 1, #view.seps do
                park(view.seps[b], true)
            end
            local level = ecs.get(stripId, 'ui.tab_strip', 'level')
            if level ~= 2 and count > 0 then
                for b = 1, visibleSlots + 1 do
                    local sepCx = -BAND_HALF + slotW * (b - 1)
                    park(ensureSeparator(view, stripHandle, b), false, sepCx)
                end
            end

            -- Selection highlight fills the selected tab's slot.
            local selectedSlot = selectedIndex - 1 - view.first
            if visibleSlots > 0 and selectedSlot >= 0 and selectedSlot < visibleSlots then
                moveHighlight(view, stripHandle,
                    -BAND_HALF + slotW * (selectedSlot + 0.5),
                    slotW - HL_PAD_X * 2, ROW_H)
            else
                for _, key in ipairs({ 'hlWide', 'hlMed', 'hlCore' }) do
                    local id = liveChild(view, key)
                    if id ~= nil then
                        park(id, true)
                    end
                end
            end
        end
    end
end)

return tabs

