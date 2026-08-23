#pragma once

#include "app.h"
#include "ecs.h"
#include "resmgr.h"

namespace openre
{
    // ECS system: advances every VideoComponent (decodes/forwards frames,
    // updates position/status, uploads to the render2d dynamic texture).
    void VideoSystem(AppContext& app);

    // Registers a movie as a streamed resource: the path is resolved and
    // validated but no video data is read; the decoder opens the file when
    // playback starts and reads it incrementally. Returns a ResourceCookie
    // holding one reference (0 on failure).
    ResourceCookie loadMovieResource(const std::string& path);

    // Creates the video entity + component for the streamed movie `movie` (a
    // cookie from loadMovieResource) and starts playback, filling the window.
    // Returns the entity or NullEntity on failure.
    ecs::Entity video_play(AppContext& app, ResourceCookie movie);

    // Stops playback for `entity`, releasing its decoder + texture. Idempotent.
    void video_stop(AppContext& app, ecs::Entity entity);
}
