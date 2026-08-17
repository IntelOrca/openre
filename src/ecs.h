#pragma once

#include <cstdint>

namespace openre::ecs
{
    /// A handle to an entity in the scene.
    ///
    /// An entity is a 64-bit handle: the low 32 bits are the entity index and
    /// the high 32 bits are a generation counter. The generation lets us
    /// detect stale handles - a handle to a destroyed entity never aliases a
    /// recycled one. Zero is the null entity.
    using Entity = uint64_t;

    inline constexpr Entity NullEntity = 0;

    /// Index of the entity slot (low 32 bits of the handle).
    constexpr uint32_t entity_index(Entity entity)
    {
        return (uint32_t)entity;
    }

    /// Generation of the handle (high 32 bits).
    constexpr uint32_t entity_generation(Entity entity)
    {
        return (uint32_t)(entity >> 32);
    }

    /// Packs an index + generation into a handle.
    constexpr Entity make_entity(uint32_t index, uint32_t generation)
    {
        return ((Entity)generation << 32) | index;
    }

    /// RGBA colour, one byte per channel.
    struct Color
    {
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        uint8_t a = 255;
    };
}