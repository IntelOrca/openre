#include "ecs_systems.h"

#include "app.h"
#include "ecs_components.h"
#include "font.h"
#include "game_module.h"
#include "input.h"
#include "logger.h"
#include "marni_renderer.h"
#include "openre.h"
#include "options.h"
#include "render2d.h"
#include "resmgr.h"
#include "script.h"
#include "system_audio.h"
#include "system_config.h"
#include "system_filesystem.h"
#include "system_window.h"
#include "text_layout.h"
#include "texture_loader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openre
{
    namespace
    {
        // Frame-pacing baseline (ms) used by the LegacyGameSystem throttle.
        // Mirrors the original win_main loop's lastFrameTime variable.
        uint32_t s_lastFrameTime = 0;
    }

    void system_set_frame_time_baseline(uint32_t lastFrameTime)
    {
        s_lastFrameTime = lastFrameTime;
    }
}

namespace openre::ecs
{
    void system_boot_scene(Scene& scene)
    {
        // The scene always carries a LegacyGame node. It starts disabled: the
        // module owns the boot (the native no-module boot is gone) and
        // core.runGame() re-enables the node to hand the frame back to the
        // legacy game.
        Entity entity = scene.createEntity(scene.getRoot(Scene::RootKind::Game));
        auto& legacy = scene.addComponent<LegacyGameComponent>(entity);
        legacy.enabled = false;
        const auto windowSize = system::config::get_screen_resolution();
        legacy.size = { (float)windowSize.width, (float)windowSize.height };
    }

    Mat4 worldMatrix(Scene& scene, Entity entity)
    {
        // Chain the transforms from the root down to the entity.
        std::vector<Entity> chain;
        for (Entity e = entity; e != NullEntity; e = scene.getComponent<TransformComponent>(e).parent)
        {
            chain.push_back(e);
        }

        Mat4 world = Mat4::identity();
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            const auto& t = scene.getComponent<TransformComponent>(*it);
            // Prepend so the entity's own transform is applied innermost; a
            // parent's scale then correctly scales child offsets and sizes.
            // (Row-vector convention: p_world = p_local * world.)
            world = trs(t.position, t.rotation, t.scale) * world;
        }
        return world;
    }
}

namespace openre
{
    void LegacyGameSystem(AppContext& app)
    {
        if (app.closing() || !app.running())
            return;

        // The legacy game only drives the frame while its node is enabled; in
        // module mode it stays disabled until core.runGame() hands over.
        auto legacyEntity = app.scene.find<ecs::LegacyGameComponent>();
        if (legacyEntity == ecs::NullEntity)
            return;

        const auto& legacy = app.scene.getComponent<ecs::LegacyGameComponent>(legacyEntity);
        if (!legacy.enabled)
            return;

        // Mirror the OG loop's window-inactive sleep: the systems keep
        // running (pumping events) but the game does not advance.
        if (!gGameTable.window_active)
            return;

        // Frame rate throttle, identical to the original win_main loop.
        constexpr uint32_t frameRateTable[4] = { 166, 333, 666, 166 };
        auto now = system::window::get_ticks();
        auto budget = frameRateTable[gGameTable.vsync_rate / 2];
        auto elapsed = 10 * (now - s_lastFrameTime);
        if (elapsed < budget)
        {
            if (budget - elapsed >= 30) // >= 3ms margin
                system::window::delay(1);
            return;
        }

        // Special states (movie / reset) consume the frame; the renderer still
        // presents whatever was drawn, and managed systems (web inspector
        // included) keep running on this paced frame (this branch replaced the
        // old explicit script::tick() call that kept HTTP requests flowing).
        if (handle_special_states())
        {
            app.framePaced = true;
            return;
        }

        update_game();

        // The module's own frame path calls script::tick() for file-watcher
        // and hot-reload maintenance; the legacy game frame must do the same
        // or module scripts can no longer be hot-reloaded mid-session. (This
        // mirrors the pre-migration update_game() behaviour.)
        openre::script::tick();

        // Keep OG-visible timer fields updated for other hooks.
        gGameTable.timer_last = now;
        gGameTable.timer_current = now;
        gGameTable.timer_10 = 10 * now;
        s_lastFrameTime = now;

        // A full game frame advanced; the render system presents it this frame.
        app.frameActive = true;
        // Managed Lua systems run on the same throttled cadence (in sync with
        // the input walk inside update_game/psx_main), replacing the old tick
        // hooks.
        app.framePaced = true;

        // core.runGame() is non-blocking: the legacy game signals a return to
        // the menu through this flag. Disable the node here so the module
        // (and its screens) resume rendering from the next frame.
        if (app.legacyReturnToMenu)
        {
            auto& mutLegacy = app.scene.getComponent<ecs::LegacyGameComponent>(legacyEntity);
            if (mutLegacy.enabled)
            {
                mutLegacy.enabled = false;
                app.legacyReturnToMenu = false;
            }
        }
    }

    void ModuleSystem(AppContext& app)
    {
        if (app.closing() || !app.running())
            return;

        // Only active when a game module is loaded; otherwise the legacy game
        // (LegacyGameSystem) drives the frame.
        if (!game_module_active())
            return;

        // While a core.runGame() session is live the LegacyGame node is enabled
        // and owns the frame: skip pad_set (the legacy game's own loop already
        // ran it) and script maintenance so the module never double-processes
        // input or renders a stale frame over gameplay.
        auto legacyEntity = app.scene.find<ecs::LegacyGameComponent>();
        if (legacyEntity != ecs::NullEntity && app.scene.getComponent<ecs::LegacyGameComponent>(legacyEntity).enabled)
            return;

        // Mirror the OG loop's window-inactive sleep.
        if (!gGameTable.window_active)
            return;

        // Load the module's main.lua on the first pass (before the throttle
        // check so the first paced frame is usable).
        static bool s_moduleLoaded = false;
        if (!s_moduleLoaded)
        {
            s_moduleLoaded = true;
            // The legacy psx_main() boot calls load_bindings(); module mode
            // skips that boot, so populate the command bindings here or no
            // command (and thus no input) is ever detected for scripts.
            openre::input::load_bindings();
            // Same story for the options registry: seed it and load persisted
            // values before the module's scripts can read or write options.
            openre::options::init();
            script::loadGameModule(game_module_dir());
        }

        // Frame rate throttle, identical to the original win_main loop (and
        // shared with the LegacyGameSystem baseline so modes never drift).
        constexpr uint32_t frameRateTable[4] = { 166, 333, 666, 166 };
        auto now = system::window::get_ticks();
        auto budget = frameRateTable[gGameTable.vsync_rate / 2];
        auto elapsed = 10 * (now - s_lastFrameTime);
        if (elapsed < budget)
        {
            if (budget - elapsed >= 30) // >= 3ms margin
                system::window::delay(1);
            return;
        }

        // Refresh command state before scripts read it (the legacy game loop
        // calls pad_set; module mode has no legacy loop, so do it here).
        openre::input::pad_set();

        // Tick every loaded Lua script (the module VM included).
        openre::script::tick();

        // Keep OG-visible timer fields updated for other hooks.
        gGameTable.timer_last = now;
        gGameTable.timer_current = now;
        gGameTable.timer_10 = 10 * now;
        s_lastFrameTime = now;

        // A full module frame advanced; the render system presents it.
        app.frameActive = true;
        // Managed Lua systems run on the same throttled cadence (in sync with
        // the input walk above), replacing the old script::tick tick hooks.
        app.framePaced = true;
    }

    // ── Font role swap ──────────────────────────────────────────────────
    //
    // The ui/font.lua catalogue in core owns font policy. When the [options]
    // "font" mode changes, the options menu runs ui.font.refresh(), whose
    // core.setRoleFont calls pin each role onto the new cookie and stash the
    // old one as `previous`. This system walks the scene on the contiguous
    // frame and remaps every TextComponent whose font resource is one of the
    // previous role fonts onto the current role cookie, then syncs the roles
    // so the pending state collapses. Font cookies from gfx.loadFont() are
    // distinct numbers each load, so the walk compares FontResource pointers
    // (the resource dedupes by path); anything that is not a role font is
    // left alone. Assigning a new font changes the layout fingerprint, so
    // TextLayoutSystem re-lays all affected text in the same frame; input
    // hint labels are scene entities too and are reached by the same walk.
    void FontSwapSystem(AppContext& app)
    {
        if (app.closing() || !app.running())
            return;
        if (!game_module_active())
            return;
        // Only act on the contiguous frame that can swap; on throttled frames
        // frameActive is still false from the previous present, and collapsing
        // there would drop a swap between frames.
        if (!app.frameActive)
            return;
        // A pending swap is signalled by previous != current on any role; the
        // registry {previous, current} pair is what setRoleFont leaves behind.
        const ResourceCookie prevBig = previousRoleFont(FontRole::Big);
        const ResourceCookie prevSmall = previousRoleFont(FontRole::Small);
        const ResourceCookie currBig = getRoleFont(FontRole::Big);
        const ResourceCookie currSmall = getRoleFont(FontRole::Small);
        if (prevBig == currBig && prevSmall == currSmall)
            return;

        auto& resmgr = ResourceManager::get();
        const auto* oldBig = resmgr.fromCookie<FontResource>(prevBig);
        const auto* oldSmall = resmgr.fromCookie<FontResource>(prevSmall);
        size_t swapped = 0;
        app.scene.each<ecs::TextComponent>([&](ecs::Entity, ecs::TextComponent& text) {
            const auto* res = resmgr.fromCookie<FontResource>(text.font);
            if (res == oldBig && oldBig != nullptr)
            {
                text.font = currBig;
                ++swapped;
            }
            else if (res == oldSmall && oldSmall != nullptr)
            {
                text.font = currSmall;
                ++swapped;
            }
        });

        syncRoleSwap();
        logging::logInfo("[font] swap: {} text component(s) {}", swapped, swapped == 0 ? "(no-op)" : "re-role'd");
    }

    // ── Input hints ─────────────────────────────────────────────────────
    //
    // Two shared atlases (32x32 cells, 16 per row) live in the core asset
    // dir: texture/input_kbm.bmp is a full keyboard + numpad + mouse layout,
    // texture/input_pad.bmp packs the PlayStation, Xbox and Nintendo sets.
    constexpr float kInputCell = 32.0f;
    constexpr int kInputCellsPerRow = 16;
    // Space between the icon's right edge and where the label starts.
    constexpr float kHintGap = 6.0f;

    // Cell index in the atlases: row-major, 16 columns.
    constexpr int cellAt(int col, int row)
    {
        return row * kInputCellsPerRow + col;
    }

    // texture/input_kbm.bmp cells. The keyboard mirrors a physical QWERTY
    // layout (rows 0-5), with mouse buttons on row 7.
    enum InputKbmCells
    {
        kKbmEscape = cellAt(0, 0),
        kKbmF1 = cellAt(1, 0),
        kKbmPrintscreen = cellAt(13, 0),
        kKbmScroll = cellAt(14, 0),
        kKbmPause = cellAt(15, 0),
        kKbmTilde = cellAt(0, 1),
        kKbmDigit1 = cellAt(1, 1),
        kKbmMinus = cellAt(11, 1),
        kKbmEqual = cellAt(12, 1),
        kKbmBackspace = cellAt(13, 1),
        kKbmInsert = cellAt(14, 1),
        kKbmHome = cellAt(15, 1),
        kKbmTab = cellAt(0, 2),
        kKbmKeyQ = cellAt(1, 2),
        kKbmBracketL = cellAt(11, 2),
        kKbmBracketR = cellAt(12, 2),
        kKbmBackslash = cellAt(13, 2),
        kKbmDel = cellAt(14, 2),
        kKbmEnd = cellAt(15, 2),
        kKbmCaps = cellAt(0, 3),
        kKbmKeyA = cellAt(1, 3),
        kKbmSemicolon = cellAt(10, 3),
        kKbmQuote = cellAt(11, 3),
        kKbmEnter = cellAt(12, 3),
        kKbmArrowUp = cellAt(13, 3),
        kKbmPageUp = cellAt(14, 3),
        kKbmPageDown = cellAt(15, 3),
        kKbmShift = cellAt(0, 4),
        kKbmKeyZ = cellAt(1, 4),
        kKbmComma = cellAt(8, 4),
        kKbmPeriod = cellAt(9, 4),
        kKbmSlash = cellAt(10, 4),
        kKbmShiftLeft = cellAt(11, 4),
        kKbmShiftRight = cellAt(12, 4),
        kKbmArrowLeft = cellAt(13, 4),
        kKbmArrowDown = cellAt(14, 4),
        kKbmArrowRight = cellAt(15, 4),
        kKbmCtrl = cellAt(0, 5),
        kKbmAlt = cellAt(1, 5),
        kKbmSpace = cellAt(2, 5),
        kKbmAltLeft = cellAt(3, 5),
        kKbmCtrlLeft = cellAt(4, 5),
        kKbmAltRight = cellAt(5, 5),
        kKbmCtrlRight = cellAt(6, 5),
        kKbmMouseLeft = cellAt(0, 7),
        kKbmMouseRight = cellAt(1, 7),
        kKbmMouseMiddle = cellAt(2, 7),
        kKbmMouseWheelUp = cellAt(3, 7),
        kKbmMouseWheelDown = cellAt(4, 7),
        kKbmMouse4 = cellAt(5, 7),
        kKbmMouse5 = cellAt(6, 7),
        kKbmMouse6 = cellAt(7, 7),
    };

    // texture/input_pad.bmp cells. Xbox occupies rows 0-1 (+ dpad directions
    // on row 2 cols 0-3), PlayStation rows 3-5 (+ row 2 cols 8-11) and
    // Nintendo rows 5-7 (+ row 2 cols 4-7).
    enum InputPadCells
    {
        // Xbox
        kXbA = cellAt(0, 0),
        kXbB = cellAt(1, 0),
        kXbStart = cellAt(2, 0),
        kXbDpad = cellAt(3, 0),
        kXbDpadUpdown = cellAt(4, 0),
        kXbLt = cellAt(5, 0),
        kXbRt = cellAt(6, 0),
        kXbLs = cellAt(7, 0),
        kXbRs = cellAt(8, 0),
        kXbLsUpdown = cellAt(9, 0),
        kXbRsUpdown = cellAt(10, 0),
        kXb360Start = cellAt(12, 0),
        kXbX = cellAt(0, 1),
        kXbY = cellAt(1, 1),
        kXbSelect = cellAt(2, 1),
        kXbDpadLeftright = cellAt(3, 1),
        kXbDpadAll = cellAt(4, 1),
        kXbLb = cellAt(5, 1),
        kXbRb = cellAt(6, 1),
        kXbLsClick = cellAt(7, 1),
        kXbRsClick = cellAt(8, 1),
        kXbLsLeftright = cellAt(9, 1),
        kXbRsLeftright = cellAt(10, 1),
        kXb360Select = cellAt(12, 1),
        kXbDpadLeft = cellAt(0, 2),
        kXbDpadRight = cellAt(1, 2),
        kXbDpadUp = cellAt(2, 2),
        kXbDpadDown = cellAt(3, 2),

        // PlayStation
        kPsCross = cellAt(0, 3),
        kPsCircle = cellAt(1, 3),
        kPsStart = cellAt(2, 3),
        kPsDpad = cellAt(3, 3),
        kPsDpadUpdown = cellAt(4, 3),
        kPsLb = cellAt(5, 3),
        kPsRb = cellAt(6, 3),
        kPsLs = cellAt(7, 3),
        kPsRs = cellAt(8, 3),
        kPsLsUpdown = cellAt(9, 3),
        kPsRsUpdown = cellAt(10, 3),
        kPsTouchpad = cellAt(11, 3),
        kPsTouchpadLeft = cellAt(12, 3),
        kPsTouchpadRight = cellAt(13, 3),
        kPsSquare = cellAt(0, 4),
        kPsTriangle = cellAt(1, 4),
        kPsSelect = cellAt(2, 4),
        kPsDpadLeftright = cellAt(3, 4),
        kPsDpadAll = cellAt(4, 4),
        kPsLt = cellAt(5, 4),
        kPsRt = cellAt(6, 4),
        kPsLsClick = cellAt(7, 4),
        kPsRsClick = cellAt(8, 4),
        kPsLsLeftright = cellAt(9, 4),
        kPsRsLeftright = cellAt(10, 4),
        kPsDs5Select = cellAt(0, 5),
        kPsDs5Start = cellAt(1, 5),
        kPs5Cross = cellAt(2, 5),
        kPs5Circle = cellAt(3, 5),
        kPs5Square = cellAt(4, 5),
        kPs5Triangle = cellAt(5, 5),
        kPsDpadLeft = cellAt(8, 2),
        kPsDpadRight = cellAt(9, 2),
        kPsDpadUp = cellAt(10, 2),
        kPsDpadDown = cellAt(11, 2),

        // Nintendo
        kNSwitchB = cellAt(0, 6),
        kNSwitchA = cellAt(1, 6),
        kNStart = cellAt(2, 6),
        kNDpad = cellAt(3, 6),
        kNDpadUpdown = cellAt(4, 6),
        kNLb = cellAt(5, 6),
        kNRb = cellAt(6, 6),
        kNLs = cellAt(7, 6),
        kNRs = cellAt(8, 6),
        kNLsUpdown = cellAt(9, 6),
        kNRsUpdown = cellAt(10, 6),
        kNJcDpad = cellAt(11, 6),
        kNJcDpadUpdown = cellAt(12, 6),
        kNHome = cellAt(13, 6),
        kNY = cellAt(0, 7),
        kNX = cellAt(1, 7),
        kNSelect = cellAt(2, 7),
        kNDpadLeftright = cellAt(3, 7),
        kNDpadAll = cellAt(4, 7),
        kNLt = cellAt(5, 7),
        kNRt = cellAt(6, 7),
        kNLsClick = cellAt(7, 7),
        kNRsClick = cellAt(8, 7),
        kNLsLeftright = cellAt(9, 7),
        kNRsLeftright = cellAt(10, 7),
        kNProSelect = cellAt(12, 5),
        kNProStart = cellAt(13, 5),
        kNDpadLeft = cellAt(4, 2),
        kNDpadRight = cellAt(5, 2),
        kNDpadUp = cellAt(6, 2),
        kNDpadDown = cellAt(7, 2),
    };

    struct InputCommandIcon
    {
        const char* command;
        int cell;
    };

    // Keyboard/mouse bindings for named commands. A command that is a single
    // letter ("p") or digit ("5") resolves to its own key cell without an
    // entry here; unknown names fall through to the direct alias table below.
    constexpr InputCommandIcon kKbmCommands[] = {
        { "accept", kKbmEnter },
        { "cancel", kKbmEscape },
        { "pause", kKbmKeyA + ('P' - 'A') },
        { "inventory", kKbmKeyA + ('I' - 'A') },
        { "map", kKbmKeyA + ('M' - 'A') },
        { "run", kKbmShift },
        { "ready", kKbmSpace },
        { "up", kKbmArrowUp },
        { "down", kKbmArrowDown },
        { "left", kKbmArrowLeft },
        { "right", kKbmArrowRight },
    };

    // Direct icon aliases (matched after the command tables): the icon name
    // itself selects its cell. Names follow the kb:/gp:/mu: binding tokens so
    // a binding can be shown directly (plus friendlier aliases).
    constexpr InputCommandIcon kKbmAliases[] = {
        { "escape", kKbmEscape },
        { "esc", kKbmEscape },
        { "enter", kKbmEnter },
        { "return", kKbmEnter },
        { "space", kKbmSpace },
        { "tab", kKbmTab },
        { "capslock", kKbmCaps },
        { "caps", kKbmCaps },
        { "backspace", kKbmBackspace },
        { "insert", kKbmInsert },
        { "delete", kKbmDel },
        { "home", kKbmHome },
        { "end", kKbmEnd },
        { "pageup", kKbmPageUp },
        { "pagedown", kKbmPageDown },
        { "printscreen", kKbmPrintscreen },
        { "scroll", kKbmScroll },
        { "pause_break", kKbmPause },
        { "f1", kKbmF1 },
        { "f2", kKbmF1 + 1 },
        { "f3", kKbmF1 + 2 },
        { "f4", kKbmF1 + 3 },
        { "f5", kKbmF1 + 4 },
        { "f6", kKbmF1 + 5 },
        { "f7", kKbmF1 + 6 },
        { "f8", kKbmF1 + 7 },
        { "f9", kKbmF1 + 8 },
        { "f10", kKbmF1 + 9 },
        { "f11", kKbmF1 + 10 },
        { "f12", kKbmF1 + 11 },
        { "`", kKbmTilde },
        { "-", kKbmMinus },
        { "=", kKbmEqual },
        { "[", kKbmBracketL },
        { "]", kKbmBracketR },
        { "\\", kKbmBackslash },
        { ";", kKbmSemicolon },
        { "'", kKbmQuote },
        { ",", kKbmComma },
        { ".", kKbmPeriod },
        { "/", kKbmSlash },
        { "shift", kKbmShift },
        { "lshift", kKbmShiftLeft },
        { "rshift", kKbmShiftRight },
        { "ctrl", kKbmCtrl },
        { "lctrl", kKbmCtrlLeft },
        { "rctrl", kKbmCtrlRight },
        { "alt", kKbmAlt },
        { "lalt", kKbmAltLeft },
        { "ralt", kKbmAltRight },
        { "arrow_up", kKbmArrowUp },
        { "arrow_down", kKbmArrowDown },
        { "arrow_left", kKbmArrowLeft },
        { "arrow_right", kKbmArrowRight },
        { "mouse_left", kKbmMouseLeft },
        { "mouse_right", kKbmMouseRight },
        { "mouse_middle", kKbmMouseMiddle },
        { "wheel_up", kKbmMouseWheelUp },
        { "wheel_down", kKbmMouseWheelDown },
        { "mouse_4", kKbmMouse4 },
        { "mouse_5", kKbmMouse5 },
        { "mouse_6", kKbmMouse6 },
    };

    bool lookupCommandCell(const InputCommandIcon* table, size_t count, const std::string& command, int& outCell)
    {
        for (size_t i = 0; i < count; i++)
        {
            // Trailing zero-filled entries (nullptr) never match.
            if (table[i].command != nullptr && command == table[i].command)
            {
                outCell = table[i].cell;
                return true;
            }
        }
        return false;
    }

    // Resolves a hint command to a cell in texture/input_kbm.bmp. Single
    // letters map onto their key (QWERTY rows); digits 1-0 sit on row 1.
    bool lookupKbmCell(const std::string& command, int& outCell)
    {
        if (command.size() == 1)
        {
            const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(command[0])));
            if (c >= 'A' && c <= 'Z')
            {
                // Letter keys: QWERTY keyboard rows (see kKbmCells above).
                static constexpr int kLetterCells[26] = {
                    cellAt(1, 3),  // A
                    cellAt(5, 4),  // B
                    cellAt(3, 4),  // C
                    cellAt(3, 3),  // D
                    cellAt(3, 2),  // E
                    cellAt(4, 3),  // F
                    cellAt(5, 3),  // G
                    cellAt(6, 3),  // H
                    cellAt(8, 2),  // I
                    cellAt(7, 3),  // J
                    cellAt(8, 3),  // K
                    cellAt(9, 3),  // L
                    cellAt(7, 4),  // M
                    cellAt(6, 4),  // N
                    cellAt(9, 2),  // O
                    cellAt(10, 2), // P
                    cellAt(1, 2),  // Q
                    cellAt(4, 2),  // R
                    cellAt(2, 3),  // S
                    cellAt(5, 2),  // T
                    cellAt(7, 2),  // U
                    cellAt(6, 2),  // V
                    cellAt(2, 2),  // W
                    cellAt(2, 4),  // X
                    cellAt(6, 2),  // Y
                    cellAt(1, 4),  // Z
                };
                outCell = kLetterCells[c - 'A'];
                return true;
            }
            if (c >= '1' && c <= '9')
            {
                outCell = kKbmDigit1 + (c - '1');
                return true;
            }
            if (c == '0')
            {
                outCell = kKbmDigit1 + 9;
                return true;
            }
            return false;
        }
        return lookupCommandCell(kKbmCommands, std::size(kKbmCommands), command, outCell)
            || lookupCommandCell(kKbmAliases, std::size(kKbmAliases), command, outCell);
    }

    // Resolves a hint command to a cell in texture/input_pad.bmp for the
    // given device family. Gameplay commands map onto the conventional face
    // button of each layout (accept = south: A / cross / B, cancel = east:
    // B / circle / A); icon aliases ("dpad_up", "ls_click", ...) and the raw
    // gamepad source names used by command bindings ("south", "l_trigger",
    // "axis_left_y-", ...) select glyphs directly.
    bool lookupPadCell(ecs::InputHintDevice device, const std::string& command, int& outCell)
    {
        constexpr size_t kStyleCount = 3; // ps, xbox, nintendo

        struct DeviceTable
        {
            InputCommandIcon commands[12];
            InputCommandIcon sources[20]; // GamepadSource names (input.h)
            int dpadLeft, dpadRight, dpadUp, dpadDown;
        };

        static constexpr DeviceTable kTables[kStyleCount] = {
            // PlayStation
            {
                {
                    { "accept", kPsCross },
                    { "cancel", kPsCircle },
                    { "pause", kPsStart },
                    { "inventory", kPsTriangle },
                    { "map", kPsSquare },
                    { "run", kPsRt },
                    { "ready", kPsLt },
                    { "start", kPsStart },
                    { "select", kPsSelect },
                    { "l1", kPsLb },
                    { "r1", kPsRb },
                    { "touchpad", kPsTouchpad },
                },
                {
                    { "south", kPsCross },        { "east", kPsCircle },        { "north", kPsTriangle },
                    { "west", kPsSquare },        { "l_trigger", kPsLt },       { "r_trigger", kPsRt },
                    { "l_shoulder", kPsLb },      { "r_shoulder", kPsRb },      { "start", kPsStart },
                    { "mode", kPsTouchpad }, // no PS logo glyph; touchpad is nearest
                    { "l3", kPsLsClick },         { "r3", kPsRsClick },         { "dpad_up", kPsDpadUp },
                    { "dpad_down", kPsDpadDown }, { "dpad_left", kPsDpadLeft }, { "dpad_right", kPsDpadRight },
                    { "axis_left_y-", kPsLs },    { "axis_left_y+", kPsLs },    { "axis_left_x-", kPsLs },
                    { "axis_left_x+", kPsLs },
                },
                kPsDpadLeft,
                kPsDpadRight,
                kPsDpadUp,
                kPsDpadDown,
            },
            // Xbox
            {
                {
                    { "accept", kXbA },
                    { "cancel", kXbB },
                    { "pause", kXbStart },
                    { "inventory", kXbY },
                    { "map", kXbX },
                    { "run", kXbRt },
                    { "ready", kXbLt },
                    { "start", kXbStart },
                    { "select", kXbSelect },
                    { "l1", kXbLb },
                    { "r1", kXbRb },
                    { "", 0 },
                },
                {
                    { "south", kXbA },
                    { "east", kXbB },
                    { "north", kXbY },
                    { "west", kXbX },
                    { "l_trigger", kXbLt },
                    { "r_trigger", kXbRt },
                    { "l_shoulder", kXbLb },
                    { "r_shoulder", kXbRb },
                    { "start", kXbStart },
                    // No guide/logo glyph in the atlas; "mode" drops the icon.
                    { "l3", kXbLsClick },
                    { "r3", kXbRsClick },
                    { "dpad_up", kXbDpadUp },
                    { "dpad_down", kXbDpadDown },
                    { "dpad_left", kXbDpadLeft },
                    { "dpad_right", kXbDpadRight },
                    { "axis_left_y-", kXbLs },
                    { "axis_left_y+", kXbLs },
                    { "axis_left_x-", kXbLs },
                    { "axis_left_x+", kXbLs },
                },
                kXbDpadLeft,
                kXbDpadRight,
                kXbDpadUp,
                kXbDpadDown,
            },
            // Nintendo
            {
                {
                    { "accept", kNSwitchA },
                    { "cancel", kNSwitchB },
                    { "pause", kNProStart },
                    { "inventory", kNY },
                    { "map", kNX },
                    { "run", kNRt },
                    { "ready", kNLt },
                    { "start", kNProStart },
                    { "select", kNProSelect },
                    { "l1", kNLb },
                    { "r1", kNRb },
                    { "home", kNHome },
                },
                {
                    { "south", kNSwitchB },
                    { "east", kNSwitchA },
                    { "north", kNX },
                    { "west", kNY },
                    { "l_trigger", kNLt },
                    { "r_trigger", kNRt },
                    { "l_shoulder", kNLb },
                    { "r_shoulder", kNRb },
                    { "start", kNProStart },
                    { "mode", kNHome },
                    { "l3", kNLsClick },
                    { "r3", kNRsClick },
                    { "dpad_up", kNDpadUp },
                    { "dpad_down", kNDpadDown },
                    { "dpad_left", kNDpadLeft },
                    { "dpad_right", kNDpadRight },
                    { "axis_left_y-", kNLs },
                    { "axis_left_y+", kNLs },
                    { "axis_left_x-", kNLs },
                    { "axis_left_x+", kNLs },
                },
                kNDpadLeft,
                kNDpadRight,
                kNDpadUp,
                kNDpadDown,
            },
        };

        const size_t index = static_cast<size_t>(device) - static_cast<size_t>(ecs::InputHintDevice::PlayStation);
        if (index >= kStyleCount)
            return false;
        const DeviceTable& table = kTables[index];

        if (lookupCommandCell(table.commands, std::size(table.commands), command, outCell))
            return true;
        if (lookupCommandCell(table.sources, std::size(table.sources), command, outCell))
            return true;

        // Shared pad aliases: directions plus stick/shoulder glyph names.
        if (command == "up")
            outCell = table.dpadUp;
        else if (command == "down")
            outCell = table.dpadDown;
        else if (command == "left")
            outCell = table.dpadLeft;
        else if (command == "right")
            outCell = table.dpadRight;
        else if (command == "dpad")
            outCell = device == ecs::InputHintDevice::PlayStation ? kPsDpad
                : device == ecs::InputHintDevice::Xbox            ? kXbDpad
                                                                  : kNDpad;
        else if (command == "ls")
            outCell = device == ecs::InputHintDevice::PlayStation ? kPsLs : device == ecs::InputHintDevice::Xbox ? kXbLs : kNLs;
        else if (command == "rs")
            outCell = device == ecs::InputHintDevice::PlayStation ? kPsRs : device == ecs::InputHintDevice::Xbox ? kXbRs : kNRs;
        else if (command == "ls_click")
            outCell = device == ecs::InputHintDevice::PlayStation ? kPsLsClick
                : device == ecs::InputHintDevice::Xbox            ? kXbLsClick
                                                                  : kNLsClick;
        else if (command == "rs_click")
            outCell = device == ecs::InputHintDevice::PlayStation ? kPsRsClick
                : device == ecs::InputHintDevice::Xbox            ? kXbRsClick
                                                                  : kNRsClick;
        else if (command == "lt")
            outCell = device == ecs::InputHintDevice::PlayStation ? kPsLt : device == ecs::InputHintDevice::Xbox ? kXbLt : kNLt;
        else if (command == "rt")
            outCell = device == ecs::InputHintDevice::PlayStation ? kPsRt : device == ecs::InputHintDevice::Xbox ? kXbRt : kNRt;
        else
            return false;
        return true;
    }

    // Engine-owned shared resources for hint icons/labels. The cookies are
    // held forever so the atlas/font outlives every component that borrows
    // them; both loaders cache the decoded file on disk, not in the manager.
    ResourceCookie inputHintAtlas(ecs::InputHintDevice device)
    {
        switch (device)
        {
        case ecs::InputHintDevice::KeyboardMouse:
        {
            static ResourceCookie cookie = loadTextureResource("texture/input_kbm", 0, 0);
            return cookie;
        }
        default:
        {
            static ResourceCookie cookie = loadTextureResource("texture/input_pad", 0, 0);
            return cookie;
        }
        }
    }

    ResourceCookie inputHintFont()
    {
        // Follow the Lua ui/font catalogue's role binding so hint labels swap
        // with the rest of the UI text. getRoleFont caches, so this is cheap.
        return getRoleFont(FontRole::Big);
    }

    // Resolves a hint to a list of atlas cells (one per icon). A registered
    // input command renders one cell per binding that matches the hint's
    // device family, so hints follow the [input] bindings automatically;
    // anything else is treated as a direct key name / icon alias.
    void resolveHintCells(const ecs::InputHintComponent& hint, std::vector<int>& outCells)
    {
        const auto cmd = openre::input::command_from_name(hint.command);
        if (cmd != openre::input::COMMAND_COUNT)
        {
            for (const auto& b : openre::input::command_bindings(cmd))
            {
                int cell = -1;
                bool ok = false;
                switch (b.device)
                {
                case openre::input::BindingDevice::Keyboard:
                    ok = hint.device == ecs::InputHintDevice::KeyboardMouse && lookupKbmCell(b.name, cell);
                    break;
                case openre::input::BindingDevice::Mouse:
                    // Mouse buttons live on the kbm atlas under mouse_* names.
                    ok = hint.device == ecs::InputHintDevice::KeyboardMouse && lookupKbmCell("mouse_" + b.name, cell);
                    break;
                case openre::input::BindingDevice::Gamepad:
                    ok = hint.device != ecs::InputHintDevice::KeyboardMouse && lookupPadCell(hint.device, b.name, cell);
                    break;
                }
                if (ok && cell >= 0)
                    outCells.push_back(cell);
            }
            return;
        }

        // Not a registered command: a single direct key ("p"), icon alias
        // ("mouse_left", "dpad") or unknown name (no icon).
        int cell = -1;
        const bool found = hint.device == ecs::InputHintDevice::KeyboardMouse ? lookupKbmCell(hint.command, cell)
                                                                              : lookupPadCell(hint.device, hint.command, cell);
        if (found)
            outCells.push_back(cell);
    }

    // Materialises InputHintComponents into child entities: one 32x32 icon
    // sprite per binding of `command` on `device` (laid out left to right),
    // then the optional text label to the right of the last icon. Runs
    // before TextLayoutSystem so freshly created labels get laid out in the
    // same frame; only rebuilds when the fields or the command bindings
    // change. Scale the hint entity's transform to resize the whole hint.
    void InputHintSystem(AppContext& app)
    {
        if (app.closing() || !app.running())
            return;
        if (!game_module_active())
            return;
        if (!app.frameActive)
            return;

        const uint32_t revision = openre::input::bindings_revision();

        app.scene.each<ecs::InputHintComponent>([&](ecs::Entity entity, ecs::InputHintComponent& hint) {
            std::string fingerprint = hint.command + "|" + hint.text + "|" + std::to_string(static_cast<uint32_t>(hint.device))
                + "|" + std::to_string(hint.font) + "|" + std::to_string(revision);
            bool entitiesValid = hint.labelEntity == ecs::NullEntity || app.scene.isValid(hint.labelEntity);
            for (const ecs::Entity icon : hint.icons)
                entitiesValid = entitiesValid && app.scene.isValid(icon);
            if (fingerprint == hint.fingerprint && entitiesValid)
                return;
            hint.fingerprint = fingerprint;

            // Rebuild from scratch: drop the previous children first.
            for (const ecs::Entity child : hint.icons)
            {
                if (child != ecs::NullEntity && app.scene.isValid(child))
                    app.scene.destroyEntity(child);
            }
            hint.icons.clear();
            if (hint.labelEntity != ecs::NullEntity && app.scene.isValid(hint.labelEntity))
                app.scene.destroyEntity(hint.labelEntity);
            hint.labelEntity = ecs::NullEntity;

            // Resolve the glyphs before touching the scene so an unknown
            // command simply drops its icons instead of corrupting layout.
            std::vector<int> cells;
            resolveHintCells(hint, cells);
            const ResourceCookie atlas = inputHintAtlas(hint.device);

            float nextX = 0.0f; // left edge of the content after the icons
            for (size_t i = 0; i < cells.size() && atlas != 0; i++)
            {
                const int cell = cells[i];
                ecs::Entity icon = app.scene.createEntity(entity);
                hint.icons.push_back(icon);
                auto& transform = app.scene.getComponent<ecs::TransformComponent>(icon);
                transform.position = { static_cast<float>(i) * (kInputCell + kHintGap), 0.0f, 0.0f };

                auto& sprite = app.scene.addComponent<ecs::SpriteComponent>(icon);
                auto& iconSize = app.scene.addComponent<ecs::UiSizeComponent>(icon);
                iconSize.width = kInputCell;
                iconSize.height = kInputCell;
                sprite.texture = atlas;
                sprite.src = {
                    static_cast<float>(cell % kInputCellsPerRow) * kInputCell,
                    static_cast<float>(cell / kInputCellsPerRow) * kInputCell,
                    kInputCell,
                    kInputCell,
                };
                nextX = static_cast<float>(i) * (kInputCell + kHintGap) + kInputCell * 0.5f + kHintGap;
            }

            if (hint.text.empty())
                return;

            // Measure the label so it can be centred on its own position,
            // keeping glyph offsets small relative to the text entity.
            const ResourceCookie font = hint.font != 0 ? hint.font : inputHintFont();
            auto* fontResource = ResourceManager::get().fromCookie<FontResource>(font);
            const float lineHeight = fontResource != nullptr && fontResource->fromFnt
                ? static_cast<float>(fontResource->lineHeight())
                : kInputCell;
            const float textWidth = fontResource != nullptr
                ? text::measureWidth(text::decodeText(hint.text, fontResource, lineHeight, lineHeight))
                : 0.0f;

            ecs::Entity label = app.scene.createEntity(entity);
            hint.labelEntity = label;
            auto& transform = app.scene.getComponent<ecs::TransformComponent>(label);
            // The word is centred on the label position: place that centre a
            // half-width right of where the first glyph should start.
            transform.position = { nextX + textWidth * 0.5f, 0.0f, 0.0f };

            auto& text = app.scene.addComponent<ecs::TextComponent>(label);
            auto& labelSize = app.scene.addComponent<ecs::UiSizeComponent>(label);
            labelSize.width = std::max(textWidth, 1.0f);
            labelSize.height = lineHeight;
            text.text = hint.text;
            text.flags = static_cast<uint32_t>(ecs::TextFlags::XCenter) | static_cast<uint32_t>(ecs::TextFlags::YMiddle);
            text.font = font;
        });
    }

    // Materialises TextComponents into per-glyph child SpriteComponents. Runs
    // before the render pass each frame. Only rebuilds when the text inputs
    // (string/flags/font/bounds) change, so an unchanged frame is cheap.
    void TextLayoutSystem(AppContext& app)
    {
        if (app.closing() || !app.running())
            return;
        if (!game_module_active())
            return;
        if (!app.frameActive)
            return;

        app.scene.each<ecs::TextComponent>([&](ecs::Entity entity, ecs::TextComponent& text) {
            std::string fingerprint;
            text::fingerprint(app.scene, entity, text, fingerprint);
            if (fingerprint == text.layoutFingerprint && !text.needsLayout)
                return;
            text::layoutEntity(app.scene, entity, text);
            text.layoutFingerprint = fingerprint;
            text.needsLayout = false;
        });
    }

    // Applies every ScaledFrameComponent: computes the uniform scale that maps
    // the component's source size onto its target size (contain = fit inside,
    // cover = fill), optionally snapped down to `increment` steps, then writes
    // it to the entity's transform scale. The position is untouched - callers
    // place the entity (e.g. at the window centre). Runs after the script tick
    // so Lua-written inputs are honoured, before rendering reads transforms.
    void ScaledFrameSystem(AppContext& app)
    {
        if (app.closing() || !app.running())
            return;
        if (!game_module_active())
            return;
        if (!app.frameActive)
            return;

        app.scene.each<ecs::ScaledFrameComponent>([&](ecs::Entity entity, ecs::ScaledFrameComponent& frame) {
            const float sw = frame.source.width;
            const float sh = frame.source.height;
            const float tw = frame.target.width;
            const float th = frame.target.height;
            if (sw <= 0 || sh <= 0 || tw <= 0 || th <= 0)
                return;

            const bool cover = frame.mode == ecs::ScaledFrameMode::Cover;
            float scale = cover ? std::max(tw / sw, th / sh) : std::min(tw / sw, th / sh);
            if (frame.increment > 0)
            {
                // Snap DOWN to whole increments so the content never exceeds
                // the target; never go below one increment.
                scale = std::floor(scale / frame.increment + 1e-4f) * frame.increment;
                scale = std::max(scale, frame.increment);
            }
            auto& transform = app.scene.getComponent<ecs::TransformComponent>(entity);
            transform.scale.x = scale;
            transform.scale.y = scale;
        });
    }

    namespace
    {
        // One immediate child of a flex container during a layout pass.
        struct FlexSlot
        {
            ecs::Entity entity = ecs::NullEntity;
            ecs::TransformComponent* transform = nullptr;
            ecs::UiSizeComponent* uiSize = nullptr; // null when the child has none
            float width = 0;
            float height = 0;
            float weight = 0;
            ecs::UiSizeAlignment alignment = ecs::UiSizeAlignment::Stretch;
        };

        // Writes a slot's final laid-out rectangle to the entity. Positions
        // are the CENTRE of the rectangle (render2d draws sprites centred on
        // their transform position).
        void placeFlexSlot(FlexSlot& slot, float centreX, float centreY)
        {
            if (slot.transform != nullptr)
            {
                slot.transform->position.x = centreX;
                slot.transform->position.y = centreY;
            }
            if (slot.uiSize != nullptr)
            {
                slot.uiSize->width = slot.width;
                slot.uiSize->height = slot.height;
            }
        }

        // Lays out the immediate children of one UiFlex container.
        //
        // Main axis (horizontal/vertical): fixed (weight-0) children take
        // their literal size; when they overflow the content box every
        // literal size AND gap is scaled by available/fixedTotal so nothing
        // escapes the container, weighted children collapsing to zero.
        // Remaining space goes to weighted children proportionally by
        // weight; when no child has weight the whole run is centred.
        // Cross axis: weighted children stretch to the content box,
        // fixed children are clamped to it and centred.
        //
        // Orientation None is an overlay stack: each child independently
        // fills (weight > 0) or is clamped+centred (weight 0) on both axes.
        void layoutFlexContainer(ecs::Scene& scene, ecs::Entity container, const ecs::UiFlexComponent& flex)
        {
            auto* size = scene.tryGetComponent<ecs::UiSizeComponent>(container);
            if (size == nullptr)
                return;

            const float pad = std::max(flex.padding, 0.0f);
            const float contentW = size->width - pad * 2;
            const float contentH = size->height - pad * 2;
            if (contentW <= 0 || contentH <= 0)
                return;

            // Container transforms are the CENTRE of their box (render2d
            // draws sprites centred on their position), so the content box's
            // top-left corner sits at (-halfWidth + pad, -halfHeight + pad)
            // in the container's local space.
            const float originX = pad - size->width * 0.5f;
            const float originY = pad - size->height * 0.5f;

            std::vector<FlexSlot> slots;
            for (ecs::Entity child = scene.getComponent<ecs::TransformComponent>(container).firstChild;
                 child != ecs::NullEntity;
                 child = scene.getComponent<ecs::TransformComponent>(child).nextSibling)
            {
                FlexSlot slot;
                slot.entity = child;
                slot.transform = &scene.getComponent<ecs::TransformComponent>(child);
                slot.uiSize = scene.tryGetComponent<ecs::UiSizeComponent>(child);
                if (slot.uiSize != nullptr)
                {
                    slot.width = slot.uiSize->width;
                    slot.height = slot.uiSize->height;
                    slot.weight = slot.uiSize->weight;
                    slot.alignment = slot.uiSize->alignment;
                }
                slots.push_back(slot);
            }
            if (slots.empty())
                return;

            if (flex.orientation == ecs::UiFlexOrientation::None)
            {
                // Every clamped/filled child shares the same mid-point:
                // the centre of the content box.
                const float centreX = originX + contentW * 0.5f;
                const float centreY = originY + contentH * 0.5f;
                for (auto& slot : slots)
                {
                    if (slot.weight > 0)
                    {
                        slot.width = contentW;
                        slot.height = contentH;
                    }
                    else
                    {
                        slot.width = std::min(slot.width, contentW);
                        slot.height = std::min(slot.height, contentH);
                    }
                    placeFlexSlot(slot, centreX, centreY);
                }
                return;
            }

            const bool horizontal = flex.orientation == ecs::UiFlexOrientation::Horizontal;
            const float originMain = horizontal ? originX : originY;
            const float originCross = horizontal ? originY : originX;
            const float contentMain = horizontal ? contentW : contentH;
            const float contentCross = horizontal ? contentH : contentW;

            float fixedTotal = slots.size() > 1 ? flex.gap * static_cast<float>(slots.size() - 1) : 0.0f;
            float totalWeight = 0;
            for (auto& slot : slots)
            {
                if (slot.weight > 0)
                    totalWeight += slot.weight;
                else
                    fixedTotal += horizontal ? slot.width : slot.height;
            }

            float gap = flex.gap;
            float cursorStart = originMain;
            if (fixedTotal > contentMain && fixedTotal > 0)
            {
                // Overflow: shrink literals and gaps into the container.
                const float f = contentMain / fixedTotal;
                gap *= f;
                for (auto& slot : slots)
                {
                    if (slot.weight <= 0)
                    {
                        if (horizontal)
                            slot.width *= f;
                        else
                            slot.height *= f;
                    }
                    else if (horizontal)
                    {
                        slot.width = 0;
                    }
                    else
                    {
                        slot.height = 0;
                    }
                }
            }
            else if (totalWeight > 0)
            {
                // Weighted children share the slack proportionally.
                const float unit = (contentMain - fixedTotal) / totalWeight;
                for (auto& slot : slots)
                {
                    if (slot.weight <= 0)
                        continue;
                    const float share = slot.weight * unit;
                    if (horizontal)
                        slot.width = share;
                    else
                        slot.height = share;
                }
            }
            else
            {
                // Nothing weighted: centre the whole run in the leftover space.
                cursorStart += (contentMain - fixedTotal) * 0.5f;
            }

            float cursor = cursorStart;
            for (auto& slot : slots)
            {
                const float mainSize = horizontal ? slot.width : slot.height;
                float crossSize = horizontal ? slot.height : slot.width;
                // `alignment` controls the cross axis for every child,
                // regardless of weight (which is purely main-axis).
                const bool stretch = slot.alignment == ecs::UiSizeAlignment::Stretch;
                if (stretch)
                    crossSize = contentCross;
                else
                    crossSize = std::min(crossSize, contentCross);

                const float centreMain = cursor + mainSize * 0.5f;
                float centreCross;
                switch (slot.alignment)
                {
                case ecs::UiSizeAlignment::Begin: centreCross = originCross + crossSize * 0.5f; break;
                case ecs::UiSizeAlignment::End: centreCross = originCross + contentCross - crossSize * 0.5f; break;
                default: // Center or Stretch
                    centreCross = originCross + contentCross * 0.5f;
                    break;
                }
                if (horizontal)
                {
                    slot.height = crossSize;
                    placeFlexSlot(slot, centreMain, centreCross);
                }
                else
                {
                    slot.width = crossSize;
                    placeFlexSlot(slot, centreCross, centreMain);
                }
                cursor += mainSize + gap;
            }
        }

        // Depth-first top-down pass: each flex container is laid out before
        // its subtrees are visited, so nested flex sees parent-fresh sizes.
        void layoutFlexTree(ecs::Scene& scene, ecs::Entity entity)
        {
            if (auto* flex = scene.tryGetComponent<ecs::UiFlexComponent>(entity))
                layoutFlexContainer(scene, entity, *flex);
            for (ecs::Entity child = scene.getComponent<ecs::TransformComponent>(entity).firstChild; child != ecs::NullEntity;
                 child = scene.getComponent<ecs::TransformComponent>(child).nextSibling)
            {
                layoutFlexTree(scene, child);
            }
        }
    }

    // Applies every UiFlexComponent: lays out the immediate children of each
    // flex container within its (authored) UiSize box. Children have their
    // local position and UiSize width/height written every frame - both are
    // system-owned on flex children. Runs after the script tick so
    // Lua-created entities participate immediately, before rendering reads
    // transforms.
    void UiFlexSystem(AppContext& app)
    {
        if (app.closing() || !app.running())
            return;
        if (!game_module_active())
            return;
        if (!app.frameActive)
            return;

        layoutFlexTree(app.scene, app.scene.root());
    }

    namespace
    {
        // Per-emitter mixer state mirrored from AudioEmitterComponent.
        struct AudioVoiceState
        {
            uint32_t handle = 0;            // system::audio voice (0 = none loaded)
            uint32_t attached = 0;          // Sound cookie the voice is bound to
            bool playing = false;           // last frame's want-to-play edge state
            bool stateDriven = false;       // playback started via state edge (vs trigger)
            bool failed = false;            // resource failed to attach (no retry until re-trigger)
            float fadeStartVolume = 100.0f; // effective volume the fade started from
            int fadeFramesLeft = 0;         // frames remaining in the active fade
            float fadeTotal = 0.0f;         // fade length in frames
            int fadeArmed = -1;             // fadeFrames value the current fade was armed for
            int32_t lastTrigger = 0;        // trigger value already fired (trigger mode)
        };
    }

    // Native audio bring-up for module mode: mirrors every
    // ecs::AudioEmitterComponent onto a voice of the SDL3 software mixer.
    // The emitter plays the sound resource referenced by its `sound` cookie
    // (small sources resident, large ones streamed). `state` edges start/stop
    // playback honouring `loop`; a new `trigger` value fires a one-shot from
    // the first sample regardless of state. Volume/pan are pushed each frame
    // while playing and the observed playback state is written back into
    // `status`.
    void AudioSystem(AppContext& app)
    {
        auto* scene = openre::current_scene();
        if (scene == nullptr || !game_module_active())
            return;

        static std::unordered_map<ecs::Entity, AudioVoiceState> s_voices;

        // Retire voices whose emitter entity/component is gone so destroyed
        // entities do not keep sounding.
        for (auto it = s_voices.begin(); it != s_voices.end();)
        {
            if (!scene->hasComponent<ecs::AudioEmitterComponent>(it->first))
            {
                if (it->second.handle != 0 && it->second.playing)
                    system::audio::stop(it->second.handle);
                it = s_voices.erase(it);
            }
            else
                ++it;
        }

        app.scene.each<ecs::AudioEmitterComponent>([&](ecs::Entity entity, ecs::AudioEmitterComponent& em) {
            AudioVoiceState& voice = s_voices[entity];

            // Resolve the attached sound resource, if any.
            const auto* snd = em.sound != 0 ? ResourceManager::get().fromCookie<SoundResource>(em.sound) : nullptr;

            // Trigger mode: every new `trigger` value plays the attached
            // resource once from its first sample regardless of `state`, so a
            // rapid-fire emitter never stacks or resumes.
            if (em.trigger != voice.lastTrigger)
            {
                voice.lastTrigger = em.trigger;
                if (snd != nullptr && snd->audioHandle != 0)
                {
                    system::audio::set_loop(snd->audioHandle, false);
                    system::audio::trigger(snd->audioHandle);
                    voice.handle = snd->audioHandle;
                    voice.attached = em.sound;
                    voice.playing = true;
                    voice.stateDriven = false;
                    voice.failed = false;
                }
            }

            // A changed/vanished attachment stops the current voice and
            // re-arms the rising-edge path below.
            if (voice.attached != em.sound)
            {
                if (voice.handle != 0 && voice.playing)
                    system::audio::stop(voice.handle);
                voice.playing = false;
                voice.handle = 0;
                voice.failed = false;
                voice.attached = em.sound;
            }

            const bool want = em.state == "playing";
            if (snd == nullptr || snd->audioHandle == 0)
            {
                // Nothing playable attached: a stale playing flag must not
                // linger on the shared per-entity state.
                if (voice.playing && voice.handle != 0)
                    system::audio::stop(voice.handle);
                voice.playing = false;
                voice.failed = want && snd != nullptr; // invalid cookie: warn-retry on next edge
            }
            else if (want && !voice.playing && !voice.failed)
            {
                // State rising edge: start the attached resource honouring the
                // emitter's loop flag.
                system::audio::set_loop(snd->audioHandle, em.loop);
                if (system::audio::play(snd->audioHandle))
                {
                    voice.handle = snd->audioHandle;
                    voice.playing = true;
                    voice.stateDriven = true;
                    voice.fadeArmed = -1; // a fresh trigger restarts an armed fade
                }
                else
                {
                    voice.failed = true;
                    logging::logWarning("[audio] failed to start audio_emitter sound cookie {}", em.sound);
                }
            }
            else if (!want && voice.playing && voice.stateDriven)
            {
                // Falling edge: stop the voice. Trigger-driven one-shots are
                // left to run out naturally.
                if (voice.handle != 0)
                    system::audio::stop(voice.handle);
                voice.playing = false;
            }
            if (!want)
                voice.failed = false; // re-arm so the next rising edge retries

            // Push volume/pan while playing. Category volumes default to full
            // until a mixer configuration exists.
            constexpr float categoryVolume = 100.0f;
            if (voice.playing && voice.handle != 0)
            {
                const float base = (em.volume / 100.0f) * (categoryVolume / 100.0f);
                float eff = base * 100.0f;
                if (em.fadeFrames > 0)
                {
                    if (voice.fadeArmed != em.fadeFrames)
                    {
                        voice.fadeArmed = em.fadeFrames;
                        voice.fadeStartVolume = base * 100.0f;
                        voice.fadeTotal = static_cast<float>(em.fadeFrames);
                        voice.fadeFramesLeft = em.fadeFrames;
                    }
                    const float t = 1.0f - static_cast<float>(voice.fadeFramesLeft) / voice.fadeTotal;
                    eff = voice.fadeStartVolume + (em.fadeTargetVolume - voice.fadeStartVolume) * t;
                    if (voice.fadeFramesLeft > 0)
                        voice.fadeFramesLeft--;
                }

                const float gain = eff / 100.0f;
                const int centibel = gain <= 0.0f ? -10000 : static_cast<int>(std::round(2000.0 * std::log10(gain)));
                system::audio::set_vol(voice.handle, centibel);
                int panDs = static_cast<int>(em.pan);
                panDs = panDs < -100 ? -100 : (panDs > 100 ? 100 : panDs);
                system::audio::set_pan(voice.handle, panDs * 100);
            }

            // Observed playback state, written back for scripts to read. A
            // non-looping one-shot that ran to its end reports "finished".
            if (!voice.playing || voice.handle == 0)
                em.status = "stopped";
            else if (!system::audio::get_status(voice.handle))
                em.status = em.loop ? "stopped" : "finished";
            else
                em.status = "playing";
        });
    }

    void RenderSystem(AppContext& app)
    {
        // The legacy game owns the frame while its node is enabled, regardless
        // of module state (core.runGame() enables it to hand over the frame);
        // otherwise the scene tree (module) renders.
        const auto legacyEntity = app.scene.find<ecs::LegacyGameComponent>();
        const bool legacyEnabled
            = legacyEntity != ecs::NullEntity && app.scene.getComponent<ecs::LegacyGameComponent>(legacyEntity).enabled;

        if (app.closing())
        {
            // Release the scene-tree renderer's GPU resources before the
            // window/device teardown that follows in the closing pass. The
            // legacy renderer (marni) is torn down by system_window.
            if (!legacyEnabled)
            {
                // Destroy every TextureResource while the GPU is still alive.
                // This also makes any later Lua __gc release a safe no-op
                // (cookies are cleared, so release() returns early), avoiding
                // touching the sprite renderer after its static teardown.
                ResourceManager::get().releaseAll();
                render2d::SpriteRenderer::get().shutdown();
            }
            return;
        }
        if (!app.frameActive)
            return;

        if (legacyEnabled)
        {
            // Render the game into the rectangle described by the LegacyGame
            // node: world transform position (top-left) + size (width/height
            // in window pixels).
            const auto& legacy = app.scene.getComponent<ecs::LegacyGameComponent>(legacyEntity);
            const ecs::Mat4 world = ecs::worldMatrix(app.scene, legacyEntity);
            const int x = (int)world.m[3][0];
            const int y = (int)world.m[3][1];
            const int w = (int)legacy.size.width;
            const int h = (int)legacy.size.height;

            marni::g_renderer->setPresentRect(x, y, w, h);
            render_frame();
            marni::g_renderer->resetPresentRect();
            app.frameActive = false;
            return;
        }

        // Scene-tree rendering: the module's entities (SpriteComponent) are
        // drawn by the self-contained render2d renderer directly into the
        // window. No marni, no ordering tables, no guest framebuffer.
        std::vector<render2d::SpriteDraw> draws;

        // Painter's order follows the scene tree (pre-order DFS): a parent
        // draws behind its children and an earlier sibling behind a later
        // one. z remains a global priority override; equal-z entities fall
        // back to tree order, which fixes sibling layering that the old
        // ECS-creation-order tie-break got wrong.
        std::unordered_map<ecs::Entity, int> treeOrder;
        {
            int nextOrder = 0;
            std::function<void(ecs::Entity)> visit = [&](ecs::Entity e) {
                treeOrder[e] = nextOrder++;
                const auto* t = app.scene.tryGetComponent<ecs::TransformComponent>(e);
                if (t == nullptr)
                    return;
                for (ecs::Entity c = t->firstChild; c != ecs::NullEntity;
                     c = app.scene.getComponent<ecs::TransformComponent>(c).nextSibling)
                {
                    visit(c);
                }
            };
            visit(app.scene.root());
        }

        app.scene.each<ecs::SpriteComponent>([&](ecs::Entity entity, ecs::SpriteComponent& sprite) {
            const ecs::Mat4 world = ecs::worldMatrix(app.scene, entity);
            const float x = world.m[3][0];
            const float y = world.m[3][1];
            // Axis-aligned scale inherited from ancestor transforms (e.g. a
            // scaled parent). Extracted from the world matrix columns.
            const float sx
                = std::sqrt(world.m[0][0] * world.m[0][0] + world.m[1][0] * world.m[1][0] + world.m[2][0] * world.m[2][0]);
            const float sy
                = std::sqrt(world.m[0][1] * world.m[0][1] + world.m[1][1] * world.m[1][1] + world.m[2][1] * world.m[2][1]);

            // The draw size comes from the entity's UiSize component.
            const auto* uiSize = app.scene.tryGetComponent<ecs::UiSizeComponent>(entity);
            const float sizeW = uiSize != nullptr ? uiSize->width : 0.0f;
            const float sizeH = uiSize != nullptr ? uiSize->height : 0.0f;

            TextureResource* tex = nullptr;
            if (sprite.texture != 0)
            {
                tex = ResourceManager::get().fromCookie<TextureResource>(sprite.texture);
                if (tex == nullptr)
                    return; // texture released underneath us - skip
            }

            render2d::SpriteDraw d{};
            d.textureHandle = tex ? tex->textureHandle : 0;
            d.src = { sprite.src.x, sprite.src.y, sprite.src.width, sprite.src.height };
            d.dst = { x, y, sizeW * sx, sizeH * sy };
            d.flags = sprite.flags;
            d.r = sprite.color.r;
            d.g = sprite.color.g;
            d.b = sprite.color.b;
            d.a = sprite.color.a;
            // Draw priority comes from the entity's world transform z
            // (composed via worldMatrix); equal-z entities are ordered by
            // their pre-order scene-tree index (computed above).
            d.z = static_cast<int>(world.m[3][2]);
            d.order = treeOrder[entity];
            draws.push_back(d);
        });

        // Sprites draw back-to-front: higher z first (further back), and
        // equal z in scene-tree (pre-order DFS) order so a parent sits
        // behind its children and an earlier sibling behind a later one.
        std::stable_sort(draws.begin(), draws.end(), [](const render2d::SpriteDraw& a, const render2d::SpriteDraw& b) {
            if (a.z != b.z)
                return a.z > b.z;
            return a.order < b.order;
        });

        // Video entities: letterbox the decoded frame into the entity
        // bounds (preserving aspect ratio) and draw it like a texture.
        app.scene.each<ecs::VideoComponent>([&](ecs::Entity entity, ecs::VideoComponent& video) {
            if (video.status != ecs::VideoStatus::Playing || video.texture == 0)
                return;
            if (video.videoWidth <= 0 || video.videoHeight <= 0)
                return;

            const ecs::Mat4 world = ecs::worldMatrix(app.scene, entity);
            const float x = world.m[3][0];
            const float y = world.m[3][1];
            const float sx
                = std::sqrt(world.m[0][0] * world.m[0][0] + world.m[1][0] * world.m[1][0] + world.m[2][0] * world.m[2][0]);
            const float sy
                = std::sqrt(world.m[0][1] * world.m[0][1] + world.m[1][1] * world.m[1][1] + world.m[2][1] * world.m[2][1]);

            const auto* uiSize = app.scene.tryGetComponent<ecs::UiSizeComponent>(entity);
            const float boundsW = (uiSize != nullptr ? uiSize->width : 0.0f) * sx;
            const float boundsH = (uiSize != nullptr ? uiSize->height : 0.0f) * sy;
            const float vw = static_cast<float>(video.videoWidth);
            const float vh = static_cast<float>(video.videoHeight);
            const float scale = std::min(boundsW / vw, boundsH / vh);
            const float dw = vw * scale;
            const float dh = vh * scale;
            // The sprite is centred on its transform (see render2d), so
            // anchor the letterboxed video on the entity position.
            render2d::SpriteDraw d{};
            d.textureHandle = video.texture;
            d.src = { 0, 0, 0, 0 };
            d.dst = { x, y, dw, dh };
            d.flags = render2d::kStretchX | render2d::kStretchY;
            d.r = 255;
            d.g = 255;
            d.b = 255;
            d.a = 255;
            draws.push_back(d);
        });

        // Full-window fade overlay set this frame (Lua gfx.fade).
        const auto fade = render2d::SpriteRenderer::get().takeFade();
        if (fade.active && fade.a > 0)
        {
            const float w = (float)render2d::SpriteRenderer::get().screenWidth();
            const float h = (float)render2d::SpriteRenderer::get().screenHeight();
            render2d::SpriteDraw d{};
            d.textureHandle = 0;
            // Sprites are centred on their position (see render2d), so anchor
            // the full-window fade on the screen centre.
            d.dst = { w * 0.5f, h * 0.5f, w, h };
            d.r = fade.r;
            d.g = fade.g;
            d.b = fade.b;
            d.a = fade.a;
            draws.push_back(d);
        }

        render2d::SpriteRenderer::get().present(draws.data(), draws.size());
        app.frameActive = false;
    }
}