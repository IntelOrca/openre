#include "ecs_systems.h"

#include "app.h"
#include "ecs_components.h"
#include "game_module.h"
#include "marni_renderer.h"
#include "openre.h"
#include "render2d.h"
#include "resmgr.h"
#include "script.h"
#include "system_config.h"
#include "system_window.h"
#include "text_layout.h"

#include <cmath>
#include <cstdint>
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

        // The hidden root already exists (Scene ctor). Add one child that
        // carries the LegacyGameComponent, sized to the configured window.
        Entity entity = scene.createEntity(scene.root());
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
                draws.push_back(d);
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
                const float dx = x + (boundsW - dw) * 0.5f;
                const float dy = y + (boundsH - dh) * 0.5f;

                render2d::SpriteDraw d{};
                d.textureHandle = video.texture;
                d.src = { 0, 0, 0, 0 };
                d.dst = { dx, dy, dw, dh };
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
                d.dst = { 0.0f, 0.0f, w, h };
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