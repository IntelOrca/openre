--- LUA type definitions for OpenRE SDK.
---@meta

---@alias byte integer

re = {}

---@param group integer
---@param index integer
---@return boolean
function re.getFlag(group, index) end

---@param group integer
---@param index integer
---@param value boolean
function re.setFlag(group, index, value) end

---@param kind HookKind
---@param callback function
function re.subscribe(kind, callback) end

---@param kind EntityKind
---@param index integer
---@return Entity
function re.getEntity(kind, index) end

---@enum HookKind
HookKind = {
    tick = 0,
    loadRoom = 1
}

---@enum EntityKind
EntityKind = {
    player = 1,
    splayer = 2,
    enemy = 3,
    object = 4,
    door = 5
}

---@class Entity
---@field kind EntityKind
---@field index integer
---@field type integer
---@field life integer

---API functions that can read/write to the game's process memory.
---This is for development only and will eventually be removed.
unsafe = {}

---@param address integer
---@param len integer
---@return byte[]
function unsafe.read(address, len) end

---@param address integer
---@param bytes byte[]
function unsafe.write(address, bytes) end

---@class Texture
---@class TextureRect

---@class Color4f
---@field red number
---@field green number
---@field blue number
---@field alpha number

gfx = {
    ---@type integer
    screenWidth = 0,
    ---@type integer
    screenHeight = 0
}

---@param path string
---@return Movie
function gfx.loadMovie(path) end

---@param path string
---@param width integer
---@param height integer
---@return Texture
function gfx.loadTexture(path, width, height) end

---@param texture Texture
---@param left integer
---@param top integer
---@param right integer
---@param bottom integer
---@return TextureRect
function gfx.getTextureRect(texture, left, top, right, bottom) end

---@param texture Texture | TextureRect | Movie | nil
---@param left integer
---@param top integer
---@param depth integer
---@param width integer
---@param height integer
function gfx.drawTexture(texture, left, top, depth, width, height) end

---@param color Color4f
---@param left integer
---@param top integer
---@param depth integer
---@param width integer
---@param height integer
function gfx.drawSolid(color, left, top, depth, width, height) end

---@class Font

---@param path string
---@return Font
function gfx.loadFont(path) end

---@param font Font
---@param text string
---@param left integer
---@param top integer
---@param depth integer
---@param right integer
---@param bottom integer
function gfx.drawText(font, text, left, top, depth, right, bottom) end

---@enum MovieState
MovieState = {
    blank = 0,
    unsupported = 1,
    error = 2,
    stopped = 3,
    playing = 4,
    finished = 5
}

---@class Movie
---@field state MovieState
local Movie = {}

function Movie:play() end

function Movie:stop() end

sfx = {}

---@param path string
---@return Sound
function sfx.loadSound(path) end

---@param sound Sound
function sfx.playSound(sound) end

---@class Sound
local Sound = {}

---@enum InputCommand
InputCommand = {
    up = 1,
    down = 2,
    left = 3,
    right = 4,

    menuStart = 100,  -- [SPACE], (MENU), <TOUCHPAD>
    menuApply = 101,  -- [RETURN], (X), (A)
    menuCancel = 102, -- [ESC], (O), (B)

    inventory = 200,
    map = 201,
    aimManual = 202,
    aimAuto = 203,
    aimAutoEnemy = 204,
    aimAutoObject = 205,
    aimNext = 206,
    fire = 207,
    reload = 208,
    run = 209,
    menu = 210,
    quickTurn = 211,
}

input = {
    led = {
        ---@type number
        red = 0,
        ---@type number
        green = 0,
        ---@type number
        blue = 0
    },
    rumble = {
        ---@type number
        low = 0,
        ---@type number
        high = 0
    }
}

---@param ... InputCommand
function input.isDown(...) end

---@param ... InputCommand
function input.isPressed(...) end
