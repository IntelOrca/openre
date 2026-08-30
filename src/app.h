#pragma once

#include "cmdline.h"
#include "ecs_scene.h"
#include "system_window.h"

#include <vector>

namespace openre
{
    // State owned by the app shell and handed to every system each frame.
    class AppContext
    {
    public:
        CmdlineOptions cmdline;
        std::vector<system::window::Event> keyEvents;
        ecs::Scene scene;

        bool running() const
        {
            return m_running;
        }
        bool closing() const
        {
            return m_closing;
        }
        int exitCode() const
        {
            return m_exitCode;
        }
        void requestClose(int exitCode = 0);
        void beginClose();

        // Set by LegacyGameSystem when a frame was produced; consumed by
        // RenderSystem so it only presents frames the game actually advanced.
        bool frameActive = false;

        // Handoff latches bridging the Lua menu to the legacy game (see
        // core.runGame in script_bindings.cpp).
        //   legacyGameStartRequested: set before a module-initiated game; makes
        //     the boot task chain straight into game_loop (skipping the native
        //     title) and title_game_init skip the prologue/setup warps.
        //   legacyReturnToMenu: set by the native "return to title" sites
        //     (repointed from task_chain(title::title)); polled by core.runGame
        //     to end the blocking session and hand control back to the module.
        bool legacyGameStartRequested = false;
        bool legacyReturnToMenu = false;

    private:
        bool m_running = true;
        bool m_closing = false;
        int m_exitCode = 0;
    };

    int appMain(const char** args, int argc);

    // Requests the running app shell to close (exit code 0). Safe to call
    // from anywhere; a no-op once the shell is already closing.
    void app_request_close();
}