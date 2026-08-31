-- game.lua - non-blocking game-session tracking (shared by the app screens).
--
-- core.runGame() is non-blocking: it enables the LegacyGame node and returns,
-- then LegacyGameSystem drives the session inside the normal app-loop until
-- the legacy game signals a return to the menu, at which point the node is
-- disabled again. Screens that start a session register a resume callback so
-- they can hand back to the menu when the session ends.
--
--   game.resumeAfterGame(fn)  - run fn once when the current/next session ends.
--   game.inGame()             - true while a session is live (core.inGame).
--   game.track()              - per-frame edge detection; called by app.GameFlow.
--   game.spawnMovieEntity()   - async video-entity helper for intro movies.

local game = {}

local pending = {}  -- resume callbacks, fired when a session ends
local inSession = false

--- Register a callback to run when the current/next game session returns to
--- the menu. Callbacks fire exactly once, in registration order.
function game.resumeAfterGame(fn)
    pending[#pending + 1] = fn
    -- `core.runGame()` enables the LegacyGame node before returning, so a
    -- session is typically already live when a caller registers its resume
    -- callback (the usual pattern is runGame() immediately followed by
    -- resumeAfterGame() in the same managed dispatch). Mark it here so track()
    -- can spot the transition even if the game returns to the menu before any
    -- later managed dispatch observes the enabled node (i.e. a session that
    -- starts and ends between dispatches would otherwise never fire).
    if core.inGame() then
        inSession = true
    end
end

--- Drop every pending resume callback. The pause menu's Restart/Load flows
--- take over the session end (they clear the callbacks the original boot
--- registered and install their own continuation), so no stale callback
--- (e.g. the load screen's Title:new) can fire on the way out.
function game.clearResumeCallbacks()
    pending = {}
end

function game.inGame()
    return core.inGame()
end

--- Per-frame edge detection: watch core.inGame() and fire the pending resume
--- callbacks on the enabled -> disabled transition. Called by the app.GameFlow
--- managed system every frame (which also runs during gameplay).
function game.track()
    local ig = core.inGame()
    if inSession and not ig then
        inSession = false
        local resumes = pending
        pending = {}
        for i = 1, #resumes do
            local ok, err = pcall(resumes[i])
            if not ok then
                print('[game] resume callback failed: ' .. tostring(err))
            end
        end
    end
    if ig then
        inSession = true
    end
end

--- Spawn a full-window async video entity (core.Video) under `root`. The
--- native VideoSystem auto-starts playback when it sees the component with a
--- movie path and status Playing; destroying the returned entity is the whole
--- teardown. Returns nil when the movie could not be created. Unless the
--- caller explicitly passes a root, movies play under the user layer.
---@param root scene handle (e.g. scene.getRoot('user'))
---@param path string movie path (game-relative, resolved by the VideoSystem)
---@return handle|nil entity
function game.spawnMovieEntity(root, path)
    local w = gfx.screenWidth()
    local h = gfx.screenHeight()

    local entity = scene.createEntity(root)
    if entity == nil then
        return nil
    end
    scene.setField(entity, 'name', 'game.movie')
    -- Sprites are centred on their transform, so anchor the full-window movie
    -- on the screen centre (same as video_play).
    scene.setField(entity, 'x', w * 0.5)
    scene.setField(entity, 'y', h * 0.5)

    local video = scene.addComponent(entity, 'core.Video')
    scene.setField(video, 'movie', path)
    scene.setField(video, 'status', 1) -- VideoStatus.Playing

    local size = scene.addComponent(entity, 'core.UiSize')
    scene.setField(size, 'width', w)
    scene.setField(size, 'height', h)

    return entity
end

return game