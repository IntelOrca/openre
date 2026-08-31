-- util.lua — small helpers for game scripts.

local util = {}

-- Design space used by the title screen: content is authored in this fixed
-- 320x240 coordinate space and then scaled to fit the window.
local CANVAS_W = 320
local CANVAS_H = 240

--- Full-window crop rect, for layering content over the whole screen.
function util.getCropRect()
    return { x = 0, y = 0, width = gfx.screenWidth(), height = gfx.screenHeight() }
end

--- Create a full-window solid-colour sprite overlay under `parent` (defaults
--- to the overlay root). The sprite is textureless so the renderer draws it as
--- a solid colour quad that covers the whole window; being the newest append to
--- `parent`, it paints above anything already under it. Returns the entity and
--- the sprite component, so callers can animate the colour/alpha or destroy
--- the entity when done. Replaces the removed gfx.fade global.
---@param parent handle|nil parent entity (default overlay root)
---@param color integer[4] the initial RGBA colour (0-255 each)
---@return handle, handle entity and sprite component
function util.spawnOverlay(parent, color)
    if parent == nil then
        parent = scene.getRoot('overlay')
    end
    local w = gfx.screenWidth()
    local h = gfx.screenHeight()
    local entity = scene.createEntity(parent)
    scene.setField(entity, 'name', 'screen.fade')
    scene.setField(entity, 'x', w * 0.5)
    scene.setField(entity, 'y', h * 0.5)
    local sprite = scene.addComponent(entity, 'core.Sprite')
    scene.setField(sprite, 'color', color or { 0, 0, 0, 255 })
    local size = scene.addComponent(entity, 'core.UiSize')
    scene.setField(size, 'width', w)
    scene.setField(size, 'height', h)
    return entity, sprite
end

--- Position and scale a 320x240 "canvas" entity so it is centred in the window
--- and scaled to fit while preserving aspect ratio (letterboxed, never
--- stretched). Call this every frame so the canvas tracks window resizes.
--- The canvas root is placed at the window centre; all of its children are
--- laid out in a coordinate space where (0,0) is the canvas centre, and each
--- piece of content is positioned so its visual centre sits on its local
--- origin (the engine renders sprites centred on their transform).
function util.fitCanvas(entity)
    local w = gfx.screenWidth()
    local h = gfx.screenHeight()
    local s = math.min(w / CANVAS_W, h / CANVAS_H)
    scene.setField(entity, 'x', w * 0.5)
    scene.setField(entity, 'y', h * 0.5)
    scene.setField(entity, 'scale.x', s)
    scene.setField(entity, 'scale.y', s)
end

return util