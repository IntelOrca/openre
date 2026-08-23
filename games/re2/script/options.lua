-- options.lua - OPTIONS overlay styled after the RE2 remake options screen.
-- The option list is driven by the engine's `options` registry API: rows are
-- rebuilt from options.list() for the current category/subcategory and values
-- are read from options.get() at refresh time. Static chrome (heading, outer
-- tab strip, rules, scrollbar track) is built once by open(); everything below
-- the subtab rule hangs off a dynamic subtree that refresh() tears down and
-- recreates on every state change - at this scale rebuilding a few dozen
-- entities is far cheaper than diffing individual text/quad fields and keeps
-- highlights, scrollbar geometry and the tooltip trivially consistent.
--
-- Layering model: every sprite carries an explicit `z` (higher z renders
-- further back). Text glyphs are materialised as sprites with the default
-- z = 0, so decorative rects live at DECOR_Z (> 0, behind any text) and the
-- full-window backdrop at BACKDROP_Z (furthest back of all).
--
-- Input model: the tick handler consumes edge-triggered commands - up/down
-- move the list cursor (skipping header rows, no wrap at the ends), left/right
-- edit the cursor option's value, tab_left/tab_right switch the outer tab and
-- subtab_left/subtab_right the subtab (both wrap, with per-tab cursor
-- memory), and cancel flushes dirty values then closes. Holding left/right
-- auto-repeats the value edit; every action updates the state table then
-- redraws exactly once through refresh().
--
-- All content is authored in a fixed 640x480 design space mapped onto the
-- window by a native scaled_frame ("contain"); the frame target is refreshed
-- every tick so resizes are tracked.

-- Module state: nil while closed.
local Options = {}
local state = nil

-------------------------------------------------------------------------------
-- Layout constants (design space is 640x480) ---------------------------------
-------------------------------------------------------------------------------

local DESIGN_W          = 640
local DESIGN_H          = 480
local SMALL_CELL        = 8                     -- font0:small advance/cell size

-- Sprite z ordering (higher = further back; text glyphs sit at z = 0).
local DECOR_Z           = 100                   -- rects/bands: behind all text
local BACKDROP_Z        = 1000000               -- backdrop: furthest back

-- Colours (sprite RGBA tables; text opacity goes through <span> markup).
local COLOR_BACKDROP    = { 0, 0, 0, 204 }
local COLOR_KEYCAP_FILL = { 26, 26, 26, 235 }
local COLOR_KEYCAP_EDGE = { 208, 208, 208, 255 }
local COLOR_GLOW_WIDE   = { 255, 255, 255, 36 }  -- selection glow: widest/dimmest
local COLOR_GLOW_MED    = { 255, 255, 255, 72 }
local COLOR_GLOW_CORE   = { 255, 255, 255, 128 } -- narrow bright core
local COLOR_ROW_BAND    = { 255, 255, 255, 42 }  -- selected row highlight
local COLOR_RULE        = { 132, 132, 132, 220 }
local COLOR_SEPARATOR   = { 150, 150, 150, 200 }
local COLOR_BANNER      = { 70, 70, 70, 215 }
local COLOR_TRACK       = { 58, 58, 58, 255 }
local COLOR_SEGMENT     = { 168, 168, 168, 255 }
local COLOR_ROW_SEP     = { 48, 48, 48, 255 }
local COLOR_BANNER_EDGE = { 104, 104, 104, 220 } -- header banner bottom edge
local COLOR_BAND_MID    = { 255, 255, 255, 72 }  -- row highlight: middle layer
local COLOR_BAND_CORE   = { 255, 255, 255, 118 } -- row highlight: bright core
local COLOR_SEG_LIT     = { 255, 255, 255, 235 } -- segment dash at/before value
local COLOR_SEG_OFF     = { 66, 66, 66, 255 }    -- segment dash after value
local COLOR_TICK_LIT    = { 255, 255, 255, 225 } -- ruler tick at/before value
local COLOR_TICK_DIM    = { 92, 92, 92, 255 }    -- ruler tick after value
local COLOR_TICK_CURSOR = { 255, 255, 255, 255 } -- ruler cursor marker
local COLOR_ICON_FALLBACK = { 170, 170, 170, 255 }

-- Shared page geometry.
local EDGE_MARGIN       = 24                    -- left/right page margin
local FONT_PATH         = 'common/data/font0:small'

-- "OPTIONS" heading (top-left corner of its text box).
local HEADING_LEFT      = EDGE_MARGIN
local HEADING_TOP       = 14
local HEADING_BAND_W    = 200
local HEADING_BAND_H    = 12

-- Keycap hint boxes (LB/RB/LT/RT): solid fill + 1px border + centred label.
local KEYCAP_W          = 26
local KEYCAP_H          = 14
local KEYCAP_EDGE       = 1                     -- border thickness

-- Tab/subtab strips: keycaps hug the margins, labels scroll between them.
local STRIP_INSET       = 12                    -- keycap <-> first/last label gap
local STRIP_LEFT        = EDGE_MARGIN + KEYCAP_W + STRIP_INSET
local STRIP_RIGHT       = DESIGN_W - EDGE_MARGIN - KEYCAP_W - STRIP_INSET
local STRIP_WIDTH       = STRIP_RIGHT - STRIP_LEFT

-- Outer tab strip (row centred near y=48).
local TAB_STRIP_CY      = 50                    -- vertical centre of the row
local TAB_RULE_Y        = 66                    -- horizontal rule below the row
local TAB_GAP           = 20                    -- label pitch gap (separator lives here)

-- Inner subtab strip (row centred near y=76), no separators.
local SUB_STRIP_CY      = 78
local SUB_RULE_Y        = 92
local SUB_GAP           = 24

-- Strip separators ("|"): thin vertical rect centred in the label gap.
local SEP_W             = 1
local SEP_H             = 16

-- Selection glow stack (widths/heights added around the label box).
local GLOW_WIDE_PAD     = 9                     -- extra half-width/half-height
local GLOW_MED_PAD      = 5
local GLOW_CORE_PAD     = 2

-- Content area (list + scrollbar).
local LIST_RIGHT        = 600
local LIST_TOP          = 100
local ROW_TOP           = 130
local ROW_HEIGHT        = 30
local ROW_TEXT_PAD      = 8                     -- title/header inset from list edge

-- Scrollbar track along the right edge of the list area.
local SCROLLBAR_X       = 615                   -- left edge of the thin track
local SCROLLBAR_W       = 2
local SCROLLBAR_BOTTOM  = 430
local SCROLL_THUMB_MIN  = 4                     -- shortest usable thumb (px)

-- The row window: whole rows only, no clipping is available.
local VISIBLE_ROWS      = math.floor((SCROLLBAR_BOTTOM - ROW_TOP) / ROW_HEIGHT)

-- Selected row highlight: stacked translucent bands, widest/dimmest first.
local BAND_INSET_STEP   = 3                     -- vertical inset added per layer

-- Value column shared by every option row (titles end far left of it).
local VALUE_LEFT        = 440
local VALUE_RIGHT       = 560
local VALUE_CX          = (VALUE_LEFT + VALUE_RIGHT) * 0.5

-- Toggle/enum rows: '<' label '>' cluster centred in the value column ...
local CHOICE_GAP        = 4                     -- chevron <-> label spacing

-- ... plus a segmented indicator (one dash per possible value) underneath.
local SEG_DASH_W        = 8
local SEG_DASH_H        = 2
local SEG_DASH_GAP      = 2
local SEG_MAX_DASHES    = 12                    -- keep dashes inside the column
local SEG_BOTTOM_INSET  = 6                     -- dash bottom inset from row bottom

-- Range rows: '<' ruler '>' then the numeric readout, left to right.
local RULER_LEFT        = 456                   -- centre of the first tick
local RULER_RIGHT       = 522                   -- centre of the last tick
local RULER_CHEV_OFF    = 8                     -- chevron centre <-> end tick
local RULER_TICK_W      = 1
local RULER_TICK_H      = 10
local RULER_CURSOR_W    = 2
local RULER_CURSOR_H    = 16
local RULER_MAX_TICKS   = 32                    -- sample longer rulers
local RANGE_READOUT_W   = 26                    -- 3-digit band, right-aligned

-- External rows: icon texture scaled into the value column.
local EXTERNAL_TEXTURE  = 'texture/external'
local EXTERNAL_ICON     = 16

-- Bottom tooltip strip (~y=440..480): grey heading + wrapped detail lines and
-- a "(Default: ...)" line for options.
local TOOLTIP_HEADING_TOP = 442
local TOOLTIP_DETAIL_TOP  = 454
local TOOLTIP_LINE_STEP   = 9
local TOOLTIP_DEFAULT_TOP = 472
local TOOLTIP_MAX_LINES   = 2
local TOOLTIP_WRAP_CHARS  = 72                    -- 8px per char, ~592px band
local TOOLTIP_HINT        = 'LB/RB change pages, LT/RT change groups.'

-- Hold-to-repeat for left/right value editing only (frames); up/down and the
-- tab commands never auto-repeat.
local HOLD_REPEAT_DELAY   = 15                    -- held this long -> repeat
local HOLD_REPEAT_RATE    = 4                     -- then one step per this many

-------------------------------------------------------------------------------
-- Colour helpers -------------------------------------------------------------
-- <span color="#rrggbbaa"> markup: the last two hex digits are the alpha.
-------------------------------------------------------------------------------

local function hexByte(n)
    return string.format('%02x', n)
end

local function tintedSpan(text, rgb, alphaFrac)
    local a = math.floor(math.max(0, math.min(1, alphaFrac)) * 255 + 0.5)
    return '<span color="#' .. hexByte(rgb[1]) .. hexByte(rgb[2]) .. hexByte(rgb[3])
        .. hexByte(a) .. '">' .. text .. '</span>'
end

local function whiteSpan(text, alphaFrac)
    return tintedSpan(text, { 255, 255, 255 }, alphaFrac)
end

local function greySpan(text, alphaFrac)
    return tintedSpan(text, { 154, 154, 154 }, alphaFrac)
end

-------------------------------------------------------------------------------
-- Tape-scroll ----------------------------------------------------------------
-- Lays sequential items of known widths (separated by `gap`) along a virtual
-- tape starting at x = 0, then scrolls it so the cursor item always ends up
-- fully inside [0, stripWidth]. There is no clipping in the renderer, so items
-- that would stick out of the strip are reported invisible and callers hide
-- them instead of drawing them over their neighbours.
--
-- Returns one `{ x = <left edge inside the strip>, visible = bool }` per item.
-------------------------------------------------------------------------------

local function tapeScroll(widths, cursorIndex, stripWidth, gap)
    local n = #widths

    -- Unscrolled left edge of every item, plus the total tape length.
    local lefts = {}
    local total = 0
    for i = 1, n do
        lefts[i] = total
        total = total + widths[i]
        if i < n then
            total = total + gap
        end
    end

    -- Classic list-box behaviour: move as little as possible so the cursor
    -- item fits. Anchor the tape at 0 until the cursor pushes past the right
    -- edge, then follow it; pull back if it sticks out past the left edge.
    local scroll = 0
    if n > 0 and widths[cursorIndex] ~= nil then
        local l = lefts[cursorIndex]
        local r = l + widths[cursorIndex]
        if r - scroll > stripWidth then
            scroll = r - stripWidth
        end
        if l - scroll < 0 then
            scroll = l
        end
        -- Never overscroll past the end of the tape.
        scroll = math.max(0, math.min(scroll, math.max(0, total - stripWidth)))
    end

    local out = {}
    for i = 1, n do
        local x = lefts[i] - scroll
        out[i] = {
            x = x,
            visible = x >= 0 and x + widths[i] <= stripWidth,
        }
    end
    return out
end

-------------------------------------------------------------------------------
-- Option list model ----------------------------------------------------------
-- Pure helpers over the registry data; the navigation step drives these.
-------------------------------------------------------------------------------

-- Greedy word wrap on spaces; returns an array of lines each <= maxChars
-- (a single overlong word becomes its own overflowing line).
local function wordWrap(text, maxChars)
    local lines = {}
    local line = nil
    for word in string.gmatch(text or '', '%S+') do
        if line == nil then
            line = word
        elseif #line + 1 + #word <= maxChars then
            line = line .. ' ' .. word
        else
            lines[#lines + 1] = line
            line = word
        end
    end
    if line ~= nil then
        lines[#lines + 1] = line
    end
    return lines
end

-- Human-readable label for an option's default value.
local function defaultLabel(def)
    if def.kind == 'external' then
        return 'n/a'
    end
    if (def.kind == 'toggle' or def.kind == 'enum') and def.labels ~= nil then
        return def.labels[(def.default or 0) + 1] or tostring(def.default)
    end
    return tostring(def.default)
end

-- Tooltip detail text folded onto at most TOOLTIP_MAX_LINES lines, marking a
-- cut-off remainder with '..'.
local function tooltipLines(text)
    local lines = wordWrap(text, TOOLTIP_WRAP_CHARS)
    if #lines <= TOOLTIP_MAX_LINES then
        return lines
    end
    local last = lines[TOOLTIP_MAX_LINES]
    if #last > TOOLTIP_WRAP_CHARS - 2 then
        last = string.sub(last, 1, TOOLTIP_WRAP_CHARS - 2)
    end
    lines[TOOLTIP_MAX_LINES] = last .. '..'
    for i = TOOLTIP_MAX_LINES + 1, #lines do
        lines[i] = nil
    end
    return lines
end

-- Unique values of one def field in registration order, optionally filtered.
local function uniqueField(defs, field, predicate)
    local values, seen = {}, {}
    for _, def in ipairs(defs) do
        local v = def[field]
        if v ~= nil and not seen[v] and (predicate == nil or predicate(def)) then
            seen[v] = true
            values[#values + 1] = v
        end
    end
    return values
end

local function categoryName(s)
    return s.categories[s.categoryIndex]
end

-- Subcategories of one category, in registration order.
local function categorySubCategories(s, category)
    return uniqueField(s.defs, 'subcategory', function(def)
        return def.category == category
    end)
end

local function subCategories(s)
    return categorySubCategories(s, categoryName(s))
end

-- Display rows for the current category/subcategory: a header row whenever
-- the registration-order group changes, then one row per option carrying its
-- live options.get() value.
local function rebuildRows(s)
    local category = categoryName(s)
    local subcategory = s.subcats[s.subcategoryIndex]
    local rows = {}
    local prevGroup = nil
    for _, def in ipairs(s.defs) do
        if def.category == category and def.subcategory == subcategory then
            if def.group ~= prevGroup then
                rows[#rows + 1] = { type = 'header', label = def.group }
                prevGroup = def.group
            end
            rows[#rows + 1] = { type = 'option', def = def, value = options.get(def.id) or 0 }
        end
    end
    return rows
end

-- Keep the cursor inside the list and the scroll window around the cursor.
local function clampScroll(s, visibleCount)
    local total = #s.rows
    s.cursorRow = math.max(1, math.min(s.cursorRow, math.max(total, 1)))
    local maxScroll = math.max(1, total - visibleCount + 1)
    s.scrollRow = math.max(1, math.min(s.scrollRow, maxScroll))
    if s.cursorRow < s.scrollRow then
        s.scrollRow = s.cursorRow
    end
    if s.cursorRow > s.scrollRow + visibleCount - 1 then
        s.scrollRow = s.cursorRow - visibleCount + 1
    end
end

-- Index of the first selectable (non-header) row at or after `start`,
-- searching in direction dir (+1 towards the list end, -1 towards the top);
-- nil when the list end comes first.
local function findSelectable(rows, start, dir)
    local limit = dir >= 0 and #rows or 1
    for i = start, limit, dir do
        if rows[i].type == 'option' then
            return i
        end
    end
    return nil
end

-------------------------------------------------------------------------------
-- Entity builders ------------------------------------------------------------
-- Coordinates: entities under the content root use offsets from the root's
-- origin (the window centre). Design-space point (dx, dy) maps to the offset
-- (dx - DESIGN_W / 2, dy - DESIGN_H / 2).
-------------------------------------------------------------------------------

-- Texture-less sprite = solid colour quad, drawn centred on its entity.
-- (cx, cy) are offsets from the parent origin.
local function addQuad(parent, name, cx, cy, w, h, color)
    local e = scene.createEntity(parent)
    scene.setField(e, 'name', name)
    scene.setField(e, 'x', cx)
    scene.setField(e, 'y', cy)
    local s = scene.addComponent(e, 'core.sprite')
    scene.setField(s, 'color', color)
    scene.setField(s, 'size.width', w)
    scene.setField(s, 'size.height', h)
    scene.setField(s, 'z', DECOR_Z)
    return e, s
end

-- Same quad, positioned by a design-space rectangle's top-left corner.
local function addQuadAt(parent, name, left, top, w, h, color)
    return addQuad(parent, name,
        left - DESIGN_W * 0.5 + w * 0.5,
        top - DESIGN_H * 0.5 + h * 0.5,
        w, h, color)
end

-- One line of text laid out inside a box. The entity sits at the box centre
-- (glyphs align around the transform origin); (cx, cy) are parent-relative.
local function addText(parent, font, name, cx, cy, bandW, bandH, text, flags)
    local e = scene.createEntity(parent)
    scene.setField(e, 'name', name)
    scene.setField(e, 'x', cx)
    scene.setField(e, 'y', cy)
    local t = scene.addComponent(e, 'core.text')
    scene.setField(t, 'font', font)
    scene.setField(t, 'text', text)
    scene.setField(t, 'size.width', bandW)
    scene.setField(t, 'size.height', bandH)
    scene.setField(t, 'flags', flags)
    return e, t
end

-- Text inside a design-space box given by its top-left corner.
local function addTextBox(parent, font, name, left, top, bandW, bandH, text, flags)
    return addText(parent, font, name,
        left - DESIGN_W * 0.5 + bandW * 0.5,
        top - DESIGN_H * 0.5 + bandH * 0.5,
        bandW, bandH, text, flags)
end

-- Keycap hint box: dark fill, four 1px border rects, centred label.
local function createKeycap(parent, font, name, cx, cy, label)
    local l = cx - KEYCAP_W * 0.5
    local t = cy - KEYCAP_H * 0.5
    addQuad(parent, name .. '.fill', cx, cy, KEYCAP_W, KEYCAP_H, COLOR_KEYCAP_FILL)
    addQuadAt(parent, name .. '.edgeT', l, t, KEYCAP_W, KEYCAP_EDGE, COLOR_KEYCAP_EDGE)
    addQuadAt(parent, name .. '.edgeB', l, t + KEYCAP_H - KEYCAP_EDGE, KEYCAP_W, KEYCAP_EDGE, COLOR_KEYCAP_EDGE)
    addQuadAt(parent, name .. '.edgeL', l, t, KEYCAP_EDGE, KEYCAP_H, COLOR_KEYCAP_EDGE)
    addQuadAt(parent, name .. '.edgeR', l + KEYCAP_W - KEYCAP_EDGE, t, KEYCAP_EDGE, KEYCAP_H, COLOR_KEYCAP_EDGE)
    addText(parent, font, name .. '.label', cx, cy, KEYCAP_W, KEYCAP_H,
        whiteSpan(label, 0.95), TextFlags.xCenter | TextFlags.yMiddle)
end

-- Selection glow: stacked translucent rects behind the label, created widest
-- first so the narrow bright core paints last (equal z keeps creation order).
local function createGlow(parent, name, cx, cy, labelW)
    addQuad(parent, name .. '.wide',
        cx, cy, labelW + GLOW_WIDE_PAD * 2, SMALL_CELL + GLOW_WIDE_PAD * 2, COLOR_GLOW_WIDE)
    addQuad(parent, name .. '.med',
        cx, cy, labelW + GLOW_MED_PAD * 2, SMALL_CELL + GLOW_MED_PAD * 2, COLOR_GLOW_MED)
    addQuad(parent, name .. '.core',
        cx, cy, labelW + GLOW_CORE_PAD * 2, SMALL_CELL + GLOW_CORE_PAD * 2, COLOR_GLOW_CORE)
end

-- Textured sprite sized `size` px square, layered with text (z = 0, above the
-- DECOR_Z rectangles); src stays unset = full texture.
local function addSprite(parent, name, cx, cy, size, texture)
    local e = scene.createEntity(parent)
    scene.setField(e, 'name', name)
    scene.setField(e, 'x', cx)
    scene.setField(e, 'y', cy)
    local sp = scene.addComponent(e, 'core.sprite')
    scene.setField(sp, 'texture', texture)
    scene.setField(sp, 'size.width', size)
    scene.setField(sp, 'size.height', size)
    scene.setField(sp, 'z', 0)
    return e, sp
end

-- Selected row highlight: full-width translucent bands stacked narrowest and
-- brightest last, echoing the strip glow for a soft edge.
local function createRowBand(parent, name, top, width)
    addQuadAt(parent, name .. '.outer', EDGE_MARGIN, top + 1,
        width, ROW_HEIGHT - 2, COLOR_ROW_BAND)
    addQuadAt(parent, name .. '.mid', EDGE_MARGIN, top + 1 + BAND_INSET_STEP,
        width, ROW_HEIGHT - 2 - BAND_INSET_STEP * 2, COLOR_BAND_MID)
    addQuadAt(parent, name .. '.core', EDGE_MARGIN, top + 1 + BAND_INSET_STEP * 2,
        width, ROW_HEIGHT - 2 - BAND_INSET_STEP * 4, COLOR_BAND_CORE)
end

-- One labelled strip: keycaps at both ends plus tape-scrolled labels, with
-- optional separators centred between adjacent labels. Returns the created
-- per-item records ({ text }) for the later navigation step.
local function createStrip(parent, font, name, cy, labels, selectedIndex, gap, withSeparators, leftCap, rightCap)
    local items = {}

    -- LB/RB or LT/RT keycap hints hugging the page margins.
    createKeycap(parent, font, name .. '.keyL',
        EDGE_MARGIN + KEYCAP_W * 0.5, cy, leftCap)
    createKeycap(parent, font, name .. '.keyR',
        DESIGN_W - EDGE_MARGIN - KEYCAP_W * 0.5, cy, rightCap)

    local widths = {}
    for i, label in ipairs(labels) do
        widths[i] = #label * SMALL_CELL
    end
    local slots = tapeScroll(widths, selectedIndex, STRIP_WIDTH, gap)

    -- Labels (invisible ones are skipped: no clipping is available).
    for i, slot in ipairs(slots) do
        if not slot.visible then
            items[i] = nil
        else
            local cx = STRIP_LEFT - DESIGN_W * 0.5 + slot.x + widths[i] * 0.5
            local selected = i == selectedIndex
            if selected then
                createGlow(parent, name .. '.' .. i .. '.glow', cx, cy, widths[i])
            end
            local _, t = addText(parent, font, name .. '.' .. i, cx, cy,
                widths[i], SMALL_CELL,
                selected and whiteSpan(labels[i], 1.0) or greySpan(labels[i], 0.75),
                TextFlags.xCenter | TextFlags.yMiddle)
            items[i] = { text = t }
        end
    end

    -- Thin "|" separators, centred in each gap; shown only while BOTH of its
    -- neighbouring labels are visible.
    if withSeparators then
        for i = 1, #slots - 1 do
            if slots[i].visible and slots[i + 1].visible then
                local sepCx = STRIP_LEFT - DESIGN_W * 0.5
                    + slots[i].x + widths[i] + gap * 0.5 - SEP_W * 0.5
                addQuad(parent, name .. '.sep' .. i, sepCx, cy, SEP_W, SEP_H, COLOR_SEPARATOR)
            end
        end
    end

    return items
end

-- Full-width horizontal hairline under a strip.
local function createRule(s, top)
    addQuadAt(s.root, 'options.rule' .. top,
        EDGE_MARGIN, top, DESIGN_W - EDGE_MARGIN * 2, 1, COLOR_RULE)
end

-------------------------------------------------------------------------------
-- List widgets ---------------------------------------------------------------
-- Everything here hangs off s.dyn and is recreated by refresh() on every
-- state change (see the module header comment).
-------------------------------------------------------------------------------

-- '<'/'>' chevron pair tinted like the row (bright on the cursor row).
local function addChevrons(parent, font, name, cy, selected, leftCx, rightCx)
    local tint = selected and whiteSpan or greySpan
    local alpha = selected and 1.0 or 0.5
    addText(parent, font, name .. '.chevL', leftCx, cy, SMALL_CELL, ROW_HEIGHT,
        tint('<', alpha), TextFlags.xCenter | TextFlags.yMiddle)
    addText(parent, font, name .. '.chevR', rightCx, cy, SMALL_CELL, ROW_HEIGHT,
        tint('>', alpha), TextFlags.xCenter | TextFlags.yMiddle)
end

-- Toggle/enum value: '<' label '>' cluster centred in the value column with a
-- segmented indicator underneath: one small dash per possible value, lit up to
-- and including the current index.
local function addChoiceWidget(s, i, row, top, selected)
    local def = row.def
    local labels = def.labels or {}
    local label = labels[row.value + 1] or tostring(row.value)
    local labelW = #label * SMALL_CELL
    local cluster = labelW + (SMALL_CELL + CHOICE_GAP) * 2
    local left = VALUE_CX - cluster * 0.5
    local cy = top + ROW_HEIGHT * 0.5
    local tint = selected and whiteSpan or greySpan
    local alpha = selected and 1.0 or 0.5
    local name = 'options.list.value' .. i

    addChevrons(s.dyn, s.font, name, cy, selected,
        left + SMALL_CELL * 0.5, left + cluster - SMALL_CELL * 0.5)
    addText(s.dyn, s.font, name .. '.label',
        left + SMALL_CELL + CHOICE_GAP + labelW * 0.5, cy, labelW, ROW_HEIGHT,
        tint(label, alpha), TextFlags.xCenter | TextFlags.yMiddle)

    local dashes = math.min(#labels, SEG_MAX_DASHES)
    local dashTop = top + ROW_HEIGHT - SEG_DASH_H - SEG_BOTTOM_INSET
    local totalW = dashes * SEG_DASH_W + (dashes - 1) * SEG_DASH_GAP
    for j = 0, dashes - 1 do
        addQuadAt(s.dyn, name .. '.seg' .. j,
            VALUE_CX - totalW * 0.5 + j * (SEG_DASH_W + SEG_DASH_GAP), dashTop,
            SEG_DASH_W, SEG_DASH_H,
            j <= row.value and COLOR_SEG_LIT or COLOR_SEG_OFF)
    end
end

-- Range value: a tick ruler with everything at/before the value filled in, a
-- taller cursor marker on the value's stop, chevrons at the ruler ends and the
-- numeric readout right-aligned at the value column edge.
local function addRangeWidget(s, i, row, top, selected)
    local def = row.def
    local lo = def.min or 0
    local hi = def.max or lo
    local step = ((def.step or 1) > 1) and def.step or 1
    local stops = math.floor(math.max(hi - lo, 0) / step) + 1
    local value = row.value
    if value < lo then
        value = lo
    end
    if value > hi then
        value = hi
    end
    local stopIdx = math.floor((value - lo) / step)
    local cy = top + ROW_HEIGHT * 0.5
    local name = 'options.list.value' .. i

    addChevrons(s.dyn, s.font, name, cy, selected,
        RULER_LEFT - RULER_CHEV_OFF, RULER_RIGHT + RULER_CHEV_OFF)

    -- Centre of the tick for one stop index along the ruler.
    local function tickCx(stopIndex)
        if stops <= 1 then
            return (RULER_LEFT + RULER_RIGHT) * 0.5
        end
        return RULER_LEFT + (RULER_RIGHT - RULER_LEFT) * stopIndex / (stops - 1)
    end

    -- Thin 1px ticks, one per stop; sample long rulers down to RULER_MAX_TICKS.
    local stride = math.ceil(stops / RULER_MAX_TICKS)
    local lastStop = -1
    for stopIndex = 0, stops - 1, stride do
        addQuadAt(s.dyn, name .. '.ruler' .. stopIndex,
            math.floor(tickCx(stopIndex)), cy - RULER_TICK_H * 0.5,
            RULER_TICK_W, RULER_TICK_H,
            stopIndex <= stopIdx and COLOR_TICK_LIT or COLOR_TICK_DIM)
        lastStop = stopIndex
    end
    if lastStop ~= stops - 1 then
        addQuadAt(s.dyn, name .. '.ruler' .. (stops - 1),
            math.floor(tickCx(stops - 1)), cy - RULER_TICK_H * 0.5,
            RULER_TICK_W, RULER_TICK_H,
            stops - 1 <= stopIdx and COLOR_TICK_LIT or COLOR_TICK_DIM)
    end

    -- Cursor marker pinned to the value's exact stop position.
    addQuadAt(s.dyn, name .. '.cursor',
        math.floor(tickCx(stopIdx)), cy - RULER_CURSOR_H * 0.5,
        RULER_CURSOR_W, RULER_CURSOR_H, COLOR_TICK_CURSOR)

    addTextBox(s.dyn, s.font, name .. '.readout',
        VALUE_RIGHT - RANGE_READOUT_W, top, RANGE_READOUT_W, ROW_HEIGHT,
        selected and whiteSpan(tostring(value), 1.0)
            or greySpan(tostring(value), 0.5),
        TextFlags.xRight | TextFlags.yMiddle)
end

-- External value: the icon texture scaled into the column, or a hollow
-- placeholder square when the texture failed to load.
local function addExternalWidget(s, i, top)
    local cx = VALUE_CX
    local cy = top + ROW_HEIGHT * 0.5
    local name = 'options.list.value' .. i
    if s.externalTex ~= nil then
        addSprite(s.dyn, name .. '.icon', cx, cy, EXTERNAL_ICON, s.externalTex)
        return
    end
    local l = cx - EXTERNAL_ICON * 0.5
    local t = cy - EXTERNAL_ICON * 0.5
    addQuadAt(s.dyn, name .. '.fallback.top',
        l, t, EXTERNAL_ICON, 1, COLOR_ICON_FALLBACK)
    addQuadAt(s.dyn, name .. '.fallback.bottom',
        l, t + EXTERNAL_ICON - 1, EXTERNAL_ICON, 1, COLOR_ICON_FALLBACK)
    addQuadAt(s.dyn, name .. '.fallback.left',
        l, t, 1, EXTERNAL_ICON, COLOR_ICON_FALLBACK)
    addQuadAt(s.dyn, name .. '.fallback.right',
        l + EXTERNAL_ICON - 1, t, 1, EXTERNAL_ICON, COLOR_ICON_FALLBACK)
end

local function buildListArea(s)
    local rows = s.rows
    local total = #rows
    if total == 0 then
        return
    end
    local font = s.font
    local bannerW = LIST_RIGHT - EDGE_MARGIN
    local lastRow = math.min(total, s.scrollRow + VISIBLE_ROWS - 1)
    local prevType = nil

    for i = s.scrollRow, lastRow do
        local row = rows[i]
        local top = ROW_TOP + (i - s.scrollRow) * ROW_HEIGHT
        local name = 'options.list.row' .. i

        if row.type == 'header' then
            -- Group banner: darker fill with a thin bottom edge, grey label.
            addQuadAt(s.dyn, name .. '.fill', EDGE_MARGIN, top,
                bannerW, ROW_HEIGHT - 1, COLOR_BANNER)
            addQuadAt(s.dyn, name .. '.edge', EDGE_MARGIN, top + ROW_HEIGHT - 1,
                bannerW, 1, COLOR_BANNER_EDGE)
            addTextBox(s.dyn, font, name .. '.label',
                EDGE_MARGIN + ROW_TEXT_PAD, top, bannerW - ROW_TEXT_PAD * 2, ROW_HEIGHT,
                greySpan(row.label, 0.7), TextFlags.xLeft | TextFlags.yMiddle)
        else
            local selected = i == s.cursorRow
            if selected then
                createRowBand(s.dyn, 'options.list.band' .. i, top, bannerW)
            end
            addTextBox(s.dyn, font, name,
                EDGE_MARGIN + ROW_TEXT_PAD, top, bannerW - ROW_TEXT_PAD * 2, ROW_HEIGHT,
                selected and whiteSpan(row.def.title, 1.0)
                    or greySpan(row.def.title, 0.5),
                TextFlags.xLeft | TextFlags.yMiddle)

            -- 1px dark separator between consecutive option rows only; header
            -- banners carry their own bottom edge.
            if prevType == 'option' then
                addQuadAt(s.dyn, 'options.list.sep' .. i,
                    EDGE_MARGIN, top - 1, bannerW, 1, COLOR_ROW_SEP)
            end

            local kind = row.def.kind
            if kind == 'toggle' or kind == 'enum' then
                addChoiceWidget(s, i, row, top, selected)
            elseif kind == 'range' then
                addRangeWidget(s, i, row, top, selected)
            else
                addExternalWidget(s, i, top)
            end
        end
        prevType = row.type
    end
end

-- Thumb segment sized/positioned as the visible window over all display rows.
local function buildScrollbarThumb(s)
    local total = #s.rows
    if total <= VISIBLE_ROWS then
        return -- everything fits: no thumb
    end
    local trackH = SCROLLBAR_BOTTOM - LIST_TOP
    local thumbH = math.max(SCROLL_THUMB_MIN,
        math.floor(trackH * VISIBLE_ROWS / total))
    local t = (s.scrollRow - 1) / (total - VISIBLE_ROWS)
    addQuadAt(s.dyn, 'options.scrollbar.thumb',
        SCROLLBAR_X, LIST_TOP + math.floor(t * (trackH - thumbH)),
        SCROLLBAR_W, thumbH, COLOR_SEGMENT)
end

-- Bottom strip: heading line, up to two wrapped detail lines and, for options,
-- a "(Default: ...)" line. Header rows show the group name + generic hint.
local function buildTooltip(s)
    local font = s.font
    local width = DESIGN_W - EDGE_MARGIN * 2
    local row = s.rows[s.cursorRow]

    if row == nil then
        addTextBox(s.dyn, font, 'options.tooltip.heading',
            EDGE_MARGIN, TOOLTIP_HEADING_TOP, width, SMALL_CELL + 2,
            greySpan('OPTIONS', 0.85), TextFlags.xLeft | TextFlags.yTop)
        return
    end

    local heading, details
    if row.type == 'header' then
        heading = row.label
        details = { TOOLTIP_HINT }
    else
        heading = row.def.title
        details = tooltipLines(row.def.tooltip)
    end

    addTextBox(s.dyn, font, 'options.tooltip.heading',
        EDGE_MARGIN, TOOLTIP_HEADING_TOP, width, SMALL_CELL + 2,
        greySpan(heading, 0.85), TextFlags.xLeft | TextFlags.yTop)
    for j = 1, #details do
        addTextBox(s.dyn, font, 'options.tooltip.detail' .. j,
            EDGE_MARGIN, TOOLTIP_DETAIL_TOP + (j - 1) * TOOLTIP_LINE_STEP,
            width, SMALL_CELL,
            whiteSpan(details[j], 0.8), TextFlags.xLeft | TextFlags.yTop)
    end
    if row.type == 'option' then
        addTextBox(s.dyn, font, 'options.tooltip.default',
            EDGE_MARGIN, TOOLTIP_DEFAULT_TOP, width, SMALL_CELL,
            greySpan('(Default: ' .. defaultLabel(row.def) .. ')', 0.75),
            TextFlags.xLeft | TextFlags.yTop)
    end
end

local function buildSubTabs(s)
    s.subtabs = createStrip(s.dyn, s.font, 'options.subtab', SUB_STRIP_CY,
        s.subcats, s.subcategoryIndex, SUB_GAP, false, 'LT', 'RT')
end

-- Tear down the previous dynamic subtree (if any) and rebuild it from the
-- current state table.
local function rebuildDynamic(s)
    if s.dyn ~= nil then
        scene.destroy(s.dyn)
        s.dyn = nil
    end
    local dyn = scene.createEntity(s.root)
    scene.setField(dyn, 'name', 'options.dyn')
    scene.setField(dyn, 'x', 0) -- identity transform: children keep using
    scene.setField(dyn, 'y', 0) -- design-space offsets from the root centre
    s.dyn = dyn

    buildSubTabs(s)
    buildListArea(s)
    buildScrollbarThumb(s)
    buildTooltip(s)
end

-------------------------------------------------------------------------------
-- State ----------------------------------------------------------------------

-- First half of open(): read the registry and derive the navigation baseline
-- before any entity exists (the tab strips need the category list). `restore`
-- (optional) is a previous session's { categoryIndex, subcategoryIndex,
-- cursorRow, scrollRow } mirrored through the shared table by persistState();
-- out-of-range values are clamped here and by the following refresh().
local function initNavState(s, restore)
    s.open = true
    s.defs = options.list() or {}
    s.categories = uniqueField(s.defs, 'category')
    s.categoryIndex = 1
    s.subcategoryIndex = 1
    s.cursorRow = 1 -- index into s.rows; may land on a header until navigation skips it
    s.scrollRow = 1 -- first visible row
    s.rows = {}
    -- Cursor memory per (category, subcategory) pair, keyed "cat|sub" -> row.
    -- In-memory only: discarded when the overlay closes.
    s.cursorMemory = {}
    -- Per-direction hold-repeat countdowns for value editing.
    s.holds = { left = { timer = 0 }, right = { timer = 0 } }

    if type(restore) == 'table' then
        if type(restore.categoryIndex) == 'number' and restore.categoryIndex >= 1
            and restore.categoryIndex <= #s.categories then
            s.categoryIndex = math.floor(restore.categoryIndex)
        end
        if type(restore.subcategoryIndex) == 'number' and restore.subcategoryIndex >= 1 then
            s.subcategoryIndex = math.floor(restore.subcategoryIndex)
        end
        if type(restore.cursorRow) == 'number' and restore.cursorRow >= 1 then
            s.cursorRow = math.floor(restore.cursorRow)
        end
        if type(restore.scrollRow) == 'number' and restore.scrollRow >= 1 then
            s.scrollRow = math.floor(restore.scrollRow)
        end
    end
end

-- Mirror the navigation state into the cross-VM `shared` table so a hot reload
-- of the game module can restore the overlay exactly as it was (menu.lua
-- reopens the overlay from shared.options when OPENRE_FAST_RELOAD is set).
local function persistState(s)
    if shared == nil then
        return
    end
    shared.options = {
        open = true,
        categoryIndex = s.categoryIndex,
        subcategoryIndex = s.subcategoryIndex,
        cursorRow = s.cursorRow,
        scrollRow = s.scrollRow,
    }
end

-- Recompute everything derived from the state table and redraw the dynamic
-- subtree. open() calls it once; the navigation actions below call it after
-- every category/cursor/value change (exactly once per input frame).
local function refresh(s)
    if s.categoryIndex < 1 or s.categoryIndex > #s.categories then
        s.categoryIndex = 1
    end
    s.subcats = subCategories(s)
    if s.subcategoryIndex < 1 or s.subcategoryIndex > #s.subcats then
        s.subcategoryIndex = 1
    end
    s.rows = rebuildRows(s)
    clampScroll(s, VISIBLE_ROWS)
    persistState(s)

    local ok, err = pcall(rebuildDynamic, s)
    if not ok then
        print('[options] refresh failed: ' .. tostring(err))
    end
end

-------------------------------------------------------------------------------
-- Input navigation -----------------------------------------------------------
-- Small named actions shared by the tick handler: each one updates the state
-- table then redraws exactly once through refresh().
-------------------------------------------------------------------------------

-- "category|subcategory" key of the current location (cursor memory).
local function locationKey(s)
    return categoryName(s) .. '|' .. tostring(s.subcats[s.subcategoryIndex])
end

-- Any state change restarts both hold-repeat countdowns.
local function resetHolds(s)
    s.holds.left.timer = 0
    s.holds.right.timer = 0
end

-- Move the list cursor one selectable row up/down. Header rows are skipped,
-- the ends do not wrap, and clampScroll() inside refresh() keeps the cursor
-- row fully visible - scrolling by exactly one row at the window edges. The
-- landing row is remembered for this tab pair.
local function moveCursor(s, delta)
    local rows = s.rows
    if rows == nil or #rows == 0 then
        return
    end
    -- Landed on a header (fresh open): vertical input snaps to the nearest
    -- selectable row instead of stepping past it.
    local from = s.cursorRow + delta
    if rows[s.cursorRow] ~= nil and rows[s.cursorRow].type ~= 'option' then
        from = s.cursorRow
    end
    local target = findSelectable(rows, from, delta)
    if target == nil then
        return
    end
    s.cursorRow = target
    s.cursorMemory[locationKey(s)] = target
    resetHolds(s)
    refresh(s)
end

-- Step the cursor option's value by delta (+/-1). Toggles/enums move their
-- label index without wrapping at the ends; ranges move along their step grid
-- clamped to [min, max]; external rows have no editable value here. A value
-- that did not change (clamped) or a rejected options.set is ignored.
local function adjustValue(s, delta)
    local row = s.rows ~= nil and s.rows[s.cursorRow] or nil
    if row == nil or row.type ~= 'option' then
        return
    end
    local def = row.def
    if def.kind ~= 'toggle' and def.kind ~= 'enum' and def.kind ~= 'range' then
        return
    end
    local value = options.get(def.id)
    if value == nil then
        return
    end
    local newValue
    if def.kind == 'range' then
        local step = ((def.step or 1) > 1) and def.step or 1
        newValue = value + delta * step
        local lo = def.min or 0
        local hi = def.max or lo
        if newValue < lo then
            newValue = lo
        end
        if newValue > hi then
            newValue = hi
        end
    else
        local count = #(def.labels or {})
        newValue = value + delta
        if newValue < 0 or newValue >= count then
            return -- against an end chevron: clamped, no wrap
        end
    end
    if newValue == value or not options.set(def.id, newValue) then
        return
    end
    refresh(s)
end

-- Shared tail of a tab/subtab switch: recompute the subcategory list for the
-- new location, restore its remembered cursor row (or the first selectable
-- row), park the scroll window around it and redraw once.
local function enterLocation(s)
    s.subcats = subCategories(s)
    if s.subcategoryIndex < 1 or s.subcategoryIndex > #s.subcats then
        s.subcategoryIndex = 1
    end
    local rows = rebuildRows(s)
    local remembered = s.cursorMemory[locationKey(s)]
    local target = nil
    if remembered ~= nil and rows[remembered] ~= nil
        and rows[remembered].type == 'option' then
        target = remembered
    end
    if target == nil then
        target = findSelectable(rows, 1, 1) or 1
    end
    s.rows = rows
    s.cursorRow = target
    -- Park the scroll window around the restored cursor (centre-ish), clamped
    -- to the list; clampScroll() inside refresh() finishes the job.
    s.scrollRow = math.max(1, math.min(
        target - math.floor(VISIBLE_ROWS * 0.5),
        math.max(1, #rows - VISIBLE_ROWS + 1)))
    resetHolds(s)
    refresh(s)
end

-- Switch the outer tab, wrapping around both ends; lands on the new
-- category's first subcategory with its remembered cursor restored.
local function switchCategory(s, delta)
    local count = #s.categories
    if count == 0 then
        return
    end
    s.categoryIndex = ((s.categoryIndex - 1 + delta) % count) + 1
    s.subcategoryIndex = 1
    enterLocation(s)
end

-- Switch the inner subtab of the current category, wrapping around both ends.
local function switchSubcategory(s, delta)
    local count = #s.subcats
    if count == 0 then
        return
    end
    s.subcategoryIndex = ((s.subcategoryIndex - 1 + delta) % count) + 1
    enterLocation(s)
end

-- Auto-repeat for held left/right VALUE EDITS only: the pressed edge already
-- applied the first step, so after HOLD_REPEAT_DELAY frames of holding the
-- same step fires again, then one more every HOLD_REPEAT_RATE frames.
-- Releasing - or any navigation via resetHolds() - restarts the countdown.
local function tickHold(s, direction, delta)
    local hold = s.holds[direction]
    if not input.down(direction) then
        hold.timer = 0
        return
    end
    hold.timer = hold.timer + 1
    if hold.timer >= HOLD_REPEAT_DELAY
        and (hold.timer - HOLD_REPEAT_DELAY) % HOLD_REPEAT_RATE == 0 then
        adjustValue(s, delta)
    end
end

-------------------------------------------------------------------------------
-- Open / close / tick --------------------------------------------------------
-------------------------------------------------------------------------------

-- Keep one scaled-frame layer tracking the window: recentre it and refresh its
-- target so ScaledFrameSystem recomputes the contain-fit scale every frame.
local function fitLayer(entity, frame)
    local w = gfx.screenWidth()
    local h = gfx.screenHeight()
    scene.setField(entity, 'x', w * 0.5)
    scene.setField(entity, 'y', h * 0.5)
    scene.setField(frame, 'target.width', w)
    scene.setField(frame, 'target.height', h)
end

-- Solid backdrop covering the whole window, behind everything (BACKDROP_Z).
local function refreshBackdrop(s)
    local w = gfx.screenWidth()
    local h = gfx.screenHeight()
    scene.setField(s.backdrop, 'x', w * 0.5)
    scene.setField(s.backdrop, 'y', h * 0.5)
    scene.setField(s.backdropSprite, 'size.width', w)
    scene.setField(s.backdropSprite, 'size.height', h)
end

local function update(s)
    -- Re-centre/re-fit every frame so resizes are tracked.
    fitLayer(s.root, s.frame)
    refreshBackdrop(s)

    -- Cancel persists any edited values, then closes.
    if input.pressed('cancel') then
        options.flush()
        Options.close()
        return
    end

    -- List cursor: edge-triggered only, never auto-repeats.
    if input.pressed('up') then
        moveCursor(s, -1)
    elseif input.pressed('down') then
        moveCursor(s, 1)
    end

    if input.pressed('left') then
        adjustValue(s, -1)
    elseif input.pressed('right') then
        adjustValue(s, 1)
    end

    -- Outer tabs wrap; subtabs wrap within the current category.
    if input.pressed('tab_left') then
        switchCategory(s, -1)
    elseif input.pressed('tab_right') then
        switchCategory(s, 1)
    end

    if input.pressed('subtab_left') then
        switchSubcategory(s, -1)
    elseif input.pressed('subtab_right') then
        switchSubcategory(s, 1)
    end

    -- Held left/right keeps stepping the edited value (value editing only).
    tickHold(s, 'left', -1)
    tickHold(s, 'right', 1)
end

local function teardown(s)
    if s == nil then
        return
    end
    -- The backdrop lives directly under the user root; everything else hangs
    -- off the content root and dies with it.
    if s.backdrop ~= nil then
        scene.destroy(s.backdrop)
    end
    if s.root ~= nil then
        scene.destroy(s.root)
    end
end

-- Build the static chrome (backdrop, scaled-frame root, heading, outer tab
-- strip, rules, scrollbar track) into the (already created) state table `s`.
-- Running it under pcall with a pre-made table means a failure midway can
-- still tear down whatever was created so far.
local function buildAll(s)
    -- Solid black overlay across the whole window, painted behind everything
    -- via its explicit z.
    s.backdrop = scene.createEntity(scene.getRoot('user'))
    scene.setField(s.backdrop, 'name', 'options.backdrop')
    s.backdropSprite = scene.addComponent(s.backdrop, 'core.sprite')
    scene.setField(s.backdropSprite, 'color', COLOR_BACKDROP)
    scene.setField(s.backdropSprite, 'size.width', gfx.screenWidth())
    scene.setField(s.backdropSprite, 'size.height', gfx.screenHeight())
    scene.setField(s.backdropSprite, 'z', BACKDROP_Z)
    scene.setField(s.backdrop, 'x', gfx.screenWidth() * 0.5)
    scene.setField(s.backdrop, 'y', gfx.screenHeight() * 0.5)

    -- Content root: maps the 640x480 design space onto the window with a
    -- native scaled frame ("contain"). fitLayer() places it at the window
    -- centre every tick; children live in offsets from that centre.
    s.root = scene.createEntity(scene.getRoot('user'))
    scene.setField(s.root, 'name', 'options.root')
    s.frame = scene.addComponent(s.root, 'core.scaled_frame')
    scene.setField(s.frame, 'source.width', DESIGN_W)
    scene.setField(s.frame, 'source.height', DESIGN_H)
    scene.setField(s.frame, 'mode', 'contain')
    scene.setField(s.frame, 'increment', 0)
    fitLayer(s.root, s.frame)

    s.font = gfx.loadFont(FONT_PATH)
    if s.font == nil then
        print('[options] failed to load font: ' .. FONT_PATH)
    end

    -- External-action icon, loaded once per open; a nil texture falls back to
    -- a hollow placeholder square (warned once, here).
    s.externalTex = gfx.loadTexture(EXTERNAL_TEXTURE)
    if s.externalTex == nil then
        print('[options] external icon texture not found: ' .. EXTERNAL_TEXTURE)
    end

    -- Heading.
    addTextBox(s.root, s.font, 'options.heading',
        HEADING_LEFT, HEADING_TOP, HEADING_BAND_W, HEADING_BAND_H,
        whiteSpan('OPTIONS', 0.95), TextFlags.xLeft | TextFlags.yTop)

    -- Outer tab strip with LB/RB hints and "|" separators; one tab per
    -- registered category.
    s.tabs = createStrip(s.root, s.font, 'options.tab', TAB_STRIP_CY,
        s.categories, s.categoryIndex, TAB_GAP, true, 'LB', 'RB')
    createRule(s, TAB_RULE_Y)

    -- Static hairline under the (dynamic) subtab strip.
    createRule(s, SUB_RULE_Y)

    -- Scrollbar track; the thumb is rebuilt by refresh().
    addQuadAt(s.root, 'options.scrollbar.track',
        SCROLLBAR_X, LIST_TOP, SCROLLBAR_W, SCROLLBAR_BOTTOM - LIST_TOP, COLOR_TRACK)
end

function Options.open(restore)
    if state ~= nil then
        return -- already open
    end

    local draft = {}
    local ok, err = pcall(initNavState, draft, restore)
    if not ok then
        print('[options] open failed: ' .. tostring(err))
        return
    end

    -- Build the whole entity tree into the (already created) state table.
    -- Running it under pcall means a failure midway can still tear down
    -- whatever was created so far.
    ok, err = pcall(buildAll, draft)
    if not ok then
        print('[options] open failed: ' .. tostring(err))
        pcall(teardown, draft)
        return
    end

    state = draft
    ok, err = pcall(refresh, state) -- rows + dynamic subtree + tooltip
    if not ok then
        print('[options] open failed: ' .. tostring(err))
        state = nil
        pcall(teardown, draft)
        return
    end
    -- Never rest the initial cursor on a group banner (a restored cursorRow
    -- may also exceed the rebuilt rows - clamp it first).
    local rows = state.rows
    if state.cursorRow > #rows then
        state.cursorRow = math.max(1, #rows)
    end
    if rows[state.cursorRow] ~= nil and rows[state.cursorRow].type ~= 'option' then
        local target = findSelectable(rows, 1, 1)
        if target ~= nil then
            state.cursorRow = target
        end
    end
    print('[options] opened')
end

function Options.close()
    local s = state
    if s == nil then
        return
    end
    state = nil -- stop the tick handler before touching the tree
    -- Record the closed state so a hot reload does not resurrect the overlay.
    if shared ~= nil then
        shared.options = { open = false }
    end
    local ok, err = pcall(teardown, s)
    if not ok then
        print('[options] close failed: ' .. tostring(err))
        return
    end
    print('[options] closed')
end

function Options.isOpen()
    return state ~= nil
end

-- The sandbox exposes no unsubscribe API, so the tick subscription made below
-- is permanent but inert: it does nothing until open() populates `state`, and
-- stops doing work once close() clears it.
re.subscribe(HookKind.tick, function()
    local s = state
    if s == nil then
        return
    end
    local ok, err = pcall(update, s)
    if not ok then
        print('[options] tick error: ' .. tostring(err))
    end
end)

return {
    open = Options.open,
    close = Options.close,
    isOpen = Options.isOpen,
}
