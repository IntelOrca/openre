#include "system_window.h"

#include "app.h"
#include "audio.h"
#include "error.h"
#include "input.h"
#include "logger.h"
#include "marni.h"
#include "marni_draw.h"
#include "marni_renderer.h"
#include "openre.h"
#include "save.h"
#include "system_config.h"
#include "system_gpu.h"
#include "systems.h"
#include "vk_codes.h"

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

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

        // The window is created resizable at the configured [video] window_size
        // and is decoupled from the internal render resolution: F8 only changes
        // the render target, never the window.
        const auto windowSize = system::config::get_window_size();
        gWindow = SDL_CreateWindow(title, windowSize.width, windowSize.height, SDL_WINDOW_RESIZABLE);
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

    void* get_window()
    {
        return gWindow;
    }

    bool set_fullscreen(bool fullscreen)
    {
        if (!gWindow)
            return false;
        return SDL_SetWindowFullscreen(gWindow, fullscreen);
    }

    void* get_hinstance()
    {
        if (!gWindow)
            return nullptr;
        return SDL_GetPointerProperty(SDL_GetWindowProperties(gWindow), SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
    }

    bool show_message_box(const char* title, const char* message)
    {
        if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, gWindow))
        {
            logging::logError("[SDL3] SDL_ShowSimpleMessageBox failed: {}", SDL_GetError());
            return false;
        }
        return true;
    }

    void set_cursor_visible(bool visible)
    {
        if (visible)
            SDL_ShowCursor();
        else
            SDL_HideCursor();
    }

    bool is_screensaver_enabled()
    {
        return SDL_ScreenSaverEnabled();
    }

    void set_screensaver_enabled(bool enabled)
    {
        if (enabled)
            SDL_EnableScreenSaver();
        else
            SDL_DisableScreenSaver();
    }

    bool get_desktop_size(int& width, int& height)
    {
        const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
        if (!mode)
        {
            logging::logError("[SDL3] SDL_GetDesktopDisplayMode failed: {}", SDL_GetError());
            return false;
        }
        width = mode->w;
        height = mode->h;
        return true;
    }

    bool get_window_rect(int& left, int& top, int& right, int& bottom)
    {
        if (!gWindow)
            return false;
        int x, y, w, h;
        if (!SDL_GetWindowPosition(gWindow, &x, &y) || !SDL_GetWindowSize(gWindow, &w, &h))
            return false;
        left = x;
        top = y;
        right = x + w;
        bottom = y + h;
        return true;
    }

    bool get_client_rect(int& left, int& top, int& right, int& bottom)
    {
        return get_window_rect(left, top, right, bottom);
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

namespace openre
{
    namespace
    {
        // Window bootstrap runs once on the first call; shutting down once on
        // the closing pass.
        bool s_windowInitialised = false;
        bool s_windowShutdown = false;

        void window_boot(AppContext& app)
        {
            if (s_windowInitialised)
                return;

            if (!system::window::init())
            {
                s_windowInitialised = true;
                app.requestClose(1);
                return;
            }

            gGameTable.hInstance = system::window::get_hinstance();
            gGameTable.hwnd = system::window::get_hwnd();
            // SDL may not deliver a focus event if another window owns focus.
            gGameTable.window_active = 1;

            marni::g_renderer->init();
            if (!gGameTable.pMarni->is_gpu_active || !marni::g_renderer->requestDisplayModeCount())
            {
                logging::logError(
                    "Renderer init failed: is_gpu_active={} display_mode_count={} gpu_initialized={}",
                    gGameTable.pMarni->is_gpu_active,
                    marni::g_renderer->requestDisplayModeCount(),
                    system::gpu::is_initialized());
                win_exit(error::ERROR_FAILED_TO_INITIALIZE_DIRECTX);
                s_windowInitialised = true;
                app.requestClose(1);
                return;
            }

            cursor_op();
            marni::set_gpu_flag(marni::GpuFlags::CLEAR_TARGET, true);
            marni::g_renderer->setGpuFlag();
            if (gGameTable.pMarni->gpu_flag & marni::GpuFlags::SOFTWARE_GPU)
            {
                gGameTable.graphics_ptr_data = 1;
            }
            else
            {
                gGameTable.graphics_ptr_data = (gGameTable.pMarni->gpu_flag & marni::GpuFlags::CLEAR_TARGET) ? 0 : 2;
            }
            update_timer();
            // Anchor the frame-pacing baseline so the first LegacyGameSystem
            // pump starts from the timers configured at boot, matching the OG
            // win_main's lastFrameTime initialisation.
            system_set_frame_time_baseline(gGameTable.timer_last);

            // Create the GDI font used by the save/debug text renderer up front
            // (normally only created on resolution change / card access).
            gGameTable.is_480p = gGameTable.pMarni->xsize != 320;
            font_create();

            s_windowInitialised = true;
        }
    }

    void system_window(AppContext& app)
    {
        if (app.closing())
        {
            if (s_windowInitialised && !s_windowShutdown)
            {
                s_windowShutdown = true;
                marni::g_renderer->shutdown();
                config_write();
                if (gGameTable.byte_680592 == 1)
                {
                    gGameTable.byte_680592 = 0;
                    system::window::set_cursor_visible(true);
                }
                system::window::set_screensaver_enabled(gGameTable.byte_680592 != 0);
            }
            system::window::destroy();
            return;
        }

        window_boot(app);
        if (!s_windowInitialised)
        {
            app.requestClose(1);
            return;
        }

        // Event pump.
        system::window::Event ev;
        while (system::window::poll_event(ev))
        {
            auto marni = gGameTable.pMarni;
            gGameTable.vk_press &= 0x1F;
            switch (ev.type)
            {
            case system::window::EventType::Quit: app.requestClose(); return;
            case system::window::EventType::CloseRequested:
                marni::g_renderer->shutdown();
                gGameTable.hwnd = nullptr;
                save::rsrc_release();
                audio::ss_close();
                font_delete();
                app.requestClose();
                return;
            case system::window::EventType::FocusGained: wnd_activate(); break;
            case system::window::EventType::FocusLost:
#ifndef DEBUG
                wnd_deactivate();
                input::input_pause(&gGameTable.input);
#endif
                break;
            case system::window::EventType::Moved:
            case system::window::EventType::Resized:
                // Forward to the OG message handler so MARNI's resize logic
                // (render target rebuild) keeps working.
                if (marni != nullptr)
                {
                    marni::message(
                        marni,
                        system::window::get_hwnd(),
                        ev.type == system::window::EventType::Moved ? WM_MOVE : WM_SIZE,
                        (void*)0,
                        (void*)MAKELPARAM(ev.data1, ev.data2));
                }
                break;
            case system::window::EventType::KeyDown:
            case system::window::EventType::KeyUp:
                // Key events are dispatched by the input system.
                app.keyEvents.push_back(ev);
                break;
            default: break;
            }
        }

        // Window inactive → sleep until the next event instead of spinning.
        if (!gGameTable.window_active)
        {
            system::window::wait_event();
        }
    }
}
