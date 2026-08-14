#include "input.h"
#include "interop.hpp"
#include "logger.h"
#include "marni.h"
#include "openre.h"
#include "system_config.h"
#include "system_input.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

using namespace openre::interop;

namespace openre::input
{
    enum InputDevice
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
    int input_get_keyboard_bits()
    {
        return gGameTable.input.keyboard;
    }

    static constexpr uint32_t input_keyboard_data[32] = {
        0x1000, 0x4000, 0x8000, 0x2000, 0x20, 0x44, 0x2, 0x10, 0x4, 0x1,    0x8,    0x80,   0x100,  0x800, 0,    0,
        0,      0,      0,      0,      0,    0,    0,   0,    0,   0x1000, 0x4000, 0x8000, 0x2000, 0x80,  0x80, 0x40,
    };

    static constexpr uint32_t input_gamepad_data[32] = {
        0x1000, 0x4000, 0x8000, 0x2000, 0, 0, 0, 0, 0x80, 0x44, 0x800, 0, 0x10, 0x100, 0, 0x8,
        0x2,    0,      0,      0,      0, 0, 0, 0, 0,    0,    0,     0, 0,    0,     0, 0,
    };

    // 0x0043BAC0
    int get_input_device_state(int rawState, InputDevice inputType)
    {
        auto inputState = 0;
        // Select the lookup table once, outside the hot loop.
        const auto* table = (inputType == INPUT_DEVICE_KEYBOARD) ? input_keyboard_data : input_gamepad_data;
        for (int i = 0; i < 32; i++)
        {
            if (rawState & (1 << i))
            {
                inputState |= table[i];
            }
        }
        return inputState;
    }

    // ---- command state engine ----

    // The command engine turns raw mouse/keyboard/gamepad input into a 20-bit
    // command state via the [input] INI bindings, OR-merges the three devices,
    // computes the rising edge once on the merged state, and fans the result
    // out to the 6 legacy outputs (g_key, key_trg, raw_state/raw_state_lo,
    // raw_edge, key_edge).

    namespace
    {
        // Token prefixes for the binding grammar. The parser and serializer
        // both use these so the spelling can't drift.
        constexpr const char* kTokenKeyboard = "kb:";
        constexpr const char* kTokenGamepad = "gp:";
        constexpr const char* kTokenMouse = "mu:";

        constexpr const char* kCommandIniNames[COMMAND_COUNT] = {
            "cancel", "accept", "up",   "down",   "left",      "right", "forward",  "backward", "turn_left",     "turn_right",
            "aim",    "run",    "fire", "reload", "inventory", "menu",  "interact", "map",      "change_target", "quick_turn",
        };
        static_assert(std::size(kCommandIniNames) == COMMAND_COUNT, "kCommandIniNames order must match Command enum");

        // Default binding token list per command, overridden by [input] INI.
        constexpr const char* kCommandDefaultTokens[COMMAND_COUNT] = {
            "kb:escape,gp:east",                           // cancel
            "kb:return,gp:start,gp:south",                 // accept
            "kb:w,kb:up,gp:dpad_up,gp:axis_left_y-",       // up
            "kb:s,kb:down,gp:dpad_down,gp:axis_left_y+",   // down
            "kb:a,kb:left,gp:dpad_left,gp:axis_left_x-",   // left
            "kb:d,kb:right,gp:dpad_right,gp:axis_left_x+", // right
            "kb:w,kb:up,gp:dpad_up,gp:axis_left_y-",       // forward
            "kb:s,kb:down,gp:dpad_down,gp:axis_left_y+",   // backward
            "kb:a,kb:left,gp:dpad_left,gp:axis_left_x-",   // turn_left
            "kb:d,kb:right,gp:dpad_right,gp:axis_left_x+", // turn_right
            "kb:o,gp:l_trigger,gp:l_shoulder,mu:right",    // aim
            "kb:shift,gp:west",                            // run
            "kb:space,gp:r_trigger,mu:left",               // fire
            "kb:r,gp:west",                                // reload
            "kb:tab,kb:i,gp:north",                        // inventory
            "kb:escape,kb:p,gp:start",                     // menu
            "kb:f,gp:south",                               // interact
            "kb:m,gp:mode",                                // map
            "gp:r_shoulder",                               // change_target
            "kb:q",                                        // quick_turn
        };
        static_assert(std::size(kCommandDefaultTokens) == COMMAND_COUNT, "kCommandDefaultTokens order must match Command enum");

        // Bumped when the binding schema or default mappings change.
        // load_bindings wipes the [input] section when it mismatches so stale
        // user configs reset to the new defaults.
        constexpr int32_t kBindingsVersion = 2;

        // Legacy output bits each command contributes. rawState feeds
        // raw_state/raw_state_lo (state), rawEdgeF8 feeds raw_edge and
        // rawEdgeFE feeds key_edge (edges). key_trg is derived from the
        // g_key edge (key_trg = newKey & ~oldKey), so most commands only need
        // gKey bits; a command that needs a key_trg bit without changing g_key
        // (change_target, quick_turn) puts it in
        // keyTrgOnly and pad_set ORs it into key_trg on the command's edge.
        // Rows 2-9 (up/forward, down/backward, left/turn_left,
        // right/turn_right) intentionally repeat: movement and turning share
        // the same g_key bits in the original binary.
        //
        // The original Pad_set wrote raw_state/raw_edge/key_edge unconditionally on
        // every path, so menu/UI commands (cancel, accept, fire, reload,
        // interact) carry identical bits in rawState and both edge outputs.
        // accept carries the interact bit 0x80 -- the same raw bit the
        // original return/enter key produced -- so the title screen still
        // advances on it (key_edge & 0x9FF) while it never opens the
        // in-game status screen (game_check_status_trigger only reads
        // key_edge & 0x800, which the inventory command emits). Consumers:
        // Title_main_wait tests key_edge & 0x9FF, Computer200 tests
        // key_edge & 0xF0 / & 0xF, and Config_main tests raw_state
        // & 0x80 (interact) to drive the speaker/volume toggles.
        struct CommandOutput
        {
            uint32_t gKey;
            uint32_t keyTrgOnly;
            uint32_t rawState;
            uint32_t rawEdgeF8;
            uint32_t rawEdgeFE;
        };

        constexpr CommandOutput kCommandOutput[COMMAND_COUNT] = {
            /* cancel     */ { 0x2000, 0, 0x2, 0x2, 0x2 },
            /* accept     */ { 0x1000, 0, 0x80, 0x80, 0x80 },
            /* up         */ { 0x1, 0, 0x1000, 0x1000, 0x1000 },
            /* down       */ { 0x4, 0, 0x4000, 0x4000, 0x4000 },
            /* left       */ { 0x8, 0, 0x8000, 0x8000, 0x8000 },
            /* right      */ { 0x2, 0, 0x2000, 0x2000, 0x2000 },
            /* forward    */ { 0x10, 0, 0x1000, 0x1000, 0x1000 },
            /* backward   */ { 0x20, 0, 0x4000, 0x4000, 0x4000 },
            /* turn_left  */ { 0x8, 0, 0x8000, 0x8000, 0x8000 },
            /* turn_right */ { 0x2, 0, 0x2000, 0x2000, 0x2000 },
            /* aim        */ { 0x100, 0, 0, 0, 0 },
            /* run        */ { 0x200, 0, 0, 0, 0 },
            // g_key = run (0x200) | weapon fire/change-target (0x20) | knife (0x10).
            // The 0x20 bit deliberately re-enters key_trg on the shot's edge
            // (same as the original fire key); raw keeps 0x10 only.
            /* fire       */ { 0x40, 0, 0x10, 0x10, 0x10 },
            /* reload     */ { 0, 0, 0, 0, 0 },
            // rawEdgeF8 0x800 lets the inventory key also confirm on the save
            // screen and skip doors, matching the original Z key.
            /* inventory  */ { 0, 0, 0, 0x800, 0x800 },
            /* menu       */ { 0, 0, 0, 0, 0x100 },
            /* interact   */ { 0x80, 0, 0x80, 0x80, 0x80 },
            /* map        */ { 0x4000, 0, 0, 0, 0 },
            /* change_target */ { 0, 0, 0x4, 0x4, 0x4 },
            /* quick_turn */ { 0x400, 0, 0, 0, 0 },
        };
        static_assert(std::size(kCommandOutput) == COMMAND_COUNT, "kCommandOutput order must match Command enum");

        // Joystick axis thresholds shared by the legacy raw layer (joy_get_pos_ex)
        // and the command engine (gamepad_source_active).
        constexpr uint32_t kStickAxisLow = 0x3000;  // beyond this = pushed one way
        constexpr uint32_t kStickAxisHigh = 0xC000; // beyond this = pushed the other way

        // POV direction bits, matching joy_get_pos_ex's POV bands exactly so
        // the D-pad behaves identically in the engine and the legacy raw layer.
        int gamepad_pov_dir(uint32_t pov)
        {
            if (pov == 0xFFFFFFFF)
                return 0;
            if (pov < 0x1187)
                return 0x10; // up
            if (pov < 0x230F)
                return 0x90; // up + right
            if (pov < 0x3496)
                return 0x80; // right
            if (pov < 0x461E)
                return 0xA0; // right + down
            if (pov < 0x57A5)
                return 0x20; // down
            if (pov < 0x692D)
                return 0x60; // down + left
            if (pov < 0x7AB4)
                return 0x40; // left
            if (pov < 0x8C3C)
                return 0x50; // left + up
            return 0;
        }

        bool gamepad_source_active(const system::input::GamepadState& state, GamepadSource source)
        {
            switch (source)
            {
            case GamepadSource::DpadUp: return (gamepad_pov_dir(state.pov) & 0x10) != 0;
            case GamepadSource::DpadDown: return (gamepad_pov_dir(state.pov) & 0x20) != 0;
            case GamepadSource::DpadLeft: return (gamepad_pov_dir(state.pov) & 0x40) != 0;
            case GamepadSource::DpadRight: return (gamepad_pov_dir(state.pov) & 0x80) != 0;
            case GamepadSource::AxisLeftYNeg: return state.yPos < kStickAxisLow;  // stick up
            case GamepadSource::AxisLeftYPos: return state.yPos > kStickAxisHigh; // stick down
            case GamepadSource::AxisLeftXNeg: return state.xPos < kStickAxisLow;  // stick left
            case GamepadSource::AxisLeftXPos: return state.xPos > kStickAxisHigh; // stick right
            case GamepadSource::South: return (state.buttons & 0x1) != 0;
            case GamepadSource::East: return (state.buttons & 0x2) != 0;
            case GamepadSource::North: return (state.buttons & 0x4) != 0;
            case GamepadSource::West: return (state.buttons & 0x8) != 0;
            case GamepadSource::LTrigger: return state.lTrigger;
            case GamepadSource::RTrigger: return state.rTrigger;
            case GamepadSource::LShoulder: return (state.buttons & 0x10) != 0;
            case GamepadSource::RShoulder: return (state.buttons & 0x20) != 0;
            case GamepadSource::Start: return (state.buttons & 0x100) != 0;
            case GamepadSource::Mode: return (state.buttons & 0x200) != 0;
            case GamepadSource::L3: return (state.buttons & 0x400) != 0;
            case GamepadSource::R3: return (state.buttons & 0x800) != 0;
            case GamepadSource::Count: break;
            }
            return false;
        }

        CommandBindings s_commandBindings;
        uint32_t s_commandState = 0;
        bool s_rebindLockout = false;

        // SDL instance ID of the pad behind each legacy joystick slot, so a
        // slot is re-initialised when a different pad takes its index after a
        // hotplug (stale caps / raw-state would otherwise linger).
        int32_t s_slotGamepadIds[system::input::kMaxGamepads] = {};

        struct KeyName
        {
            const char* name;
            uint8_t vk;
        };

        // Named keys for kb: tokens. Single letters/digits are handled
        // directly; everything else needs a name here.
        constexpr KeyName kKeyNames[] = {
            { "escape", 0x1B },   { "esc", 0x1B },  { "return", 0x0D }, { "enter", 0x0D },    { "space", 0x20 },
            { "shift", 0x10 },    { "ctrl", 0x11 }, { "alt", 0x12 },    { "tab", 0x09 },      { "backspace", 0x08 },
            { "capslock", 0x14 }, { "up", 0x26 },   { "down", 0x28 },   { "left", 0x25 },     { "right", 0x27 },
            { "home", 0x24 },     { "end", 0x23 },  { "pageup", 0x21 }, { "pagedown", 0x22 }, { "insert", 0x2D },
            { "delete", 0x2E },   { "f1", 0x70 },   { "f2", 0x71 },     { "f3", 0x72 },       { "f4", 0x73 },
            { "f5", 0x74 },       { "f6", 0x75 },   { "f7", 0x76 },     { "f8", 0x77 },       { "f9", 0x78 },
            { "f10", 0x79 },      { "f11", 0x7A },  { "f12", 0x7B },    { ";", 0xBA },        { "=", 0xBB },
            { ",", 0xBC },        { "-", 0xBD },    { ".", 0xBE },      { "/", 0xBF },        { "`", 0xC0 },
            { "[", 0xDB },        { "\\", 0xDC },   { "]", 0xDD },      { "'", 0xDE },
        };

        struct GamepadSourceName
        {
            const char* name;
            GamepadSource source;
        };

        constexpr GamepadSourceName kGamepadSourceNames[] = {
            { "dpad_up", GamepadSource::DpadUp },
            { "dpad_down", GamepadSource::DpadDown },
            { "dpad_left", GamepadSource::DpadLeft },
            { "dpad_right", GamepadSource::DpadRight },
            { "axis_left_y-", GamepadSource::AxisLeftYNeg },
            { "axis_left_y+", GamepadSource::AxisLeftYPos },
            { "axis_left_x-", GamepadSource::AxisLeftXNeg },
            { "axis_left_x+", GamepadSource::AxisLeftXPos },
            { "south", GamepadSource::South },
            { "east", GamepadSource::East },
            { "north", GamepadSource::North },
            { "west", GamepadSource::West },
            { "l_trigger", GamepadSource::LTrigger },
            { "r_trigger", GamepadSource::RTrigger },
            { "l_shoulder", GamepadSource::LShoulder },
            { "r_shoulder", GamepadSource::RShoulder },
            { "start", GamepadSource::Start },
            { "mode", GamepadSource::Mode },
            { "l3", GamepadSource::L3 },
            { "r3", GamepadSource::R3 },
        };
        static_assert(
            std::size(kGamepadSourceNames) == static_cast<size_t>(GamepadSource::Count),
            "kGamepadSourceNames must cover every GamepadSource");

        // Mouse button tokens, in Binding key order (0=left, 1=middle, 2=right).
        constexpr const char* kMouseButtonNames[] = { "left", "middle", "right" };
    }

    static int key_name_to_vk(const std::string& name)
    {
        if (name.size() == 1)
        {
            auto c = name[0];
            if (c >= '0' && c <= '9')
                return 0x30 + (c - '0');
            if (c >= 'a' && c <= 'z')
                return 0x41 + (c - 'a');
            if (c >= 'A' && c <= 'Z')
                return 0x41 + (c - 'A');
        }
        for (const auto& kn : kKeyNames)
        {
            if (name == kn.name)
                return kn.vk;
        }
        return -1;
    }

    static std::string key_name_from_vk(uint8_t vk)
    {
        for (const auto& kn : kKeyNames)
        {
            if (kn.vk == vk)
                return kn.name;
        }
        if (vk >= 0x30 && vk <= 0x39)
            return std::string(1, static_cast<char>('0' + (vk - 0x30)));
        if (vk >= 0x41 && vk <= 0x5A)
            return std::string(1, static_cast<char>('a' + (vk - 0x41)));
        char buf[16];
        sprintf(buf, "0x%02X", vk);
        return buf;
    }

    // Parses one binding token ("kb:w", "gp:dpad_up", "mu:right", ...).
    static bool parse_binding_token(const std::string& token, Binding& out)
    {
        if (token.rfind(kTokenKeyboard, 0) == 0)
        {
            auto name = token.substr(strlen(kTokenKeyboard));
            int vk = -1;
            if (name.rfind("0x", 0) == 0)
            {
                vk = static_cast<int>(std::strtoul(name.c_str() + 2, nullptr, 16));
            }
            else
            {
                vk = key_name_to_vk(name);
            }
            if (vk <= 0 || vk > 0xFF)
                return false;
            out.device = BindingDevice::Keyboard;
            out.key = static_cast<uint8_t>(vk);
            return true;
        }
        if (token.rfind(kTokenGamepad, 0) == 0)
        {
            auto name = token.substr(strlen(kTokenGamepad));
            for (const auto& gs : kGamepadSourceNames)
            {
                if (name == gs.name)
                {
                    out.device = BindingDevice::Gamepad;
                    out.key = static_cast<uint8_t>(gs.source);
                    return true;
                }
            }
            return false;
        }
        if (token.rfind(kTokenMouse, 0) == 0)
        {
            auto name = token.substr(strlen(kTokenMouse));
            int btn = -1;
            for (int i = 0; i < 3; i++)
            {
                if (name == kMouseButtonNames[i])
                {
                    btn = i;
                    break;
                }
            }
            if (btn < 0)
                return false;
            out.device = BindingDevice::Mouse;
            out.key = static_cast<uint8_t>(btn);
            return true;
        }
        return false;
    }

    static std::string binding_to_string(const Binding& b)
    {
        switch (b.device)
        {
        case BindingDevice::Keyboard: return std::string(kTokenKeyboard) + key_name_from_vk(b.key);
        case BindingDevice::Gamepad:
            for (const auto& gs : kGamepadSourceNames)
            {
                if (static_cast<uint8_t>(gs.source) == b.key)
                    return std::string(kTokenGamepad) + gs.name;
            }
            return "";
        case BindingDevice::Mouse: return b.key < 3 ? std::string(kTokenMouse) + kMouseButtonNames[b.key] : "";
        }
        return "";
    }

    void load_bindings()
    {
        // Reset the [input] section when the binding schema version changes so
        // stale user configs (with old defaults or a missing change_target key)
        // fall back to the new defaults. Only save when there was something to
        // clear so a fresh install does not write a config file at boot.
        if (system::config::get<int32_t>("input", "version", 0) != kBindingsVersion)
        {
            bool removed = system::config::remove_group("input");
            system::config::set("input", "version", kBindingsVersion);
            if (removed)
            {
                system::config::save();
            }
        }

        for (int cmd = 0; cmd < COMMAND_COUNT; cmd++)
        {
            s_commandBindings.bindings[cmd].clear();
            auto value = system::config::get<std::string>("input", kCommandIniNames[cmd], kCommandDefaultTokens[cmd]);
            size_t pos = 0;
            while (pos <= value.size())
            {
                auto comma = value.find(',', pos);
                auto token = value.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                // Trim surrounding whitespace.
                auto start = token.find_first_not_of(" \t");
                auto end = token.find_last_not_of(" \t");
                if (start != std::string::npos)
                {
                    token = token.substr(start, end - start + 1);
                }
                Binding b{};
                if (!token.empty() && parse_binding_token(token, b))
                {
                    s_commandBindings.bindings[cmd].push_back(b);
                }
                else if (!token.empty())
                {
                    logging::logWarning("[input] ignoring invalid binding '{}' for command {}", token, kCommandIniNames[cmd]);
                }
                if (comma == std::string::npos)
                    break;
                pos = comma + 1;
            }
        }
    }

    void save_bindings()
    {
        system::config::set("input", "version", kBindingsVersion);
        for (int cmd = 0; cmd < COMMAND_COUNT; cmd++)
        {
            std::string value;
            for (size_t i = 0; i < s_commandBindings.bindings[cmd].size(); i++)
            {
                if (i > 0)
                    value += ",";
                value += binding_to_string(s_commandBindings.bindings[cmd][i]);
            }
            system::config::set("input", kCommandIniNames[cmd], value);
        }
        system::config::save();
    }

    // ---- per-device transforms ----

    // Sets a command bit for each command whose bindings match on the given
    // device. `is_active` tests one raw binding source (a key bit, gamepad
    // button/axis, or mouse button).
    template<typename IsActive>
    static uint32_t transform_device(const CommandBindings& bindings, BindingDevice device, IsActive is_active)
    {
        uint32_t state = 0;
        for (int cmd = 0; cmd < COMMAND_COUNT; cmd++)
        {
            for (const auto& b : bindings.bindings[cmd])
            {
                if (b.device == device && is_active(b))
                {
                    state |= 1u << cmd;
                    break;
                }
            }
        }
        return state;
    }

    static uint32_t transform_keyboard(const CommandBindings& bindings)
    {
        uint8_t key_state[256];
        system::input::get_keyboard_state(key_state);
        return transform_device(
            bindings, BindingDevice::Keyboard, [&](const Binding& b) { return (key_state[b.key] & 0x80) != 0; });
    }

    static uint32_t transform_gamepad(const CommandBindings& bindings)
    {
        // Bindings intentionally read only the first gamepad; the legacy raw
        // layer (joy_get_pos_ex) still scans all pads for original consumers.
        system::input::GamepadState gamepad{};
        if (!system::input::poll_gamepad(0, gamepad))
        {
            return 0;
        }
        return transform_device(bindings, BindingDevice::Gamepad, [&](const Binding& b) {
            return gamepad_source_active(gamepad, static_cast<GamepadSource>(b.key));
        });
    }

    static uint32_t transform_mouse(const CommandBindings& bindings)
    {
        auto buttons = system::input::get_mouse_buttons();
        return transform_device(
            bindings, BindingDevice::Mouse, [&](const Binding& b) { return (buttons & (1u << b.key)) != 0; });
    }

    // Rising edge of `state` relative to the previous poll: bits set now that
    // were clear last time. Equivalent to state & (state ^ prev).
    static uint32_t rising_edge(uint32_t state, uint32_t prev)
    {
        return state & ~prev;
    }

    // ---- key polling helper state ----

    // Previous keyboard state snapshot used for menu key edge detection.
    // Maps to byte_663198 in the original binary.
    static uint8_t menu_key_state[256] = {};

    static uint8_t config_key_state[256] = {};

    // Menu navigation keys, in priority order. Maps to byte_522074.
    static const uint8_t menu_vk_codes[] = {
        0x21, 0x22, 0x23, 0x24, 0x2E, // VK_PRIOR, VK_NEXT, VK_END, VK_HOME, VK_DELETE
    };

    static const uint8_t config_vk_codes[] = {
        0x08, 0x09, 0x10, 0x11, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x41, 0x42, 0x43,
        0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54,
        0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A,
        0x6B, 0x6D, 0x6E, 0x6F, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xDB, 0xDC, 0xDD, 0xDE, 0xE2,
    };

    // Edge test for a single key: returns true if the key is down (bit 0x80)
    // AND bit 0x80 differs from the previous state. Note this is a deliberate
    // deviation from the original, which ANDs the XOR with (cur < 0) (bit 0);
    // with SDL's keyboard state (which only ever sets bit 0x80) that test could
    // never fire, so menu scrolling would be dead.
    static bool key_pressed_edge(uint8_t cur, uint8_t old)
    {
        return ((cur ^ old) & (cur & 0x80)) != 0;
    }

    // Polls the keyboard and returns the first VK in `codes` that transitioned
    // to down since the last poll, or 0 if none. `prev` holds the previous
    // snapshot and is updated with the current state on every call, so edge
    // detection works across consecutive polls.
    static int16_t first_pressed_key(const uint8_t* codes, size_t count, uint8_t (&prev)[256])
    {
        uint8_t key_state[256];
        system::input::get_keyboard_state(key_state);

        for (size_t i = 0; i < count; i++)
        {
            auto vk = codes[i];
            if (key_pressed_edge(key_state[vk], prev[vk]))
            {
                memcpy(prev, key_state, sizeof(prev));
                return vk;
            }
        }
        memcpy(prev, key_state, sizeof(prev));
        return 0;
    }

    // 0x00432670
    // Returns the first menu navigation key that was just pressed (transitioned
    // to down since the last poll), or 0 if none.
    int16_t get_menu_key()
    {
        return first_pressed_key(menu_vk_codes, std::size(menu_vk_codes), menu_key_state);
    }

    // 0x004354D0
    static int16_t get_config_key_state()
    {
        // The config screen is polling for a key press; suppress the command
        // engine so the captured key does not also fire a game command.
        s_rebindLockout = true;

        return first_pressed_key(config_vk_codes, std::size(config_vk_codes), config_key_state);
    }

    // Fills the JOYINFOEX header (dwSize / dwFlags) expected by joyGetPosEx
    // consumers. Shared by the slot initialisation and the per-poll reset so
    // the two sites can't drift.
    static void init_joystick_info_header(uint8_t* joystick)
    {
        memset(joystick, 0, 0x34);
        *reinterpret_cast<uint32_t*>(joystick) = 52;       // JOYINFOEX::dwSize
        *reinterpret_cast<uint32_t*>(joystick + 4) = 0xFF; // JOYINFOEX::dwFlags
    }

    // 0x004100F0 - Polls all joysticks and processes POV/buttons
    int joy_get_pos_ex(Input* self)
    {
        // Update keyboard trg/old tracking
        auto keyboardBits = input_get_keyboard_bits();
        auto keyboardChanges = keyboardBits ^ self->keyboard_raw_state;
        self->keyboard_old = self->keyboard_raw_state;
        self->keyboard_raw_state = keyboardBits;
        self->keyboard_trg = keyboardBits & keyboardChanges;
        self->keyboard_ready = 1;

        // Per-joystick data starts at offset 0x208 with stride 0x1D8
        auto joystick_base = reinterpret_cast<uint8_t*>(self) + 0x208;
        int result = 0;
        for (int joy = 1; joy < system::input::kMaxGamepads; joy++)
        {
            result = 0;
            auto joystick = reinterpret_cast<uint32_t*>(joystick_base);
            auto gamepadState = reinterpret_cast<uint32_t*>(joystick_base - 12); // self + 0x1FC

            if (joystick[0x1C8 / 4] != 0) // init flag at offset 0x1C8
            {
                init_joystick_info_header(reinterpret_cast<uint8_t*>(joystick));

                system::input::GamepadState gamepad;
                if (system::input::poll_gamepad(joy - 1, gamepad))
                {
                    // Store raw state in the JOYINFOEX layout so any original
                    // code reading this buffer keeps working.
                    joystick[2] = gamepad.xPos;    // dwXpos
                    joystick[3] = gamepad.yPos;    // dwYpos
                    joystick[8] = gamepad.buttons; // dwButtons
                    joystick[10] = gamepad.pov;    // dwPOV

                    // Process stick axes into directional bits
                    auto xPos = joystick[2]; // dwXpos
                    auto yPos = joystick[3]; // dwYpos
                    int dir = 0;
                    if (xPos > kStickAxisHigh)
                        dir |= 8; // right (dir bit 3 -> input_gamepad_data[3] = 0x2000)
                    if (xPos < kStickAxisLow)
                        dir |= 4; // left (dir bit 2 -> input_gamepad_data[2] = 0x8000)
                    if (yPos > kStickAxisHigh)
                        dir |= 2; // down
                    if (yPos < kStickAxisLow)
                        dir |= 1; // up

                    // Process POV hat into directional bits
                    dir |= gamepad_pov_dir(joystick[10]); // dwPOV

                    result = dir | (joystick[8] << 8); // buttons (dwButtons)

                    // Update gamepad state trg/old tracking
                    auto gamepadTrigger = rising_edge(result, gamepadState[0]); // gamepad_trg
                    gamepadState[2] = gamepadState[0];                          // gamepad_old = old gamepad_raw_state
                    gamepadState[0] = result;                                   // gamepad_raw_state = result
                    gamepadState[1] = gamepadTrigger;
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
        auto rawInput = gGameTable.raw_input_state;

        joy_get_pos_ex(reinterpret_cast<Input*>(gGameTable.input.mapping));

        // joy_get_pos_ex always sets keyboard_ready = 1 (matching the original
        // joyGetPosEx), so this branch is effectively unconditional.
        rawInput = get_input_device_state(gGameTable.input.keyboard_raw_state, INPUT_DEVICE_KEYBOARD);

        gGameTable.keyboard_state = gGameTable.input.keyboard_raw_state;
        gGameTable.raw_input_state = rawInput;
        gGameTable.gamepad_state = 0;
        if (gGameTable.input.joystick_count >= 2 && gGameTable.input.gamepad_present != 0)
        {
            auto gamepadInput = get_input_device_state(gGameTable.input.gamepad_raw_state, INPUT_DEVICE_GAMEPAD);
            rawInput |= gamepadInput;
            gGameTable.gamepad_state = gamepadInput;
            gGameTable.raw_input_state = rawInput;
        }

        return rawInput;
    }

    // Fills the JOYCAPS-compatible buffer + JOYINFOEX header for one legacy
    // joystick slot and marks it initialized. `slot` is 1-based (slot 1 is the
    // pad at SDL index 0, at self+0x208). Returns true if a gamepad is present
    // at that slot.
    static bool init_joystick_slot(Input* self, uint32_t slot)
    {
        auto joystick = reinterpret_cast<uint8_t*>(self) + 0x208 + (slot - 1) * 0x1D8;
        *reinterpret_cast<uint32_t*>(joystick + 0x1C8) = 1; // init flag

        // Fill a JOYCAPS-compatible buffer from SDL gamepad info so any
        // original code reading the caps area keeps working.
        auto caps = joystick + 0x34;
        memset(caps, 0, 0x194);
        strncpy(reinterpret_cast<char*>(caps + 4), system::input::get_gamepad_name(slot - 1), 0x20); // szPname
        *reinterpret_cast<uint16_t*>(caps + 48)
            = static_cast<uint16_t>(system::input::get_gamepad_button_count(slot - 1)); // wNumButtons
        *reinterpret_cast<uint16_t*>(caps + 70)
            = static_cast<uint16_t>(system::input::get_gamepad_axis_count(slot - 1)); // wNumAxes

        init_joystick_info_header(joystick);

        system::input::GamepadState gamepad;
        if (!system::input::poll_gamepad(slot - 1, gamepad))
        {
            *reinterpret_cast<uint32_t*>(joystick + 0x1C8) = 0; // mark as uninitialized
            return false;
        }
        return true;
    }

    // Re-syncs the legacy joystick slots (init flags, caps) and the
    // joystick_count/gamepad_present bookkeeping with the current SDL gamepad
    // set, so pads plugged in while the game runs are picked up by
    // joy_get_pos_ex and the config screen.
    static void sync_joystick_slots(Input* self)
    {
        auto joyCount = static_cast<uint32_t>(system::input::get_gamepad_count() + 1);
        self->joystick_count = joyCount;
        // Gate for the legacy gamepad raw merge (sub_43BB00 / sub_43BB80).
        // This doubles as the init flag of joystick slot 1 (offset 0x3D0), so
        // it must be set after the joystick area is cleared.
        self->gamepad_present = (joyCount > 1) ? 1 : 0;

        auto joystick = reinterpret_cast<uint8_t*>(self) + 0x208;
        for (auto slot = 1u; slot < system::input::kMaxGamepads; slot++)
        {
            auto* initFlag = reinterpret_cast<uint32_t*>(joystick + 0x1C8);
            if (slot < joyCount)
            {
                auto id = system::input::get_gamepad_id(slot - 1);
                if (*initFlag == 0 || s_slotGamepadIds[slot] != id)
                {
                    // New or replaced pad: reset the per-slot raw-state
                    // tracking (gamepadState, 12 bytes before the slot) so the
                    // first poll after the change does not produce a spurious
                    // edge, then (re)initialise caps.
                    memset(joystick - 12, 0, 12);
                    if (init_joystick_slot(self, slot))
                    {
                        s_slotGamepadIds[slot] = id;
                    }
                }
            }
            else
            {
                *initFlag = 0;
                s_slotGamepadIds[slot] = 0;
            }
            joystick += 0x1D8;
        }
    }

    // Re-enumerates SDL gamepads (hotplug support) and re-syncs the legacy
    // joystick slots. Called every frame from pad_set, before the raw layer
    // polls. sync_joystick_slots is idempotent, so running it unconditionally
    // keeps the mirror correct without tracking device identity here.
    static void update_gamepads()
    {
        system::input::refresh_gamepads();
        sync_joystick_slots(&gGameTable.input);
    }

    // 0x004102E0
    Input* input_init(Input* self)
    {
        if (!system::input::init())
        {
            marni::out(
                "\x83\x8F\x81\x5B\x83\x4E\x82\xAA\x91\xAB\x82\xE8\x82\xDC\x82\xB9\x82\xF1\x82\xC5\x82\xB5\x82\xBD",
                "DirectInput::WM_Create");
            return self;
        }

        if (system::input::get_gamepad_count() + 1 >= system::input::kMaxGamepads)
        {
            marni::out(
                "\x83\x8F\x81\x5B\x83\x4E\x82\xAA\x91\xAB\x82\xE8\x82\xDC\x82\xB9\x82\xF1\x82\xC5\x82\xB5\x82\xBD",
                "DirectInput::WM_Create");
            return self;
        }

        memset(reinterpret_cast<uint8_t*>(self) + 0x24, 0, 0x3B00);
        sync_joystick_slots(self);

        // Report the connected pads (the slots initialized by the sync).
        auto joystick = reinterpret_cast<uint8_t*>(self) + 0x208;
        for (auto joy = 1u; joy < self->joystick_count; joy++)
        {
            if (*reinterpret_cast<uint32_t*>(joystick + 0x1C8) != 0)
            {
                char msg[1024];
                sprintf(
                    msg,
                    "\x83\x57\x83\x87\x83\x43\x83\x58\x83\x65\x83\x42\x83\x62\x83\x4E"
                    "\x82\x68\x82\x63%d\x94\xD4\x82\xCD\x81\x41%s\x83\x7B\x83\x5E\x83\x93"
                    "\x82\xCC\x90\x94 %d \x8E\xB2\x82\xCC\x90\x94%d ",
                    joy,
                    system::input::get_gamepad_name(joy - 1),
                    system::input::get_gamepad_button_count(joy - 1),
                    system::input::get_gamepad_axis_count(joy - 1));
                marni::out(msg, "MarniSystem DirectInput Class");
            }
            joystick += 0x1D8;
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
        // Refresh the SDL gamepad set so pads plugged in while the game runs
        // are picked up before this frame's polling below.
        update_gamepads();

        // Save previous raw input
        gGameTable.raw_edge = gGameTable.raw_state;

        // Copy Vk_press bit 4 (0x10) to bit 5 (0x20), then clear bit 4
        auto vk = gGameTable.vk_press;
        vk = vk & 0xDF;
        if (vk & 0x10)
            vk |= 0x20;
        vk = vk & 0xEF;
        gGameTable.vk_press = vk;

        // Legacy raw layer: updates keyboard_state/gamepad_state/raw_input_state (used by the
        // config screen and other legacy consumers). In live play the command
        // engine below produces the actual outputs.
        int rawInput = sub_43BB00();
        int prevInput = gGameTable.raw_edge;
        gGameTable.raw_state = rawInput;

        // Demo playback: if demo flag is set, replay recorded input
        if (check_flag(FlagGroup::System, FG_SYSTEM_DEMO))
        {
            auto inputChanged = (rawInput & (rawInput ^ prevInput) & 0xFFF) != 0;
            if (inputChanged || gGameTable.demo_frame >= gGameTable.pdemo.frames || (gGameTable.vk_press & 0x40))
            {
                gGameTable.vk_press &= ~0x40;
                if (check_flag(FlagGroup::System, FG_SYSTEM_1))
                {
                    if (gGameTable.demo_frame < gGameTable.pdemo.frames)
                        gGameTable.demo_ended = 1;
                    gGameTable.demo_frame = gGameTable.pdemo.frames + 1;
                    gGameTable.raw_state = 0;
                    rawInput = 0;
                }
                else
                {
                    set_flag(FlagGroup::System, FG_SYSTEM_19, true);
                }
            }
            else if (check_flag(FlagGroup::System, FG_SYSTEM_1))
            {
                rawInput = gGameTable.pdemo.input[gGameTable.demo_frame];
                gGameTable.demo_frame++;
                gGameTable.raw_state = rawInput;
            }
        }

        int oldKey = gGameTable.g_key;
        int newKey = 0;
        gGameTable.key_copy = gGameTable.g_key;
        gGameTable.g_key = 0;

        uint32_t rawTrigger = 0;  // edge value for raw_edge
        uint32_t feEdge = 0;      // edge value for key_edge (low word)
        uint32_t keyTrgExtra = 0; // key_trg bits that are not part of g_key (change_target, quick_turn)

        if (check_flag(FlagGroup::System, FG_SYSTEM_DEMO))
        {
            // Demo replay: map raw input bits to logical key bits via the
            // legacy table (input_mapping_idx selects the keyboard/gamepad row).
            auto* mapping = &gGameTable.input_mapping_table[16 * gGameTable.input_mapping_idx];
            for (int i = 0; i < 16; i++)
            {
                if (mapping[i] & rawInput)
                    newKey |= (1 << i);
            }
            gGameTable.g_key = newKey;
            rawTrigger = rising_edge(rawInput, prevInput);
            feEdge = rawTrigger & 0xFFFF;
        }
        else if (s_rebindLockout)
        {
            // The config screen is capturing a key. Suppress only the game
            // command outputs (g_key); the original Pad_set wrote the raw
            // layer (raw_state/raw_edge/key_edge) unconditionally on every path, and
            // the capture state (Config_main case 15) navigates via
            // em_damage_table[0] (raw_edge) and raw_state, so those
            // must keep flowing.
            s_rebindLockout = false;
            gGameTable.g_key = 0;
            rawTrigger = rising_edge(rawInput, prevInput);
            feEdge = rawTrigger & 0xFFFF;
            gGameTable.raw_state = rawInput;
        }
        else
        {
            // Command engine: merge per-device command states (mouse, keyboard,
            // gamepad), compute the rising edge once on the merged state, then
            // fan out to the legacy outputs.
            gGameTable.raw_state = 0;
            uint32_t cmdState = transform_keyboard(s_commandBindings);
            cmdState |= transform_gamepad(s_commandBindings);
            cmdState |= transform_mouse(s_commandBindings);
            uint32_t cmdEdge = rising_edge(cmdState, s_commandState);
            s_commandState = cmdState;

            for (int cmd = 0; cmd < COMMAND_COUNT; cmd++)
            {
                uint32_t bit = 1u << cmd;
                if (cmdState & bit)
                {
                    newKey |= kCommandOutput[cmd].gKey;
                    gGameTable.raw_state |= kCommandOutput[cmd].rawState;
                }
                if (cmdEdge & bit)
                {
                    rawTrigger |= kCommandOutput[cmd].rawEdgeF8;
                    feEdge |= kCommandOutput[cmd].rawEdgeFE;
                    keyTrgExtra |= kCommandOutput[cmd].keyTrgOnly;
                }
            }
            gGameTable.g_key = newKey;
        }

        // Stop flag handling: if input is blocked, only allow directional keys
        if (gGameTable.fg_stop & 0x1000000)
        {
            newKey &= 0x3C00;
            keyTrgExtra = 0;
            gGameTable.fg_stop_latch = gGameTable.fg_stop;
            gGameTable.g_key = newKey;
        }
        else if (gGameTable.fg_stop_latch & 0x1000000)
        {
            oldKey = newKey;
            keyTrgExtra = 0;
            gGameTable.fg_stop_latch = 0;
            gGameTable.key_copy = newKey;
        }

        // Calculate trigger (edge detection) values. key_trg is the rising
        // edge of g_key; commands with only a key_trg bit (e.g. menu dirs)
        // also carry that bit in gKey so the edge is produced here.
        // keyTrgExtra supplies bits that must NOT appear in g_key (e.g.
        // change_target and quick_turn).
        uint32_t keyTrigger = rising_edge(newKey, oldKey) | keyTrgExtra;

        gGameTable.key_edge = (gGameTable.key_edge & 0xFFFF0000) | (feEdge & 0xFFFF);
        gGameTable.raw_state_lo = (uint16_t)gGameTable.raw_state;
        gGameTable.key_trg = keyTrigger;
        gGameTable.raw_edge = rawTrigger;

        // Key repeat handling for the mask bits in key_repeat_mask
        if (gGameTable.key_repeat_mask & rawTrigger)
        {
            gGameTable.key_repeat_counter = gGameTable.key_repeat_timing & 0xFF;
            set_flag(FlagGroup::System, FG_SYSTEM_0, true);
        }
        else if (gGameTable.key_repeat_counter)
        {
            if (gGameTable.key_repeat_mask & gGameTable.raw_state)
                gGameTable.key_repeat_counter--;
            set_flag(FlagGroup::System, FG_SYSTEM_0, false);
        }
        else
        {
            gGameTable.key_repeat_counter = (gGameTable.key_repeat_timing >> 8) & 0xFF;
            set_flag(FlagGroup::System, FG_SYSTEM_0, true);
        }
    }

    // 0x0043B950
    // Called by the config screen when the control layout is confirmed (and by
    // the original boot path, which the reimpl does not use). Copies the legacy
    // keyboard mapping (Data) into the input class like the original, then
    // translates the rebound keyboard slots into [input] command bindings.
    static void init_input_hook()
    {
        const auto* data = reinterpret_cast<const uint8_t*>(0x524DE8);

        // Match the original: copy Data into input_class.mapping (the trailing
        // byte lands in byte_67CA4F via the 32-byte copy).
        for (int i = 0; i < 32; i++)
        {
            gGameTable.input.mapping[i] = data[i];
        }

        // Legacy keyboard slots that map to commands. On confirm the user's
        // rebound VK for each slot becomes that command's keyboard binding.
        //
        // The slot -> command mapping is derived from the state bits each slot
        // contributes (input_keyboard_data, 0x524CE8): slots 0-3 are the menu
        // directional bits 0x1000/0x4000/0x8000/0x2000 (up/down/left/right),
        // slot 5 = 0x44 (run), slot 7 = 0x10 (map -> g_key 0x4000),
        // slot 10 = 0x8 (aim -> g_key 0x100), slot 11 = 0x80 (interact),
        // slot 12 = 0x100 (aim), slot 13 = 0x800 (inventory), and the arrow
        // slots 25-28 repeat the directional bits.
        struct SlotCommand
        {
            int slot;
            Command command;
        };
        static constexpr SlotCommand kSlotCommands[] = {
            { 0, COMMAND_UP },    { 1, COMMAND_DOWN },       { 2, COMMAND_LEFT }, { 3, COMMAND_RIGHT },
            { 5, COMMAND_RUN },   { 7, COMMAND_MAP },        { 10, COMMAND_AIM }, { 11, COMMAND_INTERACT },
            { 12, COMMAND_AIM },  { 13, COMMAND_INVENTORY }, { 25, COMMAND_UP },  { 26, COMMAND_DOWN },
            { 27, COMMAND_LEFT }, { 28, COMMAND_RIGHT },
        };
        for (const auto& sc : kSlotCommands)
        {
            if (data[sc.slot] == 0)
                continue;
            auto& binds = s_commandBindings.bindings[sc.command];
            binds.erase(
                std::remove_if(
                    binds.begin(), binds.end(), [](const Binding& b) { return b.device == BindingDevice::Keyboard; }),
                binds.end());
            binds.push_back(Binding{ BindingDevice::Keyboard, data[sc.slot] });
        }
        save_bindings();
    }

    void input_init_hooks()
    {
        writeJmp(0x00410450, &input_wmkeyup);
        writeJmp(0x00410410, &input_wmkeydown);
        writeJmp(0x00410400, &input_get_keyboard_bits);
        writeJmp(0x004100F0, &joy_get_pos_ex);
        writeJmp(0x004102E0, &input_init);
        writeJmp(0x00432670, &get_menu_key);
        writeJmp(0x004354D0, &get_config_key_state);
        writeJmp(0x0043BB00, &sub_43BB00);
        writeJmp(0x0043B950, &init_input_hook);
        writeJmp(0x004D0F30, &pad_set);
    }
};
