#pragma once

#include "app.h"

namespace openre
{
    // A single step of the app shell: initialise on first call, run every
    // frame while running, and tear down on the closing pass (app.closing()).
    using SystemFn = void (*)(AppContext&);

    // Window + input systems (implemented in system_window.cpp / system_input.cpp).
    void system_window(AppContext& app);
    void system_input(AppContext& app);

    // ECS systems (implemented in ecs_systems.cpp).
    void LegacyGameSystem(AppContext& app);
    void RenderSystem(AppContext& app);

    // Sets the frame-pacing baseline used by the LegacyGameSystem throttle.
    void system_set_frame_time_baseline(uint32_t lastFrameTime);
}