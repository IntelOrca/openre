-- ecs.lua - a small ECS implemented in Lua on top of the native scene tree.
--
-- Components are registered from Lua with a name and an ordered field list;
-- every field has a name and one of four primitive types (int, float, bool,
-- string) plus an optional default. Registration and storage are native:
-- scene.defineComponent() adds the definition to the engine's runtime
-- component registry and rows live on the scene itself (one typed field
-- vector per entity), so every component is visible to the debug web
-- inspector and readable/writable through the regular scene.component /
-- scene.getField / scene.setField APIs.
--
-- Systems are registered with a name and an options table
-- ({ after = {...}, before = {...}, placement = "early"|"default"|"late" })
-- into the engine's shared system registry, which interleaves them with the
-- native app-shell systems in one computed order. Managed systems default to
-- running after the game tick (core.LegacyGame) and before the native layout
-- systems (core.ScaledFrame), mirroring the old post_tick dispatch slot.
--
-- Entities are addressed by their numeric scene id (what scene.children /
-- scene.parent return). Use ecs.spawn() to create one and get both the
-- native handle (for scene.setField etc.) and its id (for the ecs API).

local ecs = {}

-- Clear any managed systems from a previous game-module load. Registration
-- and the runner are refreshed below.
re.resetManagedSystems()

-------------------------------------------------------------------------------
-- Component registry ---------------------------------------------------------
-------------------------------------------------------------------------------

local FIELD_TYPES = {
    int = true,
    float = true,
    bool = true,
    string = true,
}

--- Register a component type. `fields` is an array of
--- `{ name = ..., type = 'int'|'float'|'bool'|'string' [, default = ...] }`.
--- Field order defines the row layout; do not reorder once shipped.
---@param name string e.g. 'ui.config.Enum'
---@param fields table[]
function ecs.registerComponent(name, fields)
    assert(type(name) == 'string' and #name > 0, 'component name required')
    assert(type(fields) == 'table', 'field array required')
    local defs = {}
    for i, field in ipairs(fields) do
        assert(FIELD_TYPES[field.type], "unknown field type '" .. tostring(field.type) .. "'")
        assert(type(field.name) == 'string' and #field.name > 0, "field " .. i .. " has no name")
        defs[i] = { name = field.name, type = field.type, default = field.default }
    end
    assert(scene.defineComponent(name, defs), "component already registered: " .. name)
end

-- Resolves the runtime component handle for an (entity, name) pair.
local function rowHandle(entity, name)
    local handle = scene.entityFromId(entity)
    assert(handle ~= nil, ("entity %s has no '%s' component"):format(tostring(entity), name))
    local comp = scene.component(handle, name)
    assert(comp ~= nil, ("entity %s has no '%s' component"):format(tostring(entity), name))
    return comp
end

--- Add `name` to `entity` (a numeric scene id from ecs.spawn), initialised
--- to the registered defaults. Returns nothing; re-adding asserts.
---@param entity integer
---@param name string
function ecs.addComponent(entity, name)
    local handle = scene.entityFromId(entity)
    assert(handle ~= nil, 'ecs.addComponent: entity is not live')
    scene.addComponent(handle, name)
end

function ecs.removeComponent(entity, name)
    scene.removeComponent(scene.entityFromId(entity), name)
end

---@param entity integer
---@param name string
---@return boolean
function ecs.has(entity, name)
    return type(entity) == 'number'
        and scene.entityFromId(entity) ~= nil
        and scene.component(scene.entityFromId(entity), name) ~= nil
end

--- Read one field of a component row.
---@param entity integer
---@param name string
---@param field string
---@return any
function ecs.get(entity, name, field)
    return scene.getField(rowHandle(entity, name), field)
end

--- Write one field of a component row.
---@param entity integer
---@param name string
---@param field string
---@param value any
function ecs.set(entity, name, field, value)
    scene.setField(rowHandle(entity, name), field, value)
end

--- Call fn(entityId) for every live entity carrying `name`, in creation
--- order. The id list is a snapshot, so fn may add/remove rows safely.
---@param name string
---@param fn fun(entity: integer)
function ecs.each(name, fn)
    for _, id in ipairs(scene.entitiesWithComponent(name)) do
        fn(id)
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

--- Destroy an entity (numeric id or native handle). The scene removes its
--- runtime component rows together with the entity.
---@param entity SceneEntity|integer
function ecs.destroyEntity(entity)
    scene.destroy(entity)
end

-------------------------------------------------------------------------------
-- Systems --------------------------------------------------------------------
-------------------------------------------------------------------------------

-- Managed systems dispatcher: the engine calls this with the id it assigned
-- at registration; we look up the Lua fn in our own table.
local systemsById = {}
local nextSystemId = 0

--- Register a system executed every frame. Ordering between systems is
--- constraint-based: `after` names systems that must run before this one,
--- `before` names systems this one must run before, and `placement` is
--- "early", "default" or "late" (how eagerly the system claims its spot).
---@param name string Canonical name, e.g. 'ui.config.ListItem' (no "system").
---@param opts? table { after = string[], before = string[], placement = string }
---@param fn function()
function ecs.registerSystem(name, opts, fn)
    if type(opts) == 'function' then
        fn = opts
        opts = {}
    end
    opts = opts or {}
    nextSystemId = nextSystemId + 1
    systemsById[nextSystemId] = fn
    re.registerSystem(
        name,
        opts.after or {},
        opts.before or {},
        opts.placement or 'default',
        nextSystemId)
end

local function runSystem(id)
    local fn = systemsById[id]
    if fn then
        local ok, err = xpcall(fn, tostring)
        if not ok then
            re.log(LogLevel.error, ("[ecs] managed system '%d' failed: %s"):format(id, tostring(err)))
        end
    end
end

-- The engine dispatches managed systems through this single callback, in the
-- order computed by its shared system registry.
re.setSystemRunner(runSystem)

return ecs
