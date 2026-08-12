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

return re
