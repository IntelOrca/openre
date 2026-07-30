#include "system_filesystem.h"
#include "logger.h"
#include <SDL3/SDL.h>

namespace openre::system::fs
{
#ifdef _WIN32
    constexpr char DIR_SEPARATOR = '\\';
#else
    constexpr char DIR_SEPARATOR = '/';
#endif

    std::string resolvePath(const char* path)
    {
        // If OPENRE_RE2_DATA is set, bypass OG path resolution entirely
        auto re2Data = std::getenv("OPENRE_RE2_DATA");
        if (re2Data && re2Data[0])
        {
            return std::string(re2Data) + DIR_SEPARATOR + path;
        }

        // Fallback to next to exe
        auto basePath = SDL_GetBasePath();
        return std::string(basePath) + DIR_SEPARATOR + "data" + DIR_SEPARATOR + path;
    }

    bool exists(const char* path, std::string* resolvedPath)
    {
        auto resolvedPath2 = resolvePath(path);
        auto exists = SDL_GetPathInfo(resolvedPath2.c_str(), nullptr);
        if (resolvedPath != nullptr)
            *resolvedPath = std::move(resolvedPath2);
        return exists;
    }

    int readAllBytes(const char* path, void* dst, size_t* length)
    {
        auto resolvedPath = resolvePath(path);
        auto stream = SDL_IOFromFile(resolvedPath.c_str(), "rb");
        if (!stream)
        {
            logging::logError("[system::fs::readAllBytes] SDL_IOFromFile failed: {}", SDL_GetError());
            return -1;
        }

        auto fileSize = SDL_GetIOSize(stream);
        if (fileSize < 0)
        {
            logging::logError("[system::fs::readAllBytes] SDL_GetIOSize failed: {}", SDL_GetError());
            SDL_CloseIO(stream);
            return -1;
        }

        auto bytesToRead = (size_t)fileSize;
        if (bytesToRead > *length)
        {
            bytesToRead = *length;
        }

        auto bytesRead = SDL_ReadIO(stream, dst, bytesToRead);
        if (bytesRead != bytesToRead)
        {
            logging::logError(
                "[system::fs::readAllBytes] SDL_ReadIO failed: {} (expected {}, got {})",
                SDL_GetError(),
                bytesToRead,
                bytesRead);
            SDL_CloseIO(stream);
            return -1;
        }

        *length = bytesRead;
        SDL_CloseIO(stream);
        return 0;
    }
}
