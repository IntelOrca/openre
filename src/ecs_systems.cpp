#include "ecs_systems.h"

#include "app.h"
#include "ecs_components.h"
#include "game_module.h"
#include "input.h"
#include "logger.h"
#include "marni_renderer.h"
#include "openre.h"
#include "render2d.h"
#include "resmgr.h"
#include "script.h"
#include "system_audio.h"
#include "system_config.h"
#include "system_filesystem.h"
#include "system_window.h"
#include "text_layout.h"

#include <algorithm>
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
            bool playing = false;           // last frame's want-to-play edge state
            bool failed = false;            // source failed to load (no retry until re-trigger)
            std::string source;             // source path currently loaded
            float fadeStartVolume = 100.0f; // effective volume the fade started from
            int fadeFramesLeft = 0;         // frames remaining in the active fade
            float fadeTotal = 0.0f;         // fade length in frames
            int fadeArmed = -1;             // fadeFrames value the current fade was armed for
        };

        // Source path -> decoded voice handle, so several emitters (or a
        // re-triggered emitter) share one decoded .sap.
        std::unordered_map<std::string, uint32_t>& audioSourceCache()
        {
            static std::unordered_map<std::string, uint32_t> cache;
            return cache;
        }
    }

    // Native audio bring-up for module mode: mirrors every
    // ecs::AudioEmitterComponent onto a voice of the SDL3 software mixer.
    // Playback is edge-triggered on the `state` field ("playing" vs anything
    // else); volume/pan are pushed each frame while playing and the observed
    // playback state is written back into `status`.
    void AudioSystem(AppContext& app)
    {
        auto* scene = openre::current_scene();
        if (scene == nullptr || !game_module_active())
            return;

        static std::unordered_map<ecs::Entity, AudioVoiceState> s_voices;
        static std::unordered_map<std::string, int> s_sourceSubs;
        static int s_nextSub = 0;

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
            const bool want = em.state == "playing";

            // A source change while the voice is still flagged as playing is a
            // re-trigger (the Lua script swaps `source` then raises `state` in
            // the same frame, so the falling/rising edge is never observed).
            // Stop the old voice and let the rising-edge path reload the new
            // source.
            if (want && voice.playing && !em.source.empty() && em.source != voice.source)
            {
                if (voice.handle != 0)
                    system::audio::stop(voice.handle);
                voice.playing = false;
                voice.failed = false;
                voice.handle = 0;
                voice.source = em.source;
            }

            // Rising edge: load (once, cached by source path) and start.
            if (want && !voice.playing && !voice.failed)
            {
                if (!em.source.empty())
                {
                    if (voice.handle == 0)
                    {
                        auto& cache = audioSourceCache();
                        const auto cached = cache.find(em.source);
                        if (cached != cache.end())
                            voice.handle = cached->second;
                    }
                    if (voice.handle == 0)
                    {
                        // Resolve the source: a plain relative path is tried
                        // against the game module dir first, then falls back to
                        // the original RE2 data set (data://), mirroring the
                        // texture loader so module-relative and data assets both
                        // work. A bare tex name without a data:// prefix is
                        // resolved as data://<source> for the fallback.
                        std::vector<uint8_t> bytes;
                        if (em.source.find("data://") != 0)
                        {
                            const auto path = game_module_dir() / em.source;
                            // Probe existence first so a missing module-dir
                            // file does not log a filesystem error (the
                            // data:// fallback below is expected).
                            if (system::fs::info(path.string().c_str()).kind == system::fs::FileKind::file)
                                bytes = system::fs::readAllBytes(path.string().c_str());
                        }
                        if (bytes.empty())
                        {
                            const auto resolved = system::fs::resolve(("data://" + em.source).c_str());
                            if (!resolved.empty() && system::fs::info(resolved.c_str()).kind == system::fs::FileKind::file)
                                bytes = system::fs::readAllBytes(resolved.c_str());
                        }
                        if (!bytes.empty())
                        {
                            // Mixer slot: the 8 fixed groups (door/weapon/room/
                            // enemy/core/bgm/sbgm/voice) take a small type; use
                            // the "core" group (4, capacity 22) so many distinct
                            // emitter sources can coexist. A per-source sub keeps
                            // each unique path on its own slot.
                            const auto& srcRef = em.source;
                            auto subIt = s_sourceSubs.find(srcRef);
                            if (subIt == s_sourceSubs.end())
                            {
                                subIt = s_sourceSubs.emplace(srcRef, s_nextSub).first;
                                s_nextSub = (s_nextSub + 1) % 22;
                            }
                            const int type = 4; // core group
                            const int sub = subIt->second;
                            voice.handle = system::audio::load_sap(bytes.data(), static_cast<int>(bytes.size()), type, sub);
                        }
                        if (voice.handle != 0)
                            audioSourceCache()[em.source] = voice.handle;
                    }

                    if (voice.handle != 0)
                    {
                        system::audio::set_loop(voice.handle, em.loop);
                        system::audio::play(voice.handle);
                        voice.fadeArmed = -1; // a fresh trigger restarts an armed fade
                        voice.playing = true;
                        voice.source = em.source;
                    }
                    else
                    {
                        voice.failed = true; // stay quiet, warn once per trigger
                        logging::logWarning("[audio] failed to load audio_emitter source '{}'", em.source.c_str());
                    }
                }
            }
            // Falling edge: stop the voice.
            else if (!want && voice.playing)
            {
                if (voice.handle != 0)
                    system::audio::stop(voice.handle);
                voice.playing = false;
            }
            if (!want)
            {
                voice.failed = false; // re-arm so the next rising edge retries
                voice.source.clear();
            }

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