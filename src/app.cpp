#include "app.h"

#include "ecs_systems.h"
#include "logger.h"
#include "marni.h"
#include "marni_renderer.h"
#include "openre.h"
#include "system_window.h"
#include "systems.h"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#ifndef _WIN32
// Portable in-place lowercase; replaces the MSVC-only _strlwr().
static char* str_lwr(char* s)
{
    for (char* p = s; *p; ++p)
        *p = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    return s;
}
#endif

namespace openre
{
    // App shell systems, run in this order every frame.
    const SystemFn gAppSystems[] = {
        system_window,
        system_input,
        LegacyGameSystem,
        RenderSystem,
    };

    void AppContext::requestClose(int exitCode)
    {
        m_exitCode = exitCode;
        m_running = false;
        m_closing = true;
    }

    void AppContext::beginClose()
    {
        m_closing = true;
    }

    // Holds the scene tree for this run. The app shell keeps one canonical
    // scene so systems and hooks agree on the active ECS world.
    static ecs::Scene* s_currentScene = nullptr;

    // 0x00441910
    static int cheat_line_cmd0(char* lpCmdLine, int a1)
    {
        auto* v2 = strchr(lpCmdLine, '/');
        if (!v2)
        {
            v2 = strchr(lpCmdLine, '-');
            if (!v2)
                return -1;
        }
#ifdef _WIN32
        auto* v3 = _strlwr(v2);
#else
        auto* v3 = str_lwr(v2);
#endif
        auto* v4 = strstr(v3, gGameTable.cheat_cmds[a1]);
        if (!v4)
            return -1;
        auto* v5 = &v4[strlen(gGameTable.cheat_cmds[a1])];
        auto result = *v5 - '0';
        if (result > 9)
            return -1;
        auto v7 = v5[1] - '0';
        if (v7 <= 9)
            return v7 + 10 * result;
        return result;
    }

    // 0x00441890
    static int cheat_line_cmd1(char* lpCmdLine, int a1, int a2)
    {
        auto* v3 = strchr(lpCmdLine, '/');
        if (v3 || (v3 = strchr(lpCmdLine, '-')) != nullptr)
        {
#ifdef _WIN32
            auto* v4 = _strlwr(v3);
#else
            auto* v4 = str_lwr(v3);
#endif
            auto v5 = a1;
            if (a1 < 13)
            {
                while (v5 < a1 + a2)
                {
                    if (strstr(v4, gGameTable.cheat_cmds[v5]))
                        return v5 - a1;
                    if (++v5 >= 13)
                        return -1;
                }
            }
        }
        return -1;
    }

    // 0x0050AA60
    static void config_read()
    {
        marni::g_renderer->configReadAll();
        marni::g_renderer->configFlushAll();
    }

    int appMain(const char** args, int argc)
    {
        marni::out();
        config_read();

        // Cheat codes arrive as leading /X arguments; reconstruct the raw
        // command line string so the original scans keep working.
        std::string cmdLine;
        for (int i = 1; i < argc; i++)
        {
            if (i > 1)
                cmdLine += ' ';
            cmdLine += args[i];
        }

        gGameTable.cheat0 = cheat_line_cmd0(cmdLine.data(), 9);
        gGameTable.cheat1 = cheat_line_cmd1(cmdLine.data(), 11, 1);
        if (cheat_line_cmd1(cmdLine.data(), 12, 1) != -1)
        {
            gGameTable.ushinabe = 1;
        }

        AppContext app;
        cmdline_parse(args, argc, app.cmdline);

        // Boot the scene tree: hidden root + one LegacyGame child.
        ecs::system_boot_scene(app.scene);
        s_currentScene = &app.scene;

        if (system::window::is_screensaver_enabled())
        {
            gGameTable.byte_680590 = 1;
            system::window::set_screensaver_enabled(false);
        }

        while (app.running())
        {
            for (auto system : gAppSystems)
            {
                system(app);
            }
        }

        // Closing pass, in reverse order.
        app.beginClose();
        for (auto it = std::rbegin(gAppSystems); it != std::rend(gAppSystems); ++it)
        {
            (*it)(app);
        }

        return app.exitCode();
    }

    // The canonical scene for the current run.
    ecs::Scene* current_scene()
    {
        return s_currentScene;
    }

    void set_current_scene(ecs::Scene& scene)
    {
        s_currentScene = &scene;
    }
}