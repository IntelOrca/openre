#pragma once

#include <cstdint>

namespace openre::system::input
{
    // Generic input abstraction, decoupled from SDL so callers never need SDL
    // headers. Replaces the Win32 input APIs previously used by input.cpp.

    // Maximum number of gamepads the SDL layer will open. Also bounds the
    // legacy raw-layer scan in input.cpp's joy_get_pos_ex / input_init.
    constexpr int kMaxGamepads = 32;

    // Initialises the SDL gamepad subsystem and opens all connected gamepads.
    // Returns false on failure.
    bool init();

    // Fills keyState with the current keyboard state, keyed by Win32 VK code
    // (bit 0x80 set = key down), matching GetKeyboardState() semantics.
    void get_keyboard_state(uint8_t keyState[256]);

    // Mouse button state: bit 0 = left, bit 1 = middle, bit 2 = right.
    // Buttons only — no cursor tracking.
    uint8_t get_mouse_buttons();

    // Gamepad enumeration (replaces joyGetNumDevs / joyGetDevCapsA).
    int get_gamepad_count();
    // Device name, or "" if unavailable (replaces JOYCAPS.szPname).
    const char* get_gamepad_name(int index);
    // Number of buttons/axes supported by the device.
    int get_gamepad_button_count(int index);
    int get_gamepad_axis_count(int index);

    // Raw gamepad poll (replaces joyGetPosEx). Returns false when the gamepad
    // is unavailable/unplugged. Axis values are in 0..0xFFFF (0 = up/left,
    // 0xFFFF = down/right). `buttons` uses the classic dwButtons convention
    // so that `result = dir | (buttons << 8)` in joy_get_pos_ex reproduces the
    // original binary's raw gamepad bits exactly (south -> raw bit 8, ...).
    struct GamepadState
    {
        uint32_t xPos;    // left stick X in 0..0xFFFF (0 = left, 0xFFFF = right)
        uint32_t yPos;    // left stick Y in 0..0xFFFF (0 = up, 0xFFFF = down)
        uint32_t pov;     // D-pad angle in 0..36000 (hundredths of a degree), 0xFFFFFFFF = centered
        bool lTrigger;    // left analog trigger pressed past the threshold
        bool rTrigger;    // right analog trigger pressed past the threshold
        uint32_t buttons; // bit 0=south,1=east,2=north,3=west,4=L_shoulder,5=R_shoulder,
                          // 6=L_trigger,7=R_trigger,8=start,9=back,10=L3,11=R3
    };
    bool poll_gamepad(int index, GamepadState& state);
}
