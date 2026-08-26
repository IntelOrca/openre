#pragma once

#include "ecs.h"
#include "ecs_math.h"
#include "resmgr.h"

#include <cstdint>
#include <string>
#include <vector>

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

    /// Cross-axis placement of a child inside a UiFlex parent (the axis the
    /// parent does not flow along). `main` is controlled by `weight`.
    enum class UiSizeAlignment : uint32_t
    {
        Begin = 0,   // pin to the start of the cross axis
        Center = 1,  // centre on the cross axis
        End = 2,     // pin to the end of the cross axis
        Stretch = 3, // fill the cross axis
    };

    /// The visual size of an entity: the single source of truth for how much
    /// space a SpriteComponent draw, TextComponent alignment band or
    /// VideoComponent letterbox bounds occupy.
    ///
    /// `weight` is layout-only and ignored by rendering: inside a UiFlex
    /// parent it is the main-axis share (positive weight grabs a proportional
    /// slice of the leftover main-axis space). `alignment` is the cross-axis
    /// behaviour (left/centre/right/stretch) and applies to every child
    /// regardless of weight. When the parent has no flex (or flex `none`) the
    /// UiFlexSystem owns the size instead and `alignment` is unused.
    struct UiSizeComponent
    {
        float width = 0;
        float height = 0;
        float weight = 0;
        UiSizeAlignment alignment = UiSizeAlignment::Stretch;
    };

    /// Main-axis direction of a UiFlexComponent.
    enum class UiFlexOrientation : uint32_t
    {
        None = 0,       // no flow: each child fills or is centred independently
        Horizontal = 1, // children distributed left to right
        Vertical = 2,   // children distributed top to bottom
    };

    /// Declarative flow layout for an entity's immediate children
    /// (UiFlexSystem). The container's own UiSize must be authored; children
    /// are positioned/sized automatically and their transforms + UiSize are
    /// system-owned from the frame they are created.
    struct UiFlexComponent
    {
        UiFlexOrientation orientation = UiFlexOrientation::None;
        float gap = 0;     // space between consecutive children (flow modes)
        float padding = 0; // inset applied to every edge of the content box
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
    /// The sprite is drawn centred on the entity's world transform position
    /// (see render2d); the UiSizeComponent width/height give the destination
    /// size in window pixels. `texture` is a ResourceCookie into the engine
    /// ResourceManager; `src` selects the region of the texture to draw
    /// (0-sized src means the full texture). `flags` controls whether each
    /// axis tiles (`Repeat*`) or scales the source to the destination
    /// (`Stretch*`). `color` tints the sprite (defaults to opaque white).
    struct SpriteComponent
    {
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
    /// The emitter always plays the sound resource referenced by `sound`, a
    /// Sound cookie from audio.loadAudio() (small sources are resident PCM,
    /// large ones stream from disk). Two playback controls coexist on the one
    /// voice:
    ///
    /// State mode: playback is edge-triggered through `state` — setting it to
    /// "playing" starts the resource (honouring `loop`), changing it to
    /// anything else stops the voice. Suited to music and narration.
    ///
    /// Trigger mode: writing any new value to `trigger` plays the resource
    /// once from its first sample regardless of `state`, making it suitable
    /// for rapid-fire UI sound effects.
    ///
    /// `volume` (0-100), `pan` (-100..100), `fadeFrames` / `fadeTargetVolume`
    /// are pushed to the voice every frame while playing; a fade runs once per
    /// distinct fadeFrames value. The system writes the observed playback
    /// state back into `status` ("stopped", "playing" or "finished").
    /// `category` groups emitters for future mixer configuration ("bgm",
    /// "sfx", "voice"; all full volume for now). Scripts must keep the Sound
    /// handle alive while `sound` points at it — components only borrow its
    /// cookie.
    struct AudioEmitterComponent
    {
        std::string category = "sfx";
        std::string state = "stopped";
        float volume = 100.0f;
        float pan = 0.0f;
        bool loop = true;
        std::string status = "stopped";
        int fadeFrames = 0;
        float fadeTargetVolume = 100.0f;
        uint32_t sound = 0; // Sound cookie from audio.loadAudio(); 0 = silent
        int32_t trigger = 0;
    };

    /// Fit mode of a ScaledFrameComponent.
    enum class ScaledFrameMode : uint32_t
    {
        Contain = 0, // uniform scale so the whole source fits inside the target (letterbox)
        Cover = 1,   // uniform scale so the source covers the whole target (crops overflow)
    };

    /// Declarative fit-scaling: maps content authored at `source` width/height
    /// onto a `target` width/height area (typically the window) by writing a
    /// uniform scale to the entity's transform every frame (ScaledFrameSystem).
    ///
    /// `mode` picks contain (fit inside, may letterbox) or cover (fill, may
    /// crop). `increment` quantises the scale: 0 uses the exact fit factor,
    /// while e.g. 0.5 snaps the scale down to whole 0.5 steps so UI art only
    /// ever renders at crisp zoom levels (0.5, 1.0, 1.5, ...). The entity's
    /// position is left alone - callers place it (e.g. at the window centre).
    struct ScaledFrameComponent
    {
        Size2f source;
        Size2f target;
        ScaledFrameMode mode = ScaledFrameMode::Contain;
        float increment = 0.0f;
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
    /// relayouts only when the fingerprinted inputs change. The alignment
    /// bounds are the entity's UiSizeComponent.
    struct TextComponent
    {
        std::string text;        // UTF-8; supports <span color="#rrggbbaa"> markup
        uint32_t flags = 0;      // TextFlags alignment bits
        ResourceCookie font = 0; // FontResource cookie from gfx.loadFont

        // Mirror of the inputs used for relayout detection (filled by the
        // TextLayoutSystem; not set directly).
        std::string layoutFingerprint;
        bool needsLayout = true;

        // The per-glyph child entities produced by the last layout, in draw
        // order. Managed entirely by the TextLayoutSystem: it reuses these
        // entities in place when the glyph count matches and only
        // creates/destroys the difference.
        std::vector<Entity> glyphs;
    };

    /// Playback state of a VideoComponent.
    enum class VideoStatus : uint32_t
    {
        Stopped = 0,
        Playing = 1,
    };

    /// A video clip drawn into the window by the scene-tree render path.
    ///
    /// `movie` is a MovieResource cookie from core.loadMovie; the movie file
    /// itself is streamed from disk while playing. The entity's UiSizeComponent
    /// bounds the draw area in window pixels (top-left at the world transform
    /// position); the decoded video is letterboxed inside these bounds while
    /// preserving its aspect ratio. `status` and `position` (playback time in
    /// seconds) are advanced by the VideoSystem. `texture` is the render2d
    /// dynamic-texture handle for the latest decoded frame (0 when none), and
    /// `videoWidth`/`videoHeight` are the native decoded frame size used to
    /// compute the aspect-preserving draw rectangle.
    struct VideoComponent
    {
        ResourceCookie movie = 0;
        VideoStatus status = VideoStatus::Stopped;
        float position = 0.0f;
        int texture = 0;
        int videoWidth = 0;
        int videoHeight = 0;
    };

    /// Input device family an input hint's icon is drawn from.
    enum class InputHintDevice : uint32_t
    {
        KeyboardMouse = 0, // keyboard + mouse atlas ("kbm")
        PlayStation,       // DualShock / DualSense icons ("ps")
        Xbox,              // Xbox One / 360 pad icons ("x")
        Nintendo,          // Switch Pro / Joy-Con icons ("n")
    };

    /// Declarative input hint: the icon(s) for `command`'s current bindings
    /// on `device`, with an optional `text` label drawn to their right (e.g.
    /// command "pause" on kbm -> [P] Pause). A registered command renders
    /// one 32x32 icon per binding it has on the device; unregistered names
    /// fall back to direct key/alias lookups. Icons are centred on the
    /// entity's origin and continue rightwards; scale the entity's transform
    /// to resize the whole hint (icons and text scale together).
    ///
    /// An InputHintSystem materialises the icon and label as child entities
    /// beneath this one and relayouts only when the fingerprinted inputs
    /// change. Icons come from the shared atlases in the core asset dir:
    /// texture/input_kbm.bmp (keyboard + mouse) and texture/input_pad.bmp
    /// (PlayStation / Xbox / Nintendo).
    struct InputHintComponent
    {
        std::string command; // e.g. "accept", "dpad_up" or a single key "p"
        std::string text;    // optional label drawn to the right of the icon
        InputHintDevice device = InputHintDevice::KeyboardMouse;

        // Font used for `text` (FontResource cookie from gfx.loadFont).
        // 0 selects the engine default (fonts/std.fnt from the core dir).
        ResourceCookie font = 0;

        // Mirror of the inputs used for relayout detection (filled by the
        // InputHintSystem; not set directly).
        std::string fingerprint;

        // The child entities created by the last layout (NullEntity when the
        // hint has no icon/label). A command with several bindings on the
        // hint's device renders one icon child per binding. Owned here so
        // they can be rebuilt in place.
        std::vector<Entity> icons;
        Entity labelEntity = NullEntity;
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