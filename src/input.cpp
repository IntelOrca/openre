#include "input.h"
#include "interop.hpp"
#include "marni.h"
#include "openre.h"

#include <windows.h>

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
        0x1000, 0x4000, 0x8000, 0x2000, 0, 0, 0, 0, 0x80, 0x44, 0x800, 0, 0x10, 0x100, 0, 0x8,
        0x2,    0,      0,      0,      0, 0, 0, 0, 0,    0,    0,     0, 0,    0,     0, 0,
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

    // ---- key polling helper state ----

    static uint8_t menu_key_state[256] = {};
    static uint8_t config_key_state[256] = {};

    static const uint8_t menu_vk_codes[] = {
        0x21, 0x22, 0x23, 0x24, 0x2E, // VK_PRIOR, VK_NEXT, VK_END, VK_HOME, VK_DELETE
    };

    static const uint8_t config_vk_codes[] = {
        0x08, 0x09, 0x10, 0x11, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x41, 0x42, 0x43,
        0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54,
        0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A,
        0x6B, 0x6D, 0x6E, 0x6F, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xDB, 0xDC, 0xDD, 0xDE, 0xE2,
    };

    // 0x00432670
    static int16_t get_menu_key()
    {
        uint8_t key_state[256];
        GetKeyboardState(key_state);

        for (int i = 0; i < 5; i++)
        {
            auto vk = menu_vk_codes[i];
            auto cur = (int8_t)key_state[vk];
            auto old = (int8_t)menu_key_state[vk];
            if (((cur ^ old) & (cur < 0 ? 1 : 0)) != 0)
            {
                memcpy(menu_key_state, key_state, sizeof(menu_key_state));
                return vk;
            }
        }
        memcpy(menu_key_state, key_state, sizeof(menu_key_state));
        return 0;
    }

    // 0x004354D0
    static int16_t get_config_key_state()
    {
        uint8_t key_state[256];
        GetKeyboardState(key_state);

        for (int i = 0; i < 67; i++)
        {
            auto vk = config_vk_codes[i];
            auto cur = (int8_t)key_state[vk];
            auto old = (int8_t)config_key_state[vk];
            if (((cur ^ old) & (cur < 0 ? 1 : 0)) != 0)
            {
                memcpy(config_key_state, key_state, sizeof(config_key_state));
                return vk;
            }
        }
        memcpy(config_key_state, key_state, sizeof(config_key_state));
        return 0;
    }

    // 0x004100F0 - Polls all joysticks and processes POV/buttons
    int joy_get_pos_ex(Input* self)
    {
        // Update keyboard trg/old tracking
        auto someByte = input_get_some_byte();
        auto changes = someByte ^ self->keyboard_raw_state;
        self->keyboard_old = self->keyboard_raw_state;
        self->keyboard_raw_state = someByte;
        self->keyboard_trg = someByte & changes;
        self->var_1F8 = 1;

        // Per-joystick data starts at offset 0x208 with stride 0x1D8
        auto joystick_base = reinterpret_cast<uint8_t*>(self) + 0x208;
        int result = 0;
        for (int joy = 1; joy < 32; joy++)
        {
            result = 0;
            auto joystick = reinterpret_cast<uint32_t*>(joystick_base);
            auto gamepadState = reinterpret_cast<uint32_t*>(joystick_base - 12); // self + 0x1FC

            if (joystick[0x1C8 / 4] != 0) // init flag at offset 0x1C8
            {
                memset(joystick, 0, 0x34);
                joystick[0] = 52;   // JOYINFOEX::dwSize
                joystick[1] = 0xFF; // JOYINFOEX::dwFlags (JOY_RETURNALL)

                auto mmres = joyGetPosEx(joy - 1, (LPJOYINFOEX)joystick);
                if (mmres != MMSYSERR_NODRIVER && mmres != JOYERR_PARMS
                    && mmres != 167) // JOYERR_UNPLUGGED (not in all headers)
                {
                    // Process POV hat into directional bits
                    auto xPos = joystick[2]; // dwXpos
                    auto yPos = joystick[3]; // dwYpos
                    int dir = 0;
                    if (xPos > 0xC000)
                        dir |= 8; // left
                    if (xPos < 0x3000)
                        dir |= 4; // right
                    if (yPos > 0xC000)
                        dir |= 2; // down
                    if (yPos < 0x3000)
                        dir |= 1; // up

                    // Process POV hat
                    if ((reinterpret_cast<uint8_t*>(joystick)[148] & 0x10) != 0)
                    {
                        auto pov = joystick[10]; // dwPOV
                        if (pov != 0xFFFFFFFF)
                        {
                            if (pov < 0x1187)
                                dir |= 0x10;
                            else if (pov < 0x230F)
                                dir |= 0x90;
                            else if (pov < 0x3496)
                                dir |= 0x80;
                            else if (pov < 0x461E)
                                dir |= 0xA0;
                            else if (pov < 0x57A5)
                                dir |= 0x20;
                            else if (pov < 0x692D)
                                dir |= 0x60;
                            else if (pov < 0x7AB4)
                                dir |= 0x40;
                            else if (pov < 0x8C3C)
                                dir |= 0x50;
                        }
                    }

                    result = dir | (joystick[8] << 8); // buttons (dwButtons)

                    // Update gamepad state trg/old tracking
                    auto v = result ^ gamepadState[0]; // gamepad_raw_state
                    gamepadState[2] = gamepadState[0]; // gamepad_old = old gamepad_raw_state
                    gamepadState[0] = result;          // gamepad_raw_state = result
                    gamepadState[1] = result & v;      // gamepad_trg = result & (result ^ old)
                }
                else
                {
                    const char* msg;
                    if (mmres == MMSYSERR_NODRIVER)
                        msg = "\x83\x57\x83\x87\x83\x43\x83\x58\x83\x65\x83\x42\x83\x62\x83\x4E "
                              "\x83\x68\x83\x89\x83\x43\x83\x6F\x82\xAA\x91\xB6\x8D\xDD\x82\xB5\x82\xDC\x82\xB9\x82\xF1";
                    else if (mmres == JOYERR_PARMS)
                        msg = "\x8E\x77\x92\xE8\x82\xB3\x82\xEA\x82\xBD\x83\x57\x83\x87\x83\x43\x83\x58\x83\x65\x83\x42\x83\x62"
                              "\x83\x4E\x49\x44\x20\x28\x49\x44\x44\x65\x76\x69\x63\x65\x29\x20\x82\xAA\x96\xB3\x8C\xF8\x82\xC5"
                              "\x82\xB7";
                    else
                        msg = "\x8E\x77\x92\xE8\x82\xB3\x82\xEA\x82\xBD\x83\x57\x83\x87\x83\x43\x83\x58\x83\x65\x83\x42\x83\x62"
                              "\x83\x4E\x82\xCD\x83\x56\x83\x58\x83\x65\x83\x80\x82\xC9\x90\xDA\x91\xB1\x82\xB3\x82\xEA\x82\xC4"
                              "\x82\xA2\x82\xDC\x82\xB9\x82\xF1";
                    marni::out(msg, "MarniSystem DirectInput Class");
                }
            }
            else
            {
                gamepadState[2] = 0; // gamepad_old = 0
                gamepadState[0] = 0; // gamepad_raw_state = 0
                gamepadState[1] = 0; // gamepad_trg = 0
            }

            joystick_base += 0x1D8;
        }
        return result;
    }

    // 0x0043BB00
    int sub_43BB00()
    {
        auto v1 = gGameTable.dword_66D394;

        joy_get_pos_ex(reinterpret_cast<Input*>(gGameTable.input.mapping));
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
    Input* input_init(Input* self)
    {
        auto joyCount = joyGetNumDevs() + 1;
        self->var_3B24 = joyCount;
        if (joyCount >= 32)
        {
            marni::out(
                "\x83\x8F\x81\x5B\x83\x4E\x82\xAA\x91\xAB\x82\xE8\x82\xDC\x82\xB9\x82\xF1\x82\xC5\x82\xB5\x82\xBD",
                "DirectInput::WM_Create");
            return self;
        }

        memset(reinterpret_cast<uint8_t*>(self) + 0x24, 0, 0x3B00);

        if (self->var_3B24 > 1)
        {
            auto joystick = reinterpret_cast<uint8_t*>(self) + 0x208;
            for (auto joy = 1u; joy < self->var_3B24; joy++)
            {
                auto caps = joystick + 0x34;
                *reinterpret_cast<uint32_t*>(joystick + 0x1C8) = 1; // init flag
                joyGetDevCapsA(joy - 1, (LPJOYCAPSA)caps, 0x194);
                memset(joystick, 0, 0x34);
                *reinterpret_cast<uint32_t*>(joystick) = 52;       // JOYINFOEX::dwSize
                *reinterpret_cast<uint32_t*>(joystick + 4) = 0xFF; // JOYINFOEX::dwFlags

                auto mmres = joyGetPosEx(joy - 1, (LPJOYINFOEX)joystick);
                if (mmres == MMSYSERR_NODRIVER || mmres == JOYERR_PARMS
                    || mmres == 167) // JOYERR_UNPLUGGED (not in all headers)
                {
                    *reinterpret_cast<uint32_t*>(joystick + 0x1C8) = 0; // mark as uninitialized
                }
                else
                {
                    char msg[1024];
                    sprintf(
                        msg,
                        "\x83\x57\x83\x87\x83\x43\x83\x58\x83\x65\x83\x42\x83\x62\x83\x4E"
                        "\x82\x68\x82\x63%d\x94\xD4\x82\xCD\x81\x41%s\x83\x7B\x83\x5E\x83\x93"
                        "\x82\xCC\x90\x94 %d \x8E\xB2\x82\xCC\x90\x94%d ",
                        joy,
                        reinterpret_cast<const char*>(joystick + 0x38),
                        *reinterpret_cast<uint32_t*>(joystick + 0x70),
                        *reinterpret_cast<uint32_t*>(joystick + 0x98));
                    marni::out(msg, "MarniSystem DirectInput Class");
                }

                joystick += 0x1D8;
            }
        }
        return self;
    }

    // 0x004103F0
    void input_pause(Input* self)
    {
        interop::thiscall<int, Input*>(0x004103F0, self);
    }

    // 0x004D0F30
    void pad_set()
    {
        // Save previous raw input
        gGameTable.dword_9885F8 = gGameTable.dword_9885F4;

        // Copy Vk_press bit 4 (0x10) to bit 5 (0x20), then clear bit 4
        auto vk = gGameTable.vk_press;
        vk = vk & 0xDF;
        if (vk & 0x10)
            vk |= 0x20;
        vk = vk & 0xEF;
        gGameTable.vk_press = vk;

        // Get combined keyboard + gamepad input
        int rawInput = sub_43BB00();
        int prevInput = gGameTable.dword_9885F8;
        gGameTable.dword_9885F4 = rawInput;

        // Demo playback: if demo flag is set, replay recorded input
        if (check_flag(FlagGroup::System, FG_SYSTEM_DEMO))
        {
            auto inputChanged = (rawInput & (rawInput ^ prevInput) & 0xFFF) != 0;
            if (inputChanged || gGameTable.word_98E52A >= gGameTable.pdemo.frames || (gGameTable.vk_press & 0x40))
            {
                gGameTable.vk_press &= ~0x40;
                if (check_flag(FlagGroup::System, FG_SYSTEM_1))
                {
                    if (gGameTable.word_98E52A < gGameTable.pdemo.frames)
                        gGameTable.byte_98F1BB = 1;
                    gGameTable.word_98E52A = gGameTable.pdemo.frames + 1;
                    gGameTable.dword_9885F4 = 0;
                    rawInput = 0;
                }
                else
                {
                    set_flag(FlagGroup::System, FG_SYSTEM_19, true);
                }
            }
            else if (check_flag(FlagGroup::System, FG_SYSTEM_1))
            {
                rawInput = gGameTable.pdemo.input[gGameTable.word_98E52A];
                gGameTable.word_98E52A++;
                gGameTable.dword_9885F4 = rawInput;
            }
        }

        // Map raw input bits to logical key bits via the mapping table
        int oldKey = gGameTable.g_key;
        int newKey = 0;
        gGameTable.dword_98860C = gGameTable.g_key;
        gGameTable.g_key = 0;

        auto* mapping = &gGameTable.word_5338D8[16 * gGameTable.byte_98E9AA];

        for (int i = 0; i < 16; i++)
        {
            if (mapping[i] & rawInput)
                newKey |= (1 << i);
        }

        gGameTable.g_key = newKey;

        // Stop flag handling: if input is blocked, only allow directional keys
        if (gGameTable.fg_stop & 0x1000000)
        {
            newKey &= 0x3C00;
            gGameTable.dword_689B3C = gGameTable.fg_stop;
            gGameTable.g_key = newKey;
        }
        else if (gGameTable.dword_689B3C & 0x1000000)
        {
            oldKey = newKey;
            gGameTable.dword_689B3C = 0;
            gGameTable.dword_98860C = newKey;
        }

        // Calculate trigger (edge detection) values
        uint32_t inputTrigger = rawInput & (rawInput ^ prevInput);
        uint32_t keyTrigger = newKey & (newKey ^ oldKey);

        gGameTable.dword_9885FE = (gGameTable.dword_9885FE & 0xFFFF0000) | (inputTrigger & 0xFFFF);
        gGameTable.word_9885FC = (uint16_t)gGameTable.dword_9885F4;
        gGameTable.key_trg = keyTrigger;
        gGameTable.dword_9885F8 = inputTrigger;

        // Key repeat handling for the mask bits in dword_98F074
        if (gGameTable.dword_98F074 & inputTrigger)
        {
            gGameTable.byte_533938 = gGameTable.word_98F078 & 0xFF;
            set_flag(FlagGroup::System, FG_SYSTEM_0, true);
        }
        else if (gGameTable.byte_533938)
        {
            if (gGameTable.dword_98F074 & rawInput)
                gGameTable.byte_533938--;
            set_flag(FlagGroup::System, FG_SYSTEM_0, false);
        }
        else
        {
            gGameTable.byte_533938 = (gGameTable.word_98F078 >> 8) & 0xFF;
            set_flag(FlagGroup::System, FG_SYSTEM_0, true);
        }
    }

    void input_init_hooks()
    {
        writeJmp(0x00410450, &input_wmkeyup);
        writeJmp(0x00410410, &input_wmkeydown);
        writeJmp(0x00410400, &input_get_some_byte);
        writeJmp(0x004100F0, &joy_get_pos_ex);
        writeJmp(0x004102E0, &input_init);
        writeJmp(0x00432670, &get_menu_key);
        writeJmp(0x004354D0, &get_config_key_state);
        writeJmp(0x0043BB00, &sub_43BB00);
        writeJmp(0x004D0F30, &pad_set);
    }
};
