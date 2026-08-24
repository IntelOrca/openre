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
        bool alt = false;    // Alt modifier held during a key event
        int data1 = 0;
        int data2 = 0;
    };

    // Initialises SDL video and creates the game window. Returns false on failure.
    bool init();
    // Destroys the window and shuts SDL down.
    void destroy();

    // Toggles borderless desktop fullscreen on the game window.
    bool set_fullscreen(bool fullscreen);

    // Underlying Win32 window handle (never SDL_Window*).
    void* get_hwnd();
    // The SDL_Window* created by init(), returned as void* so the header stays
    // SDL-free. Used by the GPU backend to claim the window for a swapchain.
    void* get_window();
    // Win32 module instance, used by DialogBoxParamA (SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER).
    void* get_hinstance();

    // Shows a modal message box with the given title/message (replaces the
    // Win32 MessageBoxA dialogs). Returns true when shown.
    bool show_message_box(const char* title, const char* message);

    // Shows or hides the system cursor (replaces ShowCursor).
    void set_cursor_visible(bool visible);

    // Queries whether the OS screensaver is currently enabled, and enables or
    // disables it (replaces SystemParametersInfoA SPI_GET/SETSCREENSAVEACTIVE).
    bool is_screensaver_enabled();
    void set_screensaver_enabled(bool enabled);

    // Fills width/height with the desktop display resolution in logical pixels
    // (replaces GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)). Returns false when
    // the display cannot be queried.
    bool get_desktop_size(int& width, int& height);

    // Fills the window rect (position + size) in screen coordinates, in the
    // window's DPI coordinate space (replaces GetWindowRect). Returns false
    // when the window is not available.
    bool get_window_rect(int& left, int& top, int& right, int& bottom);

    // Fills the window's client rect in screen coordinates (replaces
    // GetClientRect + ClientToScreen). Returns false when the window is not
    // available.
    bool get_client_rect(int& left, int& top, int& right, int& bottom);

    // Polls the next window event; returns false when the queue is empty.
    bool poll_event(Event& event);
    // Blocks until at least one event is available (replaces WaitMessage()).
    void wait_event();

    // High resolution tick counter (replaces timeGetTime()).
    uint32_t get_ticks();
    // Sleeps for the given number of milliseconds (replaces Sleep()).
    void delay(uint32_t ms);
}
