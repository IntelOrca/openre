#include "relua.h"
#include "font.h"
#include "input.h"
#include "interop.hpp"
#include "mod.h"
#include "movie.h"
#include "openre.h"
#include "resmgr.h"
#include "sce.h"
#include "shell.h"
#include <cstdio>
#include <lua.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>

using namespace openre::graphics;
using namespace openre::input;
using namespace openre::modding;
using namespace openre::movie;
using namespace openre::shellextensions;

// LUA extensions
namespace openre::lua
{
    template<typename T> T luaxGetTable(lua_State* L, const char* key, int idx);

    template<> float luaxGetTable(lua_State* L, const char* key, int idx)
    {
        auto tableIndex = lua_absindex(L, idx);
        lua_pushstring(L, key);
        lua_gettable(L, tableIndex);
        auto result = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return static_cast<float>(result);
    }

    template<typename T> void luaxSetTable(lua_State* L, const char* key, T value, int idx)
    {
        static_assert(std::is_arithmetic<T>());
        auto tableIndex = lua_absindex(L, idx);
        lua_pushstring(L, key);
        if constexpr (std::is_floating_point<T>::value)
            lua_pushnumber(L, value);
        else
            lua_pushinteger(L, value);
        lua_settable(L, tableIndex);
    }
}

namespace openre::lua
{
    constexpr const char* METATABLE_ENTITY = "meta_entity";
    constexpr const char* METATABLE_GFX = "meta_gfx";
    constexpr const char* METATABLE_INPUT = "meta_input";
    constexpr const char* METATABLE_MOVIE = "meta_movie";
    constexpr const char* METATABLE_TEXTURE = "meta_texture";
    constexpr const char* METATABLE_TEXTURE_RECT = "meta_textureRect";
    constexpr const char* METATABLE_FONT = "meta_font";
    constexpr const char* METATABLE_SOUND = "meta_sound";

    class LuaVmImpl : public LuaVm
    {
    private:
        struct HookInfo
        {
            HookKind kind;
            int ref;
        };

        std::string_view path;
        lua_State* _state;
        std::vector<HookInfo> _subscriptions;
        std::function<void(const std::string& s)> _logCallback;
        OpenREShell* _shell{};

        enum class UserTypeKind
        {
            texture,
            textureRect,
            font,
            movie,
            sound,
        };

        struct UserType
        {
            UserTypeKind kind;
        };

        struct UserTexture : public UserType
        {
            ResourceCookie cookie;
            uint32_t width;
            uint32_t height;
        };

        struct UserTextureRect : public UserType
        {
            ResourceCookie cookie;
            float s0;
            float t0;
            float s1;
            float t1;
        };

        struct UserFont : public UserType
        {
            ResourceCookie cookie;
        };

        struct UserMovie : public UserType
        {
            ResourceCookie cookie;
        };

        struct UserSound : public UserType
        {
            ResourceCookie cookie;
        };

    public:
        LuaVmImpl()
        {
            createState();
        }

        LuaVmImpl(const LuaVm&) = delete;

        ~LuaVmImpl() override
        {
            close();
        }

        void close()
        {
            this->_subscriptions.clear();
            lua_close(_state);
            _state = nullptr;
        }

        void run(std::string_view path) override
        {
            this->path = path;
            loadScript(this->_state, path);
        }

        void callHooks(HookKind kind) override
        {
            for (const auto& h : _subscriptions)
            {
                if (h.kind == kind)
                {
                    lua_rawgeti(_state, LUA_REGISTRYINDEX, h.ref);
                    auto result = lua_pcall(_state, 0, 0, 0);
                    if (result != LUA_OK)
                    {
                        auto errString = lua_tostring(_state, -1);
                        log("Error: " + std::string(errString));
                    }
                }
            }
        }

        void gc() override
        {
            lua_gc(_state, LUA_GCCOLLECT);
        }

        void setLogCallback(std::function<void(const std::string& s)> s) override
        {
            _logCallback = s;
        }

        void setShell(OpenREShell* shell) override
        {
            _shell = shell;
        }

    private:
        static constexpr luaL_Reg standardLibraries[] = { { "_G", luaopen_base },
                                                          { LUA_COLIBNAME, luaopen_coroutine },
                                                          { LUA_TABLIBNAME, luaopen_table },
                                                          { LUA_STRLIBNAME, luaopen_string },
                                                          { LUA_UTF8LIBNAME, luaopen_utf8 },
                                                          { LUA_MATHLIBNAME, luaopen_math } };

        static constexpr const char* unsafeFunctions[] = { "collectgarbage", //
                                                           "dofile",         //
                                                           "_G",
                                                           "getfenv",
                                                           "getmetatable",
                                                           "load",
                                                           "loadfile",
                                                           "loadstring",
                                                           "rawequal",
                                                           "rawget",
                                                           "rawset",
                                                           "setfenv" };

        void log(const std::string& s)
        {
            if (_logCallback)
                _logCallback(s);
        }

        void loadScript(lua_State* L, std::string_view path)
        {
            if (pushRequireResult(L, path))
                return;

            auto loadResult = loadFile(*this->_shell, path, { ".lua" });
            if (loadResult.success)
            {
                std::string chunk((const char*)loadResult.buffer.data(), loadResult.buffer.size());
                if (luaL_loadbufferx(_state, chunk.c_str(), chunk.size(), std::string(path).c_str(), "t") == LUA_OK)
                {
                    if (lua_pcall(L, 0, LUA_MULTRET, 0) == LUA_OK)
                    {
                        setRequireResult(L, path);
                        pushRequireResult(L, path);
                        return;
                    }
                }

                auto errString = lua_tostring(_state, -1);
                lua_pop(_state, 1);
                log("Error: " + std::string(errString));
            }
        }

        static bool pushRequireResult(lua_State* L, std::string_view path)
        {
            lua_getfield(L, LUA_REGISTRYINDEX, "loaded");
            lua_pushlstring(L, path.data(), path.size());
            lua_gettable(L, -2);
            lua_remove(L, -2);
            if (lua_isnil(L, -1))
            {
                lua_pop(L, 1);
                return false;
            }
            else
            {
                lua_pushstring(L, "result");
                lua_gettable(L, -2);
                lua_remove(L, -2);
                return true;
            }
        }

        static void setRequireResult(lua_State* L, std::string_view path)
        {
            lua_newtable(L);
            lua_pushstring(L, "result");
            lua_pushvalue(L, -3);
            lua_remove(L, -4);
            lua_settable(L, -3);
            lua_getfield(L, LUA_REGISTRYINDEX, "loaded");
            lua_pushlstring(L, path.data(), path.size());
            lua_pushvalue(L, -3);
            lua_settable(L, -3);
            lua_pop(L, 2);
        }

        void createState()
        {
            _state = luaL_newstate();
            lua_gc(_state, LUA_GCSTOP);

            // Standard LUA APIs
            for (const auto& r : standardLibraries)
            {
                loadStandardLibrary(r);
            }

            // Remove unsafe APIs
            for (const auto& fName : unsafeFunctions)
            {
                removeGlobal(fName);
            }

            // Require table
            lua_newtable(_state);
            lua_setfield(_state, LUA_REGISTRYINDEX, "loaded");

            // RE API
            setGlobal("print", apiPrint);
            setGlobal("require", apiRequire);
            setGlobal("re.subscribe", apiSubscribe);
            setGlobal("re.getFlag", apiGetFlag);
            setGlobal("re.setFlag", apiSetFlag);
            setGlobal("re.getEntity", apiGetEntity);

            setGlobal("gfx.loadTexture", apiGfxLoadTexture);
            setGlobal("gfx.getTextureRect", apiGfxGetTextureRect);
            setGlobal("gfx.drawTexture", apiGfxDrawTexture);
            setGlobal("gfx.drawSolid", apiGfxDrawSolid);
            setGlobal("gfx.fade", apiGfxFade);
            setGlobal("gfx.loadFont", apiGfxLoadFont);
            setGlobal("gfx.drawText", apiGfxDrawText);
            setGlobal("gfx.loadMovie", apiGfxLoadMovie);

            setGlobal("sfx.loadSound", apiSfxLoadSound);
            setGlobal("sfx.playSound", apiSfxPlaySound);

            setGlobal("input.isDown", apiInputIsDown<false>);
            setGlobal("input.isPressed", apiInputIsDown<true>);

            setGlobal("InputCommand.up", static_cast<int32_t>(InputCommand::up));
            setGlobal("InputCommand.down", static_cast<int32_t>(InputCommand::down));
            setGlobal("InputCommand.left", static_cast<int32_t>(InputCommand::left));
            setGlobal("InputCommand.right", static_cast<int32_t>(InputCommand::right));
            setGlobal("InputCommand.menuStart", static_cast<int32_t>(InputCommand::menuStart));
            setGlobal("InputCommand.menuApply", static_cast<int32_t>(InputCommand::menuApply));
            setGlobal("InputCommand.menuCancel", static_cast<int32_t>(InputCommand::menuCancel));
            setGlobal("InputCommand.inventory", static_cast<int32_t>(InputCommand::inventory));
            setGlobal("InputCommand.map", static_cast<int32_t>(InputCommand::map));
            setGlobal("InputCommand.aimManual", static_cast<int32_t>(InputCommand::aimManual));
            setGlobal("InputCommand.aimAuto", static_cast<int32_t>(InputCommand::aimAuto));
            setGlobal("InputCommand.aimAutoEnemy", static_cast<int32_t>(InputCommand::aimAutoEnemy));
            setGlobal("InputCommand.aimAutoObject", static_cast<int32_t>(InputCommand::aimAutoObject));
            setGlobal("InputCommand.aimNext", static_cast<int32_t>(InputCommand::aimNext));
            setGlobal("InputCommand.fire", static_cast<int32_t>(InputCommand::fire));
            setGlobal("InputCommand.reload", static_cast<int32_t>(InputCommand::reload));
            setGlobal("InputCommand.run", static_cast<int32_t>(InputCommand::run));
            setGlobal("InputCommand.menu", static_cast<int32_t>(InputCommand::menu));
            setGlobal("InputCommand.quickTurn", static_cast<int32_t>(InputCommand::quickTurn));

            setGlobal("HookKind.tick", static_cast<int32_t>(HookKind::tick));

            setGlobal("EntityKind.player", 1);
            setGlobal("EntityKind.splayer", 2);
            setGlobal("EntityKind.enemy", 3);
            setGlobal("EntityKind.object", 4);
            setGlobal("EntityKind.door", 5);

            setGlobal("MovieState.blank", static_cast<int32_t>(MovieState::blank));
            setGlobal("MovieState.unsupported", static_cast<int32_t>(MovieState::unsupported));
            setGlobal("MovieState.error", static_cast<int32_t>(MovieState::error));
            setGlobal("MovieState.stopped", static_cast<int32_t>(MovieState::stopped));
            setGlobal("MovieState.playing", static_cast<int32_t>(MovieState::playing));
            setGlobal("MovieState.finished", static_cast<int32_t>(MovieState::finished));

            setMetatable(METATABLE_GFX, apiGfxGetProperty);
            setMetatable(METATABLE_ENTITY, entity_get, entity_set);
            setMetatable(METATABLE_INPUT, apiInputGetProperty, apiInputSetProperty);
            setMetatable(METATABLE_MOVIE, movie_get, nullptr, movie_gc);
            setMetatable(METATABLE_TEXTURE, nullptr, nullptr, texture_gc);
            setMetatable(METATABLE_TEXTURE_RECT, nullptr, nullptr, textureRect_gc);

            attachMetatable("gfx", METATABLE_GFX);
            attachMetatable("input", METATABLE_INPUT);

            setGlobal("unsafe.read", unsafe_read);
            setGlobal("unsafe.write", unsafe_write);
        }

        void loadStandardLibrary(const luaL_Reg& r)
        {
            luaL_requiref(_state, r.name, r.func, 1);
            lua_pop(_state, 1);
        }

        void removeGlobal(std::string_view name)
        {
            lua_pushnil(_state);
            lua_setglobal(_state, std::string(name).c_str());
        }

        void setGlobal(std::string_view fullName, int32_t value)
        {
            auto [ns, name] = SplitNamespace(fullName);
            getOrCreateAndPushGlobal(ns);
            lua_pushlstring(_state, name.data(), name.size());
            lua_pushinteger(_state, value);
            lua_settable(_state, -3);
            lua_pop(_state, 1);
        }

        void setGlobal(std::string_view fullName, lua_CFunction f)
        {
            auto [ns, name] = SplitNamespace(fullName);
            getOrCreateAndPushGlobal(ns);
            lua_pushlstring(_state, name.data(), name.size());
            lua_pushlightuserdata(_state, this);
            lua_pushcclosure(_state, f, 1);
            lua_settable(_state, -3);
            lua_pop(_state, 1);
        }

        void attachMetatable(const char* name, const char* metatableName)
        {
            getOrCreateAndPushGlobal(name);
            luaL_getmetatable(_state, metatableName);
            lua_setmetatable(_state, -2);
            lua_pop(_state, 1);
        }

        void setMetatable(const char* name, lua_CFunction getter, lua_CFunction setter = nullptr, lua_CFunction gc = nullptr)
        {
            luaL_newmetatable(_state, name);
            if (getter != nullptr)
            {
                lua_pushlightuserdata(_state, this);
                lua_pushcclosure(_state, getter, 1);
                lua_setfield(_state, -2, "__index");
            }
            if (setter != nullptr)
            {
                lua_pushlightuserdata(_state, this);
                lua_pushcclosure(_state, setter, 1);
                lua_setfield(_state, -2, "__newindex");
            }
            if (gc != nullptr)
            {
                lua_pushlightuserdata(_state, this);
                lua_pushcclosure(_state, gc, 1);
                lua_setfield(_state, -2, "__gc");
            }
            lua_pop(_state, 1);
        }

        void getOrCreateAndPushGlobal(std::string_view s)
        {
            lua_pushglobaltable(_state);
            while (!s.empty())
            {
                auto part = s;
                auto delim = s.find('.');
                if (delim == std::string::npos)
                {
                    s = "";
                }
                else
                {
                    part = s.substr(0, delim);
                    s = s.substr(delim + 1);
                }

                lua_pushlstring(_state, part.data(), part.size());
                lua_gettable(_state, -2);
                if (!lua_istable(_state, -1))
                {
                    lua_pop(_state, 1);
                    lua_newtable(_state);
                    lua_pushlstring(_state, part.data(), part.size());
                    lua_pushvalue(_state, -2);
                    lua_settable(_state, -4);
                }
                lua_remove(_state, -2);
            }
        }

        void printStack()
        {
            auto top = lua_gettop(_state);
            std::printf("Lua Stack (size: %d):\n", top);
            for (auto i = 1; i <= top; i++)
            {
                auto type = lua_type(_state, i);
                printf("%d: ", i);
                switch (type)
                {
                case LUA_TSTRING: printf("string: %s", lua_tostring(_state, i)); break;
                case LUA_TNUMBER: printf("number: %f", lua_tonumber(_state, i)); break;
                case LUA_TBOOLEAN: // Boolean
                    printf("boolean: %s", lua_toboolean(_state, i) ? "true" : "false");
                    break;
                case LUA_TTABLE:
                    printf("table: [");
                    lua_pushnil(_state);
                    while (lua_next(_state, i) != 0)
                    {
                        if (lua_isstring(_state, -2))
                        {
                            printf("%s = ", lua_tostring(_state, -2));
                        }
                        printf("%s", lua_tostring(_state, -1));
                        lua_pop(_state, 1);
                    }
                    printf("]");
                    break;
                case LUA_TFUNCTION: printf("function"); break;
                case LUA_TUSERDATA: printf("userdata"); break;
                case LUA_TTHREAD: printf("thread"); break;
                case LUA_TNIL: printf("nil"); break;
                default: printf("unknown"); break;
                }
                printf("\n");
            }
            printf("\n");
        }

        static std::tuple<std::string_view, std::string_view> SplitNamespace(std::string_view s)
        {
            auto delim = s.find_last_of('.');
            auto ns = delim == std::string::npos ? "" : s.substr(0, delim);
            auto name = s.substr(delim + 1);
            return { ns, name };
        }

        static int apiPrint(lua_State* L)
        {
            auto ptr = static_cast<LuaVmImpl*>(lua_touserdata(L, lua_upvalueindex(1)));
            auto s = luaL_checkstring(L, 1);
            ptr->log(s);
            return 0;
        }

        static int apiRequire(lua_State* L)
        {
            auto ptr = static_cast<LuaVmImpl*>(lua_touserdata(L, lua_upvalueindex(1)));

            auto s = luaL_checkstring(L, 1);
            ptr->loadScript(L, s);
            return 1;
        }

        static int apiSubscribe(lua_State* L)
        {
            auto ptr = static_cast<LuaVmImpl*>(lua_touserdata(L, lua_upvalueindex(1)));

            auto kind = static_cast<size_t>(lua_tointeger(L, 1));
            if (kind <= static_cast<size_t>(HookKind::undefined))
                return 0;
            if (kind > static_cast<size_t>(HookKind::tick))
                return 0;

            lua_pushvalue(L, 2);
            int ref = luaL_ref(L, LUA_REGISTRYINDEX);

            auto& h = ptr->_subscriptions.emplace_back();
            h.kind = static_cast<HookKind>(kind);
            h.ref = ref;
            return 0;
        }

        static int apiGetFlag(lua_State* L)
        {
            auto group = lua_tointeger(L, 1);
            auto index = lua_tointeger(L, 2);
            auto value = check_flag(static_cast<FlagGroup>(group), static_cast<uint32_t>(index));
            lua_pushboolean(L, value);
            return 1;
        }

        static int apiSetFlag(lua_State* L)
        {
            auto group = lua_tointeger(L, 1);
            auto index = lua_tointeger(L, 2);
            auto value = lua_toboolean(L, 3);
            set_flag(static_cast<FlagGroup>(group), static_cast<uint32_t>(index), value);
            return 0;
        }

        static int apiGetEntity(lua_State* L)
        {
            auto custom = (int32_t*)lua_newuserdata(L, 8);
            custom[0] = static_cast<int32_t>(luaL_checknumber(L, 1));
            custom[1] = static_cast<int32_t>(luaL_checknumber(L, 2));
            luaL_getmetatable(L, METATABLE_ENTITY);
            lua_setmetatable(L, -2);
            return 1;
        }

        static int entity_get(lua_State* L)
        {
            auto userdata = (int32_t*)luaL_checkudata(L, 1, METATABLE_ENTITY);
            auto key = luaL_checkstring(L, 2);
            if (strcmp(key, "kind") == 0)
            {
                lua_pushnumber(L, userdata[0]);
            }
            else if (strcmp(key, "index") == 0)
            {
                lua_pushnumber(L, userdata[1]);
            }
            else if (userdata[0] == 3)
            {
                auto enemy = (EnemyEntity*)sce::GetEnemyEntity(userdata[1]);
                if (enemy != nullptr)
                {
                    if (strcmp(key, "type") == 0)
                    {
                        lua_pushnumber(L, enemy->id);
                    }
                    else if (strcmp(key, "life") == 0)
                    {
                        lua_pushnumber(L, enemy->life);
                    }
                    else
                    {
                        lua_pushnil(L);
                    }
                }
                else
                {
                    lua_pushnil(L);
                }
            }
            else
            {
                lua_pushnil(L);
            }
            return 1;
        }

        static int entity_set(lua_State* L)
        {
            auto userdata = (int32_t*)luaL_checkudata(L, 1, METATABLE_ENTITY);
            auto key = luaL_checkstring(L, 2);
            auto value = luaL_checkinteger(L, 3);
            if (userdata[0] == 3)
            {
                auto enemy = (EnemyEntity*)sce::GetEnemyEntity(userdata[1]);
                if (enemy != nullptr)
                {
                    if (strcmp(key, "life") == 0)
                    {
                        enemy->life = static_cast<int16_t>(value);
                    }
                }
            }
            return 0;
        }

        static int unsafe_read(lua_State* L)
        {
            auto address = static_cast<uint32_t>(luaL_checkinteger(L, 1));
            auto len = luaL_checkinteger(L, 2);

            std::vector<uint8_t> buffer;
            if (len > 0)
            {
                buffer.resize(static_cast<size_t>(len));
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

        static int unsafe_write(lua_State* L)
        {
            auto address = static_cast<uint32_t>(luaL_checkinteger(L, 1));
            luaL_checktype(L, 2, LUA_TTABLE);

            auto len = static_cast<size_t>(luaL_len(L, 2));
            std::vector<uint8_t> buffer;
            buffer.resize(len);
            size_t i = 0;
            lua_pushnil(L);
            while (lua_next(L, 2) != 0)
            {
                buffer[i++] = luaL_checkinteger(L, -1) & 0xFF;
                lua_pop(L, 1);
            }

            interop::writeMemory(address, buffer.data(), buffer.size());
            return 0;
        }

        static int apiGfxGetProperty(lua_State* L)
        {
            auto shell = GetContextShell(L);
            if (!shell)
            {
                lua_pushnil(L);
                return 1;
            }

            auto key = luaL_checkstring(L, 2);
            if (strcmp(key, "screenWidth") == 0)
            {
                lua_pushinteger(L, shell->getRenderSize().width);
            }
            else if (strcmp(key, "screenHeight") == 0)
            {
                lua_pushinteger(L, shell->getRenderSize().height);
            }
            else
            {
                lua_pushnil(L);
            }
            return 1;
        }

        static int apiGfxLoadTexture(lua_State* L)
        {
            auto shell = GetContextShell(L);
            if (!shell)
            {
                lua_pushnil(L);
                return 1;
            }

            auto path = luaL_checkstring(L, 1);
            auto width = luaL_checkinteger(L, 2);
            auto height = luaL_checkinteger(L, 3);

            auto cookie = shell->loadTexture(path, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
            if (cookie == 0)
            {
                lua_pushnil(L);
                return 1;
            }

            auto custom = CreateUserObject<UserTexture>(L, UserTypeKind::texture);
            custom->cookie = cookie;
            custom->width = static_cast<uint32_t>(width);
            custom->height = static_cast<uint32_t>(height);
            luaL_getmetatable(L, METATABLE_TEXTURE);
            lua_setmetatable(L, -2);
            return 1;
        }

        static int apiGfxGetTextureRect(lua_State* L)
        {
            auto shell = GetContextShell(L);
            if (!shell)
            {
                lua_pushnil(L);
                return 1;
            }

            auto& resourceManager = shell->getResourceManager();

            auto texture = (UserTexture*)lua_touserdata(L, 1);
            auto left = luaL_checkinteger(L, 2);
            auto top = luaL_checkinteger(L, 3);
            auto right = luaL_checkinteger(L, 4);
            auto bottom = luaL_checkinteger(L, 5);

            auto custom = CreateUserObject<UserTextureRect>(L, UserTypeKind::textureRect);
            custom->cookie = resourceManager.dupRef(texture->cookie);
            custom->s0 = left / (float)texture->width;
            custom->t0 = top / (float)texture->height;
            custom->s1 = right / (float)texture->width;
            custom->t1 = bottom / (float)texture->height;
            luaL_getmetatable(L, METATABLE_TEXTURE_RECT);
            lua_setmetatable(L, -2);
            return 1;
        }

        static int texture_gc(lua_State* L)
        {
            auto self = GetContext(L);
            if (!self->_shell)
            {
                lua_pushnil(L);
                return 1;
            }

            auto userTexture = GetUserObject<UserTexture>(L, 1, METATABLE_TEXTURE);

            auto& resourceManager = self->_shell->getResourceManager();
            resourceManager.release(userTexture->cookie);
            return 0;
        }

        static int textureRect_gc(lua_State* L)
        {
            auto self = GetContext(L);
            if (!self->_shell)
            {
                lua_pushnil(L);
                return 1;
            }

            auto userTextureRect = GetUserObject<UserTextureRect>(L, 1, METATABLE_TEXTURE_RECT);

            auto& resourceManager = self->_shell->getResourceManager();
            resourceManager.release(userTextureRect->cookie);
            return 0;
        }

        static int apiGfxDrawTexture(lua_State* L)
        {
            auto shell = GetContextShell(L);
            if (!shell)
                return 0;

            auto arg0 = (UserType*)lua_touserdata(L, 1);
            if (arg0 == nullptr)
                return 0;

            auto x = static_cast<float>(luaL_checknumber(L, 2));
            auto y = static_cast<float>(luaL_checknumber(L, 3));
            auto z = static_cast<float>(luaL_checknumber(L, 4));
            auto w = static_cast<float>(luaL_checknumber(L, 5));
            auto h = static_cast<float>(luaL_checknumber(L, 6));

            if (arg0->kind == UserTypeKind::texture)
            {
                drawTexture(*shell, ((UserTexture*)arg0)->cookie, x, y, z, w, h);
            }
            else if (arg0->kind == UserTypeKind::textureRect)
            {
                auto rect = (UserTextureRect*)arg0;
                drawTexture(*shell, rect->cookie, x, y, z, w, h, rect->s0, rect->t0, rect->s1, rect->t1);
            }
            else if (arg0->kind == UserTypeKind::movie)
            {
                auto movie = (UserMovie*)arg0;
                drawMovie(*shell, movie->cookie, x, y, z, w, h);
            }

            return 0;
        }

        static int apiGfxDrawSolid(lua_State* L)
        {
            auto shell = GetContextShell(L);
            if (!shell)
                return 0;

            Color4f color{};
            color.r = luaxGetTable<float>(L, "red", 1);
            color.g = luaxGetTable<float>(L, "green", 1);
            color.b = luaxGetTable<float>(L, "blue", 1);
            color.a = luaxGetTable<float>(L, "alpha", 1);

            auto x = static_cast<float>(luaL_checknumber(L, 2));
            auto y = static_cast<float>(luaL_checknumber(L, 3));
            auto z = static_cast<float>(luaL_checknumber(L, 4));
            auto w = static_cast<float>(luaL_checknumber(L, 5));
            auto h = static_cast<float>(luaL_checknumber(L, 6));
            drawSolid(*shell, color, x, y, z, w, h);
            return 0;
        }

        static int apiGfxFade(lua_State* L)
        {
            auto shell = GetContextShell(L);
            if (!shell)
            {
                return 0;
            }

            auto r = static_cast<float>(luaL_checknumber(L, 1));
            auto g = static_cast<float>(luaL_checknumber(L, 2));
            auto b = static_cast<float>(luaL_checknumber(L, 3));
            auto a = static_cast<float>(luaL_checknumber(L, 4));

            fade(*shell, r, g, b, a);
            return 0;
        }

        static int apiGfxLoadFont(lua_State* L)
        {
            auto shell = GetContextShell(L);
            if (!shell)
            {
                lua_pushnil(L);
                return 1;
            }

            auto path = luaL_checkstring(L, 1);

            auto fontCookie = loadFont(*shell, path);
            if (fontCookie == 0)
            {
                lua_pushnil(L);
                return 1;
            }

            auto custom = CreateUserObject<UserFont>(L, UserTypeKind::font);
            custom->cookie = fontCookie;
            luaL_getmetatable(L, METATABLE_FONT);
            lua_setmetatable(L, -2);
            return 1;
        }

        static int apiGfxDrawText(lua_State* L)
        {
            auto shell = GetContextShell(L);
            if (!shell)
                return 0;

            auto font = (UserFont*)lua_touserdata(L, 1);
            if (font == nullptr)
                return 0;

            auto text = luaL_checkstring(L, 2);
            auto x = static_cast<float>(luaL_checknumber(L, 3));
            auto y = static_cast<float>(luaL_checknumber(L, 4));
            auto z = static_cast<float>(luaL_checknumber(L, 5));
            auto w = static_cast<float>(luaL_checknumber(L, 6));
            auto h = static_cast<float>(luaL_checknumber(L, 7));

            drawText(*shell, font->cookie, text, x, y, z, w, h);
            return 0;
        }

        static int apiGfxLoadMovie(lua_State* L)
        {
            auto shell = GetContextShell(L);
            if (!shell)
            {
                lua_pushnil(L);
                return 1;
            }

            auto path = luaL_checkstring(L, 1);
            auto cookie = shell->loadMovie(path);
            if (cookie == 0)
            {
                lua_pushnil(L);
                return 1;
            }

            auto custom = CreateUserObject<UserMovie>(L, UserTypeKind::movie);
            custom->cookie = cookie;
            luaL_getmetatable(L, METATABLE_MOVIE);
            lua_setmetatable(L, -2);
            return 1;
        }

        static int apiSfxLoadSound(lua_State* L)
        {
            auto shell = GetContextShell(L);
            if (!shell)
            {
                lua_pushnil(L);
                return 1;
            }

            auto path = luaL_checkstring(L, 1);
            auto cookie = shell->loadSound(path);
            if (cookie == 0)
            {
                lua_pushnil(L);
                return 1;
            }

            auto custom = CreateUserObject<UserSound>(L, UserTypeKind::sound);
            custom->cookie = cookie;
            luaL_getmetatable(L, METATABLE_SOUND);
            lua_setmetatable(L, -2);
            return 1;
        }

        static int apiSfxPlaySound(lua_State* L)
        {
            auto shell = GetContextShell(L);
            if (!shell)
                return 0;

            auto sound = (UserSound*)lua_touserdata(L, 1);
            if (sound == nullptr)
                return 0;

            shell->playSound(sound->cookie);
            return 0;
        }

        static int movie_gc(lua_State* L)
        {
            auto self = GetContext(L);
            if (!self->_shell)
            {
                lua_pushnil(L);
                return 1;
            }

            auto userMovie = GetUserObject<UserMovie>(L, 1, METATABLE_MOVIE);

            auto& resourceManager = self->_shell->getResourceManager();
            resourceManager.release(userMovie->cookie);
            return 0;
        }

        static int movie_get(lua_State* L)
        {
            auto self = GetContext(L);
            if (!self->_shell)
            {
                lua_pushnil(L);
                return 1;
            }

            auto userMovie = GetUserObject<UserMovie>(L, 1, METATABLE_MOVIE);
            auto key = luaL_checkstring(L, 2);
            if (strcmp(key, "play") == 0)
            {
                lua_pushlightuserdata(L, self);
                lua_pushcclosure(L, movie_play, 1);
                return 1;
            }
            else if (strcmp(key, "stop") == 0)
            {
                lua_pushlightuserdata(L, self);
                lua_pushcclosure(L, movie_stop, 1);
                return 1;
            }
            else if (strcmp(key, "state") == 0)
            {
                auto movie = self->_shell->getMovie(userMovie->cookie);
                lua_pushnumber(L, static_cast<int32_t>(movie->getState()));
                return 1;
            }
            else
            {
                return 0;
            }
        }

        static int movie_play(lua_State* L)
        {
            auto shell = GetContextShell(L);
            if (!shell)
                return 0;

            auto userMovie = GetUserObject<UserMovie>(L, 1, METATABLE_MOVIE);
            auto movie = shell->getMovie(userMovie->cookie);
            movie->play();
            return 0;
        }

        static int movie_stop(lua_State* L)
        {
            auto shell = GetContextShell(L);
            if (!shell)
                return 0;

            auto userMovie = GetUserObject<UserMovie>(L, 1, METATABLE_MOVIE);
            auto movie = shell->getMovie(userMovie->cookie);
            movie->stop();
            return 0;
        }

        static int apiInputGetProperty(lua_State* L)
        {
            auto shell = GetContextShell(L);
            if (!shell)
            {
                lua_pushnil(L);
                return 1;
            }

            auto& inputState = shell->getInputState();

            auto key = luaL_checkstring(L, 2);
            if (strcmp(key, "led") == 0)
            {
                lua_newtable(L);
                luaxSetTable(L, "red", inputState.led.r, -1);
                luaxSetTable(L, "green", inputState.led.g, -1);
                luaxSetTable(L, "blue", inputState.led.b, -1);
            }
            else if (strcmp(key, "rumble") == 0)
            {
                lua_newtable(L);
                luaxSetTable(L, "low", inputState.rumble.low, -1);
                luaxSetTable(L, "high", inputState.rumble.high, -1);
            }
            else
            {
                lua_pushnil(L);
            }
            return 1;
        }

        static int apiInputSetProperty(lua_State* L)
        {
            auto shell = GetContextShell(L);
            if (!shell)
            {
                lua_pushnil(L);
                return 1;
            }

            auto& inputState = shell->getInputState();
            auto key = luaL_checkstring(L, 2);
            if (strcmp(key, "led") == 0)
            {
                inputState.led.r = luaxGetTable<float>(L, "red", 3);
                inputState.led.g = luaxGetTable<float>(L, "green", 3);
                inputState.led.b = luaxGetTable<float>(L, "blue", 3);
            }
            else if (strcmp(key, "rumble") == 0)
            {
                inputState.rumble.low = luaxGetTable<float>(L, "low", 3);
                inputState.rumble.high = luaxGetTable<float>(L, "high", 3);
            }
            return 0;
        }

        template<bool TPressed> static int apiInputIsDown(lua_State* L)
        {
            auto shell = GetContextShell(L);
            if (!shell)
                return 0;

            auto& inputState = shell->getInputState();
            auto numArgs = lua_gettop(L);
            auto result = false;
            for (auto i = 1; i <= numArgs; i++)
            {
                if (lua_isinteger(L, i))
                {
                    auto argValue = lua_tointeger(L, i);
                    if (argValue >= 0 && argValue < 32)
                    {
                        auto state = TPressed ? inputState.commandsPressed[static_cast<size_t>(argValue)]
                                              : inputState.commandsDown[static_cast<size_t>(argValue)];
                        if (state)
                        {
                            result = true;
                            break;
                        }
                    }
                }
            }

            lua_pushboolean(L, result);
            return 1;
        }

        static LuaVmImpl* GetContext(lua_State* L)
        {
            return static_cast<LuaVmImpl*>(lua_touserdata(L, lua_upvalueindex(1)));
        }

        static OpenREShell* GetContextShell(lua_State* L)
        {
            auto ptr = static_cast<LuaVmImpl*>(lua_touserdata(L, lua_upvalueindex(1)));
            return ptr->_shell;
        }

        template<typename T> static T* GetUserObject(lua_State* L, int ud, const char* metatableName)
        {
            return (T*)luaL_checkudata(L, 1, metatableName);
        }

        template<typename T> static T* CreateUserObject(lua_State* L, UserTypeKind kind)
        {
            auto result = (T*)lua_newuserdata(L, sizeof(T));
            result->kind = kind;
            return result;
        }
    };

    std::unique_ptr<LuaVm> createLuaVm()
    {
        return std::make_unique<LuaVmImpl>();
    }
}
