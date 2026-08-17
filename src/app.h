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

    private:
        bool m_running = true;
        bool m_closing = false;
        int m_exitCode = 0;
    };

    int appMain(const char** args, int argc);
}