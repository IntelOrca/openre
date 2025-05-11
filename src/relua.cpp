#include "relua.h"
#include "interop.hpp"
#include "mod.h"
#include "movie.h"
#include "openre.h"
#include "sce.h"
#include "shell.h"
#include <cstdio>
#include <filesystem>
#include <lua.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>

namespace fs = std::filesystem;

using namespace openre::modding;
using namespace openre::movie;
using namespace openre::shellextensions;

namespace openre::lua
{
    constexpr const char* METATABLE_ENTITY = "meta_entity";
    constexpr const char* METATABLE_GFX = "meta_gfx";
    constexpr const char* METATABLE_MOVIE = "meta_movie";
    constexpr const char* METATABLE_TEXTURE = "meta_texture";

    class LuaVmImpl : public LuaVm
    {
    private:
        struct HookInfo
        {
            HookKind kind;
            int ref;
        };

        lua_State* _state;
        std::vector<HookInfo> _subscriptions;
        std::function<void(const std::string& s)> _logCallback;
        OpenREShell* _shell{};

        enum class UserTypeKind
        {
            texture,
            textureRect,
            movie,
        };

        struct UserType
        {
            UserTypeKind kind;
        };

        struct UserTexture : public UserType
        {
            TextureHandle handle;
            uint32_t width;
            uint32_t height;
        };

        struct UserTextureRect : public UserType
        {
            TextureHandle handle;
            float s0;
            float t0;
            float s1;
            float t1;
        };

        struct UserMovie : public UserType
        {
            MovieHandle handle;
        };

    public:
        LuaVmImpl()
        {
            createState();
        }

        LuaVmImpl(const LuaVm&) = delete;

        ~LuaVmImpl() override
        {
            lua_close(_state);
        }

        void run(const std::filesystem::path& path) override
        {
            const auto& scriptPath = path;
            auto result = luaL_dofile(_state, scriptPath.string().c_str());
            if (result != LUA_OK)
            {
                auto errString = lua_tostring(_state, -1);
                log("Error: " + std::string(errString));
            }
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
                                                           "setfenv",
                                                           "setmetatable" };

        void log(const std::string& s)
        {
            if (_logCallback)
                _logCallback(s);
        }

        void createState()
        {
            _state = luaL_newstate();

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

            // RE API
            setGlobal("print", apiPrint);
            setGlobal("re.subscribe", apiSubscribe);
            setGlobal("re.getFlag", apiGetFlag);
            setGlobal("re.setFlag", apiSetFlag);
            setGlobal("re.getEntity", apiGetEntity);

            setGlobal("gfx.loadTexture", apiGfxLoadTexture);
            setGlobal("gfx.getTextureRect", apiGfxGetTextureRect);
            setGlobal("gfx.drawTexture", apiGfxDrawTexture);
            setGlobal("gfx.fade", apiGfxFade);
            setGlobal("gfx.loadMovie", apiGfxLoadMovie);

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
            setMetatable(METATABLE_MOVIE, movie_get);

            attachMetatable("gfx", METATABLE_GFX);

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

        void setMetatable(const char* name, lua_CFunction getter, lua_CFunction setter = nullptr)
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
                case LUA_TTABLE: printf("table"); break;
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
            auto ptr = static_cast<LuaVmImpl*>(lua_touserdata(L, lua_upvalueindex(1)));
            auto shell = ptr->_shell;
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
            auto ptr = static_cast<LuaVmImpl*>(lua_touserdata(L, lua_upvalueindex(1)));
            auto shell = ptr->_shell;
            if (!shell)
            {
                lua_pushnil(L);
                return 1;
            }

            auto path = luaL_checkstring(L, 1);
            auto width = luaL_checkinteger(L, 2);
            auto height = luaL_checkinteger(L, 3);

            auto textureHandle = loadTexture(*shell, path, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
            if (textureHandle == 0)
            {
                lua_pushnil(L);
                return 1;
            }

            auto custom = (UserTexture*)lua_newuserdata(L, sizeof(UserTexture));
            custom->kind = UserTypeKind::texture;
            custom->handle = textureHandle;
            custom->width = static_cast<uint32_t>(width);
            custom->height = static_cast<uint32_t>(height);
            luaL_getmetatable(L, METATABLE_TEXTURE);
            lua_setmetatable(L, -2);
            return 1;
        }

        static int apiGfxGetTextureRect(lua_State* L)
        {
            auto ptr = static_cast<LuaVmImpl*>(lua_touserdata(L, lua_upvalueindex(1)));
            auto shell = ptr->_shell;
            if (!shell)
            {
                lua_pushnil(L);
                return 1;
            }

            auto texture = (UserTexture*)lua_touserdata(L, 1);
            auto left = luaL_checkinteger(L, 2);
            auto top = luaL_checkinteger(L, 3);
            auto right = luaL_checkinteger(L, 4);
            auto bottom = luaL_checkinteger(L, 5);

            auto custom = (UserTextureRect*)lua_newuserdata(L, sizeof(UserTextureRect));
            custom->kind = UserTypeKind::textureRect;
            custom->handle = texture->handle;
            custom->s0 = left / (float)texture->width;
            custom->t0 = top / (float)texture->height;
            custom->s1 = right / (float)texture->width;
            custom->t1 = bottom / (float)texture->height;
            luaL_getmetatable(L, METATABLE_TEXTURE);
            lua_setmetatable(L, -2);
            return 1;
        }

        static int apiGfxDrawTexture(lua_State* L)
        {
            auto ptr = static_cast<LuaVmImpl*>(lua_touserdata(L, lua_upvalueindex(1)));
            auto shell = ptr->_shell;
            if (!shell)
            {
                return 0;
            }

            auto arg0 = (UserType*)lua_touserdata(L, 1);
            auto x = static_cast<float>(luaL_checknumber(L, 2));
            auto y = static_cast<float>(luaL_checknumber(L, 3));
            auto z = static_cast<float>(luaL_checknumber(L, 4));
            auto w = static_cast<float>(luaL_checknumber(L, 5));
            auto h = static_cast<float>(luaL_checknumber(L, 6));

            if (arg0->kind == UserTypeKind::texture)
            {
                drawTexture(*shell, ((UserTexture*)arg0)->handle, x, y, z, w, h);
            }
            else if (arg0->kind == UserTypeKind::textureRect)
            {
                auto rect = (UserTextureRect*)arg0;
                drawTexture(*shell, rect->handle, x, y, z, w, h, rect->s0, rect->t0, rect->s1, rect->t1);
            }
            else if (arg0->kind == UserTypeKind::movie)
            {
                auto movie = (UserMovie*)arg0;
                drawMovie(*shell, movie->handle, x, y, z, w, h);
            }

            return 0;
        }

        static int apiGfxFade(lua_State* L)
        {
            auto ptr = static_cast<LuaVmImpl*>(lua_touserdata(L, lua_upvalueindex(1)));
            auto shell = ptr->_shell;
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

        static int apiGfxLoadMovie(lua_State* L)
        {
            auto ptr = static_cast<LuaVmImpl*>(lua_touserdata(L, lua_upvalueindex(1)));
            auto shell = ptr->_shell;
            if (!shell)
            {
                lua_pushnil(L);
                return 1;
            }

            auto path = luaL_checkstring(L, 1);

            auto stream = shell->getStream(path, { ".mp4", ".mpg" });
            if (!stream.found)
            {
                lua_pushnil(L);
                return 1;
            }

            auto movie = createMoviePlayer();
            movie->open(std::move(stream.stream));

            auto movieHandle = shell->loadMovie(std::move(movie));
            if (movieHandle == 0)
            {
                lua_pushnil(L);
                return 1;
            }

            auto custom = (UserMovie*)lua_newuserdata(L, sizeof(UserMovie));
            custom->kind = UserTypeKind::movie;
            custom->handle = movieHandle;
            luaL_getmetatable(L, METATABLE_MOVIE);
            lua_setmetatable(L, -2);
            return 1;
        }

        static int movie_get(lua_State* L)
        {
            auto ptr = static_cast<LuaVmImpl*>(lua_touserdata(L, lua_upvalueindex(1)));
            auto shell = ptr->_shell;
            if (!shell)
            {
                lua_pushnil(L);
                return 1;
            }

            auto userMovie = (UserMovie*)luaL_checkudata(L, 1, METATABLE_MOVIE);
            auto key = luaL_checkstring(L, 2);
            if (strcmp(key, "play") == 0)
            {
                lua_pushlightuserdata(L, ptr);
                lua_pushcclosure(L, movie_play, 1);
                return 1;
            }
            else if (strcmp(key, "state") == 0)
            {
                auto movie = shell->getMovie(userMovie->handle);
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
            auto ptr = static_cast<LuaVmImpl*>(lua_touserdata(L, lua_upvalueindex(1)));
            auto shell = ptr->_shell;
            if (!shell)
            {
                return 0;
            }

            auto userMovie = (UserMovie*)luaL_checkudata(L, 1, METATABLE_MOVIE);
            auto movie = shell->getMovie(userMovie->handle);
            movie->play();
            return 0;
        }
    };

    std::unique_ptr<LuaVm> createLuaVm()
    {
        return std::make_unique<LuaVmImpl>();
    }
}
