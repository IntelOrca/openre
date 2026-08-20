#include "script_bindings.h"
#include "app.h"
#include "ecs_components.h"
#include "ecs_scene.h"
#include "ecs_systems.h"
#include "font.h"
#include "interop.hpp"
#include "logger.h"
#include "openre.h"
#include "render2d.h"
#include "resmgr.h"
#include "sce.h"
#include "script_network.h"
#include "script_vm.h"
#include "system_input.h"
#include "system_window.h"
#include "systems.h"
#include "texture_loader.h"
#include "video_system.h"
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

        // ── scene.* / gfx.* ────────────────────────────────────────────────
        // The scene-tree API. Entities and components are light userdata
        // handles into the current ECS scene; textures are userdata wrapping a
        // ResourceCookie that the resource manager releases on __gc.

        namespace ComponentKind
        {
            constexpr int32_t transform = 1;
            constexpr int32_t sprite = 2;
            constexpr int32_t text = 3;
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

        int sceneCreateEntity(lua_State* L)
        {
            auto* scene = current_scene();
            if (scene == nullptr)
            {
                lua_pushnil(L);
                return 1;
            }

            auto entity = scene->createEntity(scene->root());
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
                if (strcmp(name, "x") == 0)
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
                if (strcmp(name, "x") == 0)
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
            // TIMs back to back (e.g. tmoji.bin). 0 (default) = the first.
            auto timIndex = static_cast<int>(luaL_optinteger(L, 4, 0));

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

        int fontRelease(lua_State* L)
        {
            auto* font = static_cast<FontHandle*>(luaL_checkudata(L, 1, METATABLE_FONT));
            if (font->cookie != 0)
            {
                ResourceManager::get().release(font->cookie);
                font->cookie = 0;
            }
            return 0;
        }

        int textureRelease(lua_State* L)
        {
            auto* tex = static_cast<TextureHandle*>(luaL_checkudata(L, 1, METATABLE_TEXTURE));
            if (tex->cookie != 0)
            {
                ResourceManager::get().release(tex->cookie);
                tex->cookie = 0;
            }
            return 0;
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

        // core.playMovie(path) - plays a movie, blocking until it finishes or
        // any input skips it. The video entity is created automatically to fill
        // the window; rendering is driven here because this call runs inside a
        // Lua tick hook and the rest of the frame pipeline is suspended until
        // it returns.
        int apiPlayMovie(lua_State* L)
        {
            const char* path = luaL_checkstring(L, 1);
            auto* app = openre::current_app();
            if (app == nullptr)
                return 0;

            ecs::Entity entity = video_play(*app, path);
            if (entity == ecs::NullEntity)
            {
                logging::logWarning("[core] playMovie failed to start: {}", path);
                return 0;
            }

            // Reveal the video: clear any full-screen fade left by the splash.
            render2d::SpriteRenderer::get().setFade({});

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
                    video_stop(*app, entity);
                    break;
                }

                // Advance the video and present the frame. frameActive is set
                // so RenderSystem performs the present this pass.
                app->frameActive = true;
                VideoSystem(*app);
                RenderSystem(*app);

                system::window::delay(8);
            }

            // Tear down the video entity.
            video_stop(*app, entity);
            auto* scene = openre::current_scene();
            if (scene != nullptr && scene->isValid(entity))
                scene->destroyEntity(entity);
            app->frameActive = false;
            return 0;
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
#ifdef DEBUG
        setGlobal(L, "re.eval", apiEval);
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
        setGlobal(L, "scene.setParent", sceneSetParent);
        setGlobal(L, "scene.addComponent", sceneAddComponent);
        setGlobal(L, "scene.setField", sceneSetField);
        setGlobal(L, "scene.getField", sceneGetField);
        setGlobal(L, "scene.exit", sceneExit);

        // gfx.* - 2D engine rendering (textures, fonts, window size, fades).
        setGlobal(L, "gfx.loadTexture", gfxLoadTexture);
        setGlobal(L, "gfx.loadFont", gfxLoadFont);
        setGlobal(L, "gfx.screenWidth", gfxScreenWidth);
        setGlobal(L, "gfx.screenHeight", gfxScreenHeight);
        setGlobal(L, "gfx.fade", gfxFade);

        // core.* - engine services available to game scripts.
        setGlobalWithVm(L, "core.playMovie", apiPlayMovie, vm);

        luaL_newmetatable(L, METATABLE_SCENE_ENTITY);
        lua_pop(L, 1);
        luaL_newmetatable(L, METATABLE_SCENE_COMPONENT);
        lua_pop(L, 1);
        luaL_newmetatable(L, METATABLE_TEXTURE);
        lua_pushcfunction(L, textureRelease);
        lua_setfield(L, -2, "__gc");
        lua_pop(L, 1);
        luaL_newmetatable(L, METATABLE_FONT);
        lua_pushcfunction(L, fontRelease);
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
