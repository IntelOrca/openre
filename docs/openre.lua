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

---@enum HookKind
HookKind = {
    --- Fired every game frame.
    tick = 1,
}

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

--- Subscribe a hook callback. Currently only HookKind.tick is supported.
---@param kind HookKind
---@param fn function Called every tick.
function re.subscribe(kind, fn) end

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
--- collector when this value is no longer referenced; nil-ing the Lua
--- reference is enough to free the GPU texture.
local Texture = {}

---@class Font
--- Opaque handle to a loaded RE2 font (font0.tim / font1.tim). Created with
--- `gfx.loadFont`. Reference counted and released by the garbage collector.
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

--- Return one of the five strict top-level sub-roots of the scene by name.
--- These are fixed containers the engine and Lua organise content under:
--- `"user"` (what Lua code has access to), `"game"` (contains the legacy game
--- entity), `"movie"` (for the video component entity), `"fade"` (for the fade
--- black overlay) and `"overlay"` (mostly for debug overlay text).
---@param name string one of "user", "game", "movie", "fade", "overlay"
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
--- Lua-defined components. `core.transform` is implicit on every entity.
--- `core.text` accepts the font (a `gfx.loadFont` value) as the component's
--- initial value, or set it later with `scene.setField(handle, 'font', f)`.
---@param entity SceneEntity
---@param name string e.g. "core.sprite", "core.text" or "core.transform"
---@return SceneComponent|nil
function scene.addComponent(entity, name) end

--- Write a field on an entity or component. When the handle is an entity, or
--- for a transform component, the fields are `name` (a human-readable string;
--- "" by default), `x`, `y`, `z` (world position in window pixels) and
--- `scale.x`, `scale.y`, `scale.z` (scale multiplier; 1 = natural size). Entity
--- positions are relative to the parent when the entity was attached with
--- `scene.setParent`, and a parent's scale is inherited by
--- its children (offsets and rendered sizes both scale). Sprite fields mirror the layout of the underlying C++
--- struct: `size.width`, `size.height` (destination size in window pixels),
--- `src.x`, `src.y`, `src.width`, `src.height` (source rect in texture
--- pixels; 0-sized means "full texture"), `texture` (a Texture or nil), and
--- `flags` (a bitmask of SpriteFlags values combined with `|`). Text fields:
--- `text` (UTF-8 string; supports `<span color="#ff0">` markup), `font` (a
--- Font or nil), `size.width` / `size.height` (bounds the text is aligned
--- within), and `flags` (a bitmask of TextFlags values combined with `|`).
---@param handle SceneEntity|SceneComponent
---@param name string
---@param value any
function scene.setField(handle, name, value) end

--- Read a single field on an entity or component. Returns nil for unknown
--- fields. Fields use the same names as scene.setField. The `texture` / `font`
--- fields are returned as the numeric ResourceCookie, not an object; `flags` is
--- returned as the numeric bitmask; the transform `name` field is returned as a
--- string.
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
--- one of "core.transform", "core.sprite", "core.text", "core.legacy" or
--- "core.video". Returns nil when the entity lacks that component.
---@param entity SceneEntity
---@param typeName string
---@return SceneComponent|nil
function scene.component(entity, typeName) end

--- Return a table of the component type names present on an entity (always
--- including "core.transform").
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

--[[ The gfx namespace (module mode) ]]

---@class gfx
local gfx = {}

--- Load a texture resource and return a Texture handle, or nil on failure.
--- The path is probed relative to the game module directory first, then
--- falls back to the original game data (`data://`). `.adt` and `.tim`
--- extensions are appended automatically when not present. Width and height
--- are hints for ADT files missing an intrinsic size.
---
--- When a `.tim` file packs several TIMs back to back (e.g. tmoji.bin), pass
--- `timIndex` to select which one to decode; 0 (the default) decodes the
--- first. Each entry's block-length headers let the loader walk from one TIM
--- to the next.
---@param path string e.g. "common/data/gw2"
---@param width? integer
---@param height? integer
---@param timIndex? integer Which TIM in the file to load (default 0).
---@return Texture|nil
function gfx.loadTexture(path, width, height, timIndex) end

--- Load an RE2 font (font0.tim / font1.tim) and return a Font handle, or nil
--- on failure. The same path probing as gfx.loadTexture applies. The font
--- atlas's index-0 colour is treated as transparent so glyphs render with a
--- clear background. Use it with a `core.text` component's `font` field.
---
--- A `:variant` suffix selects a virtual sub-font of the same atlas. Only
--- `:small` is supported for now - the compact ASCII set (0x20-0x7F) in an
--- 8px-cell grid (32 per row) on font0, e.g. `gfx.loadFont("common/data/font0:small")`.
---@param path string e.g. "common/data/font0"
---@return Font|nil
function gfx.loadFont(path) end

--- Current window width in pixels.
---@return integer
function gfx.screenWidth() end

--- Current window height in pixels.
---@return integer
function gfx.screenHeight() end

--- Set a full-window fade overlay for the current frame. A new call replaces
--- the previous one; the overlay is cleared automatically at the end of the
--- frame.
---@param r integer 0-255
---@param g integer 0-255
---@param b integer 0-255
---@param a integer 0-255 alpha
function gfx.fade(r, g, b, a) end

--- Enumerate every live resource in the engine ResourceManager. Returns a table
--- (array) of `{ cookie, type, path, width, height }` where `type` is
--- "texture", "font" or "unknown".
---@return { cookie: integer, type: string, path: string, width: integer, height: integer }[]
function gfx.listResources() end

--- Return the decoded RGBA pixels of a resource's texture as
--- `{ width, height, data }` where `data` is a binary string of `width*height*4`
--- bytes (R,G,B,A per pixel), or nil when unavailable. Fonts resolve to their
--- atlas texture.
---@param cookie integer
---@return { width: integer, height: integer, data: string }|nil
function gfx.getResourceImage(cookie) end

return re
