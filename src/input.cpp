#include "input.h"
#include "interop.hpp"
#include "openre.h"

#include <cstdint>

using namespace openre::interop;

namespace openre::input
{
    enum
    {
        ID_KEY_FORWARD = 0x11,
        ID_KEY_BACKWARD = 0x24,
        ID_KEY_TURN_RIGHT = 0x802,
        ID_KEY_TURN_LEFT = 0x408,
        ID_KEY_GET_READY = 0x100,
        ID_KEY_FIRE_AND_CONFIRM = 0x10C0,
        ID_KEY_RUN_AND_CANCEL = 0x2200,
        ID_KEY_MAP = 0x4000,
        // Unknown keys
        // ID_KEY_STATUS: Open inventory
        // ID_KEY_CTL_CONFIGURE: Open settings
    };

    int GetGamepadState()
    {
        int gamepadState = INPUT_NONE;
        if (gGameTable.g_key & ID_KEY_FORWARD)
        {
            gamepadState |= INPUT_UP;
        }
        if (gGameTable.g_key & ID_KEY_BACKWARD)
        {
            gamepadState |= INPUT_DOWN;
        }
        if (gGameTable.g_key & ID_KEY_TURN_RIGHT)
        {
            gamepadState |= INPUT_RIGHT;
        }
        if (gGameTable.g_key & ID_KEY_TURN_LEFT)
        {
            gamepadState |= INPUT_LEFT;
        }
        if (gGameTable.g_key & ID_KEY_GET_READY)
        {
            gamepadState |= INPUT_X;
        }
        if (gGameTable.g_key & ID_KEY_FIRE_AND_CONFIRM)
        {
            gamepadState |= INPUT_A;
        }
        if (gGameTable.g_key & ID_KEY_RUN_AND_CANCEL)
        {
            gamepadState |= INPUT_B;
        }
        if (gGameTable.g_key & ID_KEY_MAP)
        {
            gamepadState |= INPUT_START;
        }

        return gamepadState;
    }

    enum
    {
        INPUT_DEVICE_KEYBOARD,
        INPUT_DEVICE_GAMEPAD
    };

    // 0x00410450
    void input_wmkeyup(Input* self, int vk)
    {
        for (int i = 0; i < 0x20; i++)
        {
            if (vk == gGameTable.input.mapping[i])
            {
                gGameTable.input.keyboard &= ~(1 << i);
            }
        }
    }

    // 0x00410410
    void input_wmkeydown(Input* self, int vk)
    {
        for (int i = 0; i < 0x20; i++)
        {
            if (vk == gGameTable.input.mapping[i])
            {
                gGameTable.input.keyboard |= (1 << i);
            }
        }
    }

    // 0x00410400
    int input_get_some_byte()
    {
        return gGameTable.input.keyboard;
    }

    static uint32_t input_keyboard_data[32] = {
        0x1000, 0x4000, 0x8000, 0x2000, 0x20, 0x44, 0x2, 0x10, 0x4, 0x1,    0x8,    0x80,   0x100,  0x800, 0,    0,
        0,      0,      0,      0,      0,    0,    0,   0,    0,   0x1000, 0x4000, 0x8000, 0x2000, 0x80,  0x80, 0x40,
    };

    static uint32_t input_gamepad_data[32] = {
        0x1000, 0x4000, 0x8000, 0x2000, 0, 0, 0, 0, 0x80, 0x44, 0x8, 0x800, 0x10, 0x100, 0x2, 0,
        0x2,    0,      0,      0,      0, 0, 0, 0, 0,    0,    0,   0,     0,    0,     0,   0,
    };

    // 0x0043BAC0
    int get_input_device_state(int rawState, int inputType)
    {
        auto inputState = 0;
        for (int i = 0; i < 32; i++)
        {
            if (rawState & (1 << i))
            {
                if (inputType == INPUT_DEVICE_KEYBOARD)
                {
                    inputState |= input_keyboard_data[i];
                }
                else if (inputType == INPUT_DEVICE_GAMEPAD)
                {
                    inputState |= input_gamepad_data[i];
                }
            }
        }
        return inputState;
    }

    // 0x004100F0
    int joy_get_pos_ex(uint8_t* a0)
    {
        using sig = int (*)(uint8_t*);
        auto p = (sig)0x004100F0;
        return p(a0);
    }

    // 0x0043BB00
    int sub_43BB00()
    {
        auto v1 = gGameTable.dword_66D394;

        joy_get_pos_ex(gGameTable.input.mapping);
        if (gGameTable.input.var_1F8 != 0)
        {
            v1 = get_input_device_state(gGameTable.input.keyboard_raw_state, INPUT_DEVICE_KEYBOARD);

            gGameTable.dword_99CF64 = gGameTable.input.keyboard_raw_state;
            gGameTable.dword_66D394 = v1;
        }
        gGameTable.dword_99CF70 = 0;
        if (gGameTable.input.var_3B24 >= 2 && gGameTable.input.var_3D0 != 0)
        {
            auto v2 = get_input_device_state(gGameTable.input.gamepad_raw_state, INPUT_DEVICE_GAMEPAD);
            v1 |= v2;
            gGameTable.dword_99CF70 = v2;
            gGameTable.dword_66D394 = v1;
        }

        return v1;
    }

    // 0x004102E0
    void input_init(Input* self)
    {
        interop::thiscall<int, Input*>(0x004102E0, self);
    }

    // 0x004103F0
    void input_pause(Input* self)
    {
        interop::thiscall<int, Input*>(0x004103F0, self);
    }

    void input_init_hooks()
    {
        writeJmp(0x00410450, &input_wmkeyup);
        writeJmp(0x00410410, &input_wmkeydown);
        writeJmp(0x00410400, &input_get_some_byte);
        writeJmp(0x0043BB00, &sub_43BB00);
    }
};
