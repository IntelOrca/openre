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

        // Set by the frame producers (ModuleSystem, LegacyGameSystem, including
        // special states) when the game actually advanced a paced frame in this
        // registry iteration. Managed Lua systems replace the old tick hooks
        // (which ran inside script::tick on the throttled cadence), so they are
        // only dispatched on these frames - they must not run on the uncapped
        // registry iterations between paced frames or their input handling
        // desyncs from the input walk (one tap stays visible across several
        // update() calls).
        bool framePaced = false;

        // Handoff latches bridging the Lua menu to the legacy game (see
        // core.runGame in script_bindings.cpp). The native boot screens are
        // gone: every start is module-driven, so init_main always chains into
        // game_loop and game_boot_init walks only its module branches.
        //   legacyReturnToMenu: set by the native "return to menu" sites
        //     (which used to chain the legacy boot task); polled by
        //     core.runGame to end the blocking session and hand control back
        //     to the module.
        //   legacyDemoRequested: set by core.loadDemo; game_boot_init
        //     re-applies FG_SYSTEM_DEMO (cleared by the boot wipe) so the
        //     module-initiated start takes the demo branch.
        //   legacyNewGameRequested + options: set by core.startNewGame;
        //     game_boot_init re-applies the FG_SYSTEM_14 new-game path plus
        //     the chosen player/scenario/difficulty flags (also wiped by the
        //     boot) so the module-initiated start begins a fresh game.
        bool legacyReturnToMenu = false;
        bool legacyDemoRequested = false;
        bool legacyNewGameRequested = false;
        int newGamePlayer = 0;     // 0 = leon, 1 = claire
        int newGameScenario = 0;   // 0 = scenario A, 1 = scenario B
        int newGameDifficulty = 0; // 0 = standard, 1 = casual, 2 = hardcore
        //   legacyWarpRequested + options: set by core.startRoomWarp (the
        //     command-line -p/-r warp translated onto the module boot path);
        //     game_boot_init re-applies the player/scenario flags and places
        //     the player at the room's spawn with a fresh new-game inventory.
        bool legacyWarpRequested = false;
        int warpPlayer = 0; // PLD_* id
        int warpStage = 0;  // 0-based stage
        int warpRoom = 0;   // room number

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