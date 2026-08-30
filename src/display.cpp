#include "display.h"

#include "logger.h"
#include "options.h"
#include "render2d.h"
#include "system_config.h"
#include "system_window.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace openre::display
{
    namespace
    {
        // Canonical resolution lists. Sorted by height then width, static
        // regardless of the connected monitors.
        const std::vector<Resolution> kResolutions16x9 = {
            { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 },
        };
        const std::vector<Resolution> kResolutions4x3 = {
            { 1280, 960 },
            { 1600, 1200 },
            { 1920, 1440 },
            { 2560, 1920 },
        };

        const std::vector<Resolution>& list_for(bool widescreen)
        {
            return widescreen ? kResolutions16x9 : kResolutions4x3;
        }

        // Manhattan distance between two resolutions (width-heavy so a width
        // match is preferred when several entries tie).
        int64_t distance(const Resolution& a, const Resolution& b)
        {
            return std::llabs((int64_t)a.width - b.width) * 1000 + std::llabs((int64_t)a.height - b.height);
        }

        int32_t snap_index(const Resolution& value, bool widescreen)
        {
            const auto& list = list_for(widescreen);
            int64_t bestDistance = INT64_MAX;
            int32_t best = 0;
            for (size_t i = 0; i < list.size(); i++)
            {
                const auto d = distance(value, list[i]);
                if (d < bestDistance)
                {
                    bestDistance = d;
                    best = static_cast<int32_t>(i);
                }
            }
            return best;
        }
    }

    int32_t resolution_count()
    {
        return static_cast<int32_t>(list_for(system::config::get_widescreen() != 0).size());
    }

    const Resolution& resolution_at(int32_t index)
    {
        const auto& list = list_for(system::config::get_widescreen() != 0);
        index = std::clamp<int32_t>(index, 0, static_cast<int32_t>(list.size()) - 1);
        return list.at(index);
    }

    std::string resolution_string(int32_t index)
    {
        const auto& res = resolution_at(index);
        return std::to_string(res.width) + "x" + std::to_string(res.height);
    }

    int32_t resolution_index(const std::string& wxh)
    {
        int32_t width = 0;
        int32_t height = 0;
        if (std::sscanf(wxh.c_str(), "%dx%d", &width, &height) != 2)
            return 0;
        return snap_index({ width, height }, system::config::get_widescreen() != 0);
    }

    std::vector<std::string> resolution_labels()
    {
        const auto& list = list_for(system::config::get_widescreen() != 0);
        std::vector<std::string> labels;
        labels.reserve(list.size());
        for (const auto& res : list)
            labels.push_back(std::to_string(res.width) + "x" + std::to_string(res.height));
        return labels;
    }

    std::vector<std::string> monitor_labels()
    {
        std::vector<std::string> labels;
        const int count = system::window::display_count();
        labels.reserve(count);
        for (int i = 0; i < count; i++)
        {
            std::string name = system::window::display_name(i);
            if (name.empty())
                name = "Display " + std::to_string(i + 1);
            labels.push_back(std::to_string(i + 1) + ". " + name);
        }
        return labels;
    }

    void set_fullscreen(int32_t value)
    {
        system::config::set_fullscreen(value);
        apply();
    }

    void set_widescreen(int32_t value)
    {
        system::config::set_widescreen(value);
        // Snapping the current resolution onto the new aspect-ratio list keeps
        // the screen_resolution key in sync within the same flush.
        const auto current = system::config::get_screen_resolution();
        const auto snapped = snap_index({ current.width, current.height }, value != 0);
        const auto& res = list_for(value != 0).at(snapped);
        system::config::set_screen_resolution({ res.width, res.height });
        // Route through the options registry so the menu reflects the new
        // index in the aspect-specific list and flush() persists both keys.
        options::set("screen_resolution", snapped);
        apply();
    }

    void set_monitor(int32_t value)
    {
        // UI index is 0-based; [video] monitor is 1-based.
        system::config::set_monitor(value + 1);
        apply();
    }

    void set_screen_resolution(int32_t index)
    {
        const auto& list = list_for(system::config::get_widescreen() != 0);
        index = std::clamp<int32_t>(index, 0, static_cast<int32_t>(list.size()) - 1);
        const auto& res = list.at(index);
        system::config::set_screen_resolution({ res.width, res.height });
        apply();
    }

    // F8: cycles through the resolution list, persisting the [video] key and
    // applying it. While fullscreen the physical window size is decoupled
    // from screen_resolution, matching the original F8 behaviour.
    bool cycle_resolution()
    {
        if (system::config::get_fullscreen() != 0)
            return false;
        const int count = resolution_count();
        if (count < 2)
            return false;
        const auto current = system::config::get_screen_resolution();
        const int next = (snap_index({ current.width, current.height }, system::config::get_widescreen() != 0) + 1) % count;
        // Route through the options registry so the menu reflects the change
        // and flush() persists the [video] key. When the registry has not
        // been seeded (a legacy run without a game module never calls
        // options::init()), fall back to applying the config directly so F8
        // keeps working.
        if (options::set("screen_resolution", next))
        {
            options::flush();
            return true;
        }
        system::config::set_screen_resolution(
            { list_for(system::config::get_widescreen() != 0).at(next).width,
              list_for(system::config::get_widescreen() != 0).at(next).height });
        apply();
        return true;
    }

    // ALT+ENTER: flips the fullscreen state, persisting the [video] key and
    // applying it.
    bool toggle_fullscreen()
    {
        const int32_t value = system::config::get_fullscreen() == 0 ? 1 : 0;
        // See cycle_resolution(): fall back to applying directly when the
        // options registry is not seeded.
        if (options::set("fullscreen", value))
        {
            options::flush();
            return true;
        }
        system::config::set_fullscreen(value);
        apply();
        return true;
    }

    void apply()
    {
        const bool fullscreen = system::config::get_fullscreen() != 0;
        const bool widescreen = system::config::get_widescreen() != 0;

        const auto screenRes = system::config::get_screen_resolution();
        const auto index = snap_index({ screenRes.width, screenRes.height }, widescreen);
        const Resolution res = list_for(widescreen).at(index);

        int32_t monitor = system::config::get_monitor() - 1; // config is 1-based
        const int displayCount = system::window::display_count();
        if (monitor < 0 || monitor >= displayCount)
            monitor = 0;

        // The ECS scene canvas (and gfx.screenWidth/screenHeight) is the
        // screen_resolution; the present pass contain-fits it to the window.
        render2d::SpriteRenderer::get().setSceneSize(res.width, res.height);

        // Track the last applied windowed geometry so a windowed resolution or
        // monitor change recentres the window, while the very first apply
        // (boot) leaves the position to the OS. Leaving fullscreen always
        // recentres: SDL only restores the position recorded before fullscreen
        // was entered, so monitor/resolution changes made while fullscreen
        // would otherwise leave the windowed window wherever that was.
        static bool sHasApplied = false;
        static bool sLastFullscreen = false;
        static int32_t sLastMonitor = -1;
        static int32_t sLastWidth = -1;
        static int32_t sLastHeight = -1;
        const bool recentre = sHasApplied
            && (sLastFullscreen && !fullscreen || monitor != sLastMonitor || res.width != sLastWidth
                || res.height != sLastHeight);
        sLastFullscreen = fullscreen;
        sLastMonitor = monitor;
        sLastWidth = res.width;
        sLastHeight = res.height;
        sHasApplied = true;

        if (fullscreen)
        {
            // Borderless fullscreen on the chosen monitor: SDL covers the
            // display the window occupies, and a CENTRED position selects that
            // display (through the pending displayID) while keeping the frame
            // reachable if fullscreen ever fails or is exited.
            system::window::set_window_position_centered(monitor);
            system::window::set_fullscreen(true);
        }
        else
        {
            system::window::set_fullscreen(false);
            system::window::set_window_size(res.width, res.height);
            if (recentre)
            {
                // Resolution / monitor changed in windowed mode: pull the
                // window back to the centre of the chosen display so the title
                // bar stays draggable.
                system::window::set_window_position_centered(monitor);
            }
            // Boot and identical re-applies leave the position to the OS so the
            // title bar / borders stay reachable (SDL_SetWindowPosition moves
            // the client area, which pushes the frame off-screen at a corner).
        }

        logging::logInfo(
            "[video] apply: {} {}x{} monitor={}", fullscreen ? "fullscreen" : "windowed", res.width, res.height, monitor + 1);
    }
} // namespace display