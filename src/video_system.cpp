#include "video_system.h"

#include "ecs_components.h"
#include "ecs_scene.h"
#include "game_module.h"
#include "logger.h"
#include "openre.h"
#include "render2d.h"
#include "system_config.h"
#include "system_filesystem.h"
#include "video_decoder.h"

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

namespace openre
{
    namespace
    {
        // Per-entity playback state. The decoder runs on its own thread and is
        // driven from the game thread via the functions below; nothing here is
        // touched concurrently, so no locking is required.
        struct VideoState
        {
            VideoDecoder* decoder = nullptr;
            int texture = 0;      // render2d dynamic-texture handle (0 = none)
            bool stop = false;    // a skip/end was requested
            bool started = false; // logged first frame forwarded
            std::string label;    // source file (for logs)
        };

        std::unordered_map<ecs::Entity, VideoState> s_videoStates;

        // Resolves a game-relative movie path (e.g. "pl0\zmovie\title_l.bin")
        // to an absolute OS path, trying the canonical RE2 data root (data://)
        // and the game module directory. Returns an empty string if no
        // candidate exists.
        std::string resolveMoviePath(const std::string& path)
        {
            auto isResolvable = [](const std::string& p) {
                if (p.size() >= 7
                    && (p.compare(0, 7, "data://") == 0 || p.compare(0, 7, "save://") == 0 || p.compare(0, 7, "user://") == 0))
                    return true;
#ifdef _WIN32
                if (p.size() >= 2 && p[1] == ':')
                    return true;
                if (p.size() >= 2 && p[0] == '\\' && p[1] == '\\')
                    return true;
#else
                if (!p.empty() && p[0] == '/')
                    return true;
#endif
                return false;
            };

            // 1. data:// game data (canonical RE2 data root).
            const std::string dataCand = std::string("data://") + path;
            std::string resolved = system::fs::resolve(dataCand.c_str());
            if (!resolved.empty() && system::fs::info(resolved.c_str()).kind == system::fs::FileKind::file)
            {
                logging::logInfo("[video] resolved '{}' -> '{}' (data://)", path.c_str(), resolved.c_str());
                return resolved;
            }

            // 2. Game module directory (where main.lua / scripts live).
            const auto modDir = game_module_dir();
            if (!modDir.empty())
            {
                std::filesystem::path p = modDir / path;
                std::string s = p.string();
                if (system::fs::info(s.c_str()).kind == system::fs::FileKind::file)
                {
                    logging::logInfo("[video] resolved '{}' -> '{}' (module dir)", path.c_str(), s.c_str());
                    return s;
                }
            }

            // 3. Already absolute / a scheme - resolve directly as a fallback.
            if (isResolvable(path))
            {
                std::string direct = system::fs::resolve(path.c_str());
                if (!direct.empty() && system::fs::info(direct.c_str()).kind == system::fs::FileKind::file)
                    return direct;
            }

            logging::logWarning("[video] could not resolve movie path '{}'", path.c_str());
            return {};
        }
    }

    ResourceCookie loadMovieResource(const std::string& path)
    {
        auto& resmgr = ResourceManager::get();

        // Reuse an already-registered movie instead of resolving it again.
        const ResourceCookie existing = resmgr.addRef<MovieResource>(path);
        if (existing != 0)
            return existing;

        // Streaming means nothing but the resolved location is kept: validate
        // the file exists now so failures surface at loadMovie() time rather
        // than when playback starts.
        const std::string resolved = resolveMoviePath(path);
        if (resolved.empty())
            return 0;

        auto resource = std::make_unique<MovieResource>();
        resource->path = resolved;
        const ResourceCookie cookie = resmgr.addFirstRef<MovieResource>(path, std::move(resource));
        logging::logInfo("[video] registered movie '{}' -> cookie {}, file '{}'", path, cookie, resolved);
        return cookie;
    }

    ecs::Entity video_play(AppContext& app, ResourceCookie movie)
    {
        if (!game_module_active())
            return ecs::NullEntity;

        auto* resource = ResourceManager::get().fromCookie<MovieResource>(movie);
        if (resource == nullptr || resource->path.empty())
        {
            logging::logWarning("[video] video_play: cookie {} is not a loaded movie", movie);
            return ecs::NullEntity;
        }

        const std::string& resolved = resource->path;
        auto& scene = app.scene;
        ecs::Entity entity = scene.createEntity(scene.getRoot(ecs::Scene::RootKind::Movie));
        const auto ws = system::config::get_screen_resolution();
        auto& videoTransform = scene.getComponent<ecs::TransformComponent>(entity);
        videoTransform.name = "movie";
        // Sprites (and thus the video) are centred on their transform (see
        // render2d), so anchor the full-window movie on the screen centre.
        videoTransform.position.x = ws.width * 0.5f;
        videoTransform.position.y = ws.height * 0.5f;
        auto& video = scene.addComponent<ecs::VideoComponent>(entity);
        video.movie = movie;
        video.status = ecs::VideoStatus::Playing;

        auto& videoSize = scene.addComponent<ecs::UiSizeComponent>(entity);
        videoSize.width = static_cast<float>(ws.width);
        videoSize.height = static_cast<float>(ws.height);

        // The movie is interruptible through the input middleware: carry an
        // InputHandler so scripts and the web plugin can inspect/replace the
        // frame while the movie plays (the blocking movie loop in apiPlayMovie
        // polls input via movie_input_poll and runs this walk).
        scene.addComponent<ecs::InputHandlerComponent>(entity);

        auto* decoder = new VideoDecoder();
        if (!decoder->open(resolved))
        {
            logging::logWarning("[video] failed to open movie: {}", resolved.c_str());
            delete decoder;
            scene.destroyEntity(entity);
            return ecs::NullEntity;
        }

        video.videoWidth = decoder->videoWidth();
        video.videoHeight = decoder->videoHeight();

        const int tex = render2d::SpriteRenderer::get().createDynamicTexture(video.videoWidth, video.videoHeight);

        // Start with a black frame so nothing undefined shows before the first
        // decoded frame is forwarded.
        if (tex != 0)
        {
            std::vector<uint8_t> black(static_cast<size_t>(video.videoWidth) * video.videoHeight * 4, 0);
            render2d::SpriteRenderer::get().updateDynamicTexture(tex, black.data(), video.videoWidth, video.videoHeight);
        }

        VideoState state;
        state.decoder = decoder;
        state.texture = tex;
        state.label = resolved;
        s_videoStates[entity] = std::move(state);
        video.texture = tex;
        return entity;
    }

    void video_stop(AppContext& app, ecs::Entity entity)
    {
        auto it = s_videoStates.find(entity);
        if (it == s_videoStates.end())
            return;

        VideoState& st = it->second;
        if (st.decoder != nullptr)
        {
            st.decoder->close();
            delete st.decoder;
            st.decoder = nullptr;
        }
        if (st.texture != 0)
        {
            render2d::SpriteRenderer::get().unloadTexture(st.texture);
            st.texture = 0;
        }

        auto* scene = openre::current_scene();
        if (scene != nullptr && scene->isValid(entity) && scene->hasComponent<ecs::VideoComponent>(entity))
        {
            auto& video = scene->getComponent<ecs::VideoComponent>(entity);
            video.texture = 0;
            video.status = ecs::VideoStatus::Stopped;
        }
        s_videoStates.erase(it);
    }

    void VideoSystem(AppContext& app)
    {
        if (!game_module_active())
            return;

        auto* scene = openre::current_scene();
        if (scene == nullptr)
            return;

        scene->each<ecs::VideoComponent>([&](ecs::Entity entity, ecs::VideoComponent& video) {
            auto it = s_videoStates.find(entity);
            if (it == s_videoStates.end())
                return;
            VideoState& st = it->second;

            // Stopped (finished or skipped): release everything.
            if (video.status != ecs::VideoStatus::Playing || st.stop || st.decoder == nullptr)
            {
                if (st.decoder != nullptr)
                {
                    st.decoder->close();
                    delete st.decoder;
                    st.decoder = nullptr;
                }
                if (st.texture != 0)
                {
                    render2d::SpriteRenderer::get().unloadTexture(st.texture);
                    st.texture = 0;
                }
                video.texture = 0;
                video.status = ecs::VideoStatus::Stopped;
                s_videoStates.erase(it);
                return;
            }

            // Forward the latest decoded frame to the GPU texture.
            std::vector<uint8_t> frame;
            int w = 0;
            int h = 0;
            if (st.decoder->acquireFrame(frame, w, h) && st.texture != 0)
            {
                render2d::SpriteRenderer::get().updateDynamicTexture(st.texture, frame.data(), w, h);
                if (!st.started)
                {
                    st.started = true;
                    logging::logInfo("[video] playing movie: {}x{} ({})", w, h, st.label.c_str());
                }
            }

            video.position = static_cast<float>(st.decoder->position());

            if (st.decoder->finished())
                st.stop = true; // released on the next VideoSystem pass
        });
    }
}
