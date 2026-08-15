#pragma once

// ============================================================================
// vk_codes.h - Win32 virtual-key codes (from winuser.h) shared by the SDL
// input/window modules and the game code so the values are defined once.
//
// On _WIN32 the real <windows.h> macros (VK_*) take precedence, so each
// constant is guarded with #ifndef. Everywhere else plain constexpr values
// with the legacy Win32 ABI are defined.
//
// The values must stay stable because the game's input mapping is keyed on VK
// codes.
// ============================================================================

#ifndef VK_BACK
constexpr int VK_BACK = 0x08;
#endif
#ifndef VK_TAB
constexpr int VK_TAB = 0x09;
#endif
#ifndef VK_RETURN
constexpr int VK_RETURN = 0x0D;
#endif
#ifndef VK_SHIFT
constexpr int VK_SHIFT = 0x10;
#endif
#ifndef VK_CONTROL
constexpr int VK_CONTROL = 0x11;
#endif
#ifndef VK_MENU
constexpr int VK_MENU = 0x12;
#endif
#ifndef VK_CAPITAL
constexpr int VK_CAPITAL = 0x14;
#endif
#ifndef VK_ESCAPE
constexpr int VK_ESCAPE = 0x1B;
#endif
#ifndef VK_SPACE
constexpr int VK_SPACE = 0x20;
#endif
#ifndef VK_PRIOR
constexpr int VK_PRIOR = 0x21; // Page Up
#endif
#ifndef VK_NEXT
constexpr int VK_NEXT = 0x22; // Page Down
#endif
#ifndef VK_END
constexpr int VK_END = 0x23;
#endif
#ifndef VK_HOME
constexpr int VK_HOME = 0x24;
#endif
#ifndef VK_LEFT
constexpr int VK_LEFT = 0x25;
#endif
#ifndef VK_UP
constexpr int VK_UP = 0x26;
#endif
#ifndef VK_RIGHT
constexpr int VK_RIGHT = 0x27;
#endif
#ifndef VK_DOWN
constexpr int VK_DOWN = 0x28;
#endif
#ifndef VK_SNAPSHOT
constexpr int VK_SNAPSHOT = 0x2C; // Print Screen
#endif
#ifndef VK_INSERT
constexpr int VK_INSERT = 0x2D;
#endif
#ifndef VK_DELETE
constexpr int VK_DELETE = 0x2E;
#endif
#ifndef VK_NUMPAD0
constexpr int VK_NUMPAD0 = 0x60;
#endif
#ifndef VK_NUMPAD1
constexpr int VK_NUMPAD1 = 0x61;
#endif
#ifndef VK_MULTIPLY
constexpr int VK_MULTIPLY = 0x6A;
#endif
#ifndef VK_ADD
constexpr int VK_ADD = 0x6B;
#endif
#ifndef VK_SUBTRACT
constexpr int VK_SUBTRACT = 0x6D;
#endif
#ifndef VK_DECIMAL
constexpr int VK_DECIMAL = 0x6E;
#endif
#ifndef VK_DIVIDE
constexpr int VK_DIVIDE = 0x6F;
#endif
#ifndef VK_F1
constexpr int VK_F1 = 0x70;
#endif
#ifndef VK_F2
constexpr int VK_F2 = 0x71;
#endif
#ifndef VK_F3
constexpr int VK_F3 = 0x72;
#endif
#ifndef VK_F4
constexpr int VK_F4 = 0x73;
#endif
#ifndef VK_F5
constexpr int VK_F5 = 0x74;
#endif
#ifndef VK_F6
constexpr int VK_F6 = 0x75;
#endif
#ifndef VK_F7
constexpr int VK_F7 = 0x76;
#endif
#ifndef VK_F8
constexpr int VK_F8 = 0x77;
#endif
#ifndef VK_F9
constexpr int VK_F9 = 0x78;
#endif
#ifndef VK_F10
constexpr int VK_F10 = 0x79;
#endif
#ifndef VK_F11
constexpr int VK_F11 = 0x7A;
#endif
#ifndef VK_F12
constexpr int VK_F12 = 0x7B;
#endif
#ifndef VK_OEM_1
constexpr int VK_OEM_1 = 0xBA; // ;:
#endif
#ifndef VK_OEM_PLUS
constexpr int VK_OEM_PLUS = 0xBB; // =+
#endif
#ifndef VK_OEM_COMMA
constexpr int VK_OEM_COMMA = 0xBC;
#endif
#ifndef VK_OEM_MINUS
constexpr int VK_OEM_MINUS = 0xBD;
#endif
#ifndef VK_OEM_PERIOD
constexpr int VK_OEM_PERIOD = 0xBE;
#endif
#ifndef VK_OEM_2
constexpr int VK_OEM_2 = 0xBF; // /?
#endif
#ifndef VK_OEM_3
constexpr int VK_OEM_3 = 0xC0; // `~
#endif
#ifndef VK_OEM_4
constexpr int VK_OEM_4 = 0xDB; // [{
#endif
#ifndef VK_OEM_5
constexpr int VK_OEM_5 = 0xDC; // \|
#endif
#ifndef VK_OEM_6
constexpr int VK_OEM_6 = 0xDD; // ]}
#endif
#ifndef VK_OEM_7
constexpr int VK_OEM_7 = 0xDE; // '"
#endif
#ifndef VK_OEM_102
constexpr int VK_OEM_102 = 0xE2; // <>| (non-US backslash)
#endif
