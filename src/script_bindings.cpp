#include "script_bindings.h"
#include "interop.hpp"
#include "logger.h"
#include "openre.h"
#include "sce.h"
#include "script_network.h"
#include "script_vm.h"
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

        luaL_newmetatable(L, METATABLE_ENTITY);
        lua_pushcfunction(L, entity_get);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, entity_set);
        lua_setfield(L, -2, "__newindex");
        lua_pop(L, 1);

        registerNetworkBindings(L, vm);
    }
}
