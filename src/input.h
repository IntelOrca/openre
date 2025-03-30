#pragma once

#include "openre.h"

namespace openre::input
{
    enum InputKey
    {
        INPUT_NONE = (0 << 0),
        INPUT_LEFT = (1 << 0),
        INPUT_DOWN = (1 << 1),
        INPUT_RIGHT = (1 << 2),
        INPUT_UP = (1 << 3),
        INPUT_X = (1 << 4),
        INPUT_A = (1 << 5),
        INPUT_B = (1 << 6),
        INPUT_Y = (1 << 7),
        INPUT_SELECT = (1 << 8),
        INPUT_START = (1 << 9)
    };

    enum
    {
        KEY_TYPE_FORWARD = 1,
        KEY_TYPE_BACKWARD = 4,
        KEY_TYPE_LEFT = 2,
        KEY_TYPE_RIGHT = 8,
        KEY_TYPE_ROTATE = 10,
        KEY_TYPE_128 = 128,
        KEY_TYPE_AIM = 256,
        KEY_TYPE_RUN_AND_CANCEL = 512,
        KEY_TYPE_ACTION = 4096,
    };

    [[nodiscard]] int GetGamepadState();

    [[nodiscard]] inline bool check_input(int key)
    {
        return gGameTable.key_trg & key;
    }

    void input_init_hooks();
};