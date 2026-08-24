-- ecs.lua - a small ECS implemented entirely in Lua on top of the native
-- scene tree.
--
-- Components are registered from Lua with a name and an ordered field list;
-- every field has a name and one of four primitive types (int, float, bool,
-- string) plus an optional default. Under the covers each entity's component
-- row is stored as BINARY: the field list is compiled to a string.pack format
-- at registration time and rows are packed/unpacked on access. Nothing but
-- packed bytes is kept per entity.
--
-- Systems are registered with a name and a priority; this module subscribes
-- once to HookKind.tick and dispatches them in ascending priority order
-- (lower runs first) every frame. The dispatch happens inside the engine's
-- script tick (ModuleSystem), i.e. before TextLayoutSystem/RenderSystem read
-- the scene, so entities created or updated by a system render same-frame.
--
-- Entities are addressed by their numeric scene id (what scene.children /
-- scene.parent return). Use ecs.spawn() to create one and get both the
-- native handle (for scene.setField etc.) and its id (for the ecs API).
-- Rows of destroyed entities are swept lazily during iteration: ids are
-- generation-checked against the scene via scene.entityFromId, so recycled
-- slots can never alias a dead component.

local ecs = {}

-------------------------------------------------------------------------------
-- Component registry ---------------------------------------------------------
-------------------------------------------------------------------------------

-- Binary encoding per field type (little-endian).
local FORMATS = {
    int = '<i4',    -- 32-bit signed integer
    float = '<f',   -- 32-bit float
    bool = '<B',    -- one byte, 0/1
    string = '<s4', -- 4-byte length prefix + bytes
}

local components = {} -- name -> definition

local function defaultFor(fieldType)
    if fieldType == 'bool' then
        return false
    elseif fieldType == 'string' then
        return ''
    end
    return 0
end

--- Register a component type. `fields` is an array of
--- `{ name = ..., type = 'int'|'float'|'bool'|'string' [, default = ...] }`.
--- Field order defines the binary layout; do not reorder once shipped.
---@param name string e.g. 'ui.tab_strip'
---@param fields table[]
function ecs.registerComponent(name, fields)
    assert(type(name) == 'string' and #name > 0, 'component name required')
    assert(components[name] == nil, "component already registered: " .. name)
    local def = {
        name = name,
        fields = {},
        index = {},
        format = '<',
        rows = {}, -- entityId -> { data = binary string, seq = int }
        nextSeq = 0,
    }
    local defaults = {}
    for i, field in ipairs(fields) do
        local fmt = FORMATS[field.type]
        assert(fmt ~= nil, "unknown field type '" .. tostring(field.type) .. "'")
        def.format = def.format .. fmt
        def.fields[i] = { name = field.name, type = field.type }
        def.index[field.name] = i
        local value = field.default ~= nil and field.default or defaultFor(field.type)
        -- Coerce to the packed representation up front so the template row
        -- packs once without surprises.
        if field.type == 'bool' then
            value = value and 1 or 0
        elseif field.type == 'int' then
            value = math.floor(tonumber(value) or 0)
        elseif field.type == 'float' then
            value = tonumber(value) or 0.0
        else
            value = tostring(value)
        end
        defaults[i] = value
    end
    def.defaultRow = string.pack(def.format, table.unpack(defaults, 1, #def.fields))
    components[name] = def
end

local function definition(name)
    local def = components[name]
    assert(def ~= nil, "component not registered: " .. tostring(name))
    return def
end

-- Unpacks a row into a table.pack result (values at 1..#fields).
local function unpackRow(def, id)
    local row = def.rows[id]
    if row == nil then
        error(("entity %s has no '%s' component"):format(tostring(id), def.name))
    end
    return table.pack(string.unpack(def.format, row.data))
end

-- True when the numeric id refers to a live entity right now.
local function isLive(id)
    return type(id) == 'number' and scene.entityFromId(id) ~= nil
end

--- Add `name` to `entity` (a numeric scene id from ecs.spawn), initialised
--- to the registered defaults. Returns nothing; re-adding asserts.
---@param entity integer
---@param name string
function ecs.addComponent(entity, name)
    local def = definition(name)
    assert(isLive(entity), 'ecs.addComponent: entity is not live')
    assert(def.rows[entity] == nil, ("entity %d already has '%s'"):format(entity, name))
    def.nextSeq = def.nextSeq + 1
    def.rows[entity] = { data = def.defaultRow, seq = def.nextSeq }
end

function ecs.removeComponent(entity, name)
    definition(name).rows[entity] = nil
end

---@param entity integer
---@param name string
---@return boolean
function ecs.has(entity, name)
    local def = components[name]
    return def ~= nil and def.rows[entity] ~= nil
end

--- Read one field of a component row.
---@param entity integer
---@param name string
---@param field string
---@return any
function ecs.get(entity, name, field)
    local def = definition(name)
    local idx = def.index[field]
    assert(idx ~= nil, ("'%s' has no field '%s'"):format(name, tostring(field)))
    local values = unpackRow(def, entity)
    if def.fields[idx].type == 'bool' then
        -- Packed as a byte; surface it as a real boolean.
        return values[idx] == 1
    end
    return values[idx]
end

--- Write one field of a component row (the whole row repacks).
---@param entity integer
---@param name string
---@param field string
---@param value any
function ecs.set(entity, name, field, value)
    local def = definition(name)
    local idx = def.index[field]
    assert(idx ~= nil, ("'%s' has no field '%s'"):format(name, tostring(field)))
    local values = unpackRow(def, entity)
    local fieldType = def.fields[idx].type
    if fieldType == 'int' then
        values[idx] = math.floor(tonumber(value) or 0)
    elseif fieldType == 'float' then
        values[idx] = tonumber(value) or 0.0
    elseif fieldType == 'bool' then
        values[idx] = value and 1 or 0
    else
        values[idx] = tostring(value)
    end
    def.rows[entity].data =
        string.pack(def.format, table.unpack(values, 1, #def.fields))
end

--- Call fn(entityId) for every live entity carrying `name`, in creation
--- order. Rows of entities destroyed since last touch are dropped here.
---@param name string
---@param fn fun(entity: integer)
function ecs.each(name, fn)
    local def = definition(name)

    -- Snapshot in creation order so fn may add/remove rows safely.
    local ordered = {}
    for id, row in pairs(def.rows) do
        if not isLive(id) then
            def.rows[id] = nil -- stale: entity was destroyed
        else
            ordered[#ordered + 1] = { id = id, seq = row.seq }
        end
    end
    table.sort(ordered, function(a, b)
        return a.seq < b.seq
    end)
    for _, item in ipairs(ordered) do
        fn(item.id)
    end
end

-------------------------------------------------------------------------------
-- Entity helpers -------------------------------------------------------------
-------------------------------------------------------------------------------

--- Create an entity under `parent` (a native handle, defaults to the user
--- root). Returns the handle plus the entity's numeric scene id - use the
--- handle for scene.setField/addComponent and the id for the ecs API.
---@param parent? SceneEntity
---@return SceneEntity|nil handle
---@return integer|nil id
function ecs.spawn(parent)
    parent = parent or scene.getRoot('user')
    local seen = {}
    for _, id in ipairs(scene.children(parent)) do
        seen[id] = true
    end
    local handle = scene.createEntity(parent)
    if handle == nil then
        return nil, nil
    end
    for _, id in ipairs(scene.children(parent)) do
        if not seen[id] then
            return handle, id
        end
    end
    -- Should not happen (createEntity always attaches); fall back to no id.
    return handle, nil
end

--- Destroy an entity (numeric id or native handle) and drop all of its
--- Lua-ECS component rows first. Numeric ids are the canonical addressing;
--- with a handle the rows are left for the lazy sweep in ecs.each().
---@param entity SceneEntity|integer
function ecs.destroyEntity(entity)
    if type(entity) == 'number' then
        for _, def in pairs(components) do
            def.rows[entity] = nil
        end
        local handle = scene.entityFromId(entity)
        if handle ~= nil then
            scene.destroy(handle)
        end
    else
        scene.destroy(entity)
    end
end

-------------------------------------------------------------------------------
-- Systems --------------------------------------------------------------------
-------------------------------------------------------------------------------

local systems = {} -- sorted array of { name, priority, order, fn }
local nextSystemOrder = 0

--- Register a system executed every frame. Lower `priority` runs first;
--- ties keep registration order.
---@param name string Diagnostic label only.
---@param priority number
---@param fn function()
function ecs.registerSystem(name, priority, fn)
    nextSystemOrder = nextSystemOrder + 1
    systems[#systems + 1] = { name = name, priority = priority, order = nextSystemOrder, fn = fn }
    table.sort(systems, function(a, b)
        if a.priority ~= b.priority then
            return a.priority < b.priority
        end
        return a.order < b.order
    end)
end

local function runSystems()
    for _, system in ipairs(systems) do
        local ok, err = xpcall(system.fn, tostring)
        if not ok then
            re.log(LogLevel.error, ("[ecs] system '%s' failed: %s"):format(system.name, tostring(err)))
        end
    end
end

re.subscribe(HookKind.tick, runSystems)

return ecs
