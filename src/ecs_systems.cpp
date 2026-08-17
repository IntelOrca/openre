#include "ecs_systems.h"

#include "app.h"
#include "marni_renderer.h"
#include "openre.h"
#include "system_config.h"
#include "system_window.h"

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
            world = world * trs(t.position, t.rotation, t.scale);
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
            // Special states (movie / reset) consume the frame.
            if (handle_special_states())
                return;

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

    void RenderSystem(AppContext& app)
    {
        if (app.closing() || !app.frameActive)
            return;

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