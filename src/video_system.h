#pragma once

#include "app.h"
#include "ecs.h"

namespace openre
{
    // ECS system: advances every VideoComponent (decodes/forwards frames,
    // updates position/status, uploads to the render2d dynamic texture).
    void VideoSystem(AppContext& app);

    // Creates the video entity + component for `path` and starts playback,
    // filling the window. Returns the entity or NullEntity on failure.
    ecs::Entity video_play(AppContext& app, const std::string& path);

    // Stops playback for `entity`, releasing its decoder + texture. Idempotent.
    void video_stop(AppContext& app, ecs::Entity entity);
}
