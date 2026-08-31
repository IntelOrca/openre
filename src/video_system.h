#pragma once

#include "app.h"
#include "ecs.h"
#include "resmgr.h"

namespace openre
{
    // ECS system: advances every VideoComponent (decodes/forwards frames,
    // updates position/status, uploads to the render2d dynamic texture).
    void VideoSystem(AppContext& app);
}
