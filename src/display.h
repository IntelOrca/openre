#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openre::display
{
    // The scene render texture size for the current widescreen state (i.e.
    // the selected screen_resolution).
    struct Resolution
    {
        int32_t width = 0;
        int32_t height = 0;
    };

    // Number of resolutions in the current aspect-ratio list.
    int32_t resolution_count();

    // Resolution at the given index of the current aspect-ratio list.
    const Resolution& resolution_at(int32_t index);

    // "WxH" string for the given list index (what screen_resolution persists).
    std::string resolution_string(int32_t index);

    // Latest matching list index for a "WxH" value (snapped when absent).
    int32_t resolution_index(const std::string& wxh);

    // List index labels shown by the Screen Resolution option.
    std::vector<std::string> resolution_labels();

    // Monitor labels shown by the Output Display Settings option, rebuilt
    // from SDL each time the menu lists them.
    std::vector<std::string> monitor_labels();

    // Option change handlers: persist the [video] key and live-apply it.
    void set_fullscreen(int32_t value);
    void set_widescreen(int32_t value); // also snaps screen_resolution
    void set_monitor(int32_t value);    // 0-based display index
    void set_screen_resolution(int32_t index);

    // F8: cycles screen_resolution through the current aspect-ratio list,
    // persisting the [video] key and applying it. No-op while fullscreen.
    // Returns true when the resolution actually changed.
    bool cycle_resolution();

    // ALT+ENTER: toggles fullscreen, persisting the [video] key and applying
    // it. Returns true when the fullscreen state actually changed.
    bool toggle_fullscreen();

    // Applies the current [video] user settings to the window and the
    // renderer's scene texture.
    void apply();
}