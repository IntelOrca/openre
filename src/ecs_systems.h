#pragma once

#include "ecs_scene.h"

namespace openre::ecs
{
    // Boots the scene tree: creates a single LegacyGame node under the hidden
    // root, sized to the configured window size.
    void system_boot_scene(Scene& scene);

    // Computes the world matrix of `entity` by composing the transforms from
    // the scene root down to `entity`.
    Mat4 worldMatrix(Scene& scene, Entity entity);
}