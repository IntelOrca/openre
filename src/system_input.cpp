#include "system_input.h"
#include "logger.h"

#include <windows.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstring>

namespace openre::system::input
{
    namespace
    {
        SDL_Gamepad* g_gamepads[kMaxGamepads] = {};
        int g_gamepadCount = 0;

        // Converts an SDL_Keycode into the equivalent Win32 VK code. This is a
        // superset of the mapping in system_window.cpp so that every key in the
        // game's menu/config VK code tables (including OEM keys) is covered.
        int sdl_keycode_to_vk(SDL_Keycode key)
        {
            // Letters: SDL3 uses lowercase ASCII keycodes (SDLK_A = 'a' = 0x61).
            if (key >= SDLK_A && key <= SDLK_Z)
                return key - 'a' + 'A';

            // Digits map to the same value.
            if (key >= SDLK_0 && key <= SDLK_9)
                return key;

            // F1-F12
            if (key >= SDLK_F1 && key <= SDLK_F12)
                return VK_F1 + (key - SDLK_F1);

            // Numpad 1-9
            if (key >= SDLK_KP_1 && key <= SDLK_KP_9)
                return VK_NUMPAD1 + (key - SDLK_KP_1);

            switch (key)
            {
            case SDLK_RETURN: return VK_RETURN;
            case SDLK_ESCAPE: return VK_ESCAPE;
            case SDLK_BACKSPACE: return VK_BACK;
            case SDLK_TAB: return VK_TAB;
            case SDLK_SPACE: return VK_SPACE;
            case SDLK_PRINTSCREEN: return VK_SNAPSHOT;
            case SDLK_CAPSLOCK: return VK_CAPITAL;
            case SDLK_KP_0: return VK_NUMPAD0;
            case SDLK_KP_ENTER: return VK_RETURN;
            case SDLK_KP_DIVIDE: return VK_DIVIDE;
            case SDLK_KP_MULTIPLY: return VK_MULTIPLY;
            case SDLK_KP_MINUS: return VK_SUBTRACT;
            case SDLK_KP_PLUS: return VK_ADD;
            case SDLK_KP_PERIOD: return VK_DECIMAL;
            case SDLK_LEFT: return VK_LEFT;
            case SDLK_UP: return VK_UP;
            case SDLK_RIGHT: return VK_RIGHT;
            case SDLK_DOWN: return VK_DOWN;
            case SDLK_INSERT: return VK_INSERT;
            case SDLK_DELETE: return VK_DELETE;
            case SDLK_HOME: return VK_HOME;
            case SDLK_PAGEUP: return VK_PRIOR;
            case SDLK_END: return VK_END;
            case SDLK_PAGEDOWN: return VK_NEXT;
            case SDLK_LSHIFT: [[fallthrough]];
            case SDLK_RSHIFT: return VK_SHIFT;
            case SDLK_LCTRL: [[fallthrough]];
            case SDLK_RCTRL: return VK_CONTROL;
            case SDLK_LALT: [[fallthrough]];
            case SDLK_RALT: return VK_MENU;

            // OEM keys, used by the control configuration screen.
            case SDLK_SEMICOLON: return VK_OEM_1;
            case SDLK_EQUALS: return VK_OEM_PLUS;
            case SDLK_COMMA: return VK_OEM_COMMA;
            case SDLK_MINUS: return VK_OEM_MINUS;
            case SDLK_PERIOD: return VK_OEM_PERIOD;
            case SDLK_SLASH: return VK_OEM_2;
            case SDLK_GRAVE: return VK_OEM_3;
            case SDLK_LEFTBRACKET: return VK_OEM_4;
            case SDLK_BACKSLASH: return VK_OEM_5;
            case SDLK_RIGHTBRACKET: return VK_OEM_6;
            case SDLK_APOSTROPHE: return VK_OEM_7;
            case SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_NONUSBACKSLASH): return VK_OEM_102;
            default: break;
            }
            return 0;
        }

        // Maps SDL gamepad buttons to the classic WinMM joystick dwButtons
        // bits (south = button 1 = bit 0, ...). The game shifts these left by
        // 8 (result = dir | (buttons << 8)) to build its raw gamepad state, so
        // south -> raw bit 8 -> input_gamepad_data[8] = 0x80 (interact), which
        // matches the original binary exactly. Triggers are analog axes that
        // behave as button 7 / button 8 (bits 6/7) when pressed.
        struct ButtonMapping
        {
            SDL_GamepadButton sdlButton;
            uint32_t bit;
        };
        constexpr ButtonMapping kButtonMap[] = {
            { SDL_GAMEPAD_BUTTON_SOUTH, 0 },          // button 1
            { SDL_GAMEPAD_BUTTON_EAST, 1 },           // button 2
            { SDL_GAMEPAD_BUTTON_NORTH, 2 },          // button 3
            { SDL_GAMEPAD_BUTTON_WEST, 3 },           // button 4
            { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 4 },  // button 5
            { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 5 }, // button 6
            { SDL_GAMEPAD_BUTTON_START, 8 },          // button 9
            { SDL_GAMEPAD_BUTTON_BACK, 9 },           // button 10
            { SDL_GAMEPAD_BUTTON_LEFT_STICK, 10 },    // button 11
            { SDL_GAMEPAD_BUTTON_RIGHT_STICK, 11 },   // button 12
        };

        bool is_trigger_pressed(SDL_Gamepad* gamepad, SDL_GamepadAxis axis)
        {
            // Analog trigger axis range is 0..32767; treat the upper half as pressed.
            constexpr int kTriggerThreshold = 0x4000;
            return SDL_GetGamepadAxis(gamepad, axis) > kTriggerThreshold;
        }
    }

    bool init()
    {
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
        {
            logging::logError("[SDL3] SDL_InitSubSystem(SDL_INIT_GAMEPAD) failed: {}", SDL_GetError());
            return false;
        }

        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (count > kMaxGamepads)
        {
            count = kMaxGamepads;
        }
        g_gamepadCount = count;
        for (int i = 0; i < count; i++)
        {
            g_gamepads[i] = SDL_OpenGamepad(ids[i]);
            if (!g_gamepads[i])
            {
                logging::logError("[SDL3] SDL_OpenGamepad failed for device {}: {}", i, SDL_GetError());
            }
        }
        SDL_free(ids);
        return true;
    }

    void get_keyboard_state(uint8_t keyState[256])
    {
        memset(keyState, 0, 256);
        const bool* state = SDL_GetKeyboardState(nullptr);
        for (int sc = 0; sc < SDL_SCANCODE_COUNT; sc++)
        {
            if (!state[sc])
            {
                continue;
            }
            auto key = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(sc), SDL_KMOD_NONE, false);
            auto vk = sdl_keycode_to_vk(key);
            if (vk > 0 && vk < 256)
            {
                keyState[vk] |= 0x80;
            }
        }
    }

    int get_gamepad_count()
    {
        return g_gamepadCount;
    }

    uint8_t get_mouse_buttons()
    {
        float x = 0, y = 0;
        uint32_t buttons = SDL_GetMouseState(&x, &y);
        uint8_t result = 0;
        if (buttons & SDL_BUTTON_LMASK)
        {
            result |= 1u << 0;
        }
        if (buttons & SDL_BUTTON_MMASK)
        {
            result |= 1u << 1;
        }
        if (buttons & SDL_BUTTON_RMASK)
        {
            result |= 1u << 2;
        }
        return result;
    }

    const char* get_gamepad_name(int index)
    {
        if (index < 0 || index >= g_gamepadCount || !g_gamepads[index])
        {
            return "";
        }
        auto name = SDL_GetGamepadName(g_gamepads[index]);
        return name ? name : "";
    }

    int get_gamepad_button_count(int index)
    {
        return SDL_GAMEPAD_BUTTON_COUNT;
    }

    int get_gamepad_axis_count(int index)
    {
        return SDL_GAMEPAD_AXIS_COUNT;
    }

    bool poll_gamepad(int index, GamepadState& state)
    {
        if (index < 0 || index >= g_gamepadCount)
        {
            return false;
        }
        SDL_Gamepad* gamepad = g_gamepads[index];
        if (!gamepad || !SDL_GamepadConnected(gamepad))
        {
            return false;
        }

        // Left stick, mapped from -32768..32767 to 0..0xFFFF so the game's
        // axis thresholds (0x3000/0xC000) keep working.
        state.xPos = static_cast<uint32_t>(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX) + 0x8000);
        state.yPos = static_cast<uint32_t>(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY) + 0x8000);

        uint32_t buttons = 0;
        for (const auto& mapping : kButtonMap)
        {
            if (SDL_GetGamepadButton(gamepad, mapping.sdlButton))
            {
                buttons |= 1u << mapping.bit;
            }
        }
        // Analog triggers behave as button 7 / button 8 when pressed.
        bool lTrigger = is_trigger_pressed(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        bool rTrigger = is_trigger_pressed(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        if (lTrigger)
        {
            buttons |= 1u << 6;
        }
        if (rTrigger)
        {
            buttons |= 1u << 7;
        }
        state.lTrigger = lTrigger;
        state.rTrigger = rTrigger;
        state.buttons = buttons;

        // Synthesize the POV angle from the D-pad buttons, in hundredths of a
        // degree clockwise from up (0 = up, 9000 = right, ...), 0xFFFFFFFF when
        // centered. The values fall inside the game's POV threshold bands.
        bool dpadUp = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
        bool dpadDown = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        bool dpadLeft = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        bool dpadRight = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        if (dpadUp && dpadLeft)
        {
            state.pov = 31500;
        }
        else if (dpadUp && dpadRight)
        {
            state.pov = 4500;
        }
        else if (dpadDown && dpadLeft)
        {
            state.pov = 22500;
        }
        else if (dpadDown && dpadRight)
        {
            state.pov = 13500;
        }
        else if (dpadUp)
        {
            state.pov = 0;
        }
        else if (dpadRight)
        {
            state.pov = 9000;
        }
        else if (dpadDown)
        {
            state.pov = 18000;
        }
        else if (dpadLeft)
        {
            state.pov = 27000;
        }
        else
        {
            state.pov = 0xFFFFFFFF;
        }
        return true;
    }
}
