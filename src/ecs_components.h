#pragma once

#include "ecs.h"
#include "ecs_math.h"
#include "resmgr.h"

#include <cstdint>

namespace openre::ecs
{
    /// A node in the scene tree.
    ///
    /// Every entity created through Scene::createEntity() carries exactly one
    /// of these; it links the entity into its parent's child list and holds
    /// its local transform (relative to the parent). The world transform is
    /// the chain of parent transforms up to the scene root.
    struct TransformComponent
    {
        Entity parent = NullEntity;
        Entity firstChild = NullEntity;
        Entity nextSibling = NullEntity;

        Vec3 position;
        Quat rotation;
        Vec3 scale{ 1.0f, 1.0f, 1.0f };
    };

    /// A rectangular extent measured in screen pixels (0, 0 is the top-left
    /// of the window).
    struct Size2f
    {
        float width = 0;
        float height = 0;
    };

    /// An axis-aligned rectangle with a top-left origin.
    struct Rect2f
    {
        float x = 0;
        float y = 0;
        float width = 0;
        float height = 0;
    };

    /// Per-axis draw behaviour of a SpriteComponent.
    enum class SpriteFlags : uint32_t
    {
        RepeatX = 1 << 0,
        RepeatY = 1 << 1,
        StretchX = 1 << 2,
        StretchY = 1 << 3,
    };

    /// A 2D image drawn into the window by the scene-tree render path.
    ///
    /// The entity's world transform position is the top-left corner and
    /// `size` is the destination width/height in window pixels. `texture`
    /// is a ResourceCookie into the engine ResourceManager; `src` selects the
    /// region of the texture to draw (0-sized src means the full texture).
    /// `flags` controls whether each axis tiles (`Repeat*`) or scales the
    /// source to the destination (`Stretch*`).
    struct SpriteComponent
    {
        Size2f size;
        ResourceCookie texture = 0;
        Rect2f src;
        uint32_t flags = static_cast<uint32_t>(SpriteFlags::StretchX) | static_cast<uint32_t>(SpriteFlags::StretchY);
    };

    /// Marks the entity as the "legacy game": the game driven by the original
    /// RE2 loop (psx_main). The LegacyGameSystem updates it and the
    /// RenderSystem presents it, both only while `enabled` is true. Its world
    /// transform position is the top-left corner of the render area and its
    /// `size` is its width/height in window pixels.
    struct LegacyGameComponent
    {
        bool enabled = true;
        Size2f size;
    };
}