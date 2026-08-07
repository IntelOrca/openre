#include "system_filesystem.h"
#include "logger.h"
#include "stream.h"
#include <SDL3/SDL.h>
#include <cstring>

namespace openre::system::fs
{
#ifdef _WIN32
    constexpr char DIR_SEPARATOR = '\\';
#else
    constexpr char DIR_SEPARATOR = '/';
#endif

    // --- SDLStream: wraps SDL_IOStream* as a Stream ---

    class SDLStream : public Stream
    {
    public:
        explicit SDLStream(SDL_IOStream* stream)
            : _stream(stream)
        {
        }

        ~SDLStream() override
        {
            if (_stream)
                SDL_CloseIO(_stream);
        }

        SDLStream(const SDLStream&) = delete;
        SDLStream& operator=(const SDLStream&) = delete;
        SDLStream(SDLStream&&) = delete;
        SDLStream& operator=(SDLStream&&) = delete;

        size_t read(void* buffer, size_t size) override
        {
            return SDL_ReadIO(_stream, buffer, size);
        }

        size_t write(const void* buffer, size_t size) override
        {
            return SDL_WriteIO(_stream, buffer, size);
        }

        int64_t seek(int64_t offset, int origin) override
        {
            SDL_IOWhence whence;
            switch (origin)
            {
            case SEEK_SET: whence = SDL_IO_SEEK_SET; break;
            case SEEK_CUR: whence = SDL_IO_SEEK_CUR; break;
            case SEEK_END: whence = SDL_IO_SEEK_END; break;
            default: return -1;
            }
            auto result = SDL_SeekIO(_stream, offset, whence);
            return (result < 0) ? -1 : result;
        }

        int64_t tell() const override
        {
            auto result = SDL_TellIO(_stream);
            return (result < 0) ? -1 : result;
        }

    private:
        SDL_IOStream* _stream;
    };

    // --- resolve (internal) / readAllBytes ---

    static std::string resolvePath(const char* path)
    {
        // Handle data:// prefix — game data files
        if (std::strncmp(path, "data://", 7) == 0)
        {
            auto subPath = path + 7;
            // If subPath is already absolute, return as-is
            if (subPath[0] && subPath[1] == ':')
                return std::string(subPath);
            auto re2Data = std::getenv("OPENRE_RE2_DATA");
            if (re2Data && re2Data[0])
            {
                return std::string(re2Data) + DIR_SEPARATOR + subPath;
            }
            auto basePath = SDL_GetBasePath();
            return std::string(basePath) + DIR_SEPARATOR + "data" + DIR_SEPARATOR + subPath;
        }

        // Handle save:// prefix — save data files
        if (std::strncmp(path, "save://", 7) == 0)
        {
            auto subPath = path + 7;
            // If subPath is already absolute, return as-is
            if (subPath[0] && subPath[1] == ':')
                return std::string(subPath);
            auto savePath = std::getenv("OPENRE_SAVE_PATH");
            if (savePath && savePath[0])
            {
                return std::string(savePath) + DIR_SEPARATOR + subPath;
            }
            auto basePath = SDL_GetBasePath();
            return std::string(basePath) + "savedata" + DIR_SEPARATOR + subPath;
        }

        // Handle user:// prefix — user config/data files
        if (std::strncmp(path, "user://", 7) == 0)
        {
            auto subPath = path + 7;
            auto prefPath = SDL_GetPrefPath(nullptr, "openre"); // deliberately null for org.
            if (!prefPath || !prefPath[0])
            {
                logging::logError("[system::fs::resolvePath] SDL_GetPrefPath failed");
                return {};
            }
            // Ensure directory exists
            SDL_CreateDirectory(prefPath);
            return std::string(prefPath) + subPath;
        }

        // Already an absolute path — pass through as-is
#ifdef _WIN32
        if ((path[0] && path[1] == ':') || (path[0] == '\\' && path[1] == '\\'))
#else
        if (path[0] == '/')
#endif
        {
            return std::string(path);
        }

        // No recognized prefix — fail
        logging::logError("[system::fs::resolvePath] unknown path scheme: {}", path);
        return {};
    }

    std::vector<uint8_t> readAllBytes(const char* path)
    {
        auto stream = open(path, FileMode::read);
        if (!stream)
            return {};

        // Get file size via seek
        auto fileSize = stream->seek(0, SEEK_END);
        if (fileSize < 0)
        {
            logging::logError("[system::fs::readAllBytes] seek to end failed");
            return {};
        }
        stream->seek(0, SEEK_SET);

        std::vector<uint8_t> buf((size_t)fileSize);
        auto bytesRead = stream->read(buf.data(), buf.size());
        if (bytesRead != buf.size())
        {
            logging::logError("[system::fs::readAllBytes] read failed: expected {}, got {}", buf.size(), bytesRead);
            return {};
        }

        return buf;
    }

    std::unique_ptr<Stream> open(const char* path, FileMode mode)
    {
        const char* modeStr;
        const char* modeLabel;
        switch (mode)
        {
        case FileMode::read:
            modeStr = "rb";
            modeLabel = "READ";
            break;
        case FileMode::write:
            modeStr = "wb";
            modeLabel = "WRITE";
            break;
        case FileMode::readWrite:
            modeStr = "r+b";
            modeLabel = "READ/WRITE";
            break;
        default: return nullptr;
        }

        auto resolvedPath = resolvePath(path);
        logging::logInfo("[OPEN {}] {}", modeLabel, resolvedPath.c_str());
        auto sdlStream = SDL_IOFromFile(resolvedPath.c_str(), modeStr);
        if (!sdlStream)
        {
            logging::logError("[system::fs::open] SDL_IOFromFile failed: {} ({})", SDL_GetError(), resolvedPath.c_str());
            return nullptr;
        }

        return std::make_unique<SDLStream>(sdlStream);
    }

    int writeAllBytes(const char* path, const void* data, size_t len)
    {
        auto stream = open(path, FileMode::write);
        if (!stream)
            return -1;

        auto written = stream->write(data, len);
        if (written != len)
        {
            logging::logError("[system::fs::writeAllBytes] write failed: expected {}, got {}", len, written);
            return -1;
        }
        return 0;
    }

    FileInfo info(const char* path)
    {
        FileInfo result;
        auto resolvedPath = resolvePath(path);
        if (resolvedPath.empty())
            return result;

        result.physicalPath = std::move(resolvedPath);

        SDL_PathInfo pathInfo;
        if (!SDL_GetPathInfo(result.physicalPath.c_str(), &pathInfo))
            return result;

        switch (pathInfo.type)
        {
        case SDL_PATHTYPE_DIRECTORY: result.kind = FileKind::directory; break;
        case SDL_PATHTYPE_FILE: result.kind = FileKind::file; break;
        default: break;
        }
        result.size = pathInfo.size;
        result.lastModified = pathInfo.modify_time;
        return result;
    }

    bool remove(const char* path)
    {
        auto resolved = resolvePath(path);
        if (resolved.empty())
            return false;
        return SDL_RemovePath(resolved.c_str());
    }

    bool createDirectory(const char* path)
    {
        auto resolved = resolvePath(path);
        if (resolved.empty())
            return false;
        return SDL_CreateDirectory(resolved.c_str());
    }

    std::vector<std::string> getDirectoryContents(const char* path, const char* pattern)
    {
        auto resolved = resolvePath(path);
        if (resolved.empty())
            return {};

        std::vector<std::string> result;
        int count = 0;
        auto entries = SDL_GlobDirectory(resolved.c_str(), pattern, 0, &count);
        if (!entries)
            return result;

        result.reserve(count);
        for (int i = 0; i < count; i++)
        {
            if (entries[i])
                result.emplace_back(entries[i]);
        }
        SDL_free(entries);
        return result;
    }
}
