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
-- Systems are registered with a name and a priority; this module subscribes
-- once to HookKind.tick and dispatches them in ascending priority order
-- (lower runs first) every frame. The dispatch happens inside the engine's
-- script tick (ModuleSystem), i.e. before TextLayoutSystem/RenderSystem read
-- the scene, so entities created or updated by a system render same-frame.
--
-- Entities are addressed by their numeric scene id (what scene.children /
-- scene.parent return). Use ecs.spawn() to create one and get both the
-- native handle (for scene.setField etc.) and its id (for the ecs API).

local ecs = {}

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
---@param name string e.g. 'ui.tab_strip'
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
