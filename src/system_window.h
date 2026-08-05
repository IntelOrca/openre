#pragma once

#include <cstdint>

namespace openre::system::window
{
    // Generic window events, decoupled from SDL so callers never need SDL headers.
    enum class EventType
    {
        None,           // unhandled / ignored event
        Quit,           // app quit requested
        KeyDown,        // vk = Win32 VK code
        KeyUp,          // vk = Win32 VK code
        FocusGained,    // -> wnd_activate()
        FocusLost,      // -> wnd_deactivate()
        CloseRequested, // -> marni::kill()
        Moved,          // data1/data2 = x/y
        Resized,        // data1/data2 = w/h
    };

    struct Event
    {
        EventType type = EventType::None;
        int vk = 0;          // Win32 VK code for KeyDown/KeyUp
        bool repeat = false; // key auto-repeat (was lParam & 0x40000000)
        int data1 = 0;
        int data2 = 0;
    };

    // Initialises SDL video and creates the game window. Returns false on failure.
    bool init();
    // Destroys the window and shuts SDL down.
    void destroy();

    // Underlying Win32 window handle (never SDL_Window*).
    void* get_hwnd();
    // Win32 module instance, used by DialogBoxParamA (SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER).
    void* get_hinstance();

    // Polls the next window event; returns false when the queue is empty.
    bool poll_event(Event& event);
    // Blocks until at least one event is available (replaces WaitMessage()).
    void wait_event();

    // High resolution tick counter (replaces timeGetTime()).
    uint32_t get_ticks();
    // Sleeps for the given number of milliseconds (replaces Sleep()).
    void delay(uint32_t ms);
}
