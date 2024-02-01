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

    static uint8_t* inputMapping = (uint8_t*)0x67CA30;

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

    // 0x00410450
    void on_key_up(int keyCode, int a1)
    {
       for (int i = 0; i < 0x20; i++)
       {
            if (keyCode == inputMapping[i])
            {              
                gGameTable.word_680558 &= ~(1 << i);
            }
       }
    }

    // 0x00410410
   void on_read_key(int keyCode, int a1)
   {
       for (int i = 0; i < 0x20; i++)
       {
            if (keyCode == inputMapping[i])
            {
                gGameTable.word_680558 |= (1 << i);
            }
       }
   }

    void input_init_hooks()
    {
        writeJmp(0x00410450, on_key_up);
        writeJmp(0x00410410, on_read_key);
    }
};
