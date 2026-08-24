#include "ecs_systems.h"

#include "app.h"
#include "ecs_components.h"
#include "font.h"
#include "game_module.h"
#include "input.h"
#include "logger.h"
#include "marni_renderer.h"
#include "openre.h"
#include "options.h"
#include "render2d.h"
#include "resmgr.h"
#include "script.h"
#include "system_audio.h"
#include "system_config.h"
#include "system_filesystem.h"
#include "system_window.h"
#include "text_layout.h"
#include "texture_loader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openre
{
    namespace
    {
        // Frame-pacing baseline (ms) used by the LegacyGameSystem throttle.
        // Mirrors the original win_main loop's lastFrameTime variable.
        uint32_t s_lastFrameTime = 0;
    }

    void system_set_frame_time_baseline(uint32_t lastFrameTime)
    {
        s_lastFrameTime = lastFrameTime;
    }
}

namespace openre::ecs
{
    void system_boot_scene(Scene& scene)
    {
        // With a game module active the module builds its own entities; there
        // is no legacy game node and the legacy systems are skipped entirely.
        if (game_module_active())
            return;

        // The hidden root already exists (Scene ctor). Add one child under the
        // "game" sub-root that carries the LegacyGameComponent, sized to the
        // configured window.
        Entity entity = scene.createEntity(scene.getRoot(Scene::RootKind::Game));
        auto& legacy = scene.addComponent<LegacyGameComponent>(entity);
        legacy.enabled = true;
        const auto windowSize = system::config::get_window_size();
        legacy.size = { (float)windowSize.width, (float)windowSize.height };
    }

    Mat4 worldMatrix(Scene& scene, Entity entity)
    {
        // Chain the transforms from the root down to the entity.
        std::vector<Entity> chain;
        for (Entity e = entity; e != NullEntity; e = scene.getComponent<TransformComponent>(e).parent)
        {
            chain.push_back(e);
        }

        Mat4 world = Mat4::identity();
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        {
            const auto& t = scene.getComponent<TransformComponent>(*it);
            // Prepend so the entity's own transform is applied innermost; a
            // parent's scale then correctly scales child offsets and sizes.
            // (Row-vector convention: p_world = p_local * world.)
            world = trs(t.position, t.rotation, t.scale) * world;
        }
        return world;
    }
}

namespace openre
{
    void LegacyGameSystem(AppContext& app)
    {
        if (app.closing() || !app.running())
            return;

        // A game module drives the frame instead of the legacy game.
        if (game_module_active())
            return;

        // Mirror the OG loop's window-inactive sleep: the systems keep
        // running (pumping events) but the game does not advance.
        if (!gGameTable.window_active)
            return;

        // Frame rate throttle, identical to the original win_main loop.
        constexpr uint32_t frameRateTable[4] = { 166, 333, 666, 166 };
        auto now = system::window::get_ticks();
        auto budget = frameRateTable[gGameTable.vsync_rate / 2];
        auto elapsed = 10 * (now - s_lastFrameTime);
        if (elapsed < budget)
        {
            if (budget - elapsed >= 30) // >= 3ms margin
                system::window::delay(1);
            return;
        }

        auto legacyEntity = app.scene.find<ecs::LegacyGameComponent>();
        if (legacyEntity == ecs::NullEntity)
            return;

        const auto& legacy = app.scene.getComponent<ecs::LegacyGameComponent>(legacyEntity);
        if (legacy.enabled)
        {
            // Special states (movie / reset) consume the frame, but we still
            // tick scripts so the web inspector server keeps responding while
            // a movie plays (otherwise HTTP requests hang until it finishes).
            if (handle_special_states())
            {
                openre::script::tick();
                return;
            }

            update_game();
        }

        // Keep OG-visible timer fields updated for other hooks.
        gGameTable.timer_last = now;
        gGameTable.timer_current = now;
        gGameTable.timer_10 = 10 * now;
        s_lastFrameTime = now;

        // A full game frame advanced; the render system presents it this frame.
        app.frameActive = true;
    }

    void ModuleSystem(AppContext& app)
    {
        if (app.closing() || !app.running())
            return;

        // Only active when a game module is loaded; otherwise the legacy game
        // (LegacyGameSystem) drives the frame.
        if (!game_module_active())
            return;

        // Mirror the OG loop's window-inactive sleep.
        if (!gGameTable.window_active)
            return;

        // Load the module's main.lua on the first pass (before the throttle
        // check so the first paced frame is usable).
        static bool s_moduleLoaded = false;
        if (!s_moduleLoaded)
        {
            s_moduleLoaded = true;
            // The legacy psx_main() boot calls load_bindings(); module mode
            // skips that boot, so populate the command bindings here or no
            // command (and thus no input) is ever detected for scripts.
            openre::input::load_bindings();
            // Same story for the options registry: seed it and load persisted
            // values before the module's scripts can read or write options.
            openre::options::init();
            script::loadGameModule(game_module_dir());
        }

        // Frame rate throttle, identical to the original win_main loop (and
        // shared with the LegacyGameSystem baseline so modes never drift).
        constexpr uint32_t frameRateTable[4] = { 166, 333, 666, 166 };
        auto now = system::window::get_ticks();
        auto budget = frameRateTable[gGameTable.vsync_rate / 2];
        auto elapsed = 10 * (now - s_lastFrameTime);
        if (elapsed < budget)
        {
            if (budget - elapsed >= 30) // >= 3ms margin
                system::window::delay(1);
            return;
        }

        // Refresh command state before scripts read it (the legacy game loop
        // calls pad_set; module mode has no legacy loop, so do it here).
        openre::input::pad_set();

        // Tick every loaded Lua script (the module VM included).
        openre::script::tick();

        // Keep OG-visible timer fields updated for other hooks.
        gGameTable.timer_last = now;
        gGameTable.timer_current = now;
        gGameTable.timer_10 = 10 * now;
        s_lastFrameTime = now;

        // A full module frame advanced; the render system presents it.
        app.frameActive = true;
    }

    // ── Input hints ─────────────────────────────────────────────────────
    //
    // The button atlas (texture/input_keys.bmp) is a grid of 16x16 keycap
    // cells: row 0 holds A-P, row 1 Q-Z then 0-5, row 2 6-9 plus the special
    // keys and arrows, row 3 the mouse buttons. Gamepad themes will extend
    // the atlas once input device detection exists.
    constexpr float kInputKeyCell = 16.0f;
    constexpr int kInputKeyCellsPerRow = 16;
    // Space between the key icon's right edge and where the label starts.
    constexpr float kHintGap = 4.0f;

    enum InputKeyCells
    {
        kCellKeyA = 0,
        kCellDigit0 = 26,
        kCellEsc = 36,
        kCellTab = 37,
        kCellCtrl = 38,
        kCellAlt = 39,
        kCellShift = 40,
        kCellEnter = 41,
        kCellSpace = 42,
        kCellUp = 43,
        kCellDown = 44,
        kCellLeft = 45,
        kCellRight = 46,
        kCellMouseLeft = 48,
        kCellMouseMiddle = 49,
        kCellMouseRight = 50,
    };

    struct InputCommandKey
    {
        const char* command;
        int cell;
    };

    // Keyboard bindings for named commands. A command that is a single letter
    // or digit ("p") resolves to its own key cell without an entry here.
    constexpr InputCommandKey kKeyboardCommands[] = {
        { "accept", kCellEnter },
        { "cancel", kCellEsc },
        { "pause", kCellKeyA + ('P' - 'A') },
        { "inventory", kCellKeyA + ('I' - 'A') },
        { "map", kCellKeyA + ('M' - 'A') },
        { "run", kCellShift },
        { "ready", kCellSpace },
    };

    bool lookupInputKeyCell(const std::string& command, int& outCell)
    {
        if (command.size() == 1)
        {
            const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(command[0])));
            if (c >= 'A' && c <= 'Z')
            {
                outCell = kCellKeyA + (c - 'A');
                return true;
            }
            if (c >= '0' && c <= '9')
            {
                outCell = kCellDigit0 + (c - '0');
                return true;
            }
            return false;
        }
        for (const auto& entry : kKeyboardCommands)
        {
            if (command == entry.command)
            {
                outCell = entry.cell;
                return true;
            }
        }
        return false;
    }

    // Engine-owned shared resources for hint icons/labels. The cookies are
    // held forever so the atlas/font outlives every component that borrows
    // them; both loaders cache the decoded file on disk, not in the manager.
    ResourceCookie inputHintAtlas()
    {
        static ResourceCookie cookie = loadTextureResource("texture/input_keys", 0, 0);
        return cookie;
    }

    ResourceCookie inputHintFont()
    {
        static ResourceCookie cookie = loadFontResource("fonts/std.fnt");
        return cookie;
    }

    // Materialises InputHintComponents into child entities: a 16x16 key icon
    // sprite centred on the entity origin and the optional text label to its
    // right, centred on its own position. Runs before TextLayoutSystem so
    // freshly created labels get laid out in the same frame; only rebuilds
    // when the fingerprinted inputs change. Scale the hint entity's transform
    // to resize the whole hint.
    void InputHintSystem(AppContext& app)
    {
        if (app.closing() || !app.running())
            return;
        if (!game_module_active())
            return;
        if (!app.frameActive)
            return;

        app.scene.each<ecs::InputHintComponent>([&](ecs::Entity entity, ecs::InputHintComponent& hint) {
            std::string fingerprint = hint.command + "|" + hint.text;
            const bool entitiesValid = (hint.iconEntity == ecs::NullEntity || app.scene.isValid(hint.iconEntity))
                && (hint.labelEntity == ecs::NullEntity || app.scene.isValid(hint.labelEntity));
            if (fingerprint == hint.fingerprint && entitiesValid)
                return;
            hint.fingerprint = fingerprint;

            // Rebuild from scratch: drop the previous children first.
            for (ecs::Entity child : { hint.iconEntity, hint.labelEntity })
            {
                if (child != ecs::NullEntity && app.scene.isValid(child))
                    app.scene.destroyEntity(child);
            }
            hint.iconEntity = ecs::NullEntity;
            hint.labelEntity = ecs::NullEntity;

            // Resolve the key glyph before touching the scene so an unknown
            // command simply drops the icon instead of corrupting the layout.
            int cell = -1;
            const bool hasIcon = lookupInputKeyCell(hint.command, cell);
            const ResourceCookie atlas = inputHintAtlas();

            float nextX = 0.0f; // left edge of the content after the icon
            if (hasIcon && atlas != 0)
            {
                ecs::Entity icon = app.scene.createEntity(entity);
                hint.iconEntity = icon;
                auto& transform = app.scene.getComponent<ecs::TransformComponent>(icon);
                transform.position = { 0.0f, 0.0f, 0.0f }; // sprites draw centred on their position

                auto& sprite = app.scene.addComponent<ecs::SpriteComponent>(icon);
                sprite.size = { kInputKeyCell, kInputKeyCell };
                sprite.texture = atlas;
                sprite.src = {
                    static_cast<float>(cell % kInputKeyCellsPerRow) * kInputKeyCell,
                    static_cast<float>(cell / kInputKeyCellsPerRow) * kInputKeyCell,
                    kInputKeyCell,
                    kInputKeyCell,
                };
                nextX = kInputKeyCell * 0.5f + kHintGap;
            }

            if (hint.text.empty())
                return;

            // Measure the label so it can be centred on its own position,
            // keeping glyph offsets small relative to the text entity.
            auto* fontResource = ResourceManager::get().fromCookie<FontResource>(inputHintFont());
            const float lineHeight = fontResource != nullptr && fontResource->fromFnt
                ? static_cast<float>(fontResource->lineHeight())
                : kInputKeyCell;
            const float textWidth = fontResource != nullptr
                ? text::measureWidth(text::decodeText(hint.text, fontResource, lineHeight, lineHeight))
                : 0.0f;

            ecs::Entity label = app.scene.createEntity(entity);
            hint.labelEntity = label;
            auto& transform = app.scene.getComponent<ecs::TransformComponent>(label);
            // The word is centred on the label position: place that centre a
            // half-width right of where the first glyph should start.
            transform.position = { nextX + textWidth * 0.5f, 0.0f, 0.0f };

            auto& text = app.scene.addComponent<ecs::TextComponent>(label);
            text.text = hint.text;
            text.size = { std::max(textWidth, 1.0f), lineHeight };
            text.flags = static_cast<uint32_t>(ecs::TextFlags::XCenter) | static_cast<uint32_t>(ecs::TextFlags::YMiddle);
            text.font = inputHintFont();
        });
    }

    // Materialises TextComponents into per-glyph child SpriteComponents. Runs
    // before the render pass each frame. Only rebuilds when the text inputs
    // (string/flags/font/bounds) change, so an unchanged frame is cheap.
    void TextLayoutSystem(AppContext& app)
    {
        if (app.closing() || !app.running())
            return;
        if (!game_module_active())
            return;
        if (!app.frameActive)
            return;

        app.scene.each<ecs::TextComponent>([&](ecs::Entity entity, ecs::TextComponent& text) {
            std::string fingerprint;
            text::fingerprint(text, fingerprint);
            if (fingerprint == text.layoutFingerprint && !text.needsLayout)
                return;
            text::layoutEntity(app.scene, entity, text);
            text.layoutFingerprint = fingerprint;
            text.needsLayout = false;
        });
    }

    // Applies every ScaledFrameComponent: computes the uniform scale that maps
    // the component's source size onto its target size (contain = fit inside,
    // cover = fill), optionally snapped down to `increment` steps, then writes
    // it to the entity's transform scale. The position is untouched - callers
    // place the entity (e.g. at the window centre). Runs after the script tick
    // so Lua-written inputs are honoured, before rendering reads transforms.
    void ScaledFrameSystem(AppContext& app)
    {
        if (app.closing() || !app.running())
            return;
        if (!game_module_active())
            return;
        if (!app.frameActive)
            return;

        app.scene.each<ecs::ScaledFrameComponent>([&](ecs::Entity entity, ecs::ScaledFrameComponent& frame) {
            const float sw = frame.source.width;
            const float sh = frame.source.height;
            const float tw = frame.target.width;
            const float th = frame.target.height;
            if (sw <= 0 || sh <= 0 || tw <= 0 || th <= 0)
                return;

            const bool cover = frame.mode == ecs::ScaledFrameMode::Cover;
            float scale = cover ? std::max(tw / sw, th / sh) : std::min(tw / sw, th / sh);
            if (frame.increment > 0)
            {
                // Snap DOWN to whole increments so the content never exceeds
                // the target; never go below one increment.
                scale = std::floor(scale / frame.increment + 1e-4f) * frame.increment;
                scale = std::max(scale, frame.increment);
            }
            auto& transform = app.scene.getComponent<ecs::TransformComponent>(entity);
            transform.scale.x = scale;
            transform.scale.y = scale;
        });
    }

    namespace
    {
        // Per-emitter mixer state mirrored from AudioEmitterComponent.
        struct AudioVoiceState
        {
            uint32_t handle = 0;            // system::audio voice (0 = none loaded)
            uint32_t attached = 0;          // Sound cookie the voice is bound to
            bool playing = false;           // last frame's want-to-play edge state
            bool stateDriven = false;       // playback started via state edge (vs trigger)
            bool failed = false;            // resource failed to attach (no retry until re-trigger)
            float fadeStartVolume = 100.0f; // effective volume the fade started from
            int fadeFramesLeft = 0;         // frames remaining in the active fade
            float fadeTotal = 0.0f;         // fade length in frames
            int fadeArmed = -1;             // fadeFrames value the current fade was armed for
            int32_t lastTrigger = 0;        // trigger value already fired (trigger mode)
        };
    }

    // Native audio bring-up for module mode: mirrors every
    // ecs::AudioEmitterComponent onto a voice of the SDL3 software mixer.
    // The emitter plays the sound resource referenced by its `sound` cookie
    // (small sources resident, large ones streamed). `state` edges start/stop
    // playback honouring `loop`; a new `trigger` value fires a one-shot from
    // the first sample regardless of state. Volume/pan are pushed each frame
    // while playing and the observed playback state is written back into
    // `status`.
    void AudioSystem(AppContext& app)
    {
        auto* scene = openre::current_scene();
        if (scene == nullptr || !game_module_active())
            return;

        static std::unordered_map<ecs::Entity, AudioVoiceState> s_voices;

        // Retire voices whose emitter entity/component is gone so destroyed
        // entities do not keep sounding.
        for (auto it = s_voices.begin(); it != s_voices.end();)
        {
            if (!scene->hasComponent<ecs::AudioEmitterComponent>(it->first))
            {
                if (it->second.handle != 0 && it->second.playing)
                    system::audio::stop(it->second.handle);
                it = s_voices.erase(it);
            }
            else
                ++it;
        }

        app.scene.each<ecs::AudioEmitterComponent>([&](ecs::Entity entity, ecs::AudioEmitterComponent& em) {
            AudioVoiceState& voice = s_voices[entity];

            // Resolve the attached sound resource, if any.
            const auto* snd = em.sound != 0 ? ResourceManager::get().fromCookie<SoundResource>(em.sound) : nullptr;

            // Trigger mode: every new `trigger` value plays the attached
            // resource once from its first sample regardless of `state`, so a
            // rapid-fire emitter never stacks or resumes.
            if (em.trigger != voice.lastTrigger)
            {
                voice.lastTrigger = em.trigger;
                if (snd != nullptr && snd->audioHandle != 0)
                {
                    system::audio::set_loop(snd->audioHandle, false);
                    system::audio::trigger(snd->audioHandle);
                    voice.handle = snd->audioHandle;
                    voice.attached = em.sound;
                    voice.playing = true;
                    voice.stateDriven = false;
                    voice.failed = false;
                }
            }

            // A changed/vanished attachment stops the current voice and
            // re-arms the rising-edge path below.
            if (voice.attached != em.sound)
            {
                if (voice.handle != 0 && voice.playing)
                    system::audio::stop(voice.handle);
                voice.playing = false;
                voice.handle = 0;
                voice.failed = false;
                voice.attached = em.sound;
            }

            const bool want = em.state == "playing";
            if (snd == nullptr || snd->audioHandle == 0)
            {
                // Nothing playable attached: a stale playing flag must not
                // linger on the shared per-entity state.
                if (voice.playing && voice.handle != 0)
                    system::audio::stop(voice.handle);
                voice.playing = false;
                voice.failed = want && snd != nullptr; // invalid cookie: warn-retry on next edge
            }
            else if (want && !voice.playing && !voice.failed)
            {
                // State rising edge: start the attached resource honouring the
                // emitter's loop flag.
                system::audio::set_loop(snd->audioHandle, em.loop);
                if (system::audio::play(snd->audioHandle))
                {
                    voice.handle = snd->audioHandle;
                    voice.playing = true;
                    voice.stateDriven = true;
                    voice.fadeArmed = -1; // a fresh trigger restarts an armed fade
                }
                else
                {
                    voice.failed = true;
                    logging::logWarning("[audio] failed to start audio_emitter sound cookie {}", em.sound);
                }
            }
            else if (!want && voice.playing && voice.stateDriven)
            {
                // Falling edge: stop the voice. Trigger-driven one-shots are
                // left to run out naturally.
                if (voice.handle != 0)
                    system::audio::stop(voice.handle);
                voice.playing = false;
            }
            if (!want)
                voice.failed = false; // re-arm so the next rising edge retries

            // Push volume/pan while playing. Category volumes default to full
            // until a mixer configuration exists.
            constexpr float categoryVolume = 100.0f;
            if (voice.playing && voice.handle != 0)
            {
                const float base = (em.volume / 100.0f) * (categoryVolume / 100.0f);
                float eff = base * 100.0f;
                if (em.fadeFrames > 0)
                {
                    if (voice.fadeArmed != em.fadeFrames)
                    {
                        voice.fadeArmed = em.fadeFrames;
                        voice.fadeStartVolume = base * 100.0f;
                        voice.fadeTotal = static_cast<float>(em.fadeFrames);
                        voice.fadeFramesLeft = em.fadeFrames;
                    }
                    const float t = 1.0f - static_cast<float>(voice.fadeFramesLeft) / voice.fadeTotal;
                    eff = voice.fadeStartVolume + (em.fadeTargetVolume - voice.fadeStartVolume) * t;
                    if (voice.fadeFramesLeft > 0)
                        voice.fadeFramesLeft--;
                }

                const float gain = eff / 100.0f;
                const int centibel = gain <= 0.0f ? -10000 : static_cast<int>(std::round(2000.0 * std::log10(gain)));
                system::audio::set_vol(voice.handle, centibel);
                int panDs = static_cast<int>(em.pan);
                panDs = panDs < -100 ? -100 : (panDs > 100 ? 100 : panDs);
                system::audio::set_pan(voice.handle, panDs * 100);
            }

            // Observed playback state, written back for scripts to read. A
            // non-looping one-shot that ran to its end reports "finished".
            if (!voice.playing || voice.handle == 0)
                em.status = "stopped";
            else if (!system::audio::get_status(voice.handle))
                em.status = em.loop ? "stopped" : "finished";
            else
                em.status = "playing";
        });
    }

    void RenderSystem(AppContext& app)
    {
        if (app.closing())
        {
            // Release the scene-tree renderer's GPU resources before the
            // window/device teardown that follows in the closing pass.
            if (game_module_active())
            {
                // Destroy every TextureResource while the GPU is still alive.
                // This also makes any later Lua __gc release a safe no-op
                // (cookies are cleared, so release() returns early), avoiding
                // touching the sprite renderer after its static teardown.
                ResourceManager::get().releaseAll();
                render2d::SpriteRenderer::get().shutdown();
            }
            return;
        }
        if (!app.frameActive)
            return;

        if (game_module_active())
        {
            // Scene-tree rendering: the module's entities (SpriteComponent)
            // are drawn by the self-contained render2d renderer directly into
            // the window. No marni, no ordering tables, no guest framebuffer.
            std::vector<render2d::SpriteDraw> draws;
            app.scene.each<ecs::SpriteComponent>([&](ecs::Entity entity, ecs::SpriteComponent& sprite) {
                const ecs::Mat4 world = ecs::worldMatrix(app.scene, entity);
                const float x = world.m[3][0];
                const float y = world.m[3][1];
                // Axis-aligned scale inherited from ancestor transforms (e.g. a
                // scaled parent). Extracted from the world matrix columns.
                const float sx
                    = std::sqrt(world.m[0][0] * world.m[0][0] + world.m[1][0] * world.m[1][0] + world.m[2][0] * world.m[2][0]);
                const float sy
                    = std::sqrt(world.m[0][1] * world.m[0][1] + world.m[1][1] * world.m[1][1] + world.m[2][1] * world.m[2][1]);

                TextureResource* tex = nullptr;
                if (sprite.texture != 0)
                {
                    tex = ResourceManager::get().fromCookie<TextureResource>(sprite.texture);
                    if (tex == nullptr)
                        return; // texture released underneath us - skip
                }

                render2d::SpriteDraw d{};
                d.textureHandle = tex ? tex->textureHandle : 0;
                d.src = { sprite.src.x, sprite.src.y, sprite.src.width, sprite.src.height };
                d.dst = { x, y, sprite.size.width * sx, sprite.size.height * sy };
                d.flags = sprite.flags;
                d.r = sprite.color.r;
                d.g = sprite.color.g;
                d.b = sprite.color.b;
                d.a = sprite.color.a;
                d.z = sprite.z;
                draws.push_back(d);
            });

            // Sprites draw back-to-front by z (higher z first / further back).
            // The stable sort keeps equal-z entities in creation order, so the
            // painter's-algorithm pass below mirrors the original ordering
            // table (primitives were sorted by their z the same way).
            std::stable_sort(draws.begin(), draws.end(), [](const render2d::SpriteDraw& a, const render2d::SpriteDraw& b) {
                return a.z > b.z;
            });

            // Video entities: letterbox the decoded frame into the entity
            // bounds (preserving aspect ratio) and draw it like a texture.
            app.scene.each<ecs::VideoComponent>([&](ecs::Entity entity, ecs::VideoComponent& video) {
                if (video.status != ecs::VideoStatus::Playing || video.texture == 0)
                    return;
                if (video.videoWidth <= 0 || video.videoHeight <= 0)
                    return;

                const ecs::Mat4 world = ecs::worldMatrix(app.scene, entity);
                const float x = world.m[3][0];
                const float y = world.m[3][1];
                const float sx
                    = std::sqrt(world.m[0][0] * world.m[0][0] + world.m[1][0] * world.m[1][0] + world.m[2][0] * world.m[2][0]);
                const float sy
                    = std::sqrt(world.m[0][1] * world.m[0][1] + world.m[1][1] * world.m[1][1] + world.m[2][1] * world.m[2][1]);

                const float boundsW = video.size.width * sx;
                const float boundsH = video.size.height * sy;
                const float vw = static_cast<float>(video.videoWidth);
                const float vh = static_cast<float>(video.videoHeight);
                const float scale = std::min(boundsW / vw, boundsH / vh);
                const float dw = vw * scale;
                const float dh = vh * scale;
                // The sprite is centred on its transform (see render2d), so
                // anchor the letterboxed video on the entity position.
                render2d::SpriteDraw d{};
                d.textureHandle = video.texture;
                d.src = { 0, 0, 0, 0 };
                d.dst = { x, y, dw, dh };
                d.flags = render2d::kStretchX | render2d::kStretchY;
                d.r = 255;
                d.g = 255;
                d.b = 255;
                d.a = 255;
                draws.push_back(d);
            });

            // Full-window fade overlay set this frame (Lua gfx.fade).
            const auto fade = render2d::SpriteRenderer::get().takeFade();
            if (fade.active && fade.a > 0)
            {
                const float w = (float)render2d::SpriteRenderer::get().screenWidth();
                const float h = (float)render2d::SpriteRenderer::get().screenHeight();
                render2d::SpriteDraw d{};
                d.textureHandle = 0;
                // Sprites are centred on their position (see render2d), so anchor
                // the full-window fade on the screen centre.
                d.dst = { w * 0.5f, h * 0.5f, w, h };
                d.r = fade.r;
                d.g = fade.g;
                d.b = fade.b;
                d.a = fade.a;
                draws.push_back(d);
            }

            render2d::SpriteRenderer::get().present(draws.data(), draws.size());
            app.frameActive = false;
            return;
        }

        auto legacyEntity = app.scene.find<ecs::LegacyGameComponent>();
        if (legacyEntity == ecs::NullEntity)
            return;

        const auto& legacy = app.scene.getComponent<ecs::LegacyGameComponent>(legacyEntity);
        if (!legacy.enabled)
        {
            // Disabled game (or missing node): present black.
            marni::g_renderer->setPresentRect(0, 0, 0, 0);
            marni::g_renderer->flip();
            marni::g_renderer->resetPresentRect();
            app.frameActive = false;
            return;
        }

        // Render the game into the rectangle described by the LegacyGame node:
        // world transform position (top-left) + size (width/height in pixels).
        const ecs::Mat4 world = ecs::worldMatrix(app.scene, legacyEntity);
        const int x = (int)world.m[3][0];
        const int y = (int)world.m[3][1];
        const int w = (int)legacy.size.width;
        const int h = (int)legacy.size.height;

        marni::g_renderer->setPresentRect(x, y, w, h);
        render_frame();
        marni::g_renderer->resetPresentRect();
        app.frameActive = false;
    }
}