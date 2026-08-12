#include "script_network.h"

#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include <lua.hpp>

#include "logger.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace openre::script
{
    namespace
    {
        constexpr const char* kListenerMetatable = "re.TcpListener";
        constexpr const char* kSocketMetatable = "re.TcpSocket";

        constexpr int kRecvChunkSize = 4096;
        constexpr int kMaxIoOpsPerIteration = 16;
        constexpr long kSelectTimeoutUs = 50000;

        struct ListenerState;
        struct SocketState;

        struct ListenerState
        {
            SOCKET handle = INVALID_SOCKET;
            std::string host;
            int port = 0;
            bool closed = false;
            std::deque<SocketState*> pending;
        };

        struct SocketState
        {
            SOCKET handle = INVALID_SOCKET;
            std::string recvBuf;
            std::string sendBuf;
            std::string localAddress;
            int localPort = 0;
            std::string remoteAddress;
            int remotePort = 0;
            bool noDelay = false;
            bool closed = false;
            bool peerClosed = false;
        };

        struct ListenerUserdata
        {
            ListenerState* state = nullptr;
        };

        struct SocketUserdata
        {
            SocketState* state = nullptr;
        };

        void closeHandle(SOCKET& handle)
        {
            if (handle != INVALID_SOCKET)
            {
                closesocket(handle);
                handle = INVALID_SOCKET;
            }
        }

        template<typename T> void eraseFrom(std::vector<T*>& items, T* item)
        {
            items.erase(std::remove(items.begin(), items.end(), item), items.end());
        }

        bool setNonBlocking(SOCKET handle)
        {
            u_long mode = 1;
            return ioctlsocket(handle, FIONBIO, &mode) == 0;
        }

        void formatAddress(const sockaddr* address, std::string& outAddress, int& outPort)
        {
            char buffer[INET6_ADDRSTRLEN] = {};
            if (address->sa_family == AF_INET)
            {
                const auto* in = reinterpret_cast<const sockaddr_in*>(address);
                if (inet_ntop(AF_INET, &in->sin_addr, buffer, sizeof(buffer)))
                    outAddress = buffer;
                outPort = ntohs(in->sin_port);
            }
            else if (address->sa_family == AF_INET6)
            {
                const auto* in6 = reinterpret_cast<const sockaddr_in6*>(address);
                if (inet_ntop(AF_INET6, &in6->sin6_addr, buffer, sizeof(buffer)))
                    outAddress = buffer;
                outPort = ntohs(in6->sin6_port);
            }
        }

        class NetworkManager
        {
        public:
            static NetworkManager& get()
            {
                static NetworkManager instance;
                return instance;
            }

            NetworkManager(const NetworkManager&) = delete;
            NetworkManager& operator=(const NetworkManager&) = delete;

            NetworkManager() = default;

            ~NetworkManager()
            {
                shutdown();
            }

            void init()
            {
                if (_initialized)
                    return;

                WSADATA data;
                if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
                {
                    openre::logging::logWarning("network: WSAStartup failed: {}", WSAGetLastError());
                    return;
                }

                _running = true;
                try
                {
                    _thread = std::thread(&NetworkManager::threadMain, this);
                }
                catch (...)
                {
                    _running = false;
                    WSACleanup();
                    throw;
                }

                _wsaStarted = true;
                _initialized = true;
                openre::logging::logDebug("network: background thread started");
            }

            void shutdown()
            {
                if (!_initialized)
                    return;

                _running = false;
                if (_thread.joinable())
                    _thread.join();

                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    for (auto* listener : _listeners)
                    {
                        listener->closed = true;
                        closeHandle(listener->handle);
                        for (auto* socket : listener->pending)
                        {
                            closeHandle(socket->handle);
                            delete socket;
                        }
                        listener->pending.clear();
                    }
                    _listeners.clear();
                    for (auto* socket : _sockets)
                    {
                        socket->closed = true;
                        closeHandle(socket->handle);
                    }
                    _sockets.clear();
                }

                if (_wsaStarted)
                {
                    WSACleanup();
                    _wsaStarted = false;
                }
                _initialized = false;
            }

            void addListener(ListenerState* listener)
            {
                std::lock_guard<std::mutex> lock(_mutex);
                _listeners.push_back(listener);
            }

            void disposeListener(ListenerState* listener)
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (listener->closed)
                    return;
                listener->closed = true;
                closeHandle(listener->handle);
                for (auto* socket : listener->pending)
                {
                    closeHandle(socket->handle);
                    delete socket;
                }
                listener->pending.clear();
                eraseFrom(_listeners, listener);
            }

            SocketState* popPending(ListenerState* listener)
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (listener->closed || listener->pending.empty())
                    return nullptr;
                SocketState* socket = listener->pending.front();
                listener->pending.pop_front();
                _sockets.push_back(socket);
                return socket;
            }

            void disposeSocket(SocketState* socket)
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (socket->closed)
                    return;
                socket->closed = true;
                closeHandle(socket->handle);
                eraseFrom(_sockets, socket);
            }

            std::string takeRecv(SocketState* socket, size_t len)
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (socket->closed || socket->recvBuf.empty())
                    return {};
                size_t count = std::min(len, socket->recvBuf.size());
                std::string result = socket->recvBuf.substr(0, count);
                socket->recvBuf.erase(0, count);
                return result;
            }

            size_t appendSend(SocketState* socket, const std::string& data)
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (socket->closed)
                    return 0;
                socket->sendBuf.append(data);
                return data.size();
            }

            void flushSocket(SocketState* socket)
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (socket->closed || socket->handle == INVALID_SOCKET)
                    return;
                while (!socket->sendBuf.empty())
                {
                    int sent = send(socket->handle, socket->sendBuf.data(), static_cast<int>(socket->sendBuf.size()), 0);
                    if (sent > 0)
                    {
                        socket->sendBuf.erase(0, static_cast<size_t>(sent));
                        continue;
                    }
                    int error = WSAGetLastError();
                    if (error == WSAEWOULDBLOCK)
                        break;
                    if (error == WSAENOTSOCK)
                        break;
                    openre::logging::logDebug("network: flush failed: {}", error);
                    markPeerGone(socket);
                    break;
                }
            }

            void setNoDelay(SocketState* socket, bool enabled)
            {
                std::lock_guard<std::mutex> lock(_mutex);
                socket->noDelay = enabled;
                if (socket->handle != INVALID_SOCKET)
                {
                    BOOL value = enabled ? TRUE : FALSE;
                    setsockopt(
                        socket->handle,
                        IPPROTO_TCP,
                        TCP_NODELAY,
                        reinterpret_cast<const char*>(&value),
                        static_cast<int>(sizeof(value)));
                }
            }

        private:
            void threadMain()
            {
                while (_running.load())
                {
                    std::vector<ListenerState*> listeners;
                    std::vector<SocketState*> sockets;
                    fd_set readSet;
                    fd_set writeSet;
                    FD_ZERO(&readSet);
                    FD_ZERO(&writeSet);
                    int readCount = 0;
                    int writeCount = 0;

                    {
                        std::lock_guard<std::mutex> lock(_mutex);
                        if (!_running.load())
                            return;
                        listeners = _listeners;
                        sockets = _sockets;
                        for (auto* listener : listeners)
                        {
                            if (listener->closed || listener->handle == INVALID_SOCKET)
                                continue;
                            if (readCount < FD_SETSIZE)
                            {
                                FD_SET(listener->handle, &readSet);
                                ++readCount;
                            }
                        }
                        for (auto* socket : sockets)
                        {
                            if (socket->closed || socket->handle == INVALID_SOCKET)
                                continue;
                            if (readCount < FD_SETSIZE)
                            {
                                FD_SET(socket->handle, &readSet);
                                ++readCount;
                            }
                            if (!socket->peerClosed && !socket->sendBuf.empty() && writeCount < FD_SETSIZE)
                            {
                                FD_SET(socket->handle, &writeSet);
                                ++writeCount;
                            }
                        }
                        if (readCount >= FD_SETSIZE)
                            openre::logging::logDebug("network: too many sockets to poll in one round");
                    }

                    timeval timeout;
                    timeout.tv_sec = 0;
                    timeout.tv_usec = kSelectTimeoutUs;
                    if (readCount == 0 && writeCount == 0)
                    {
                        // select() returns WSAEINVAL on Windows when all
                        // fd_sets are empty, so just idle briefly instead.
                        Sleep(kSelectTimeoutUs / 1000);
                        continue;
                    }
                    int ready = select(0, &readSet, &writeSet, nullptr, &timeout);
                    if (ready == SOCKET_ERROR)
                    {
                        int error = WSAGetLastError();
                        if (error != WSAENOTSOCK)
                            openre::logging::logDebug("network: select failed: {}", error);
                        continue;
                    }
                    if (ready == 0)
                        continue;
                    if (!_running.load())
                        return;

                    {
                        std::lock_guard<std::mutex> lock(_mutex);
                        if (!_running.load())
                            return;
                        for (auto* listener : listeners)
                        {
                            if (!isListenerRegistered(listener))
                                continue;
                            if (listener->closed || listener->handle == INVALID_SOCKET)
                                continue;
                            if (FD_ISSET(listener->handle, &readSet))
                                acceptPending(listener);
                        }
                        for (auto* socket : sockets)
                        {
                            if (!isSocketRegistered(socket))
                                continue;
                            if (socket->closed || socket->handle == INVALID_SOCKET)
                                continue;
                            if (FD_ISSET(socket->handle, &readSet))
                                recvAvailable(socket);
                            if (FD_ISSET(socket->handle, &writeSet))
                                sendPending(socket);
                        }
                    }
                }
            }

            bool isListenerRegistered(ListenerState* listener)
            {
                return std::find(_listeners.begin(), _listeners.end(), listener) != _listeners.end();
            }

            bool isSocketRegistered(SocketState* socket)
            {
                return std::find(_sockets.begin(), _sockets.end(), socket) != _sockets.end();
            }

            void acceptPending(ListenerState* listener)
            {
                for (int i = 0; i < kMaxIoOpsPerIteration; ++i)
                {
                    sockaddr_storage address;
                    int addressLen = static_cast<int>(sizeof(address));
                    SOCKET client = accept(listener->handle, reinterpret_cast<sockaddr*>(&address), &addressLen);
                    if (client == INVALID_SOCKET)
                    {
                        int error = WSAGetLastError();
                        if (error == WSAEWOULDBLOCK)
                            break;
                        if (error == WSAECONNRESET || error == WSAECONNABORTED)
                            continue;
                        if (error == WSAENOTSOCK)
                            break;
                        openre::logging::logDebug("network: accept failed: {}", error);
                        break;
                    }

                    setNonBlocking(client);
                    auto* socket = new SocketState();
                    socket->handle = client;
                    formatAddress(reinterpret_cast<const sockaddr*>(&address), socket->remoteAddress, socket->remotePort);
                    sockaddr_storage local;
                    int localLen = static_cast<int>(sizeof(local));
                    if (getsockname(client, reinterpret_cast<sockaddr*>(&local), &localLen) == 0)
                        formatAddress(reinterpret_cast<const sockaddr*>(&local), socket->localAddress, socket->localPort);
                    listener->pending.push_back(socket);
                    openre::logging::logDebug("network: accepted {}:{}", socket->remoteAddress, socket->remotePort);
                }
            }

            void recvAvailable(SocketState* socket)
            {
                char buffer[kRecvChunkSize];
                for (int i = 0; i < kMaxIoOpsPerIteration; ++i)
                {
                    int received = recv(socket->handle, buffer, static_cast<int>(sizeof(buffer)), 0);
                    if (received > 0)
                    {
                        socket->recvBuf.append(buffer, static_cast<size_t>(received));
                        continue;
                    }
                    if (received == 0)
                    {
                        socket->peerClosed = true;
                        break;
                    }
                    int error = WSAGetLastError();
                    if (error == WSAEWOULDBLOCK)
                        break;
                    if (error == WSAENOTSOCK)
                        break;
                    openre::logging::logDebug("network: recv failed: {}", error);
                    markPeerGone(socket);
                    break;
                }
            }

            void sendPending(SocketState* socket)
            {
                for (int i = 0; i < kMaxIoOpsPerIteration; ++i)
                {
                    if (socket->sendBuf.empty())
                        break;
                    int sent = send(socket->handle, socket->sendBuf.data(), static_cast<int>(socket->sendBuf.size()), 0);
                    if (sent > 0)
                    {
                        socket->sendBuf.erase(0, static_cast<size_t>(sent));
                        continue;
                    }
                    int error = WSAGetLastError();
                    if (error == WSAEWOULDBLOCK)
                        break;
                    if (error == WSAENOTSOCK)
                        break;
                    openre::logging::logDebug("network: send failed: {}", error);
                    markPeerGone(socket);
                    break;
                }
            }

            static void markPeerGone(SocketState* socket)
            {
                socket->peerClosed = true;
                closeHandle(socket->handle);
            }

            std::mutex _mutex;
            std::thread _thread;
            std::atomic<bool> _running{ false };
            bool _initialized = false;
            bool _wsaStarted = false;
            std::vector<ListenerState*> _listeners;
            std::vector<SocketState*> _sockets;
        };

        ListenerUserdata* checkListener(lua_State* L, int index)
        {
            return static_cast<ListenerUserdata*>(luaL_checkudata(L, index, kListenerMetatable));
        }

        SocketUserdata* checkSocket(lua_State* L, int index)
        {
            return static_cast<SocketUserdata*>(luaL_checkudata(L, index, kSocketMetatable));
        }

        int l_createTcpListener(lua_State* L)
        {
            const char* host = luaL_checkstring(L, 1);
            lua_Integer port = luaL_checkinteger(L, 2);
            if (port < 0 || port > 65535)
                return luaL_error(L, "Invalid port %I", port);

            int family = AF_INET;
            if (strcmp(host, "127.0.0.1") == 0 || strcmp(host, "localhost") == 0)
                family = AF_INET;
            else if (strcmp(host, "::1") == 0)
                family = AF_INET6;
            else
                return luaL_error(
                    L, "Host '%s' is not allowed: only loopback addresses (127.0.0.1, localhost, ::1) are supported", host);

            SOCKET sock = socket(family, SOCK_STREAM, IPPROTO_TCP);
            if (sock == INVALID_SOCKET)
                return luaL_error(
                    L, "Failed to create socket for %s:%d (WSA error %d)", host, static_cast<int>(port), WSAGetLastError());

            if (!setNonBlocking(sock))
            {
                int error = WSAGetLastError();
                closesocket(sock);
                return luaL_error(
                    L, "Failed to set non-blocking mode for %s:%d (WSA error %d)", host, static_cast<int>(port), error);
            }

            sockaddr_storage address;
            memset(&address, 0, sizeof(address));
            int addressLen = 0;
            if (family == AF_INET)
            {
                auto* in = reinterpret_cast<sockaddr_in*>(&address);
                in->sin_family = AF_INET;
                in->sin_port = htons(static_cast<u_short>(port));
                in->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                addressLen = static_cast<int>(sizeof(sockaddr_in));
            }
            else
            {
                auto* in6 = reinterpret_cast<sockaddr_in6*>(&address);
                in6->sin6_family = AF_INET6;
                in6->sin6_port = htons(static_cast<u_short>(port));
                in6->sin6_addr = in6addr_loopback;
                addressLen = static_cast<int>(sizeof(sockaddr_in6));
            }

            if (bind(sock, reinterpret_cast<sockaddr*>(&address), addressLen) == SOCKET_ERROR)
            {
                int error = WSAGetLastError();
                closesocket(sock);
                return luaL_error(L, "Failed to bind %s:%d (WSA error %d)", host, static_cast<int>(port), error);
            }

            if (listen(sock, SOMAXCONN) == SOCKET_ERROR)
            {
                int error = WSAGetLastError();
                closesocket(sock);
                return luaL_error(L, "Failed to listen on %s:%d (WSA error %d)", host, static_cast<int>(port), error);
            }

            auto* listener = new ListenerState();
            listener->handle = sock;
            listener->host = host;
            listener->port = static_cast<int>(port);

            NetworkManager::get().addListener(listener);

            auto* ud = static_cast<ListenerUserdata*>(lua_newuserdata(L, sizeof(ListenerUserdata)));
            ud->state = listener;
            luaL_setmetatable(L, kListenerMetatable);
            openre::logging::logDebug("network: listening on {}:{}", host, static_cast<int>(port));
            return 1;
        }

        int l_listenerPop(lua_State* L)
        {
            ListenerUserdata* ud = checkListener(L, 1);
            SocketState* socket = NetworkManager::get().popPending(ud->state);
            if (!socket)
                return 0;

            auto* sud = static_cast<SocketUserdata*>(lua_newuserdata(L, sizeof(SocketUserdata)));
            sud->state = socket;
            luaL_setmetatable(L, kSocketMetatable);
            return 1;
        }

        int l_listenerDispose(lua_State* L)
        {
            ListenerUserdata* ud = checkListener(L, 1);
            openre::logging::logDebug("network: listener {}:{} closed", ud->state->host, ud->state->port);
            NetworkManager::get().disposeListener(ud->state);
            return 0;
        }

        int l_listenerGc(lua_State* L)
        {
            ListenerUserdata* ud = checkListener(L, 1);
            if (ud->state)
            {
                NetworkManager::get().disposeListener(ud->state);
                delete ud->state;
                ud->state = nullptr;
            }
            return 0;
        }

        int l_socketRead(lua_State* L)
        {
            SocketUserdata* ud = checkSocket(L, 1);
            lua_Integer len = luaL_optinteger(L, 2, -1);
            if (len < 0)
                len = std::numeric_limits<lua_Integer>::max();
            std::string data = NetworkManager::get().takeRecv(ud->state, static_cast<size_t>(len));
            if (data.empty())
                return 0;
            lua_pushlstring(L, data.data(), data.size());
            return 1;
        }

        int l_socketWrite(lua_State* L)
        {
            SocketUserdata* ud = checkSocket(L, 1);
            size_t len = 0;
            const char* data = luaL_checklstring(L, 2, &len);
            size_t accepted = NetworkManager::get().appendSend(ud->state, std::string(data, len));
            lua_pushinteger(L, static_cast<lua_Integer>(accepted));
            return 1;
        }

        int l_socketFlush(lua_State* L)
        {
            SocketUserdata* ud = checkSocket(L, 1);
            NetworkManager::get().flushSocket(ud->state);
            return 0;
        }

        int l_socketDispose(lua_State* L)
        {
            SocketUserdata* ud = checkSocket(L, 1);
            openre::logging::logDebug("network: socket {}:{} closed", ud->state->remoteAddress, ud->state->remotePort);
            NetworkManager::get().disposeSocket(ud->state);
            return 0;
        }

        int l_socketGc(lua_State* L)
        {
            SocketUserdata* ud = checkSocket(L, 1);
            if (ud->state)
            {
                NetworkManager::get().disposeSocket(ud->state);
                delete ud->state;
                ud->state = nullptr;
            }
            return 0;
        }

        int l_socketIndex(lua_State* L)
        {
            SocketUserdata* ud = checkSocket(L, 1);
            const char* key = lua_tostring(L, 2);
            if (!key)
                return 0;
            if (strcmp(key, "read") == 0)
            {
                lua_pushcfunction(L, l_socketRead);
                return 1;
            }
            if (strcmp(key, "write") == 0)
            {
                lua_pushcfunction(L, l_socketWrite);
                return 1;
            }
            if (strcmp(key, "flush") == 0)
            {
                lua_pushcfunction(L, l_socketFlush);
                return 1;
            }
            if (strcmp(key, "dispose") == 0)
            {
                lua_pushcfunction(L, l_socketDispose);
                return 1;
            }
            if (strcmp(key, "localAddress") == 0)
            {
                lua_pushstring(L, ud->state->localAddress.c_str());
                return 1;
            }
            if (strcmp(key, "localPort") == 0)
            {
                lua_pushinteger(L, ud->state->localPort);
                return 1;
            }
            if (strcmp(key, "remoteAddress") == 0)
            {
                lua_pushstring(L, ud->state->remoteAddress.c_str());
                return 1;
            }
            if (strcmp(key, "remotePort") == 0)
            {
                lua_pushinteger(L, ud->state->remotePort);
                return 1;
            }
            if (strcmp(key, "noDelay") == 0)
            {
                lua_pushboolean(L, ud->state->noDelay);
                return 1;
            }
            return 0;
        }

        int l_socketNewIndex(lua_State* L)
        {
            SocketUserdata* ud = checkSocket(L, 1);
            const char* key = lua_tostring(L, 2);
            if (!key)
                return luaL_error(L, "TcpSocket properties must be set by name");
            if (strcmp(key, "noDelay") != 0)
                return luaL_error(L, "TcpSocket property '%s' is read-only", key);
            NetworkManager::get().setNoDelay(ud->state, lua_toboolean(L, 3) != 0);
            return 0;
        }

        void createListenerMetatable(lua_State* L)
        {
            luaL_newmetatable(L, kListenerMetatable);
            lua_createtable(L, 0, 2);
            lua_pushcfunction(L, l_listenerPop);
            lua_setfield(L, -2, "pop");
            lua_pushcfunction(L, l_listenerDispose);
            lua_setfield(L, -2, "dispose");
            lua_setfield(L, -2, "__index");
            lua_pushcfunction(L, l_listenerGc);
            lua_setfield(L, -2, "__gc");
            lua_pop(L, 1);
        }

        void createSocketMetatable(lua_State* L)
        {
            luaL_newmetatable(L, kSocketMetatable);
            lua_pushcfunction(L, l_socketIndex);
            lua_setfield(L, -2, "__index");
            lua_pushcfunction(L, l_socketNewIndex);
            lua_setfield(L, -2, "__newindex");
            lua_pushcfunction(L, l_socketGc);
            lua_setfield(L, -2, "__gc");
            lua_pop(L, 1);
        }
    }

    void registerNetworkBindings(lua_State* L, LuaVm* vm)
    {
        (void)vm;

        createListenerMetatable(L);
        createSocketMetatable(L);

        lua_getglobal(L, "re");
        if (lua_isnil(L, -1))
        {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setglobal(L, "re");
        }

        lua_createtable(L, 0, 1);
        lua_pushcfunction(L, l_createTcpListener);
        lua_setfield(L, -2, "createTcpListener");
        lua_setfield(L, -2, "network");
        lua_pop(L, 1);
    }

    void networkInit()
    {
        NetworkManager::get().init();
    }

    void networkShutdown()
    {
        NetworkManager::get().shutdown();
    }
}
