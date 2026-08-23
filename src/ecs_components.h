#pragma once

#include "ecs.h"
#include "ecs_math.h"
#include "resmgr.h"

#include <cstdint>
#include <string>

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

        // Optional human-readable name ("" by default). Lua can get/set it via
        // the transform component; the debug web UI shows it in place of the
        // raw entity id.
        std::string name;

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

    /// An RGBA colour in the 0-255 range. (ecs::Color is defined in ecs.h.)

    /// A 2D image drawn into the window by the scene-tree render path.
    ///
    /// The entity's world transform position is the top-left corner and
    /// `size` is the destination width/height in window pixels. `texture`
    /// is a ResourceCookie into the engine ResourceManager; `src` selects the
    /// region of the texture to draw (0-sized src means the full texture).
    /// `flags` controls whether each axis tiles (`Repeat*`) or scales the
    /// source to the destination (`Stretch*`). `color` tints the sprite
    /// (defaults to opaque white).
    struct SpriteComponent
    {
        Size2f size;
        ResourceCookie texture = 0;
        Rect2f src;
        uint32_t flags = static_cast<uint32_t>(SpriteFlags::StretchX) | static_cast<uint32_t>(SpriteFlags::StretchY);
        Color color{ 255, 255, 255, 255 };
        // Draw priority: sprites are rendered back-to-front grouped by this
        // value. Higher z sorts earlier (further back); sprites with equal z
        // keep their creation order. The scene renderer draws the whole list
        // in array order, so late array entries end up on top.
        int z = 0;
    };

    /// A sound emitter mixed by the native SDL3 audio layer (AudioSystem).
    ///
    /// Playback is edge-triggered through `state`: setting it to "playing"
    /// starts the source, changing it to anything else stops the voice.
    /// `source` is a `.sap` file (RIFF WAV container) resolved relative to the
    /// game module directory. `volume` (0-100), `pan` (-100..100),
    /// `fadeFrames` / `fadeTargetVolume` are pushed to the voice every frame
    /// while playing; a fade runs once per distinct fadeFrames value. The
    /// system writes the observed playback state back into `status`
    /// ("stopped", "playing" or "finished"). `category` groups emitters for
    /// future mixer configuration ("bgm", "sfx", "voice"; all full volume for
    /// now).
    struct AudioEmitterComponent
    {
        std::string category = "sfx";
        std::string state = "stopped";
        std::string source;
        float volume = 100.0f;
        float pan = 0.0f;
        bool loop = true;
        std::string status = "stopped";
        int fadeFrames = 0;
        float fadeTargetVolume = 100.0f;
    };

    /// Horizontal alignment of a TextComponent within its size bounds.
    enum class TextAlignX : uint32_t
    {
        Left = 0,
        Center = 1,
        Right = 2,
    };

    /// Vertical alignment of a TextComponent within its size bounds.
    enum class TextAlignY : uint32_t
    {
        Top = 0,
        Middle = 1,
        Bottom = 2,
    };

    /// Alignment bits for a TextComponent (2 bits X, 2 bits Y, composable
    /// with bitwise OR). Stored in the low nibble of the TextComponent flags.
    enum class TextFlags : uint32_t
    {
        XLeft = static_cast<uint32_t>(TextAlignX::Left) << 0,
        XCenter = static_cast<uint32_t>(TextAlignX::Center) << 0,
        XRight = static_cast<uint32_t>(TextAlignX::Right) << 0,
        XMask = 0b11u << 0,
        YTop = static_cast<uint32_t>(TextAlignY::Top) << 2,
        YMiddle = static_cast<uint32_t>(TextAlignY::Middle) << 2,
        YBottom = static_cast<uint32_t>(TextAlignY::Bottom) << 2,
        YMask = 0b11u << 2,
    };

    /// Declarative text: which string to draw, in what bounds, with what
    /// alignment and font. A TextLayoutSystem materialises the glyphs as
    /// child SpriteComponents (one per character) beneath this entity, and
    /// relayouts only when the fingerprinted inputs change.
    struct TextComponent
    {
        std::string text;        // UTF-8; supports <span color="#rrggbb"> markup
        Size2f size;             // bounds within which the text is aligned
        uint32_t flags = 0;      // TextFlags alignment bits
        ResourceCookie font = 0; // FontResource cookie from gfx.loadFont

        // Mirror of the inputs used for relayout detection (filled by the
        // TextLayoutSystem; not set directly).
        std::string layoutFingerprint;
        bool needsLayout = true;

        // Owns the glyph child entities produced by the last layout. The
        // layout system creates/clears this container only, leaving any
        // other user-attached children of the text entity untouched.
        Entity layoutContainer = NullEntity;
    };

    /// Playback state of a VideoComponent.
    enum class VideoStatus : uint32_t
    {
        Stopped = 0,
        Playing = 1,
    };

    /// A video clip drawn into the window by the scene-tree render path.
    ///
    /// `resource` is the source movie path (set by core.playMovie). `size` is
    /// the entity bounds in window pixels (top-left at the world transform
    /// position); the decoded video is letterboxed inside these bounds while
    /// preserving its aspect ratio. `status` and `position` (playback time in
    /// seconds) are advanced by the VideoSystem. `texture` is the render2d
    /// dynamic-texture handle for the latest decoded frame (0 when none), and
    /// `videoWidth`/`videoHeight` are the native decoded frame size used to
    /// compute the aspect-preserving draw rectangle.
    struct VideoComponent
    {
        std::string resource;
        Size2f size;
        VideoStatus status = VideoStatus::Stopped;
        float position = 0.0f;
        int texture = 0;
        int videoWidth = 0;
        int videoHeight = 0;
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