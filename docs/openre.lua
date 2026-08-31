---@meta
--[[
    openre.lua — LuaLS definition file for the OpenRE Lua scripting API.

    This file is never executed; it only provides type annotations and
    autocompletion for the lua-language-server
    (https://luals.github.io/wiki/definition-files/).

    Scripts run in a sandboxed VM: there is no os, io, package or debug
    library. The controlled require() resolves modules within the script
    directories (dots become directory separators).
]]

--[[ Globals ]]

--- Write to stdout, prefixed with `[scriptname]`.
---@param ... any
function print(...) end

--- Controlled module loader. Dots become directory separators
--- (e.g. "foo.bar" resolves to foo/bar.lua inside a script directory).
---@param name string
---@return any
function require(name) end

--[[ Enum tables (runtime globals) ]]

---@enum LogLevel
LogLevel = {
    info = 1,
    warning = 2,
    error = 3,
    debug = 4,
}

---@enum EntityKind
EntityKind = {
    player = 1,
    splayer = 2,
    enemy = 3,
    object = 4,
    door = 5,
}

---@enum SpriteFlags
SpriteFlags = {
    repeatX = 1 << 0,
    repeatY = 1 << 1,
    stretchX = 1 << 2,
    stretchY = 1 << 3,
}

---@enum TextFlags
TextFlags = {
    xLeft   = 0 << 0,
    xCenter = 1 << 0,
    xRight  = 2 << 0,
    yTop    = 0 << 2,
    yMiddle = 1 << 2,
    yBottom = 2 << 2,
}

--[[ Flag groups ]]

-- FlagGroup is not exposed as a runtime table; pass these numeric values as
-- the `group` argument of re.getFlag / re.setFlag.
---@enum FlagGroup
local FlagGroup = {
    System = 0,
    Status = 1,
    Stop = 2,
    Scenario = 3,
    Common = 4,
    Room = 5,
    Enemy = 6,
    Enemy2 = 7,
    Item = 8,
    Map = 9,
    Use = 10,
    Message = 11,
    RoomEnemy = 12,
    Pbf00 = 13,
    Pbf01 = 14,
    Pbf02 = 15,
    Pbf03 = 16,
    Pbf04 = 17,
    Pbf05 = 18,
    Pbf06 = 19,
    Pbf07 = 20,
    Pbf08 = 21,
    Pbf09 = 22,
    Pbf0A = 23,
    Pbf0B = 24,
    Pbf0C = 25,
    Pbf0D = 26,
    Pbf0E = 27,
    Pbf0F = 28,
    Zapping = 29,
    RbjSet = 30,
    Key = 31,
    MapC = 32,
    MapI = 33,
    Item2 = 34,
}

--[[ Entities ]]

---@class Entity
---@field kind EntityKind Which kind this entity is.
---@field index integer Index within its kind.
---@field type integer Type id (enemy/object/door).
---@field life integer Current health (player/splayer/enemy).
---@field maxLife integer Maximum health (player/splayer/enemy).
---@field posX number World position X.
---@field posY number World position Y.
---@field posZ number World position Z.
---@field id integer Instance id (player/splayer only).
local Entity = {}

--[[ The re namespace ]]

---@class re
local re = {}

--- All registered systems (native `core.*` and Lua-managed) in resolved
--- dispatch order. Each entry is
--- `{ name, kind = "native"|"lua", placement = "early"|"default"|"late", after = string[], before = string[] }`.
--- Used by the debug web inspector.
---@return {name:string, kind:string, placement:string, after:string[], before:string[]}[]
function re.systems() end

--- Register a Lua-managed ECS system with the engine for inspection and
--- dispatch. Called by `ecs.registerSystem`.
---@param name string Canonical system name (e.g. `ui.config.ListItem`).
---@param after string[] Systems this must run after.
---@param before string[] Systems this must run before.
---@param placement string `"early"`, `"default"` or `"late"`.
---@param managedId integer Opaque id resolving the runner callback.
function re.registerSystem(name, after, before, placement, managedId) end

--- Clear the Lua-managed system registry (called when the game module reloads).
function re.resetManagedSystems() end

--- Install the callback used to dispatch a Lua-managed system by its id. Set
--- once by the ECS module; the callback resolves the id to a function.
---@param fn function Called with the managed system id.
function re.setSystemRunner(fn) end

--- Every registered component's registered fields and their types.
--- Native components are `core.*`, Lua-defined ones are runtime components.
--- Each entry is `{ name, kind = "native"|"runtime", fields = { { name, type, default } } }`.
--- Used by the debug web inspector.
---@return {name:string, kind:string, fields:{name:string, type:string, default:string}[]}[]
function re.components() end

--- Read a bit flag. Bit indexes use MSB-first numbering
--- (index 0 = 0x80000000, index 31 = 0x00000001).
---@param group FlagGroup
---@param index integer
---@return boolean
function re.getFlag(group, index) end

--- Write a bit flag.
---@param group FlagGroup
---@param index integer
---@param value boolean
function re.setFlag(group, index, value) end

--- Get an entity by kind and index. Returns nil when the slot is empty or
--- no game is loaded.
---@param kind EntityKind
---@param index integer
---@return Entity|nil
function re.getEntity(kind, index) end

--- Read an environment variable of the host process.
---@param name string
---@return string|nil
function re.getEnv(name) end

--- Run a full Lua garbage collection. Resource handles (textures, fonts,
--- sounds) unload their underlying resource when collected, so call this
--- after dropping the last references to free them immediately instead of
--- waiting for the automatic collector — e.g. when a screen hands over to
--- the next one.
function re.collectGarbage() end

--- Route a message to the C++ logger.
---@param level LogLevel
---@param msg string
function re.log(level, msg) end

-- DEBUG builds only.
--- Evaluate Lua code inside the script's VM. Returns (true, result) on
--- success or (false, errorString) on failure. The function may be absent in
--- release builds — always guard with `if re.eval then`.
---@param code string
---@param name? string Chunk name used in error messages.
---@return boolean, any
function re.eval(code, name) end

--[[ re.unsafe — raw memory access ]]

---@class re.unsafe
re.unsafe = {}

-- DEBUG builds only.
--- Read raw process memory as a 1-based table of bytes.
---@param address integer
---@param len integer
---@return integer[]
function re.unsafe.read(address, len) end

-- DEBUG builds only.
--- Write raw process memory.
---@param address integer
---@param bytes integer[] 1-based table of byte values.
function re.unsafe.write(address, bytes) end

--[[ re.network ]]

---@class re.network
re.network = {}

--- Create a TCP listener. Loopback addresses only
--- (127.0.0.1, localhost or ::1). Raises a catchable Lua error on bind
--- failure.
---@param host string
---@param port integer
---@return TcpListener
function re.network.createTcpListener(host, port) end

--[[ TCP ]]

---@class TcpListener
local TcpListener = {}

--- Accept a pending connection, or nil if there is none this frame.
--- Accepts happen on a background thread; poll from a tick handler.
---@return TcpSocket|nil
function TcpListener:pop() end

--- Close the listener.
function TcpListener:dispose() end

---@class TcpSocket
---@field localAddress string Read-only.
---@field localPort integer Read-only.
---@field remoteAddress string Read-only.
---@field remotePort integer Read-only.
---@field noDelay boolean Read/write. Disables Nagle's algorithm; default false.
local TcpSocket = {}

--- Best-effort read: up to `len` buffered bytes, or nil if nothing is
--- buffered. Reads happen on a background thread.
---@param len integer
---@return string|nil
function TcpSocket:read(len) end

--- Queue bytes for sending; returns the number of bytes accepted.
---@param bytes string
---@return integer
function TcpSocket:write(bytes) end

--- Flush buffered write data to the socket.
function TcpSocket:flush() end

--- Close the socket.
function TcpSocket:dispose() end

--[[ Scene entities & components (module mode) ]]

---@class SceneEntity
--- Opaque handle to an entity in the current ECS scene. Created with
--- `scene.createEntity`. Pass to `scene.addComponent`, `scene.setField`
--- and `scene.getField`.
local SceneEntity = {}

---@class SceneComponent
--- Opaque handle to a component on an entity. Created with
--- `scene.addComponent`. Used as the handle for `scene.setField` /
--- `scene.getField`.
local SceneComponent = {}

---@class Texture
--- Opaque handle to a loaded GPU texture. Created with `gfx.loadTexture`.
--- The underlying resource is reference counted and released by the garbage
--- collector when the last handle is collected; nil-ing the Lua reference is
--- enough to free the GPU texture (after one final collection). Scene
--- components borrowing the texture through their `texture` field do NOT keep
--- it alive — keep the handle referenced for as long as a sprite uses it.
local Texture = {}

---@class Font
--- Opaque handle to a loaded RE2 font (a `.fnt` file, or a raw font0.tim /
--- font1.tim atlas). Created with `gfx.loadFont`. Reference counted and
--- released by the garbage collector; like textures, keep the handle
--- referenced while text components use it.
local Font = {}

--[[ The scene namespace (module mode) ]]

---@class scene
local scene = {}

--- Create a new entity. With no argument it is created under the scene root;
--- pass a parent (e.g. a `scene.getRoot(...)` handle) to nest it. Returns nil
--- when no scene is active (for example inside the legacy game).
---@param parent? SceneEntity Parent entity; defaults to the scene root.
---@return SceneEntity|nil
function scene.createEntity(parent) end

--- Return one of the three top-level sub-roots of the scene by name.
--- These are fixed containers the engine and Lua organise content under:
--- `"user"` (what Lua code has access to), `"game"` (contains the legacy game
--- entity) and `"overlay"` (mostly for debug overlay text).
---@param name string one of "user", "game", "overlay"
---@return SceneEntity
function scene.getRoot(name) end

--- Reparent an entity under another entity. Child entities inherit the
--- parent's transform: their `x`/`y` set via `scene.setField` are interpreted
--- as offsets relative to the parent's position (which itself may be nested).
--- Useful for grouping sprites that should move or scale together.
---@param child SceneEntity
---@param parent SceneEntity
function scene.setParent(child, parent) end

--- Add a component to an entity and return a component handle. Built-in
--- components are namespaced `core.*`; `app.*` names are reserved for future
--- Lua-defined components. `core.Transform` is implicit on every entity.
--- `core.Text` accepts the font (a `gfx.loadFont` value) as the component's
--- initial value, or set it later with `scene.setField(handle, 'font', f)`.
---@param entity SceneEntity
---@param name string e.g. "core.Sprite", "core.Text", "core.AudioEmitter",
---  "core.InputHandler" or "core.Transform"
---@return SceneComponent|nil
function scene.addComponent(entity, name) end

--- Write a field on an entity or component. When the handle is an entity, or
--- for a transform component, the fields are `name` (a human-readable string;
--- "" by default), `x`, `y`, `z` (world position in window pixels) and
--- `scale.x`, `scale.y`, `scale.z` (scale multiplier; 1 = natural size). Entity
--- positions are relative to the parent when the entity was attached with
--- `scene.setParent`, and a parent's scale is inherited by
--- its children (offsets and rendered sizes both scale). Sprite fields mirror the layout of the underlying C++
--- struct: `src.x`, `src.y`, `src.width`, `src.height` (source rect in texture
--- pixels; 0-sized means "full texture"), `texture` (a Texture or nil),
--- `flags` (a bitmask of SpriteFlags values combined with `|`) and `color`
--- (an RGBA tint/opacity table `{r, g, b, a}` with each channel 0-255; the `a`
--- channel is the sprite's opacity; defaults to opaque white). The draw
--- priority is the entity's transform `z` (set with
--- `scene.setField(entity, "z", value)`): sprites paint back-to-front, so a
--- higher `z` renders further behind and a lower `z` renders in front (equal
--- `z` keeps creation order).
--- A sprite is
--- drawn centred on its entity's `(x, y)` position (not top-left anchored), so
--- place the entity where you want the centre of the sprite; its drawn size
--- comes from the entity's `core.UiSize` component. Text fields:
--- `text` (UTF-8 string; supports `<span color="#ff0">` markup plus the
--- `#rrggbb` and `#rrggbbaa` forms — the last two hex digits are the alpha
--- channel, 0-255), `font` (a
--- Font or nil) and `flags` (a bitmask of TextFlags values combined with
--- `|`); the bounds the text aligns within come from the entity's
--- `core.UiSize` component.
--- UI size fields (`core.UiSize`): `width` / `height` (the entity's visual
--- size in window pixels — every sprite, text band and video reads them),
--- plus `weight` for flex layouts (see core.UiFlex) and `alignment` for the
--- cross-axis placement. `alignment` is one of "begin", "center", "end" or
--- "stretch" (default "stretch") and controls the axis the parent flex does
--- *not* flow along: in a horizontal flex it governs the child's height
--- (top/centre/bottom/fill), in a vertical flex its width (left/centre/right/
--- fill). It applies to every child regardless of `weight` and is unused when
--- the parent has no flex (or flex "none"). Flex containers write their
--- children's width/height every frame, so treat those as read-only on flex
--- children.
--- Audio emitter fields: `category` ("bgm", "sfx" or "voice"), `state`
--- ("playing" starts the source, anything else stops it — see below),
--- `source` (.sap file path relative to the game module directory),
--- `volume` (0-100), `pan` (-100..100; 0 = centre), `loop` (boolean),
--- `fadeFrames` / `fadeTargetVolume`, the read-only `status`, plus the
--- resident one-shot pair `sound` / `trigger` (see the audio emitter section
--- below for details).
--- Scaled frame fields (`core.ScaledFrame`): `source.width` / `source.height`
--- (authored design-space size), `target.width` / `target.height`
--- (window-pixel area to map onto), `mode` ("contain" fits the whole source
--- inside the target with letterbox margins, "cover" fills the target and
--- crops overflow) and `increment` (0 = exact fit scale; a positive value
--- snaps the scale DOWN to whole multiples of itself, e.g. 0.5 allows only
--- 0.5, 1.0, 1.5, ...). The engine writes the computed uniform scale to the
--- entity's transform every frame; position the entity yourself.
--- Input hint fields (`core.InputHint`): `command` (a registered input
--- command name like "pause" — its current bindings are shown as icons; a
--- single key name like "p" or an icon alias also works), `device` ("kbm",
--- "ps", "x" or "n"), `text` (optional label drawn right of the icons) and
--- `font` (optional label font). The icons are fixed 32x32 sprites; scale
--- the entity's transform to resize the whole hint.
--- Flex fields (`core.UiFlex`): `orientation` ("none", "horizontal" or
--- "vertical"; default "none"), `gap` (space between consecutive children in
--- the flow orientations) and `padding` (inset applied to every edge of the
--- content box). Every immediate child of a flex entity is laid out each
--- frame: `weight` (on `core.UiSize`, default 0) is the main-axis share —
--- children with positive weight grab a proportional slice of the leftover
--- main-axis space. The child's cross-axis behaviour is set by its
--- `core.UiSize` `alignment` ("begin", "center", "end" or "stretch";
--- default "stretch") and applies to every child regardless of weight. When
--- fixed content overflows, every literal size and the gap shrink by the same
--- factor so the run fits exactly; when no child has weight the whole run is
--- centred on the main axis. Orientation "none" is an overlay stack: each
--- child fills the box (weighted) or is clamped and centred on both axes
--- independently, and `alignment` is ignored.
---@param handle SceneEntity|SceneComponent
---@param name string
---@param value any
function scene.setField(handle, name, value) end

--- Read a single field on an entity or component. Returns nil for unknown
--- fields. Fields use the same names as scene.setField. The `texture` / `font`
--- fields are returned as the numeric ResourceCookie, not an object; `flags` is
--- returned as the numeric bitmask; the transform `name` field is returned as a
--- string; the sprite `color` field is returned as a `{r, g, b, a}` table.
---@param handle SceneEntity|SceneComponent
---@param name string
---@return any
function scene.getField(handle, name) end

--- Return a table of every live entity id (integers) in the current scene,
--- including the hidden root. Returns an empty table when no scene is active.
---@return integer[]
function scene.allEntities() end

--- Build a SceneEntity handle from an integer entity id, or nil when the scene
--- is inactive or the id is stale.
---@param id integer
---@return SceneEntity|nil
function scene.entityFromId(id) end

--- Build a SceneComponent handle for `(entity, typeName)` where `typeName` is
--- one of "core.Transform", "core.Sprite", "core.Text", "core.Legacy",
--- "core.Video", "core.AudioEmitter", "core.ScaledFrame",
--- "core.InputHint", "core.UiSize", "core.UiFlex", "core.InputHandler"
--- or a runtime component
--- name registered via `scene.defineComponent`. Returns nil when the entity
--- lacks that component.
---@param entity SceneEntity
---@param typeName string
---@return SceneComponent|nil
function scene.component(entity, typeName) end

--- Register a runtime (Lua-defined) component type. `fields` is an array of
--- `{ name = ..., type = "int"|"float"|"bool"|"string" [, default = ...] }`;
--- field order defines the row layout and must stay stable once shipped.
--- Registered components behave exactly like core.* ones afterwards: add
--- them with `scene.addComponent`, read/write them with scene.getField /
--- scene.setField, and they appear in the debug web inspector.
---@param name string e.g. "ui.TabStrip"
---@param fields table[]
function scene.defineComponent(name, fields) end

--- Remove a runtime component from an entity (no-op when absent).
---@param entity SceneEntity
---@param name string
function scene.removeComponent(entity, name) end

--- Return an array of entity ids (integers) carrying the named runtime
--- component, oldest first. The list is a snapshot, so entries may be added
--- or removed while iterating it.
---@param name string
---@return integer[]
function scene.entitiesWithComponent(name) end

--- Return a table of the component type names present on an entity (always
--- including "core.Transform").
---@param entity SceneEntity
---@return string[]
function scene.getComponents(entity) end

--- Return the parent entity id, or nil for the scene root / no scene.
---@param entity SceneEntity
---@return integer|nil
function scene.parent(entity) end

--- Return a table of child entity ids.
---@param entity SceneEntity
---@return integer[]
function scene.children(entity) end

--- Request the application to exit.
function scene.exit() end

--[[ Audio emitters (core.AudioEmitter) ]]

--- A sound emitter mixed by the native SDL3 audio layer. Add it with
--- `scene.addComponent(e, "core.AudioEmitter")` and configure its fields
--- with `scene.setField`. The emitter plays the sound resource referenced by
--- its `sound` field, a Sound handle from `audio.loadAudio` (small sources
--- are decoded fully into memory, large ones such as BGM tracks stream from
--- disk). Keep the Sound handle alive while `sound` points at it — the
--- component only borrows the resource.
---
--- Two playback controls coexist on the one voice:
---
--- * State mode: writing `"playing"` to `state` starts the resource
---   (honouring `loop`), writing anything else stops the voice (writing
---   `"playing"` again while playing does nothing; write another value first
---   to re-trigger). Suited to music and narration.
--- * Trigger mode: writing any new value to `trigger` plays the resource once
---   from its first sample regardless of `state`, which makes rapid repeated
---   input safe (each blip restarts cleanly instead of stacking or resuming).
---
--- The engine writes the observed voice state back into the read-only
--- `status` field every frame: `"stopped"`, `"playing"`, or `"finished"` for
--- a non-looping source that played to its end.
---
--- Fields:
--- * `category` — "bgm", "sfx" or "voice"; all categories mix at full volume
---   for now (no mixer configuration yet).
--- * `sound` — a Sound handle from `audio.loadAudio`, or nil for silence.
--- * `volume` — 0-100 (default 100).
--- * `pan` -100..100, 0 = centre.
--- * `loop` — whether the voice loops while state is "playing" (default true).
--- * `fadeFrames` — length in frames of a linear volume ramp from the current
---   effective volume to `fadeTargetVolume`. Arming is edge-triggered like
---   playback: setting a new non-zero fadeFrames value starts one fade;
---   set it back to 0 first to run the same fade again later.
--- * `fadeTargetVolume` — 0-100 target of the ramp.

--[[ Scaled frames (core.ScaledFrame) ]]

--- Declarative window-fit scaling. Add it with
--- `scene.addComponent(e, "core.ScaledFrame")`; every frame the engine
--- computes a uniform scale mapping `source` onto `target` and writes it to
--- the entity's transform scale, so children laid out in the source's design
--- space track the target (typically the window). The entity position is not
--- touched — place it yourself (e.g. at the window centre) and refresh
--- `target.width`/`target.height` when the window resizes.
---
--- Fields:
--- * `source.width` / `source.height` — authored size of the content,
---   e.g. 320x240.
--- * `target.width` / `target.height` — area in window pixels to scale onto,
---   usually gfx.screenWidth() x gfx.screenHeight().
--- * `mode` — "contain" scales so the whole source fits inside the target
---   (aspect ratio preserved, letterbox margins); "cover" scales so it fills
---   the target (overflow is cropped). Default "contain".
--- * `increment` — 0 uses the exact fit scale. A positive value snaps the
---   scale DOWN to whole multiples of itself (e.g. 0.5 allows only 0.5, 1.0,
---   1.5, ...) so UI art never renders at arbitrary fractions; the minimum
---   applied scale is one increment.

--[[ Input hints (core.InputHint) ]]
--- Declarative input hint: the icon(s) for a command's current binding on a
--- chosen input device plus an optional text label. Add it with
--- `scene.addComponent(e, "core.InputHint")`; every frame the engine
--- materialises the icons and label as child entities and relayouts only
--- when the fields or the command bindings change. The first icon is drawn
--- centred on the entity's `(x, y)` origin, further icons continue to the
--- right, and the label is centred after the last icon.
---
--- Each icon is a fixed 32x32 sprite (the native atlas cell); scale the
--- hint entity's transform (`scale.x` / `scale.y`) to resize the whole
--- hint - icons and label scale together.
---
--- Icons come from two shared atlases in the core asset directory
--- (`games/core`, searched by the engine after the game module directory):
--- `texture/input_kbm.bmp` holds a full keyboard + numpad + mouse layout and
--- `texture/input_pad.bmp` packs PlayStation, Xbox and Nintendo sets.
---
--- Fields:
--- * `command` — what to show:
---   - A registered input command name ("accept", "cancel", "fire",
---     "inventory", "map", "run", "up", ...) renders one icon per binding
---     that command currently has on the hint's device, straight from the
---     [input] INI section. Rebinds update hints live;
---     a command with no binding on the device shows no icon.
---   - A single letter or digit ("a", "5") shows that key (kbm only).
---   - Icon aliases pick a glyph directly, e.g. "esc", "enter", "space",
---     "lshift", "arrow_up", "mouse_left", "wheel_up" (kbm) or "dpad",
---     "dpad_up", "ls", "rs_click", "lt", "rt", "l1", "select" (pads).
---   - Unknown commands drop the icons but still show the label.
--- * `device` — which atlas / button set: "kbm" (default), "ps", "x" or
---   "n". Writing it relayouts the hint in place.
--- * `text` — optional label string, e.g. "Pause".
--- * `font` — a Font from `gfx.loadFont` used for the label, or nil to use
---   the engine default (`fonts/std.fnt` from the core asset directory).
---
--- Examples: `hint.command = "pause"; hint.text = "Pause"` renders
--- `[P] Pause`; with `hint.device = "ps"` the same command renders the
--- Options button; `hint.command = "accept"` renders Enter, cross, A or B
--- depending on the device.

--[[ The gfx namespace (module mode) ]]

---@class gfx
local gfx = {}

--- Load a texture resource and return a Texture handle, or nil on failure.
--- The path is probed relative to the game module directory first, then the
--- shared core asset directory (`games/core`), then falls back to the
--- original game data (`data://`). `.adt` and `.tim` extensions are appended
--- automatically when not present. Width and height are hints for ADT files
--- missing an intrinsic size.
---
--- When a `.tim` file packs several TIMs back to back (e.g. tmoji.bin), pass
--- `timIndex` to select which one to decode: 0 is the first, 1 the second,
--- and so on. Omitting it (or passing a negative value) decodes the whole
--- file / ADT frame. Each entry's block-length headers let the loader walk
--- from one TIM to the next.
---@param path string e.g. "common/data/gw2"
---@param width? integer
---@param height? integer
---@param timIndex? integer Which TIM in the file to load; nil = whole file.
---@return Texture|nil
function gfx.loadTexture(path, width, height, timIndex) end

--- Load an RE2 font and return a Font handle, or nil on failure. The same
--- path probing as gfx.loadTexture applies. The font atlas's index-0 colour
--- is treated as transparent so glyphs render with a clear background. Use it
--- with a `core.Text` component's `font` field.
---
--- A path ending in `.fnt` selects the OpenRE binary font format: explicit
--- per-character records with kerning and one or more texture atlases,
--- e.g. `gfx.loadFont("fonts/std.fnt")` (normal font) or
--- `gfx.loadFont("fonts/sm.fnt")` (small font).
---
--- A path ending in `.ttf`, `.otf` or `.ttc` selects an SDL3_ttf font. Glyphs
--- are rasterized on demand at `size` pixels (default 16) into a shared atlas;
--- the size is part of the resource key, so loading the same file at different
--- sizes yields separate font handles. Non-Latin scripts (kana/kanji, accents)
--- work as long as the font covers them, e.g.
--- `gfx.loadFont("fonts/Barlow-Regular.ttf", 24)` or
--- `gfx.loadFont("fonts/msgothic.ttc:20")`.
---
--- Otherwise the path names a raw RE2 atlas (font0.tim / font1.tim). A
--- `:variant` suffix selects a virtual sub-font of the same atlas. Only
--- `:small` is supported for now - the compact ASCII set (0x20-0x7F) in an
--- 8px-cell grid (32 per row) on font0, e.g. `gfx.loadFont("common/data/font0:small")`.
---@param path string e.g. "fonts/std.fnt"
---@param size? integer Point/pixel size for TTF/OTF/TTC fonts (default 16).
---@return Font|nil
function gfx.loadFont(path, size) end

--- Measure a string exactly the way the engine will lay it out, so UI code can
--- size and centre labels before creating entities. The sandbox has no io,
--- so this is also the only way scripts can read glyph widths from a
--- proportional .fnt font. Width covers the longest single line (span markup
--- is honoured); height is the font's line height.
---@param Font font A gfx.loadFont handle.
---@param text string
---@return number width
---@return number height
function gfx.measureText(Font, text) end

--- Current scene canvas width in pixels (the configured screen_resolution,
--- not the physical SDL window).
---@return integer
function gfx.screenWidth() end

--- Current scene canvas height in pixels (the configured screen_resolution,
--- not the physical SDL window).
---@return integer
function gfx.screenHeight() end

--- REMOVED: gfx.fade was removed. Fade overlays are now just entities — spawn
--- a textureless full-window sprite (see games/re2 util.spawnOverlay) under the
--- user layer and animate its `color` alpha, destroying it when done.

--- Ask the renderer to grab the next presented frame. The download runs on
--- the render thread inside that frame's submit, so call this from a tick
--- and collect the result with gfx.takeScreenshot() on a later tick. The
--- web inspector's Screenshot tab uses this pair.
---@return boolean false only when no frame can ever be captured
function gfx.requestScreenshot() end

--- Return the frame grabbed by the most recent gfx.requestScreenshot() as an
--- uncompressed BMP: `{ width = w, height = h, data = <binary BMP bytes> }`,
--- or nil while no grabbed frame is ready yet.
---@return { width: integer, height: integer, data: string }|nil
function gfx.takeScreenshot() end

--- Enumerate every live resource in the engine ResourceManager. Returns a
--- table (array) of `{ resource, type, path, width, height, streaming,
--- cookies }` where `resource` is the resource number, `type` is "texture",
--- "font", "sound" or "movie", `streaming` marks sources read from disk on
--- demand (movies and large sounds), and `cookies` lists the consumer cookie
--- numbers currently referencing the resource.
---@return { resource: integer, type: string, path: string, width: integer, height: integer, streaming: boolean, cookies: integer[] }[]
function gfx.listResources() end

--- Return the decoded RGBA pixels of a resource's texture as
--- `{ width, height, data }` where `data` is a binary string of `width*height*4`
--- bytes (R,G,B,A per pixel), or nil when unavailable. Fonts resolve to their
--- atlas texture.
---@param cookie integer
---@return { width: integer, height: integer, data: string }|nil
function gfx.getResourceImage(cookie) end

--[[ audio — resident sound effects ]]

---@class Sound
--- Opaque handle to a decoded sound kept in memory for the whole session.
--- Created with `audio.loadAudio`. Like textures and fonts, the resource is
--- unloaded when the last handle is garbage collected, so keep the handle
--- referenced while an emitter's `sound` field points at it.
local Sound = {}

--- Load and decode a .sap sound file and return a resident one-shot Sound
--- handle. Small files are decoded fully into memory; large whole-file
--- sources (>= 1 MiB, e.g. BGM tracks) are streamed from disk instead — the
--- file is read incrementally during playback rather than being expanded to
--- PCM in memory, so the handle costs almost nothing to keep around.
---
--- The optional `sub` index selects one sub-sound of a multi-sound .sap
--- container (one RIFF WAV per set bit after the 8-byte mask header), e.g.
--- sub 0 of core22.sap is its first blip; sub-selected sounds always decode
--- fully into memory. The same module-dir-then-data:// probing as textures
--- applies. Loading the same path/sub pair again returns a handle onto the
--- same shared sound. Returns nil when the file is missing or cannot be
--- decoded.
---@param path string e.g. "common/sound/core/core17.sap"
---@param sub? integer sub-sound index of a multi-sound container (default: none)
---@return Sound|nil
function audio.loadAudio(path, sub) end

--- Play a previously loaded sound as a one-shot, at full volume, centred.
--- Re-triggering while it is still playing restarts it from the beginning,
--- so rapid menu input never stacks copies of the same blip.
---@param sound Sound
function audio.play(sound) end

--[[ save — saved-game listing ]]

---@class save
local save = {}

--- List the saved games in the save folder, sorted newest first. Each entry is
--- a table:
--- ```
--- {
---     name       = "SAVE00",            -- filename without extension
---     character  = "leon" | "claire",   -- from the saved player id
---     scenario   = "a" | "b",           -- from the saved status flags
---     difficulty = "casual" | "standard", -- easy flag; no hardcore marker yet
---     room       = "Waiting Room",      -- English name for the saved room id
---     numSaves   = 3,                   -- times saved in that game
--- }
--- ```
--- Files qualify when named `*.BIOHAZARD2` (or containing RESIDENT2) and
--- carrying the save magic; unreadable or corrupt files are skipped silently.
--- With no save folder (or an empty one) the result is an empty table.
---@return { name: string, character: string, scenario: string, difficulty: string, room: string, numSaves: integer }[]
function save.list() end

--[[ core — engine services ]]

---@class core
core = {}

--- REMOVED: the blocking Movie workflow (`core.loadMovie` / `core.playMovie`
--- / the Movie class) was removed along with the `"movie"` scene root. Movies
--- now play async and under the user layer: create an entity, give it a
--- `core.Video` component, set `movie` to the movie's **path string** (e.g.
--- `"pl0\\zmovie\\title_l.bin"`) and `status` = 1 (Playing), and let the native
--- VideoSystem auto-start the decoder on its next pass. Destroying that entity
--- is the whole teardown: the decoder and dynamic texture are released by the
--- system's mark-and-sweep.

--- Load a save file from the save folder before entering gameplay with
--- `core.runGame()`. `name` is the entry name from `save.list()` (the save
--- file's base name, without the `.BIOHAZARD2` extension).
---
--- The save image (the first 0x798 bytes of the 0x800-byte file) is restored
--- into the game table exactly like a memory-card load, so the session resumes
--- with the saved flags, inventory, position and health. Returns `true` on
--- success; `false` (leaving the game state untouched) when no save file
--- matches `name` or it cannot be read.
---
--- After a successful load, call `core.runGame()` to actually start the game.
------@param name string a save.list() entry name
---@return boolean
function core.loadGame(name) end

--- Load a save file by explicit path before entering gameplay with
--- `core.runGame()`. The path form of `core.loadGame`: used by the
--- command-line save argument (`openre.exe <savefile>`), whose path may live
--- anywhere rather than in the save folder. Behaviour is otherwise identical
--- to `core.loadGame` (the save image is restored like a memory-card load).
--- Returns `true` on success; `false` (leaving the game state untouched) when
--- the file cannot be read.
---
--- After a successful load, call `core.runGame()` to actually start the game.
---@param path string a path to a 0x800-byte save file
---@return boolean
function core.loadSave(path) end

--- Load a recorded demo before entering gameplay with `core.runGame()`.
--- `name` is the demo file base name (e.g. `'PDEMO00'` for
--- `data://Pl0/Pld/PDEMO00.DAT`).
---
--- The demo header+replay block is copied into the demo work area and the
--- boot is latched as a demo session: the game starts at the demo's recorded
--- stage/room/position and replays its recorded input. When the demo ends (or
--- the player presses a key, cancelling it), the game signals "return to
--- menu" the same way a load-game session does. Returns `true` on success;
--- `false` when the file cannot be read or is too small to be a valid demo.
---
--- After a successful load, call `core.runGame()` to actually start the game.
---@param name string a demo file base name (e.g. 'PDEMO00')
---@return boolean
function core.loadDemo(name) end

--- Begin a fresh game from the menu before entering gameplay with
--- `core.runGame()`. `player` is `'leon'` or `'claire'`, `scenario` is
--- `'a'` or `'b'`, `difficulty` is `'casual'`, `'standard'` or `'hardcore'`.
---
--- The choices are latched and re-applied in `game_boot_init` after the
--- boot wipe, so the FG_SYSTEM_14 new-game branch sets up a fresh
--- stage/inventory with the chosen character and scenario (casual enables
--- easy mode, hardcore enables hard mode). Returns `true` on success;
--- `false` for an unknown player/scenario/difficulty.
---
--- After a successful start, call `core.runGame()` to actually start the game.
---@param player string 'leon' | 'claire'
---@param scenario string 'a' | 'b'
---@param difficulty string 'casual' | 'standard' | 'hardcore'
---@return boolean
function core.startNewGame(player, scenario, difficulty) end

--- Get the parsed command-line options. The module translates a command-line
--- start (`openre.exe <savefile> -p <player> -r <roomcode> -s <scenario>`)
--- into the module-driven setup APIs + `core.runGame()` at boot (replacing
--- the old command-line divert from the native boot). Returns a table:
--- `{ startRequested, saveRequested, savePath, player, stage, room,
--- scenario }`; `player`/`stage`/`room`/`scenario` are -1 when the argument
--- was not given.
---@return { startRequested: boolean, saveRequested: boolean, savePath: string, player: integer, stage: integer, room: integer, scenario: integer }
function core.cmdline() end

--- Place the player at a room's spawn point with a fresh new-game inventory
--- before entering gameplay with `core.runGame()`. This is the command-line
--- `-p` / `-r` room warp translated onto the module boot path. `player` is a
--- PLD_* id (0 = Leon, 1 = Claire, ...), `stage` a 0-based stage and `room` a
--- room map number (e.g. 0 for room 1000, 0xC for room 10C0). Returns `true`
--- on success; `false` when a previous session's latches could not be cleared.
---
--- After a successful start, call `core.runGame()` to actually start the game.
---@param player integer PLD_* id (0 = Leon)
---@param stage integer 0-based stage number
---@param room integer room map number
---@return boolean
function core.startRoomWarp(player, stage, room) end

--- Hand the frame to the legacy game and return immediately (non-blocking).
---
--- The LegacyGame node is enabled so the legacy game drives the frame inside
--- the normal app loop; when the legacy game signals "return to menu" (e.g. a
--- demo ended or a save session finished), the node is disabled again and the
--- module scene renders once more. Poll `core.inGame()` to react to that
--- transition, or register a resume callback with the game module's
--- `game.resumeAfterGame` helper (see `games/re2/script/game.lua`).
---
--- The first call boots the legacy game (fonts, audio, room tables) and
--- chains straight into gameplay; there are no native boot screens or
--- prologue any more. When a save was loaded with `core.loadGame`, the
--- session resumes from it; otherwise the game starts as the current
--- player/scenario settings dictate.
---
--- The async form keeps the whole registry (including the web inspector's
--- managed systems) running while the game plays.
---@return boolean
function core.runGame() end

--- True while a `core.runGame()` session is live (the LegacyGame node is
--- enabled and driving the frame). Flips to false on the first frame after
--- the legacy game returns to the menu.
---@return boolean
function core.inGame() end

--- End the current `core.runGame()` session: sets the return-to-menu latch
--- that LegacyGameSystem picks up at the next legacy frame, disabling the
--- LegacyGame node so the module takes over the frame again. A resume
--- callback registered with `game.resumeAfterGame` immediately after (e.g.
--- one that calls `core.runGame()` again for an in-game Restart / Load-Game
--- flow) runs on that same transition. Returns true when a session was live
--- (the latch was set), false otherwise.
---@return boolean
function core.requestReturnToMenu() end

--- Pin a font handle onto one of the standard UI font roles and return nothing.
---
--- The native text system addresses logical fonts by **role** (`'big'` /
--- `'small'`), never by concrete path. The core Lua catalogue
--- `games/core/script/ui/font.lua` owns which font each role uses per
--- `[options] "font"` mode and calls this to register its choice; engine text
--- (input hints, movie captions), the swap walk and every role consumer then
--- follow the binding.
---
--- `fallback` (optional) names a font the engine consults when a sprite
--- (`.fnt`) primary lacks a codepoint - the game's choice of
--- Unicode-coverage font. Pass nil to clear it.
---
--- The native registry keeps its own reference on each registered cookie, so
--- the role fonts stay valid even if the caller's handles are collected; when
--- the underlying resource changes, `core.FontSwap` remaps all existing
--- `core.Text` entities from the previous to the new font on the next frame.
---
--- Prefer going through the catalogue (`ui.font.get(role)` /
--- `ui.font.refresh()`) rather than calling this directly.
---@param role "big" | "small"
---@param font Font a gfx.loadFont handle
---@param fallback? Font|nil optional per-role glyph fallback font
function core.setRoleFont(role, font, fallback) end

--[[ InputHandler — the only way to receive input ]]

-- The old global input.down / input.pressed / input.commands /
-- input.bindings API is REMOVED. The only way to retrieve input is a
-- `core.InputHandler` scene component; the engine walks the scene tree every
-- frame (deepest first, last-created child first, root last) and invokes the
-- callback of every entity that carries one, sharing one mutable InputFrame.
-- Tree position IS the ordering: a handler on a deep, late-created overlay
-- entity runs before the game intake, and writing empty state/press there
-- blocks everything behind it (a modal). A plugin that injects automated
-- input sits on an entity at the earliest walk position and replaces
-- state/press wholesale. Register one with:
--
--   local handler = scene.addComponent(e, "core.InputHandler")
--   scene.setField(handler, "callback", function(frame) ... end)
--
-- Screens typically snapshot the commands they care about in the callback
-- (the walk runs before the script tick) and consume that snapshot in their
-- tick handler. See games/re2/script/options.lua (a modal that nulls the
-- frame), games/re2/script/menu.lua / load.lua / prologue.lua and
-- scripts/web.lua (OPENRE_WEB_INPUT=1; automated input injection) for worked
-- examples.

---@class InputFrame
--- The per-frame context passed to every `core.InputHandler` callback. It is a
--- plain table with two command-name arrays:
---
---   { state = { "up", "fire" }, press = { "accept" } }
---
--- `state` lists every command currently held; `press` lists the commands that
--- just went down this frame. A handler may read, replace or clear either
--- array (assign a fresh table literal, table.remove to drop one entry, etc).
--- Handlers never consume input: every handler in the tree runs, and values
--- flow downstream, so "nulling the input" is writing empty tables
--- (state = {}, press = {}).
local InputFrame = {}

--[[ options — engine-wide settings registry ]]

--- Static description of one registered option (as returned by
--- `options.list`). Fields other than `labels`/`min`/`max`/`step` depend on
--- `kind`: `labels` is present for toggle and enum options, while
--- `min`/`max`/`step` are present for range options only.
---@class OptionsDef
---@field id string Stable persistence key inside the [options] INI section.
---@field category string Outer tab the option lives under.
---@field subcategory string Inner tab the option lives under.
---@field group string Section header; a changed group starts a new section.
---@field title string Display title.
---@field tooltip string Help text shown for the option.
---@field kind "toggle"|"enum"|"range"|"external"
---@field labels string[]|nil Value labels ("toggle"/"enum" kinds only).
---@field min integer|nil Range lower bound ("range" kind only).
---@field max integer|nil Range upper bound ("range" kind only).
---@field step integer|nil Range step grid, <= 1 means unstepped ("range" only).
---@field default integer Value applied on first run.

---@class options
options = {}

--- Enumerate every registered option definition in registration order.
---@return OptionsDef[]
function options.list() end

--- Current value of an option, or nil when the id is unknown.
---@param id string|integer Option id, or its registration index from options.list().
---@return integer|nil
function options.get(id) end

--- Validate and store a new value for an option, marking it dirty when the
--- value changed. Returns false when the option is unknown or the value is
--- rejected by its kind: an enum index outside its label list, a toggle other
--- than 0/1, or an external entry. Range values are clamped to [min, max] and
--- snapped down onto the step grid instead of being rejected.
---@param id string|integer Option id, or its registration index from options.list().
---@param value integer
---@return boolean success
function options.set(id, value) end

--- Write every dirty option into the [options] INI section of the config file
--- and save once afterwards. No-op when nothing is dirty; call after batching
--- options.set changes.
function options.flush() end

--[[ shared — persistent data across script VMs ]]

--- Persistent key/value storage shared by every script VM (and the game
--- module). Values written here survive a script VM being destroyed and
--- recreated on hot reload, so read your previous state back when the script
--- loads to restore it:
---
--- ```lua
--- local state = shared.options or {}
--- shared.options = { open = true, page = state.page or 1 }
--- ```
---
--- Keys must be strings or numbers. Values may be booleans, numbers,
--- strings, or tables of those (copied recursively; functions and userdata
--- are dropped). `shared` is backed by native code, so pairs()/ipairs() do
--- not work on it — keep your own key list if you need to enumerate.
---@class shared
---@field [string|number] any
shared = {}

return re
