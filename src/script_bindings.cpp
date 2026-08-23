#include "script_bindings.h"
#include "app.h"
#include "ecs_components.h"
#include "ecs_scene.h"
#include "ecs_systems.h"
#include "font.h"
#include "input.h"
#include "interop.hpp"
#include "logger.h"
#include "openre.h"
#include "render2d.h"
#include "resmgr.h"
#include "sce.h"
#include "script.h"
#include "script_manager.h"
#include "script_network.h"
#include "script_vm.h"
#include "sound_loader.h"
#include "system_audio.h"
#include "system_filesystem.h"
#include "system_input.h"
#include "system_window.h"
#include "systems.h"
#include "texture_loader.h"
#include "video_system.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <lua.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openre::script
{
    namespace
    {
        constexpr const char* METATABLE_ENTITY = "meta_entity";
        constexpr const char* METATABLE_SCENE_ENTITY = "meta_scene_entity";
        constexpr const char* METATABLE_SCENE_COMPONENT = "meta_scene_component";
        constexpr const char* METATABLE_TEXTURE = "meta_texture";
        constexpr const char* METATABLE_FONT = "meta_font";
        constexpr const char* METATABLE_SOUND = "meta_sound";
        constexpr const char* METATABLE_MOVIE = "meta_movie";

        namespace LogLevel
        {
            constexpr int32_t info = 1;
            constexpr int32_t warning = 2;
            constexpr int32_t error = 3;
            constexpr int32_t debug = 4;
        }

        namespace EntityKind
        {
            constexpr int32_t player = 1;
            constexpr int32_t splayer = 2;
            constexpr int32_t enemy = 3;
            constexpr int32_t object = 4;
            constexpr int32_t door = 5;
        }

        struct EntityHandle
        {
            int32_t kind;
            int32_t index;
        };

        std::pair<std::string_view, std::string_view> splitNamespace(std::string_view fullName)
        {
            auto delimiter = fullName.find_last_of('.');
            auto ns = delimiter == std::string_view::npos ? std::string_view{} : fullName.substr(0, delimiter);
            auto name = fullName.substr(delimiter + 1);
            return { ns, name };
        }

        void getOrCreateAndPushGlobal(lua_State* L, std::string_view ns)
        {
            lua_pushglobaltable(L);
            while (!ns.empty())
            {
                auto part = ns;
                auto delimiter = ns.find('.');
                if (delimiter == std::string_view::npos)
                {
                    ns = {};
                }
                else
                {
                    part = ns.substr(0, delimiter);
                    ns = ns.substr(delimiter + 1);
                }

                lua_pushlstring(L, part.data(), part.size());
                lua_gettable(L, -2);
                if (!lua_istable(L, -1))
                {
                    lua_pop(L, 1);
                    lua_newtable(L);
                    lua_pushlstring(L, part.data(), part.size());
                    lua_pushvalue(L, -2);
                    lua_settable(L, -4);
                }
                lua_remove(L, -2);
            }
        }

        void setGlobal(lua_State* L, std::string_view fullName, lua_CFunction fn)
        {
            auto [ns, name] = splitNamespace(fullName);
            getOrCreateAndPushGlobal(L, ns);
            lua_pushlstring(L, name.data(), name.size());
            lua_pushcclosure(L, fn, 0);
            lua_settable(L, -3);
            lua_pop(L, 1);
        }

        void setGlobal(lua_State* L, std::string_view fullName, int32_t value)
        {
            auto [ns, name] = splitNamespace(fullName);
            getOrCreateAndPushGlobal(L, ns);
            lua_pushlstring(L, name.data(), name.size());
            lua_pushinteger(L, value);
            lua_settable(L, -3);
            lua_pop(L, 1);
        }

        void setGlobalWithVm(lua_State* L, std::string_view fullName, lua_CFunction fn, LuaVm* vm)
        {
            auto [ns, name] = splitNamespace(fullName);
            getOrCreateAndPushGlobal(L, ns);
            lua_pushlstring(L, name.data(), name.size());
            lua_pushlightuserdata(L, vm);
            lua_pushcclosure(L, fn, 1);
            lua_settable(L, -3);
            lua_pop(L, 1);
        }

        int apiPrint(lua_State* L)
        {
            auto* vm = static_cast<LuaVm*>(lua_touserdata(L, lua_upvalueindex(1)));

            std::string text;
            auto top = lua_gettop(L);
            for (int i = 1; i <= top; i++)
            {
                if (i > 1)
                {
                    text += '\t';
                }
                size_t length = 0;
                auto* str = luaL_tolstring(L, i, &length);
                if (str != nullptr)
                {
                    text.append(str, length);
                }
                lua_pop(L, 1);
            }

            std::printf("[%s] %s\n", vm->name().c_str(), text.c_str());
            return 0;
        }

        int apiGetFlag(lua_State* L)
        {
            auto group = static_cast<FlagGroup>(luaL_checkinteger(L, 1));
            auto index = static_cast<uint32_t>(luaL_checkinteger(L, 2));
            lua_pushboolean(L, check_flag(group, index));
            return 1;
        }

        int apiSetFlag(lua_State* L)
        {
            auto group = static_cast<FlagGroup>(luaL_checkinteger(L, 1));
            auto index = static_cast<uint32_t>(luaL_checkinteger(L, 2));
            auto value = lua_toboolean(L, 3);
            set_flag(group, index, value);
            return 0;
        }

        int apiGetEntity(lua_State* L)
        {
            auto* handle = static_cast<EntityHandle*>(lua_newuserdata(L, sizeof(EntityHandle)));
            handle->kind = static_cast<int32_t>(luaL_checkinteger(L, 1));
            handle->index = static_cast<int32_t>(luaL_checkinteger(L, 2));
            luaL_getmetatable(L, METATABLE_ENTITY);
            lua_setmetatable(L, -2);
            return 1;
        }

        int entity_get(lua_State* L)
        {
            auto* handle = static_cast<EntityHandle*>(luaL_checkudata(L, 1, METATABLE_ENTITY));
            auto* key = luaL_checkstring(L, 2);

            if (strcmp(key, "kind") == 0)
            {
                lua_pushinteger(L, handle->kind);
                return 1;
            }
            if (strcmp(key, "index") == 0)
            {
                lua_pushinteger(L, handle->index);
                return 1;
            }

            switch (handle->kind)
            {
            case EntityKind::enemy:
            {
                auto* enemy = static_cast<EnemyEntity*>(sce::GetEnemyEntity(handle->index));
                if (enemy == nullptr)
                {
                    break;
                }
                if (strcmp(key, "type") == 0)
                {
                    lua_pushinteger(L, enemy->id);
                }
                else if (strcmp(key, "life") == 0 || strcmp(key, "hp") == 0)
                {
                    lua_pushinteger(L, enemy->life);
                }
                else if (strcmp(key, "maxLife") == 0)
                {
                    lua_pushinteger(L, enemy->max_life);
                }
                else if (strcmp(key, "posX") == 0)
                {
                    lua_pushinteger(L, enemy->f_pos.x);
                }
                else if (strcmp(key, "posY") == 0)
                {
                    lua_pushinteger(L, enemy->f_pos.y);
                }
                else if (strcmp(key, "posZ") == 0)
                {
                    lua_pushinteger(L, enemy->f_pos.z);
                }
                else
                {
                    lua_pushnil(L);
                }
                return 1;
            }
            case EntityKind::player:
            case EntityKind::splayer:
            {
                ActorEntity* actor = nullptr;
                if (handle->kind == EntityKind::player)
                {
                    actor = sce::GetPlayerEntity();
                }
                else
                {
                    actor = static_cast<ActorEntity*>(sce::GetPartnerEntity());
                }
                if (actor == nullptr)
                {
                    break;
                }
                if (strcmp(key, "id") == 0)
                {
                    lua_pushinteger(L, actor->id);
                }
                else if (strcmp(key, "life") == 0)
                {
                    lua_pushinteger(L, actor->life);
                }
                else if (strcmp(key, "maxLife") == 0)
                {
                    lua_pushinteger(L, actor->max_life);
                }
                else if (strcmp(key, "posX") == 0)
                {
                    lua_pushinteger(L, actor->f_pos.x);
                }
                else if (strcmp(key, "posY") == 0)
                {
                    lua_pushinteger(L, actor->f_pos.y);
                }
                else if (strcmp(key, "posZ") == 0)
                {
                    lua_pushinteger(L, actor->f_pos.z);
                }
                else
                {
                    lua_pushnil(L);
                }
                return 1;
            }
            case EntityKind::object:
            {
                auto* object = sce::GetObjectEntity(handle->index);
                if (object == nullptr)
                {
                    break;
                }
                if (strcmp(key, "type") == 0)
                {
                    lua_pushinteger(L, object->id);
                }
                else
                {
                    lua_pushnil(L);
                }
                return 1;
            }
            case EntityKind::door:
            {
                auto* door = sce::GetDoorEntity(handle->index);
                if (door == nullptr)
                {
                    break;
                }
                if (strcmp(key, "type") == 0)
                {
                    lua_pushinteger(L, door->id);
                }
                else
                {
                    lua_pushnil(L);
                }
                return 1;
            }
            default: break;
            }

            lua_pushnil(L);
            return 1;
        }

        int entity_set(lua_State* L)
        {
            auto* handle = static_cast<EntityHandle*>(luaL_checkudata(L, 1, METATABLE_ENTITY));
            auto* key = luaL_checkstring(L, 2);
            auto value = static_cast<int16_t>(luaL_checkinteger(L, 3));

            if (strcmp(key, "life") != 0)
            {
                return 0;
            }

            switch (handle->kind)
            {
            case EntityKind::enemy:
            {
                auto* enemy = static_cast<EnemyEntity*>(sce::GetEnemyEntity(handle->index));
                if (enemy != nullptr)
                {
                    enemy->life = value;
                }
                break;
            }
            case EntityKind::player:
            {
                auto* player = sce::GetPlayerEntity();
                if (player != nullptr)
                {
                    player->life = value;
                }
                break;
            }
            case EntityKind::splayer:
            {
                auto* partner = static_cast<ActorEntity*>(sce::GetPartnerEntity());
                if (partner != nullptr)
                {
                    partner->life = value;
                }
                break;
            }
            default: break;
            }
            return 0;
        }

        int apiSubscribe(lua_State* L)
        {
            auto* vm = static_cast<LuaVm*>(lua_touserdata(L, lua_upvalueindex(1)));
            auto kind = static_cast<int32_t>(luaL_checkinteger(L, 1));
            if (kind != static_cast<int32_t>(HookKind::tick))
            {
                return luaL_error(L, "unsupported hook kind: %d", kind);
            }

            luaL_checktype(L, 2, LUA_TFUNCTION);
            lua_pushvalue(L, 2);
            auto ref = luaL_ref(L, LUA_REGISTRYINDEX);
            vm->subscribe(static_cast<HookKind>(kind), ref);
            return 0;
        }

        int apiLog(lua_State* L)
        {
            auto* vm = static_cast<LuaVm*>(lua_touserdata(L, lua_upvalueindex(1)));
            auto level = static_cast<int32_t>(luaL_checkinteger(L, 1));
            auto* message = luaL_checkstring(L, 2);

            switch (level)
            {
            case LogLevel::info: openre::logging::logInfo("[script] {}: {}", vm->name(), message); break;
            case LogLevel::warning: openre::logging::logWarning("[script] {}: {}", vm->name(), message); break;
            case LogLevel::error: openre::logging::logError("[script] {}: {}", vm->name(), message); break;
            case LogLevel::debug: openre::logging::logDebug("[script] {}: {}", vm->name(), message); break;
            default: break;
            }
            return 0;
        }

        int apiGetEnv(lua_State* L)
        {
            auto* name = luaL_checkstring(L, 1);
            auto* value = std::getenv(name);
            if (value == nullptr)
            {
                lua_pushnil(L);
            }
            else
            {
                lua_pushstring(L, value);
            }
            return 1;
        }

        // re.collectGarbage() - runs a full Lua garbage collection step. The
        // sandboxed VM removes the standard collectgarbage(); this safe
        // replacement lets scripts release resource handles (textures, fonts,
        // sounds) deterministically after dropping their last reference,
        // e.g. when handing over from one screen to another.
        int apiCollectGarbage(lua_State* L)
        {
            lua_gc(L, LUA_GCCOLLECT);
            return 0;
        }

        // ── scene.* / gfx.* ────────────────────────────────────────────────
        // The scene-tree API. Entities and components are light userdata
        // handles into the current ECS scene; textures/fonts/sounds are
        // userdata wrapping a ResourceCookie. The Lua handle OWNS its
        // resource-manager reference: when the last handle is garbage
        // collected the resource is unloaded. Scene components that point at
        // a resource through a field (sprite texture, text font, emitter
        // sound) borrow it, so scripts must keep the handle alive as long as
        // the component uses it (e.g. store it alongside the entity).
        // Resources still live at shutdown are released wholesale by
        // ResourceManager::releaseAll(); late __gc finalizers then release
        // against an empty registry and are safe no-ops.

        namespace ComponentKind
        {
            constexpr int32_t transform = 1;
            constexpr int32_t sprite = 2;
            constexpr int32_t text = 3;
            constexpr int32_t legacy = 4;
            constexpr int32_t video = 5;
            constexpr int32_t audio_emitter = 6;
            constexpr int32_t scaled_frame = 7;
        }

        struct SceneEntityHandle
        {
            ecs::Entity entity;
        };

        struct SceneComponentHandle
        {
            ecs::Entity entity;
            int32_t kind;
        };

        struct TextureHandle
        {
            ResourceCookie cookie;
        };

        struct FontHandle
        {
            ResourceCookie cookie;
        };

        struct SoundHandle
        {
            ResourceCookie cookie;
        };

        struct MovieHandle
        {
            ResourceCookie cookie;
        };

        // __gc for TextureHandle/FontHandle/SoundHandle/MovieHandle: returns
        // the handle's resource-manager reference. Safe after releaseAll()
        // (the empty registry makes release a no-op).
        template<typename T> int resourceHandleGC(lua_State* L)
        {
            auto* handle = static_cast<T*>(lua_touserdata(L, 1));
            if (handle != nullptr && handle->cookie != 0)
            {
                ResourceManager::get().release(handle->cookie);
                handle->cookie = 0;
            }
            return 0;
        }

        int sceneCreateEntity(lua_State* L)
        {
            auto* scene = current_scene();
            if (scene == nullptr)
            {
                lua_pushnil(L);
                return 1;
            }

            ecs::Entity parent = scene->root();
            if (lua_gettop(L) >= 1 && !lua_isnil(L, 1))
            {
                auto* parentHandle = static_cast<SceneEntityHandle*>(luaL_checkudata(L, 1, METATABLE_SCENE_ENTITY));
                parent = parentHandle->entity;
            }

            auto entity = scene->createEntity(parent);
            auto* handle = static_cast<SceneEntityHandle*>(lua_newuserdata(L, sizeof(SceneEntityHandle)));
            handle->entity = entity;
            luaL_getmetatable(L, METATABLE_SCENE_ENTITY);
            lua_setmetatable(L, -2);
            return 1;
        }

        // Returns one of the five strict top-level sub-roots (user, game, movie,
        // fade, overlay) by name.
        int sceneGetRoot(lua_State* L)
        {
            auto* name = luaL_checkstring(L, 1);
            auto* scene = current_scene();
            if (scene == nullptr)
            {
                lua_pushnil(L);
                return 1;
            }
            ecs::Scene::RootKind kind;
            if (strcmp(name, "user") == 0)
                kind = ecs::Scene::RootKind::User;
            else if (strcmp(name, "game") == 0)
                kind = ecs::Scene::RootKind::Game;
            else if (strcmp(name, "movie") == 0)
                kind = ecs::Scene::RootKind::Movie;
            else if (strcmp(name, "fade") == 0)
                kind = ecs::Scene::RootKind::Fade;
            else if (strcmp(name, "overlay") == 0)
                kind = ecs::Scene::RootKind::Overlay;
            else
                return luaL_error(L, "scene.getRoot: unknown root '%s'", name);

            auto entity = scene->getRoot(kind);
            auto* handle = static_cast<SceneEntityHandle*>(lua_newuserdata(L, sizeof(SceneEntityHandle)));
            handle->entity = entity;
            luaL_getmetatable(L, METATABLE_SCENE_ENTITY);
            lua_setmetatable(L, -2);
            return 1;
        }

        int sceneSetParent(lua_State* L)
        {
            auto* scene = current_scene();
            if (scene == nullptr)
                return 0;

            // Child and parent are both scene entity handles.
            auto* child = static_cast<SceneEntityHandle*>(luaL_checkudata(L, 1, METATABLE_SCENE_ENTITY));
            auto* parent = static_cast<SceneEntityHandle*>(luaL_checkudata(L, 2, METATABLE_SCENE_ENTITY));
            scene->setParent(child->entity, parent->entity);
            return 0;
        }

        // Destroys an entity and its whole subtree. Safe to call on an already-
        // destroyed handle (no-op). Scripts use this to remove whole scenes
        // when transitioning between them (e.g. splash -> menu -> prologue).
        int sceneDestroy(lua_State* L)
        {
            auto* handle = static_cast<SceneEntityHandle*>(luaL_checkudata(L, 1, METATABLE_SCENE_ENTITY));
            auto* scene = current_scene();
            if (scene == nullptr || !scene->isValid(handle->entity) || handle->entity == scene->root())
                return 0;
            scene->destroyEntity(handle->entity);
            return 0;
        }

        int sceneAddComponent(lua_State* L)
        {
            auto* handle = static_cast<SceneEntityHandle*>(luaL_checkudata(L, 1, METATABLE_SCENE_ENTITY));
            auto* name = luaL_checkstring(L, 2);
            auto* scene = current_scene();
            if (scene == nullptr)
            {
                lua_pushnil(L);
                return 1;
            }
            if (!scene->isValid(handle->entity))
            {
                return luaL_error(L, "scene.addComponent: entity is no longer valid");
            }

            int32_t kind = 0;
            if (strcmp(name, "core.sprite") == 0)
            {
                if (scene->tryGetComponent<ecs::SpriteComponent>(handle->entity) == nullptr)
                {
                    scene->addComponent<ecs::SpriteComponent>(handle->entity);
                }
                kind = ComponentKind::sprite;
            }
            else if (strcmp(name, "core.transform") == 0)
            {
                kind = ComponentKind::transform; // implicit on every entity
            }
            else if (strcmp(name, "core.text") == 0)
            {
                // Optional font cookie (a gfx.loadFont value) as a 3rd arg.
                if (scene->tryGetComponent<ecs::TextComponent>(handle->entity) == nullptr)
                {
                    scene->addComponent<ecs::TextComponent>(handle->entity);
                }
                kind = ComponentKind::text;
            }
            else if (strcmp(name, "core.audio_emitter") == 0)
            {
                if (scene->tryGetComponent<ecs::AudioEmitterComponent>(handle->entity) == nullptr)
                {
                    scene->addComponent<ecs::AudioEmitterComponent>(handle->entity);
                }
                kind = ComponentKind::audio_emitter;
            }
            else if (strcmp(name, "core.scaled_frame") == 0)
            {
                if (scene->tryGetComponent<ecs::ScaledFrameComponent>(handle->entity) == nullptr)
                {
                    scene->addComponent<ecs::ScaledFrameComponent>(handle->entity);
                }
                kind = ComponentKind::scaled_frame;
            }
            else
            {
                return luaL_error(L, "scene.addComponent: unknown component '%s'", name);
            }

            auto* comp = static_cast<SceneComponentHandle*>(lua_newuserdata(L, sizeof(SceneComponentHandle)));
            comp->entity = handle->entity;
            comp->kind = kind;
            luaL_getmetatable(L, METATABLE_SCENE_COMPONENT);
            lua_setmetatable(L, -2);
            return 1;
        }

        // Reads an integer at stack index `index` and clamps it into the
        // 0-255 range used by ecs::Color channels.
        uint8_t readByteChannel(lua_State* L, int index)
        {
            auto value = luaL_checkinteger(L, index);
            if (value < 0)
                value = 0;
            if (value > 255)
                value = 255;
            return static_cast<uint8_t>(value);
        }

        int sceneSetField(lua_State* L)
        {
            auto* comp = static_cast<SceneComponentHandle*>(luaL_testudata(L, 1, METATABLE_SCENE_COMPONENT));
            auto* ent
                = comp != nullptr ? nullptr : static_cast<SceneEntityHandle*>(luaL_testudata(L, 1, METATABLE_SCENE_ENTITY));
            if (comp == nullptr && ent == nullptr)
            {
                return luaL_error(L, "scene.setField: expected an entity or component");
            }

            auto* name = luaL_checkstring(L, 2);
            auto* scene = current_scene();
            if (scene == nullptr)
            {
                return luaL_error(L, "scene.setField: no active scene");
            }

            const ecs::Entity entity = comp != nullptr ? comp->entity : ent->entity;
            if (ent != nullptr || comp->kind == ComponentKind::transform)
            {
                auto& transform = scene->getComponent<ecs::TransformComponent>(entity);
                if (strcmp(name, "name") == 0)
                {
                    const char* value = luaL_checkstring(L, 3);
                    transform.name = value;
                }
                else if (strcmp(name, "x") == 0)
                    transform.position.x = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "y") == 0)
                    transform.position.y = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "z") == 0)
                    transform.position.z = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "scale.x") == 0)
                    transform.scale.x = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "scale.y") == 0)
                    transform.scale.y = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "scale.z") == 0)
                    transform.scale.z = static_cast<float>(luaL_checknumber(L, 3));
                else
                    return luaL_error(L, "scene.setField: unknown transform field '%s'", name);
                return 0;
            }

            if (comp->kind == ComponentKind::sprite)
            {
                auto* sprite = scene->tryGetComponent<ecs::SpriteComponent>(entity);
                if (sprite == nullptr)
                {
                    return luaL_error(L, "scene.setField: entity has no sprite component");
                }

                if (strcmp(name, "size.width") == 0)
                    sprite->size.width = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "size.height") == 0)
                    sprite->size.height = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "src.x") == 0)
                    sprite->src.x = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "src.y") == 0)
                    sprite->src.y = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "src.width") == 0)
                    sprite->src.width = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "src.height") == 0)
                    sprite->src.height = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "texture") == 0)
                {
                    if (lua_isnoneornil(L, 3))
                    {
                        sprite->texture = 0;
                    }
                    else
                    {
                        auto* tex = static_cast<TextureHandle*>(luaL_testudata(L, 3, METATABLE_TEXTURE));
                        if (tex == nullptr)
                        {
                            return luaL_error(L, "scene.setField: texture must be a gfx.loadTexture value");
                        }
                        sprite->texture = tex->cookie;
                    }
                }
                else if (strcmp(name, "flags") == 0)
                {
                    const auto value = luaL_checkinteger(L, 3);
                    if (value < 0 || value > static_cast<lua_Integer>(UINT32_MAX))
                    {
                        return luaL_error(L, "scene.setField: flags out of range");
                    }
                    sprite->flags = static_cast<uint32_t>(value);
                }
                else if (strcmp(name, "color") == 0)
                {
                    // {r, g, b, a}, each 0-255.
                    luaL_checktype(L, 3, LUA_TTABLE);
                    uint8_t channels[4];
                    for (int i = 0; i < 4; i++)
                    {
                        lua_rawgeti(L, 3, i + 1);
                        channels[i] = readByteChannel(L, -1);
                        lua_pop(L, 1);
                    }
                    sprite->color = ecs::Color{ channels[0], channels[1], channels[2], channels[3] };
                }
                else if (strcmp(name, "opacity") == 0)
                    sprite->color.a = readByteChannel(L, 3);
                else if (strcmp(name, "z") == 0)
                    sprite->z = static_cast<int>(luaL_checkinteger(L, 3));
                else
                    return luaL_error(L, "scene.setField: unknown sprite field '%s'", name);
                return 0;
            }

            if (comp->kind == ComponentKind::text)
            {
                auto* text = scene->tryGetComponent<ecs::TextComponent>(entity);
                if (text == nullptr)
                {
                    return luaL_error(L, "scene.setField: entity has no text component");
                }
                if (strcmp(name, "text") == 0)
                {
                    text->text = luaL_checkstring(L, 3);
                }
                else if (strcmp(name, "size.width") == 0)
                    text->size.width = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "size.height") == 0)
                    text->size.height = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "flags") == 0)
                {
                    const auto value = luaL_checkinteger(L, 3);
                    if (value < 0 || value > static_cast<lua_Integer>(UINT32_MAX))
                    {
                        return luaL_error(L, "scene.setField: flags out of range");
                    }
                    text->flags = static_cast<uint32_t>(value);
                }
                else if (strcmp(name, "font") == 0)
                {
                    if (lua_isnoneornil(L, 3))
                    {
                        text->font = 0;
                    }
                    else
                    {
                        auto* font = static_cast<FontHandle*>(luaL_testudata(L, 3, METATABLE_FONT));
                        if (font == nullptr)
                        {
                            return luaL_error(L, "scene.setField: font must be a gfx.loadFont value");
                        }
                        text->font = font->cookie;
                    }
                }
                else
                    return luaL_error(L, "scene.setField: unknown text field '%s'", name);
                return 0;
            }

            if (comp->kind == ComponentKind::legacy)
            {
                auto* legacy = scene->tryGetComponent<ecs::LegacyGameComponent>(entity);
                if (legacy == nullptr)
                    return luaL_error(L, "scene.setField: entity has no legacy component");
                if (strcmp(name, "enabled") == 0)
                    legacy->enabled = lua_toboolean(L, 3);
                else if (strcmp(name, "size.width") == 0)
                    legacy->size.width = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "size.height") == 0)
                    legacy->size.height = static_cast<float>(luaL_checknumber(L, 3));
                else
                    return luaL_error(L, "scene.setField: unknown legacy field '%s'", name);
                return 0;
            }

            if (comp->kind == ComponentKind::video)
            {
                auto* video = scene->tryGetComponent<ecs::VideoComponent>(entity);
                if (video == nullptr)
                    return luaL_error(L, "scene.setField: entity has no video component");
                if (strcmp(name, "status") == 0)
                    video->status = static_cast<ecs::VideoStatus>(luaL_checkinteger(L, 3));
                else if (strcmp(name, "position") == 0)
                    video->position = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "videoWidth") == 0)
                    video->videoWidth = static_cast<int>(luaL_checkinteger(L, 3));
                else if (strcmp(name, "videoHeight") == 0)
                    video->videoHeight = static_cast<int>(luaL_checkinteger(L, 3));
                else if (strcmp(name, "movie") == 0)
                {
                    // A Movie from core.loadMovie() selects what the component
                    // plays; nil clears it.
                    if (lua_isnoneornil(L, 3))
                        video->movie = 0;
                    else if (auto* movie = static_cast<MovieHandle*>(luaL_testudata(L, 3, METATABLE_MOVIE)))
                        video->movie = movie->cookie;
                    else
                        return luaL_error(L, "scene.setField: expected a movie or nil for video field 'movie'");
                }
                else
                    return luaL_error(L, "scene.setField: unknown video field '%s'", name);
                return 0;
            }

            if (comp->kind == ComponentKind::audio_emitter)
            {
                auto* emitter = scene->tryGetComponent<ecs::AudioEmitterComponent>(entity);
                if (emitter == nullptr)
                    return luaL_error(L, "scene.setField: entity has no audio_emitter component");
                if (strcmp(name, "category") == 0)
                    emitter->category = luaL_checkstring(L, 3);
                else if (strcmp(name, "state") == 0)
                    emitter->state = luaL_checkstring(L, 3);
                else if (strcmp(name, "volume") == 0)
                    emitter->volume = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "pan") == 0)
                    emitter->pan = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "loop") == 0)
                    emitter->loop = lua_toboolean(L, 3);
                else if (strcmp(name, "fadeFrames") == 0)
                    emitter->fadeFrames = static_cast<int>(luaL_checkinteger(L, 3));
                else if (strcmp(name, "fadeTargetVolume") == 0)
                    emitter->fadeTargetVolume = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "sound") == 0)
                {
                    // A Sound from audio.loadAudio() selects what the emitter
                    // plays; nil detaches it (the emitter goes silent).
                    if (lua_isnil(L, 3))
                        emitter->sound = 0;
                    else if (auto* snd = static_cast<SoundHandle*>(luaL_testudata(L, 3, METATABLE_SOUND)))
                        emitter->sound = snd->cookie;
                    else
                        return luaL_error(L, "scene.setField: expected a sound or nil for audio_emitter field 'sound'");
                }
                else if (strcmp(name, "trigger") == 0)
                    emitter->trigger = static_cast<int32_t>(luaL_checkinteger(L, 3));
                else
                    return luaL_error(L, "scene.setField: unknown audio_emitter field '%s'", name);
                return 0;
            }

            if (comp->kind == ComponentKind::scaled_frame)
            {
                auto* frame = scene->tryGetComponent<ecs::ScaledFrameComponent>(entity);
                if (frame == nullptr)
                    return luaL_error(L, "scene.setField: entity has no scaled_frame component");
                if (strcmp(name, "source.width") == 0)
                    frame->source.width = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "source.height") == 0)
                    frame->source.height = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "target.width") == 0)
                    frame->target.width = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "target.height") == 0)
                    frame->target.height = static_cast<float>(luaL_checknumber(L, 3));
                else if (strcmp(name, "mode") == 0)
                {
                    auto* value = luaL_checkstring(L, 3);
                    if (strcmp(value, "contain") == 0)
                        frame->mode = ecs::ScaledFrameMode::Contain;
                    else if (strcmp(value, "cover") == 0)
                        frame->mode = ecs::ScaledFrameMode::Cover;
                    else
                        return luaL_error(L, "scene.setField: unknown scaled_frame mode '%s'", value);
                }
                else if (strcmp(name, "increment") == 0)
                    frame->increment = static_cast<float>(luaL_checknumber(L, 3));
                else
                    return luaL_error(L, "scene.setField: unknown scaled_frame field '%s'", name);
                return 0;
            }

            return luaL_error(L, "scene.setField: unsupported component (kind %d)", (int)comp->kind);
        }

        int sceneGetField(lua_State* L)
        {
            auto* comp = static_cast<SceneComponentHandle*>(luaL_testudata(L, 1, METATABLE_SCENE_COMPONENT));
            auto* ent
                = comp != nullptr ? nullptr : static_cast<SceneEntityHandle*>(luaL_testudata(L, 1, METATABLE_SCENE_ENTITY));
            if (comp == nullptr && ent == nullptr)
            {
                return luaL_error(L, "scene.getField: expected an entity or component");
            }

            auto* name = luaL_checkstring(L, 2);
            auto* scene = current_scene();
            if (scene == nullptr)
            {
                lua_pushnil(L);
                return 1;
            }

            const ecs::Entity entity = comp != nullptr ? comp->entity : ent->entity;

            if (ent != nullptr || comp->kind == ComponentKind::transform)
            {
                const auto& transform = scene->getComponent<ecs::TransformComponent>(entity);
                if (strcmp(name, "name") == 0)
                    lua_pushlstring(L, transform.name.data(), transform.name.size());
                else if (strcmp(name, "x") == 0)
                    lua_pushnumber(L, transform.position.x);
                else if (strcmp(name, "y") == 0)
                    lua_pushnumber(L, transform.position.y);
                else if (strcmp(name, "z") == 0)
                    lua_pushnumber(L, transform.position.z);
                else if (strcmp(name, "scale.x") == 0)
                    lua_pushnumber(L, transform.scale.x);
                else if (strcmp(name, "scale.y") == 0)
                    lua_pushnumber(L, transform.scale.y);
                else if (strcmp(name, "scale.z") == 0)
                    lua_pushnumber(L, transform.scale.z);
                else
                    lua_pushnil(L);
                return 1;
            }

            if (comp->kind == ComponentKind::sprite)
            {
                auto* sprite = scene->tryGetComponent<ecs::SpriteComponent>(entity);
                if (sprite == nullptr)
                {
                    lua_pushnil(L);
                    return 1;
                }
                if (strcmp(name, "size.width") == 0)
                    lua_pushnumber(L, sprite->size.width);
                else if (strcmp(name, "size.height") == 0)
                    lua_pushnumber(L, sprite->size.height);
                else if (strcmp(name, "src.x") == 0)
                    lua_pushnumber(L, sprite->src.x);
                else if (strcmp(name, "src.y") == 0)
                    lua_pushnumber(L, sprite->src.y);
                else if (strcmp(name, "src.width") == 0)
                    lua_pushnumber(L, sprite->src.width);
                else if (strcmp(name, "src.height") == 0)
                    lua_pushnumber(L, sprite->src.height);
                else if (strcmp(name, "texture") == 0)
                    lua_pushinteger(L, sprite->texture);
                else if (strcmp(name, "flags") == 0)
                    lua_pushinteger(L, sprite->flags);
                else if (strcmp(name, "color") == 0)
                {
                    lua_newtable(L);
                    for (int i = 0; i < 4; i++)
                    {
                        lua_pushinteger(L, (&sprite->color.r)[i]);
                        lua_rawseti(L, -2, i + 1);
                    }
                }
                else if (strcmp(name, "opacity") == 0)
                    lua_pushinteger(L, sprite->color.a);
                else if (strcmp(name, "z") == 0)
                    lua_pushinteger(L, sprite->z);
                else
                    lua_pushnil(L);
                return 1;
            }

            if (comp->kind == ComponentKind::text)
            {
                auto* text = scene->tryGetComponent<ecs::TextComponent>(entity);
                if (text == nullptr)
                {
                    lua_pushnil(L);
                    return 1;
                }
                if (strcmp(name, "text") == 0)
                    lua_pushlstring(L, text->text.data(), text->text.size());
                else if (strcmp(name, "size.width") == 0)
                    lua_pushnumber(L, text->size.width);
                else if (strcmp(name, "size.height") == 0)
                    lua_pushnumber(L, text->size.height);
                else if (strcmp(name, "flags") == 0)
                    lua_pushinteger(L, text->flags);
                else if (strcmp(name, "font") == 0)
                    lua_pushinteger(L, text->font);
                else
                    lua_pushnil(L);
                return 1;
            }

            if (comp->kind == ComponentKind::legacy)
            {
                auto* legacy = scene->tryGetComponent<ecs::LegacyGameComponent>(entity);
                if (legacy == nullptr)
                {
                    lua_pushnil(L);
                    return 1;
                }
                if (strcmp(name, "enabled") == 0)
                    lua_pushboolean(L, legacy->enabled);
                else if (strcmp(name, "size.width") == 0)
                    lua_pushnumber(L, legacy->size.width);
                else if (strcmp(name, "size.height") == 0)
                    lua_pushnumber(L, legacy->size.height);
                else
                    lua_pushnil(L);
                return 1;
            }

            if (comp->kind == ComponentKind::video)
            {
                auto* video = scene->tryGetComponent<ecs::VideoComponent>(entity);
                if (video == nullptr)
                {
                    lua_pushnil(L);
                    return 1;
                }
                if (strcmp(name, "status") == 0)
                    lua_pushinteger(L, static_cast<lua_Integer>(video->status));
                else if (strcmp(name, "position") == 0)
                    lua_pushnumber(L, video->position);
                else if (strcmp(name, "videoWidth") == 0)
                    lua_pushinteger(L, video->videoWidth);
                else if (strcmp(name, "videoHeight") == 0)
                    lua_pushinteger(L, video->videoHeight);
                else if (strcmp(name, "movie") == 0)
                    lua_pushinteger(L, video->movie);
                else
                    lua_pushnil(L);
                return 1;
            }

            if (comp->kind == ComponentKind::audio_emitter)
            {
                auto* emitter = scene->tryGetComponent<ecs::AudioEmitterComponent>(entity);
                if (emitter == nullptr)
                {
                    lua_pushnil(L);
                    return 1;
                }
                if (strcmp(name, "category") == 0)
                    lua_pushlstring(L, emitter->category.data(), emitter->category.size());
                else if (strcmp(name, "state") == 0)
                    lua_pushlstring(L, emitter->state.data(), emitter->state.size());
                else if (strcmp(name, "volume") == 0)
                    lua_pushnumber(L, emitter->volume);
                else if (strcmp(name, "pan") == 0)
                    lua_pushnumber(L, emitter->pan);
                else if (strcmp(name, "loop") == 0)
                    lua_pushboolean(L, emitter->loop);
                else if (strcmp(name, "status") == 0)
                    lua_pushlstring(L, emitter->status.data(), emitter->status.size());
                else if (strcmp(name, "fadeFrames") == 0)
                    lua_pushinteger(L, emitter->fadeFrames);
                else if (strcmp(name, "fadeTargetVolume") == 0)
                    lua_pushnumber(L, emitter->fadeTargetVolume);
                else if (strcmp(name, "sound") == 0)
                {
                    if (emitter->sound == 0)
                        lua_pushnil(L);
                    else
                    {
                        // The returned handle owns a reference (released by
                        // __gc), so take one now.
                        auto* snd = static_cast<SoundHandle*>(lua_newuserdata(L, sizeof(SoundHandle)));
                        snd->cookie = ResourceManager::get().dupRef(emitter->sound);
                        luaL_getmetatable(L, METATABLE_SOUND);
                        lua_setmetatable(L, -2);
                    }
                }
                else if (strcmp(name, "trigger") == 0)
                    lua_pushinteger(L, emitter->trigger);
                else
                    lua_pushnil(L);
                return 1;
            }

            if (comp->kind == ComponentKind::scaled_frame)
            {
                auto* frame = scene->tryGetComponent<ecs::ScaledFrameComponent>(entity);
                if (frame == nullptr)
                {
                    lua_pushnil(L);
                    return 1;
                }
                if (strcmp(name, "source.width") == 0)
                    lua_pushnumber(L, frame->source.width);
                else if (strcmp(name, "source.height") == 0)
                    lua_pushnumber(L, frame->source.height);
                else if (strcmp(name, "target.width") == 0)
                    lua_pushnumber(L, frame->target.width);
                else if (strcmp(name, "target.height") == 0)
                    lua_pushnumber(L, frame->target.height);
                else if (strcmp(name, "mode") == 0)
                    lua_pushstring(L, frame->mode == ecs::ScaledFrameMode::Cover ? "cover" : "contain");
                else if (strcmp(name, "increment") == 0)
                    lua_pushnumber(L, frame->increment);
                else
                    lua_pushnil(L);
                return 1;
            }

            lua_pushnil(L);
            return 1;
        }

        int sceneExit(lua_State* L)
        {
            app_request_close();
            return 0;
        }

        int gfxLoadTexture(lua_State* L)
        {
            auto* path = luaL_checkstring(L, 1);
            auto width = static_cast<int>(luaL_optinteger(L, 2, 0));
            auto height = static_cast<int>(luaL_optinteger(L, 3, 0));
            // Optional 4th arg: which TIM to decode when the file packs several
            // TIMs back to back (e.g. tmoji.bin). Non-negative selects one TIM
            // (0 = the first); omitted or negative decodes the whole file.
            auto timIndex = static_cast<int>(luaL_optinteger(L, 4, -1));

            auto cookie = loadTextureResource(path, width, height, timIndex);
            if (cookie == 0)
            {
                lua_pushnil(L);
                return 1;
            }

            auto* tex = static_cast<TextureHandle*>(lua_newuserdata(L, sizeof(TextureHandle)));
            tex->cookie = cookie;
            luaL_getmetatable(L, METATABLE_TEXTURE);
            lua_setmetatable(L, -2);
            return 1;
        }

        int gfxLoadFont(lua_State* L)
        {
            auto* path = luaL_checkstring(L, 1);
            auto cookie = loadFontResource(path);
            if (cookie == 0)
            {
                lua_pushnil(L);
                return 1;
            }
            auto* font = static_cast<FontHandle*>(lua_newuserdata(L, sizeof(FontHandle)));
            font->cookie = cookie;
            luaL_getmetatable(L, METATABLE_FONT);
            lua_setmetatable(L, -2);
            return 1;
        }

        int gfxScreenWidth(lua_State* L)
        {
            lua_pushinteger(L, render2d::SpriteRenderer::get().screenWidth());
            return 1;
        }

        int gfxScreenHeight(lua_State* L)
        {
            lua_pushinteger(L, render2d::SpriteRenderer::get().screenHeight());
            return 1;
        }

        int gfxFade(lua_State* L)
        {
            render2d::FadeState state;
            state.active = true;
            state.r = static_cast<uint8_t>(luaL_checkinteger(L, 1));
            state.g = static_cast<uint8_t>(luaL_checkinteger(L, 2));
            state.b = static_cast<uint8_t>(luaL_checkinteger(L, 3));
            state.a = static_cast<uint8_t>(luaL_checkinteger(L, 4));
            render2d::SpriteRenderer::get().setFade(state);
            return 0;
        }

        // ── save.* ──────────────────────────────────────────────────────────
        // Saved-game listing for the Lua load/save screens.

        // English room names for the save screen info line, indexed by the
        // room id stored in the save image (the same index the original save
        // name builder feeds into its Japanese room-name table).
        constexpr const char* const SAVE_ROOM_NAMES[] = {
            "Waiting Room",
            "Main Hall",
            "Darkroom",
            "Plant Left Storage",
            "Plant Right Storage",
            "Sewers Control Room 1",
            "Sewers Control Room 2",
            "Sewers Treatment Pool",
            "Factory Panel Room",
            "Lab Pump Room",
            "Lab Security Room",
            "Lab Monitor Room",
            "Gun Shop",
            "Station Storage Shed",
            "Lab Cable Car",
            "Scenario 1st",
        };

        struct ListedSave
        {
            std::string name;
            std::string character;
            std::string scenario;
            std::string difficulty;
            std::string room;
            int32_t numSaves;
            uint64_t lastModified;
        };

        // A file qualifies when its uppercased name carries a .BIOHAZARD2
        // extension or contains RESIDENT2 (same rule as the original card
        // validation) and starts with the 'SC' save magic.
        bool is_valid_save_file(const std::string& entry, const std::vector<uint8_t>& data)
        {
            std::string lower;
            lower.reserve(entry.size());
            for (char c : entry)
                lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower.find(".biohazard2") == std::string::npos && lower.find("resident2") == std::string::npos)
                return false;
            return data.size() >= 0x224 && data[0] == 'S' && data[1] == 'C';
        }

        // save.list() - enumerates the save folder and returns an array of
        // tables sorted newest first:
        //   { name       = filename without extension,
        //     character  = 'leon' | 'claire',
        //     scenario   = 'a' | 'b',
        //     difficulty = 'casual' | 'standard',  (no hardcore marker exists yet)
        //     room       = English room name,
        //     numSaves   = times saved }
        // Fields come from the save image copied from gGameTable.table_start:
        //   0x20A plId (bit0: leon/claire), 0x20B room id,
        //   0x214 fg_status copy (bit mask 0x40000000 scenario B,
        //   0x04000000 easy), 0x220 save count.
        // Unreadable or corrupt files are skipped; with no save folder the
        // result is an empty table. Never raises.
        int saveList(lua_State* L)
        {
            std::vector<ListedSave> saves;
            auto entries = system::fs::getDirectoryContents("save://", "*");
            for (const auto& entry : entries)
            {
                if (!entry.empty() && entry[0] == '.')
                    continue;

                auto data = system::fs::readAllBytes(("save://" + entry).c_str());
                if (!is_valid_save_file(entry, data))
                    continue;

                auto info = system::fs::info(("save://" + entry).c_str());

                uint32_t status
                    = data[0x214] | (data[0x215] << 8) | (data[0x216] << 16) | (static_cast<uint32_t>(data[0x217]) << 24);

                auto name = entry;
                auto dot = name.rfind('.');
                if (dot != std::string::npos)
                    name.resize(dot);

                auto roomId = data[0x20B];
                saves.push_back(
                    {
                        name,
                        (data[0x20A] & 1) ? "claire" : "leon",
                        (status & 0x40000000) ? "b" : "a",
                        (status & 0x04000000) ? "casual" : "standard",
                        roomId < std::size(SAVE_ROOM_NAMES) ? SAVE_ROOM_NAMES[roomId] : "",
                        static_cast<int32_t>(data[0x220] | (data[0x221] << 8)),
                        info.lastModified,
                    });
            }

            std::sort(saves.begin(), saves.end(), [](const ListedSave& a, const ListedSave& b) {
                return a.lastModified > b.lastModified;
            });

            lua_newtable(L);
            for (size_t i = 0; i < saves.size(); i++)
            {
                lua_createtable(L, 0, 6);
                lua_pushstring(L, saves[i].name.c_str());
                lua_setfield(L, -2, "name");
                lua_pushstring(L, saves[i].character.c_str());
                lua_setfield(L, -2, "character");
                lua_pushstring(L, saves[i].scenario.c_str());
                lua_setfield(L, -2, "scenario");
                lua_pushstring(L, saves[i].difficulty.c_str());
                lua_setfield(L, -2, "difficulty");
                lua_pushstring(L, saves[i].room.c_str());
                lua_setfield(L, -2, "room");
                lua_pushinteger(L, saves[i].numSaves);
                lua_setfield(L, -2, "numSaves");
                lua_rawseti(L, -2, static_cast<int>(i + 1));
            }
            return 1;
        }

        // ── audio.* ─────────────────────────────────────────────────────────
        // Resident sound effects. Sounds are decoded fully into memory at load
        // time (unlike audio emitters, which are meant for longer sources) and
        // stay loaded for the session; play() is a fire-and-forget one-shot.

        // audio.loadAudio(path [, sub]) - decodes a .sap file into memory and
        // returns a Sound handle, or nil on failure. The optional `sub` index
        // selects one sub-sound of a multi-sound .sap container (e.g. sub 0
        // of core22.sap). Loading the same path/sub pair twice returns a
        // handle to the same shared sound.
        int audioLoadAudio(lua_State* L)
        {
            auto* path = luaL_checkstring(L, 1);
            const auto sub = static_cast<int>(luaL_optinteger(L, 2, -1));
            const auto cookie = loadSoundResource(path, sub);
            if (cookie == 0)
            {
                lua_pushnil(L);
                return 1;
            }
            auto* sound = static_cast<SoundHandle*>(lua_newuserdata(L, sizeof(SoundHandle)));
            sound->cookie = cookie;
            luaL_getmetatable(L, METATABLE_SOUND);
            lua_setmetatable(L, -2);
            return 1;
        }

        // audio.play(sound) - plays a previously loaded sound as a one-shot.
        // Re-triggering while it is still playing restarts it from the start.
        int audioPlay(lua_State* L)
        {
            auto* handle = static_cast<SoundHandle*>(luaL_checkudata(L, 1, METATABLE_SOUND));
            if (handle == nullptr)
                return luaL_argerror(L, 1, "expected a sound");
            if (auto* resource = ResourceManager::get().fromCookie<SoundResource>(handle->cookie))
            {
                if (resource->audioHandle != 0)
                    system::audio::play(resource->audioHandle);
            }
            return 0;
        }

        // ── scene enumeration / inspection (used by the debug web UI) ───────

        // Resolve a component type name to its ComponentKind (0 if unknown).
        int32_t componentKindFromName(const char* name)
        {
            if (strcmp(name, "core.transform") == 0)
                return ComponentKind::transform;
            if (strcmp(name, "core.sprite") == 0)
                return ComponentKind::sprite;
            if (strcmp(name, "core.text") == 0)
                return ComponentKind::text;
            if (strcmp(name, "core.legacy") == 0)
                return ComponentKind::legacy;
            if (strcmp(name, "core.video") == 0)
                return ComponentKind::video;
            if (strcmp(name, "core.audio_emitter") == 0)
                return ComponentKind::audio_emitter;
            if (strcmp(name, "core.scaled_frame") == 0)
                return ComponentKind::scaled_frame;
            return 0;
        }

        // Returns a table of every live entity id in the scene.
        int sceneAllEntities(lua_State* L)
        {
            auto* scene = current_scene();
            lua_newtable(L);
            if (scene == nullptr)
                return 1;
            int n = 0;
            scene->eachEntity([&](ecs::Entity e) {
                n++;
                lua_pushinteger(L, static_cast<lua_Integer>(e));
                lua_rawseti(L, -2, n);
            });
            return 1;
        }

        // Builds a SceneEntityHandle userdata from an integer entity id, or nil
        // if the scene is inactive or the id is stale.
        int sceneEntityFromId(lua_State* L)
        {
            auto id = static_cast<ecs::Entity>(luaL_checkinteger(L, 1));
            auto* scene = current_scene();
            if (scene == nullptr || !scene->isValid(id))
            {
                lua_pushnil(L);
                return 1;
            }
            auto* h = static_cast<SceneEntityHandle*>(lua_newuserdata(L, sizeof(SceneEntityHandle)));
            h->entity = id;
            luaL_getmetatable(L, METATABLE_SCENE_ENTITY);
            lua_setmetatable(L, -2);
            return 1;
        }

        // Returns a SceneComponentHandle for (entity, typeName), or nil when the
        // entity lacks that component.
        int sceneComponent(lua_State* L)
        {
            auto* h = static_cast<SceneEntityHandle*>(luaL_checkudata(L, 1, METATABLE_SCENE_ENTITY));
            auto* name = luaL_checkstring(L, 2);
            auto* scene = current_scene();
            if (scene == nullptr || !scene->isValid(h->entity))
            {
                lua_pushnil(L);
                return 1;
            }
            const int32_t kind = componentKindFromName(name);
            if (kind == 0)
            {
                lua_pushnil(L);
                return 1;
            }
            bool present = true;
            switch (kind)
            {
            case ComponentKind::transform: present = true; break;
            case ComponentKind::sprite: present = scene->hasComponent<ecs::SpriteComponent>(h->entity); break;
            case ComponentKind::text: present = scene->hasComponent<ecs::TextComponent>(h->entity); break;
            case ComponentKind::legacy: present = scene->hasComponent<ecs::LegacyGameComponent>(h->entity); break;
            case ComponentKind::video: present = scene->hasComponent<ecs::VideoComponent>(h->entity); break;
            case ComponentKind::audio_emitter: present = scene->hasComponent<ecs::AudioEmitterComponent>(h->entity); break;
            case ComponentKind::scaled_frame: present = scene->hasComponent<ecs::ScaledFrameComponent>(h->entity); break;
            }
            if (!present)
            {
                lua_pushnil(L);
                return 1;
            }
            auto* comp = static_cast<SceneComponentHandle*>(lua_newuserdata(L, sizeof(SceneComponentHandle)));
            comp->entity = h->entity;
            comp->kind = kind;
            luaL_getmetatable(L, METATABLE_SCENE_COMPONENT);
            lua_setmetatable(L, -2);
            return 1;
        }

        // Returns a table of the component type names present on an entity.
        int sceneGetComponents(lua_State* L)
        {
            auto* h = static_cast<SceneEntityHandle*>(luaL_checkudata(L, 1, METATABLE_SCENE_ENTITY));
            auto* scene = current_scene();
            lua_newtable(L);
            if (scene == nullptr || !scene->isValid(h->entity))
                return 1;
            int n = 0;
            auto push = [&](const char* name) {
                n++;
                lua_pushlstring(L, name, strlen(name));
                lua_rawseti(L, -2, n);
            };
            push("core.transform");
            if (scene->hasComponent<ecs::SpriteComponent>(h->entity))
                push("core.sprite");
            if (scene->hasComponent<ecs::TextComponent>(h->entity))
                push("core.text");
            if (scene->hasComponent<ecs::LegacyGameComponent>(h->entity))
                push("core.legacy");
            if (scene->hasComponent<ecs::VideoComponent>(h->entity))
                push("core.video");
            if (scene->hasComponent<ecs::AudioEmitterComponent>(h->entity))
                push("core.audio_emitter");
            if (scene->hasComponent<ecs::ScaledFrameComponent>(h->entity))
                push("core.scaled_frame");
            return 1;
        }

        // Returns the writable/readable field names for a component, so the
        // web inspector can render fields from the actual API rather than a
        // hard-coded schema.
        int sceneGetFieldNames(lua_State* L)
        {
            auto* comp = static_cast<SceneComponentHandle*>(luaL_checkudata(L, 1, METATABLE_SCENE_COMPONENT));
            lua_newtable(L);
            int n = 0;
            auto push = [&](const char* name) {
                n++;
                lua_pushlstring(L, name, strlen(name));
                lua_rawseti(L, -2, n);
            };
            switch (comp->kind)
            {
            case ComponentKind::transform:
                push("name");
                push("x");
                push("y");
                push("z");
                push("scale.x");
                push("scale.y");
                push("scale.z");
                break;
            case ComponentKind::sprite:
                push("size.width");
                push("size.height");
                push("src.x");
                push("src.y");
                push("src.width");
                push("src.height");
                push("texture");
                push("flags");
                push("color");
                push("opacity");
                push("z");
                break;
            case ComponentKind::text:
                push("text");
                push("size.width");
                push("size.height");
                push("flags");
                push("font");
                break;
            case ComponentKind::legacy:
                push("enabled");
                push("size.width");
                push("size.height");
                break;
            case ComponentKind::video:
                push("status");
                push("position");
                push("videoWidth");
                push("videoHeight");
                push("movie");
                break;
            case ComponentKind::audio_emitter:
                push("category");
                push("state");
                push("volume");
                push("pan");
                push("loop");
                push("status");
                push("fadeFrames");
                push("fadeTargetVolume");
                break;
            case ComponentKind::scaled_frame:
                push("source.width");
                push("source.height");
                push("target.width");
                push("target.height");
                push("mode");
                push("increment");
                break;
            default: break;
            }
            return 1;
        }

        // Returns the parent entity id, or nil for the scene root.
        int sceneParent(lua_State* L)
        {
            auto* h = static_cast<SceneEntityHandle*>(luaL_checkudata(L, 1, METATABLE_SCENE_ENTITY));
            auto* scene = current_scene();
            if (scene == nullptr || !scene->isValid(h->entity))
            {
                lua_pushnil(L);
                return 1;
            }
            const auto& t = scene->getComponent<ecs::TransformComponent>(h->entity);
            if (t.parent == ecs::NullEntity)
                lua_pushnil(L);
            else
                lua_pushinteger(L, static_cast<lua_Integer>(t.parent));
            return 1;
        }

        // Returns a table of child entity ids.
        int sceneChildren(lua_State* L)
        {
            auto* h = static_cast<SceneEntityHandle*>(luaL_checkudata(L, 1, METATABLE_SCENE_ENTITY));
            auto* scene = current_scene();
            lua_newtable(L);
            if (scene == nullptr || !scene->isValid(h->entity))
                return 1;
            const auto& t = scene->getComponent<ecs::TransformComponent>(h->entity);
            int n = 0;
            ecs::Entity child = t.firstChild;
            while (child != ecs::NullEntity)
            {
                n++;
                lua_pushinteger(L, static_cast<lua_Integer>(child));
                lua_rawseti(L, -2, n);
                child = scene->getComponent<ecs::TransformComponent>(child).nextSibling;
            }
            return 1;
        }

        // ── gfx resource inspection (used by the debug web UI) ──────────────

        // Returns a table of every live resource:
        // { resource, type, path, width, height, streaming, cookies } where
        // `resource` is the resource number, `cookies` the list of live
        // consumer cookies and `streaming` marks sources read from disk on
        // demand (movies and large sounds).
        int gfxListResources(lua_State* L)
        {
            auto& rm = ResourceManager::get();
            auto infos = rm.list();
            lua_newtable(L);
            int n = 0;
            for (const auto& info : infos)
            {
                n++;
                lua_newtable(L);
                lua_pushinteger(L, info.resource);
                lua_setfield(L, -2, "resource");
                lua_pushlstring(L, info.type.data(), info.type.size());
                lua_setfield(L, -2, "type");
                lua_pushlstring(L, info.path.data(), info.path.size());
                lua_setfield(L, -2, "path");
                lua_pushinteger(L, info.width);
                lua_setfield(L, -2, "width");
                lua_pushinteger(L, info.height);
                lua_setfield(L, -2, "height");
                lua_pushboolean(L, info.streaming);
                lua_setfield(L, -2, "streaming");
                lua_newtable(L);
                for (size_t i = 0; i < info.cookies.size(); i++)
                {
                    lua_pushinteger(L, info.cookies[i]);
                    lua_rawseti(L, -2, (int)i + 1);
                }
                lua_setfield(L, -2, "cookies");
                lua_rawseti(L, -2, n);
            }
            return 1;
        }

        // Returns { width, height, data } for a resource's texture pixels
        // (RGBA byte string), or nil when unavailable. Fonts resolve to their
        // atlas texture.
        int gfxGetResourceImage(lua_State* L)
        {
            auto cookie = static_cast<ResourceCookie>(luaL_checkinteger(L, 1));
            auto& rm = ResourceManager::get();
            auto pushTex = [&](const TextureResource* tex) -> int {
                if (tex == nullptr || tex->rgba.empty())
                {
                    lua_pushnil(L);
                    return 1;
                }
                lua_newtable(L);
                lua_pushinteger(L, tex->rgbaWidth);
                lua_setfield(L, -2, "width");
                lua_pushinteger(L, tex->rgbaHeight);
                lua_setfield(L, -2, "height");
                lua_pushlstring(L, reinterpret_cast<const char*>(tex->rgba.data()), tex->rgba.size());
                lua_setfield(L, -2, "data");
                return 1;
            };
            if (auto* tex = rm.fromCookie<TextureResource>(cookie))
                return pushTex(tex);
            if (auto* font = rm.fromCookie<FontResource>(cookie))
                return pushTex(rm.fromCookie<TextureResource>(font->atlas));
            lua_pushnil(L);
            return 1;
        }

        // input.down(name) / input.pressed(name) - read the runtime command
        // state so game scripts can drive menus / title screens from the same
        // bindings as gameplay. `name` is an INI command name ("up", "accept",
        // "menu", ...) or a numeric Command index. `down` reports the command
        // active this frame; `pressed` reports a rising edge (just pressed).
        int inputCommandState(lua_State* L, uint32_t mask)
        {
            int cmd;
            if (lua_type(L, 1) == LUA_TSTRING)
                cmd = static_cast<int>(openre::input::command_from_name(lua_tostring(L, 1)));
            else
                cmd = static_cast<int>(luaL_checknumber(L, 1));
            bool active = cmd >= 0 && cmd < openre::input::COMMAND_COUNT && (mask & (1u << cmd)) != 0;
            lua_pushboolean(L, active);
            return 1;
        }

        int inputCommandDown(lua_State* L)
        {
            return inputCommandState(L, openre::input::command_state());
        }

        int inputCommandPressed(lua_State* L)
        {
            return inputCommandState(L, openre::input::command_edge());
        }

        // core.loadMovie(path) - registers a movie as a streamed resource and
        // returns a Movie handle, or nil when the file cannot be resolved.
        // Nothing is read beyond validating that the file exists; playback
        // streams it from disk. Loading the same path twice returns a handle
        // onto the same shared resource.
        int coreLoadMovie(lua_State* L)
        {
            auto* path = luaL_checkstring(L, 1);
            const auto cookie = loadMovieResource(path);
            if (cookie == 0)
            {
                lua_pushnil(L);
                return 1;
            }
            auto* movie = static_cast<MovieHandle*>(lua_newuserdata(L, sizeof(MovieHandle)));
            movie->cookie = cookie;
            luaL_getmetatable(L, METATABLE_MOVIE);
            lua_setmetatable(L, -2);
            return 1;
        }

        // core.playMovie(movie [, captions]) - plays a movie previously
        // registered with core.loadMovie(), blocking until it finishes or any
        // input skips it. The video entity is created automatically to fill
        // the window; rendering is driven here because this call runs inside a
        // Lua tick hook and the rest of the frame pipeline is suspended until
        // it returns.
        //
        // The optional `captions` argument is an array of
        // { from = seconds, to = seconds, text = string } entries shown as a
        // bottom-centre overlay while the playback position is inside their
        // range. Returns true when the movie was skipped early, false when it
        // played through; nil/nothing when it could not be started.
        int apiPlayMovie(lua_State* L)
        {
            auto* movieHandle = static_cast<MovieHandle*>(luaL_checkudata(L, 1, METATABLE_MOVIE));
            if (movieHandle == nullptr)
                return luaL_argerror(L, 1, "expected a movie from core.loadMovie");
            const auto cookie = movieHandle->cookie;
            auto* app = openre::current_app();
            if (app == nullptr)
                return 0;

            // Optional caption list: { {from=..., to=..., text=...}, ... }.
            struct Caption
            {
                float from;
                float to;
                std::string text;
            };
            std::vector<Caption> captions;
            if (lua_gettop(L) >= 2 && !lua_isnil(L, 2))
            {
                luaL_checktype(L, 2, LUA_TTABLE);
                const int count = static_cast<int>(luaL_len(L, 2));
                captions.reserve(count);
                for (int i = 1; i <= count; i++)
                {
                    lua_rawgeti(L, 2, i);
                    if (lua_istable(L, -1))
                    {
                        Caption c{};
                        lua_getfield(L, -1, "from");
                        c.from = static_cast<float>(luaL_optnumber(L, -1, 0.0));
                        lua_pop(L, 1);
                        lua_getfield(L, -1, "to");
                        c.to = static_cast<float>(luaL_optnumber(L, -1, c.from));
                        lua_pop(L, 1);
                        lua_getfield(L, -1, "text");
                        c.text = luaL_optstring(L, -1, "");
                        lua_pop(L, 1);
                        captions.push_back(std::move(c));
                    }
                    lua_pop(L, 1);
                }
            }

            ecs::Entity entity = video_play(*app, cookie);
            if (entity == ecs::NullEntity)
            {
                logging::logWarning("[core] playMovie failed to start movie cookie {}", cookie);
                return 0;
            }

            // Movies take over the soundtrack: silence every emitter voice so
            // BGM/narration does not bleed into the film. The movie playback
            // loop below drives the video/render systems itself and never runs
            // AudioSystem, so stopping the mix here is the only effective point.
            system::audio::stop_all();
            render2d::SpriteRenderer::get().setFade({});

            // One caption entity for the whole movie, created up front and its
            // string swapped as playback advances. Without a usable font the
            // layout pass simply draws nothing (never a crash).
            ecs::Entity captionEntity = ecs::NullEntity;
            if (!captions.empty())
            {
                auto* scene = openre::current_scene();
                if (scene != nullptr)
                {
                    captionEntity = scene->createEntity(scene->getRoot(ecs::Scene::RootKind::Overlay));
                    auto& transform = scene->getComponent<ecs::TransformComponent>(captionEntity);
                    transform.name = "movie_captions";
                    // Sprites are centred on their transform (see render2d), so
                    // anchor the caption strip at the bottom-centre of the
                    // window.
                    transform.position.x = static_cast<float>(render2d::SpriteRenderer::get().screenWidth()) * 0.5f;
                    transform.position.y = static_cast<float>(render2d::SpriteRenderer::get().screenHeight()) - 20.0f;
                    auto& captionText = scene->addComponent<ecs::TextComponent>(captionEntity);
                    captionText.size = { static_cast<float>(render2d::SpriteRenderer::get().screenWidth()), 24.0f };
                    captionText.flags
                        = static_cast<uint32_t>(ecs::TextFlags::XCenter) | static_cast<uint32_t>(ecs::TextFlags::YMiddle);
                    captionText.font = loadFontResource("fonts/sm.fnt");
                }
            }

            bool skipped = false;

            // Edge-detect input so a key already held from the splash does not
            // skip the movie immediately. Seed the previous state with the
            // current state so only presses after start count.
            uint8_t prevKb[256];
            system::input::get_keyboard_state(prevKb);
            uint8_t prevMb = system::input::get_mouse_buttons();

            while (true)
            {
                auto* scene = openre::current_scene();
                const bool playing = scene != nullptr && scene->isValid(entity)
                    && scene->hasComponent<ecs::VideoComponent>(entity)
                    && scene->getComponent<ecs::VideoComponent>(entity).status == ecs::VideoStatus::Playing;
                if (!playing || app->closing() || !app->running())
                    break;

                // Pump events (keep the window responsive, detect quit).
                system::window::Event ev;
                bool skip = false;
                while (system::window::poll_event(ev))
                {
                    if (ev.type == system::window::EventType::Quit || ev.type == system::window::EventType::CloseRequested)
                    {
                        skip = true;
                        app->requestClose();
                    }
                }

                // Skip on a fresh key/button press (edge since movie start).
                uint8_t kb[256];
                system::input::get_keyboard_state(kb);
                uint8_t mb = system::input::get_mouse_buttons();
                for (int i = 0; i < 256; i++)
                {
                    if (kb[i] && !prevKb[i])
                        skip = true;
                }
                if (mb && !prevMb)
                    skip = true;
                std::memcpy(prevKb, kb, sizeof(prevKb));
                prevMb = mb;

                if (skip)
                {
                    skipped = true;
                    video_stop(*app, entity);
                    break;
                }

                // Advance the video and present the frame. frameActive is set
                // so RenderSystem performs the present this pass.
                app->frameActive = true;
                VideoSystem(*app);

                // Show whichever caption covers the current playback position
                // (VideoSystem has just refreshed video.position). Relayout
                // only when the string actually changed.
                if (captionEntity != ecs::NullEntity && scene != nullptr)
                {
                    const char* activeText = nullptr;
                    const auto& video = scene->getComponent<ecs::VideoComponent>(entity);
                    for (const auto& caption : captions)
                    {
                        if (video.position >= caption.from && video.position <= caption.to)
                        {
                            activeText = caption.text.c_str();
                            break;
                        }
                    }
                    const std::string_view desired(activeText != nullptr ? activeText : "");
                    auto* captionText = scene->tryGetComponent<ecs::TextComponent>(captionEntity);
                    if (captionText != nullptr && captionText->text != desired)
                    {
                        captionText->text.assign(desired);
                        TextLayoutSystem(*app); // lay out the glyphs before presenting
                    }
                }

                RenderSystem(*app);

                // Keep the web inspector responsive while this blocking loop
                // occupies the main thread (the regular script tick never runs
                // until the movie ends otherwise).
                openre::script::pumpScript("web");

                system::window::delay(8);
            }

            // Tear down the video entity (and the caption entity with it).
            video_stop(*app, entity);
            auto* scene = openre::current_scene();
            if (scene != nullptr)
            {
                if (scene->isValid(entity))
                    scene->destroyEntity(entity);
                if (captionEntity != ecs::NullEntity && scene->isValid(captionEntity))
                    scene->destroyEntity(captionEntity);
            }
            app->frameActive = false;

            lua_pushboolean(L, skipped ? 1 : 0);
            return 1;
        }

#ifdef DEBUG
        int apiEval(lua_State* L)
        {
            size_t length = 0;
            auto* code = luaL_checklstring(L, 1, &length);
            auto* name = luaL_optstring(L, 2, "=eval");

            if (luaL_loadbuffer(L, code, length, name) != LUA_OK)
            {
                lua_pushboolean(L, 0);
                lua_insert(L, -2);
                return 2;
            }

            if (lua_pcall(L, 0, 1, 0) != LUA_OK)
            {
                lua_pushboolean(L, 0);
                lua_insert(L, -2);
                return 2;
            }

            lua_pushboolean(L, 1);
            lua_insert(L, -2);
            return 2;
        }

        int apiEvalIn(lua_State* L)
        {
            auto* name = luaL_checkstring(L, 1);
            size_t length = 0;
            auto* code = luaL_checklstring(L, 2, &length);

            std::string result;
            const bool ok = script::ScriptManager::get().evalIn(name, std::string(code, length), result);
            lua_pushboolean(L, ok ? 1 : 0);
            lua_pushlstring(L, result.data(), result.size());
            return 2;
        }

        int unsafeRead(lua_State* L)
        {
            auto address = static_cast<uint32_t>(luaL_checkinteger(L, 1));
            auto length = static_cast<size_t>(luaL_checkinteger(L, 2));

            std::vector<uint8_t> buffer;
            if (length > 0)
            {
                buffer.resize(length);
                interop::readMemory(address, buffer.data(), buffer.size());
            }

            lua_newtable(L);
            for (size_t i = 0; i < buffer.size(); i++)
            {
                lua_pushinteger(L, buffer[i]);
                lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
            }
            return 1;
        }

        int unsafeWrite(lua_State* L)
        {
            auto address = static_cast<uint32_t>(luaL_checkinteger(L, 1));
            luaL_checktype(L, 2, LUA_TTABLE);

            auto length = static_cast<size_t>(luaL_len(L, 2));
            std::vector<uint8_t> buffer(length);

            size_t i = 0;
            lua_pushnil(L);
            while (lua_next(L, 2) != 0)
            {
                if (i < buffer.size())
                {
                    buffer[i] = static_cast<uint8_t>(luaL_checkinteger(L, -1) & 0xFF);
                }
                i++;
                lua_pop(L, 1);
            }

            interop::writeMemory(address, buffer.data(), buffer.size());
            return 0;
        }
#endif
    }

    void registerBindings(lua_State* L, LuaVm* vm)
    {
        setGlobalWithVm(L, "print", apiPrint, vm);

        setGlobalWithVm(L, "re.subscribe", apiSubscribe, vm);
        setGlobal(L, "re.getFlag", apiGetFlag);
        setGlobal(L, "re.setFlag", apiSetFlag);
        setGlobal(L, "re.getEntity", apiGetEntity);
        setGlobalWithVm(L, "re.log", apiLog, vm);
        setGlobal(L, "re.getEnv", apiGetEnv);
        setGlobal(L, "re.collectGarbage", apiCollectGarbage);
#ifdef DEBUG
        setGlobal(L, "re.eval", apiEval);
        setGlobal(L, "re.evalIn", apiEvalIn);
        setGlobal(L, "re.unsafe.read", unsafeRead);
        setGlobal(L, "re.unsafe.write", unsafeWrite);
#endif

        setGlobal(L, "HookKind.tick", static_cast<int32_t>(HookKind::tick));

        setGlobal(L, "LogLevel.info", LogLevel::info);
        setGlobal(L, "LogLevel.warning", LogLevel::warning);
        setGlobal(L, "LogLevel.error", LogLevel::error);
        setGlobal(L, "LogLevel.debug", LogLevel::debug);

        setGlobal(L, "EntityKind.player", EntityKind::player);
        setGlobal(L, "EntityKind.splayer", EntityKind::splayer);
        setGlobal(L, "EntityKind.enemy", EntityKind::enemy);
        setGlobal(L, "EntityKind.object", EntityKind::object);
        setGlobal(L, "EntityKind.door", EntityKind::door);

        // SpriteComponent flag bits, composable with bitwise OR (`|`).
        setGlobal(L, "SpriteFlags.repeatX", static_cast<int32_t>(ecs::SpriteFlags::RepeatX));
        setGlobal(L, "SpriteFlags.repeatY", static_cast<int32_t>(ecs::SpriteFlags::RepeatY));
        setGlobal(L, "SpriteFlags.stretchX", static_cast<int32_t>(ecs::SpriteFlags::StretchX));
        setGlobal(L, "SpriteFlags.stretchY", static_cast<int32_t>(ecs::SpriteFlags::StretchY));

        // TextComponent alignment flags, composable with bitwise OR (`|`).
        setGlobal(L, "TextFlags.xLeft", static_cast<int32_t>(ecs::TextFlags::XLeft));
        setGlobal(L, "TextFlags.xCenter", static_cast<int32_t>(ecs::TextFlags::XCenter));
        setGlobal(L, "TextFlags.xRight", static_cast<int32_t>(ecs::TextFlags::XRight));
        setGlobal(L, "TextFlags.yTop", static_cast<int32_t>(ecs::TextFlags::YTop));
        setGlobal(L, "TextFlags.yMiddle", static_cast<int32_t>(ecs::TextFlags::YMiddle));
        setGlobal(L, "TextFlags.yBottom", static_cast<int32_t>(ecs::TextFlags::YBottom));

        // scene.* - ECS scene-tree API (component names are namespaced:
        // core.* for built-ins, app.* reserved for future Lua components).
        setGlobal(L, "scene.createEntity", sceneCreateEntity);
        setGlobal(L, "scene.getRoot", sceneGetRoot);
        setGlobal(L, "scene.setParent", sceneSetParent);
        setGlobal(L, "scene.destroy", sceneDestroy);
        setGlobal(L, "scene.addComponent", sceneAddComponent);
        setGlobal(L, "scene.setField", sceneSetField);
        setGlobal(L, "scene.getField", sceneGetField);
        setGlobal(L, "scene.exit", sceneExit);
        setGlobal(L, "scene.allEntities", sceneAllEntities);
        setGlobal(L, "scene.entityFromId", sceneEntityFromId);
        setGlobal(L, "scene.component", sceneComponent);
        setGlobal(L, "scene.getComponents", sceneGetComponents);
        setGlobal(L, "scene.getFieldNames", sceneGetFieldNames);
        setGlobal(L, "scene.parent", sceneParent);
        setGlobal(L, "scene.children", sceneChildren);

        // gfx.* - 2D engine rendering (textures, fonts, window size, fades).
        setGlobal(L, "gfx.loadTexture", gfxLoadTexture);
        setGlobal(L, "gfx.loadFont", gfxLoadFont);
        setGlobal(L, "gfx.screenWidth", gfxScreenWidth);
        setGlobal(L, "gfx.screenHeight", gfxScreenHeight);
        setGlobal(L, "gfx.fade", gfxFade);
        setGlobal(L, "gfx.listResources", gfxListResources);
        setGlobal(L, "gfx.getResourceImage", gfxGetResourceImage);

        setGlobal(L, "audio.loadAudio", audioLoadAudio);
        setGlobal(L, "audio.play", audioPlay);

        // save.* - saved-game listing for the Lua load/save screens.
        setGlobal(L, "save.list", saveList);

        // core.* - engine services available to game scripts.
        setGlobalWithVm(L, "core.loadMovie", coreLoadMovie, vm);
        setGlobalWithVm(L, "core.playMovie", apiPlayMovie, vm);

        // input.* - runtime command state for menu / title-screen navigation.
        setGlobal(L, "input.down", inputCommandDown);
        setGlobal(L, "input.pressed", inputCommandPressed);

        luaL_newmetatable(L, METATABLE_SCENE_ENTITY);
        lua_pop(L, 1);
        luaL_newmetatable(L, METATABLE_SCENE_COMPONENT);
        lua_pop(L, 1);
        luaL_newmetatable(L, METATABLE_TEXTURE);
        lua_pushcfunction(L, resourceHandleGC<TextureHandle>);
        lua_setfield(L, -2, "__gc");
        lua_pop(L, 1);
        luaL_newmetatable(L, METATABLE_FONT);
        lua_pushcfunction(L, resourceHandleGC<FontHandle>);
        lua_setfield(L, -2, "__gc");
        lua_pop(L, 1);
        luaL_newmetatable(L, METATABLE_SOUND);
        lua_pushcfunction(L, resourceHandleGC<SoundHandle>);
        lua_setfield(L, -2, "__gc");
        lua_pop(L, 1);
        luaL_newmetatable(L, METATABLE_MOVIE);
        lua_pushcfunction(L, resourceHandleGC<MovieHandle>);
        lua_setfield(L, -2, "__gc");
        lua_pop(L, 1);

        luaL_newmetatable(L, METATABLE_ENTITY);
        lua_pushcfunction(L, entity_get);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, entity_set);
        lua_setfield(L, -2, "__newindex");
        lua_pop(L, 1);

        registerNetworkBindings(L, vm);
    }
}
