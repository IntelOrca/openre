#include "texture_loader.h"

#include "game_module.h"
#include "gfx.h"
#include "logger.h"
#include "marni_renderer.h"
#include "render2d.h"
#include "system_filesystem.h"
#include "tim.h"

#include <cctype>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace openre
{
    namespace
    {
        constexpr int kAdtFull = 320 * 240;                       // RGB555 pixels after reorg
        constexpr int kAdtPage = 256 * 256 + 128 * 64 + 128 * 64; // reorg input

        std::string lowerCase(std::string s)
        {
            for (auto& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        enum class TextureType
        {
            none,
            adt,
            tim,
        };

        // Reads the first candidate (module dir, then data://) that exists,
        // probing "", ".tim" and ".adt" extensions (raw TIM preferred, matching
        // font.cpp). Returns the bytes and the detected type.
        bool findAndRead(
            std::string_view logicalPath, const std::filesystem::path& moduleDir, std::vector<uint8_t>& outBytes,
            TextureType& outType, std::string& outLabel)
        {
            const std::vector<std::string> extensions = { "", ".tim", ".adt" };

            // 1. Module directory first.
            if (!moduleDir.empty())
            {
                for (const auto& ext : extensions)
                {
                    auto path = moduleDir / std::string(logicalPath);
                    if (!ext.empty())
                        path += ext;
                    outBytes = system::fs::readAllBytes(path.string().c_str());
                    if (!outBytes.empty())
                    {
                        outType = lowerCase(ext) == ".adt" ? TextureType::adt
                            : lowerCase(ext) == ".tim"     ? TextureType::tim
                                                           : TextureType::none;
                        outLabel = path.string();
                        return true;
                    }
                }
            }

            // 2. data:// fallback (original game data).
            {
                const std::string dataPrefix = std::string("data://") + std::string(logicalPath);
                for (const auto& ext : extensions)
                {
                    const std::string candidate = dataPrefix + ext;
                    const std::string resolved = system::fs::resolve(candidate.c_str());
                    if (resolved.empty())
                        continue;
                    outBytes = system::fs::readAllBytes(resolved.c_str());
                    if (!outBytes.empty())
                    {
                        outType = lowerCase(ext) == ".adt" ? TextureType::adt
                            : lowerCase(ext) == ".tim"     ? TextureType::tim
                                                           : TextureType::none;
                        outLabel = resolved;
                        return true;
                    }
                }
            }

            return false;
        }

        // Builds a render2d image from the raw compressed ADT file bytes. The
        // decoded payload is a layered 256x256 + side-panel RGB555 buffer which
        // reorgAdt restitches into a 320x240 RGB555 buffer.
        render2d::Image imageFromAdt(std::vector<uint8_t> bytes)
        {
            auto decoded = graphics::decodeAdt(bytes);
            if (decoded.empty())
            {
                logging::logWarning("[texture_loader] ADT decode failed ({})", bytes.size());
                return {};
            }

            render2d::Image image;
            if (decoded.size() == (size_t)kAdtFull * 2)
            {
                // Already a full 320x240 image.
                image.width = 320;
                image.height = 240;
                image.depth = 16;
                image.psxFormat = true;
                image.pixels = std::move(decoded);
                return image;
            }

            if (decoded.size() < (size_t)kAdtPage * 2)
            {
                logging::logWarning("[texture_loader] unexpected ADT buffer size {}", decoded.size());
                return image;
            }

            // The RE2 layered layout (256x256 + side panels) -> 320x240.
            auto reorg = graphics::reorgAdt(decoded);
            if (reorg.size() != (size_t)kAdtFull * 2)
            {
                logging::logWarning("[texture_loader] ADT reorg produced {} bytes", reorg.size());
                return image;
            }
            image.width = 320;
            image.height = 240;
            image.depth = 16;
            image.psxFormat = true;
            image.pixels = std::move(reorg);
            return image;
        }

        // Copies a marni::Image (produced by tim::decodeTim) into a render2d
        // image so the scene-tree renderer stays decoupled from marni types.
        render2d::Image imageFromTim(const marni::Image& src)
        {
            render2d::Image image;
            image.width = src.width;
            image.height = src.height;
            image.depth = src.depth;
            image.palBpp = src.palBpp;
            image.palCnt = src.palCnt;
            image.psxFormat = src.psxFormat;
            image.pixels = src.pixels;
            image.palette = src.palette;
            return image;
        }
    } // namespace

    ResourceCookie loadTextureResource(std::string_view path, int width, int height)
    {
        (void)width;
        (void)height;

        std::vector<uint8_t> bytes;
        TextureType type = TextureType::none;
        std::string label;
        if (!findAndRead(path, game_module_dir(), bytes, type, label))
        {
            logging::logError("[texture_loader] texture '{}' not found (module dir or data://)", std::string(path));
            return 0;
        }

        render2d::Image image;
        switch (type)
        {
        case TextureType::adt: image = imageFromAdt(std::move(bytes)); break;
        case TextureType::tim:
        {
            marni::Image marniImage;
            if (!tim::decodeTim(bytes.data(), marniImage))
            {
                logging::logError("[texture_loader] failed to decode TIM '{}'", label);
                return 0;
            }
            image = imageFromTim(marniImage);
            break;
        }
        default:
        {
            // Unknown extension - sniff the header: ADT is a compressed stream
            // starting with a 4-byte block size, TIM starts with the "10" tag.
            if (bytes.size() >= 4 && (uint8_t)bytes[0] == 0x10 && bytes[1] == 0x00 && bytes[2] == 0x00 && bytes[3] == 0x00)
            {
                marni::Image marniImage;
                if (tim::decodeTim(bytes.data(), marniImage))
                {
                    image = imageFromTim(marniImage);
                    break;
                }
            }
            logging::logError("[texture_loader] unsupported texture '{}'", label);
            return 0;
        }
        }

        if (image.width <= 0 || image.height <= 0)
        {
            logging::logError("[texture_loader] texture '{}' decoded to an empty image", label);
            return 0;
        }

        auto& resmgr = ResourceManager::get();
        auto resource = std::make_unique<TextureResource>();
        resource->textureHandle = render2d::SpriteRenderer::get().loadTexture(image);
        if (resource->textureHandle == 0)
        {
            logging::logError("[texture_loader] texture '{}' failed to upload to GPU", label);
            return 0;
        }
        resource->width = image.width;
        resource->height = image.height;

        const int textureHandle = resource->textureHandle;
        const int textureWidth = resource->width;
        const int textureHeight = resource->height;
        auto cookie = resmgr.addFirstRef<TextureResource>(std::string(path), std::move(resource));
        logging::logInfo(
            "[texture_loader] loaded '{}' -> cookie {}, handle {}, {}x{}",
            label,
            cookie,
            textureHandle,
            textureWidth,
            textureHeight);
        return cookie;
    }
}