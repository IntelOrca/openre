#pragma once

#include "openre.h"

#include <cstdint>
#include <vector>

namespace openre::input
{
    // The game commands. Command state is a 19-bit mask: bit N == command N
    // active. Each command maps to one or more of the 6 legacy output values.
    enum Command : uint8_t
    {
        COMMAND_CANCEL = 0,
        COMMAND_ACCEPT,
        COMMAND_UP,
        COMMAND_DOWN,
        COMMAND_LEFT,
        COMMAND_RIGHT,
        COMMAND_FORWARD,
        COMMAND_BACKWARD,
        COMMAND_TURN_LEFT,
        COMMAND_TURN_RIGHT,
        COMMAND_AIM,
        COMMAND_RUN,
        COMMAND_FIRE,
        COMMAND_RELOAD,
        COMMAND_INVENTORY,
        COMMAND_MENU,
        COMMAND_INTERACT,
        COMMAND_MAP,
        COMMAND_CHANGE_TARGET,
        COMMAND_COUNT,
    };

    // Which raw device a binding token refers to.
    enum class BindingDevice : uint8_t
    {
        Keyboard,
        Gamepad,
        Mouse,
    };

    // Gamepad sources usable in bindings.
    enum class GamepadSource : uint8_t
    {
        DpadUp,
        DpadDown,
        DpadLeft,
        DpadRight,
        AxisLeftYNeg, // left stick up
        AxisLeftYPos, // left stick down
        AxisLeftXNeg, // left stick left
        AxisLeftXPos, // left stick right
        South,
        East,
        North,
        West,
        LTrigger,
        RTrigger,
        LShoulder,
        RShoulder,
        Start,
        Mode,
        L3,
        R3,
        Count,
    };

    // One parsed binding token. For Keyboard, key is a Win32 VK code; for
    // Gamepad, key is a GamepadSource; for Mouse, key is 0=left, 1=middle,
    // 2=right.
    struct Binding
    {
        BindingDevice device;
        uint8_t key;
    };

    // The parsed bindings for all commands (from the [input] INI section).
    struct CommandBindings
    {
        std::vector<Binding> bindings[COMMAND_COUNT];
    };

    enum
    {
        KEY_TYPE_FORWARD = 1,
        KEY_TYPE_BACKWARD = 4,
        // g_key bit 1 (0x2) = right / turn right; g_key bit 3 (0x8) = left /
        // turn left. Verified in pl_br_03 (0x4DA6C0): Key&2 moves the player
        // right, Key&8 moves left. These were previously named LEFT=2/RIGHT=8
        // (matching the original binary's g_key layout), renamed to make the
        // semantics explicit.
        KEY_TYPE_TURN_RIGHT = 2,
        KEY_TYPE_TURN_LEFT = 8,
        KEY_TYPE_ROTATE = 10,
        KEY_TYPE_CHANGE_TARGET = 32, // 0x20: re-acquire/change aim target (key_trg only)
        KEY_TYPE_128 = 128,
        KEY_TYPE_AIM = 256,
        KEY_TYPE_RUN_AND_CANCEL = 512,
        KEY_TYPE_4096 = 4096,
        KEY_TYPE_16384 = 16384,
    };

    [[nodiscard]] inline bool check_input(int key)
    {
        return gGameTable.key_trg & key;
    }

    Input* input_init(Input* self);
    void input_pause(Input* self);
    void input_wmkeyup(Input* self, int vk);
    void input_wmkeydown(Input* self, int vk);
    void pad_set();
    [[nodiscard]] int16_t get_menu_key();
    void input_init_hooks();

    // Command engine API.
    // Loads command bindings from the [input] INI section (falls back to the
    // built-in defaults when a key is missing). Called once during init.
    void load_bindings();
    // Writes the current command bindings back to the [input] INI section.
    void save_bindings();
};