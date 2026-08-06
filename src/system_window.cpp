#include "system_window.h"
#include "logger.h"

#include <cstdlib>
#include <windows.h>

#include <SDL3/SDL.h>

namespace openre::system::window
{
    namespace
    {
        SDL_Window* gWindow = nullptr;
        SDL_Event gPendingEvent; // consumed by wait_event(), replayed by poll_event()
        bool gHasPendingEvent = false;

        // Converts an SDL_Keycode into the equivalent Win32 VK code so that the
        // existing input mapping (which is keyed on VK codes) keeps working.
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
            default: break;
            }
            return 0;
        }

        void translate_event(const SDL_Event& sdlEvent, Event& event)
        {
            switch (sdlEvent.type)
            {
            case SDL_EVENT_QUIT: event.type = EventType::Quit; break;
            case SDL_EVENT_KEY_DOWN:
                event.type = EventType::KeyDown;
                event.vk = sdl_keycode_to_vk(sdlEvent.key.key);
                event.repeat = sdlEvent.key.repeat;
                event.alt = (sdlEvent.key.mod & SDL_KMOD_ALT) != 0;
                break;
            case SDL_EVENT_KEY_UP:
                event.type = EventType::KeyUp;
                event.vk = sdl_keycode_to_vk(sdlEvent.key.key);
                event.alt = (sdlEvent.key.mod & SDL_KMOD_ALT) != 0;
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED: event.type = EventType::FocusGained; break;
            case SDL_EVENT_WINDOW_FOCUS_LOST: event.type = EventType::FocusLost; break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED: event.type = EventType::CloseRequested; break;
            case SDL_EVENT_WINDOW_MOVED:
                event.type = EventType::Moved;
                event.data1 = sdlEvent.window.data1;
                event.data2 = sdlEvent.window.data2;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                event.type = EventType::Resized;
                event.data1 = sdlEvent.window.data1;
                event.data2 = sdlEvent.window.data2;
                break;
            default: break;
            }
        }
    }

    bool init()
    {
        // High-resolution timer to match the original timeBeginPeriod(1) call.
        SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "1");

        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            logging::logError("[SDL3] SDL_Init failed: {}", SDL_GetError());
            return false;
        }

        const char* title = "BIOHAZARD(R) 2 PC";
        if (const char* envTitle = std::getenv("OPENRE_WINDOW_TITLE"); envTitle && *envTitle)
        {
            title = envTitle;
        }

        gWindow = SDL_CreateWindow(title, 640, 480, 0);
        if (!gWindow)
        {
            logging::logError("[SDL3] SDL_CreateWindow failed: {}", SDL_GetError());
            SDL_Quit();
            return false;
        }
        return true;
    }

    void destroy()
    {
        if (gWindow)
        {
            SDL_DestroyWindow(gWindow);
            gWindow = nullptr;
        }
        SDL_Quit();
    }

    void* get_hwnd()
    {
        if (!gWindow)
            return nullptr;
        return SDL_GetPointerProperty(SDL_GetWindowProperties(gWindow), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    }

    bool set_fullscreen(bool fullscreen)
    {
        if (!gWindow)
            return false;
        return SDL_SetWindowFullscreen(gWindow, fullscreen);
    }

    bool set_window_size(int width, int height)
    {
        if (!gWindow)
            return false;
        return SDL_SetWindowSize(gWindow, width, height);
    }

    void* get_hinstance()
    {
        if (!gWindow)
            return nullptr;
        return SDL_GetPointerProperty(SDL_GetWindowProperties(gWindow), SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
    }

    bool poll_event(Event& event)
    {
        SDL_Event sdlEvent;
        if (gHasPendingEvent)
        {
            sdlEvent = gPendingEvent;
            gHasPendingEvent = false;
        }
        else if (!SDL_PollEvent(&sdlEvent))
        {
            return false;
        }
        event = Event{};
        translate_event(sdlEvent, event);
        return true;
    }

    void wait_event()
    {
        if (SDL_WaitEvent(&gPendingEvent))
            gHasPendingEvent = true;
    }

    uint32_t get_ticks()
    {
        return static_cast<uint32_t>(SDL_GetTicks());
    }

    void delay(uint32_t ms)
    {
        SDL_Delay(ms);
    }
}
