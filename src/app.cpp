#include "app.h"

#include "ecs_systems.h"
#include "game_create.h"
#include "game_module.h"
#include "logger.h"
#include "marni.h"
#include "marni_renderer.h"
#include "openre.h"
#include "system_audio.h"
#include "system_registry.h"
#include "system_window.h"
#include "systems.h"

#include <SDL3/SDL.h>

#include <cctype>
#include <cstring>
#include <filesystem>
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
    // App shell systems, run in this order every frame. Registered once on
    // boot into the shared SystemRegistry, which interleaves them with any
    // Lua-managed ECS systems.
    void register_app_systems()
    {
        auto& registry = systemRegistry();
        registry.registerNative("core.Window", {}, {}, SystemPlacement::Early, system_window);
        registry.registerNative("core.Input", { "core.Window" }, {}, SystemPlacement::Default, system_input);
        registry.registerNative("core.Module", { "core.Input" }, {}, SystemPlacement::Default, ModuleSystem);
        registry.registerNative("core.LegacyGame", { "core.Module" }, {}, SystemPlacement::Default, LegacyGameSystem);
        registry.registerNative("core.UiFlex", { "core.LegacyGame" }, {}, SystemPlacement::Default, UiFlexSystem);
        registry.registerNative("core.FontSwap", { "core.UiFlex" }, {}, SystemPlacement::Default, FontSwapSystem);
        registry.registerNative("core.InputHint", { "core.FontSwap" }, {}, SystemPlacement::Default, InputHintSystem);
        registry.registerNative("core.TextLayout", { "core.InputHint" }, {}, SystemPlacement::Default, TextLayoutSystem);
        registry.registerNative("core.Video", { "core.TextLayout" }, {}, SystemPlacement::Default, VideoSystem);
        registry.registerNative("core.Audio", { "core.Video" }, {}, SystemPlacement::Default, AudioSystem);
        // ScaledFrame runs late (after the Lua-managed systems write
        // frame.target) but before Render consumes the scale.
        registry.registerNative("core.ScaledFrame", { "core.LegacyGame" }, { "core.Render" }, SystemPlacement::Late, ScaledFrameSystem);
        registry.registerNative("core.Render", { "core.Audio", "core.ScaledFrame" }, {}, SystemPlacement::Late, RenderSystem);
    }

    // The resolved game module directory (set once at boot below).
    namespace
    {
        std::filesystem::path s_gameModuleDir;
        std::filesystem::path s_gameCoreDir;
        AppContext* s_currentApp = nullptr;
    }

    std::filesystem::path resolve_game_module_dir()
    {
        const char* game = std::getenv("OPENRE_GAME");
        if (game == nullptr || game[0] == '\0')
            return {};

        std::filesystem::path candidates[] = {
            std::filesystem::current_path() / "games" / game,
            std::filesystem::path(SDL_GetBasePath()) / "games" / game,
        };
        for (const auto& dir : candidates)
        {
            std::error_code ec;
            if (std::filesystem::is_regular_file(dir / "main.lua", ec))
            {
                return dir;
            }
        }

        logging::logWarning("[app] OPENRE_GAME='{}' set but no games/{}/main.lua found (tried cwd and exe base)", game, game);
        return {};
    }

    const std::filesystem::path& game_module_dir()
    {
        return s_gameModuleDir;
    }

    bool game_module_active()
    {
        return !s_gameModuleDir.empty();
    }

    void set_game_module_dir(std::filesystem::path dir)
    {
        s_gameModuleDir = std::move(dir);
    }

    // The shared asset directory sits next to the game module directories
    // (<games>/core); fall back to the same roots resolve_game_module_dir
    // uses when there is no module or the sibling does not exist.
    static std::filesystem::path resolve_game_core_dir()
    {
        std::filesystem::path candidates[3];
        if (game_module_active())
            candidates[0] = s_gameModuleDir.parent_path() / "core";
        candidates[1] = std::filesystem::current_path() / "games" / "core";
        candidates[2] = std::filesystem::path(SDL_GetBasePath()) / "games" / "core";

        for (const auto& dir : candidates)
        {
            if (dir.empty())
                continue;
            std::error_code ec;
            if (std::filesystem::is_directory(dir, ec))
                return dir;
        }
        return {};
    }

    const std::filesystem::path& game_core_dir()
    {
        return s_gameCoreDir;
    }

    void set_game_core_dir(std::filesystem::path dir)
    {
        s_gameCoreDir = std::move(dir);
    }

    void app_request_close()
    {
        if (s_currentApp != nullptr)
        {
            s_currentApp->requestClose();
        }
    }

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
        // Subcommands short-circuit before any engine subsystem comes up.
        if (game_create_requested(args, argc))
            return game_create_main(args, argc);

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

        // The app shell context, kept alive for the whole run so global
        // requesters (e.g. Lua scene.exit) can reach it.
        AppContext app;
        s_currentApp = &app;

        cmdline_parse(args, argc, app.cmdline);

        // Resolve the game module (OPENRE_GAME) so the boot-scene and the
        // module-driven systems know whether to skip the legacy game.
        set_game_module_dir(resolve_game_module_dir());
        set_game_core_dir(resolve_game_core_dir());
        if (game_module_active())
        {
            logging::logInfo("[app] game module active: {}", game_module_dir().string());
            if (!game_core_dir().empty())
                logging::logInfo("[app] core asset dir: {}", game_core_dir().string());

            // Bring up the SDL3 audio device for module-mode emitters. Voices
            // may already have been created; they start mixing once open.
            if (!system::audio::init())
            {
                logging::logWarning("[app] audio init failed; running without sound");
            }
        }
        else
        {
            // The native boot is gone: without a module the scene stays empty
            // (the LegacyGame node never enables), so make the dark screen
            // deliberate instead of silent.
            logging::logWarning(
                "[app] no game module active: set OPENRE_GAME to a games/<name> folder with main.lua; "
                "the boot scene will otherwise be empty");
        }

        // Boot the scene tree: hidden root + a LegacyGame child (disabled
        // until core.runGame() re-enables it to hand the frame to the legacy
        // game).
        ecs::system_boot_scene(app.scene);
        s_currentScene = &app.scene;

        register_app_systems();

        if (system::window::is_screensaver_enabled())
        {
            gGameTable.byte_680590 = 1;
            system::window::set_screensaver_enabled(false);
        }

        while (app.running())
        {
            systemRegistry().run(app);
        }

        // Closing pass, in reverse order. Snapshot by value so systems that
        // mutate the registry during close can't invalidate the in-flight
        // iteration.
        app.beginClose();
        struct ClosingDispatch
        {
            SystemKind kind;
            SystemFn nativeFn = nullptr;
            int managedId = -1;
        };
        std::vector<ClosingDispatch> closing;
        {
            const auto& ordering = systemRegistry().ordered();
            closing.reserve(ordering.size());
            for (SystemDef* def : ordering)
                closing.push_back({ def->kind, def->nativeFn, def->managedId });
        }
        for (auto it = closing.rbegin(); it != closing.rend(); ++it)
        {
            if (it->kind == SystemKind::Native && it->nativeFn != nullptr)
                it->nativeFn(app);
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

    // The running app shell, so bindings/systems can reach the AppContext.
    AppContext* current_app()
    {
        return s_currentApp;
    }
}