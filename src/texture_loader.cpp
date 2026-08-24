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
            bmp,
        };

        TextureType textureTypeForExtension(const std::string& ext)
        {
            const std::string e = lowerCase(ext);
            if (e == ".adt")
                return TextureType::adt;
            if (e == ".tim")
                return TextureType::tim;
            if (e == ".bmp")
                return TextureType::bmp;
            return TextureType::none;
        }

        // Reads the first candidate (module dir, then the shared core dir,
        // then data://) that exists, probing "", ".tim", ".adt" and ".bmp"
        // extensions (raw TIM preferred, matching font.cpp). Returns the
        // bytes and the detected type.
        bool findAndRead(
            std::string_view logicalPath, const std::filesystem::path& moduleDir, std::vector<uint8_t>& outBytes,
            TextureType& outType, std::string& outLabel)
        {
            const std::vector<std::string> extensions = { "", ".tim", ".adt", ".bmp" };

            auto tryDir = [&](const std::filesystem::path& dir) -> bool {
                if (dir.empty())
                    return false;
                for (const auto& ext : extensions)
                {
                    auto path = dir / std::string(logicalPath);
                    if (!ext.empty())
                        path += ext;
                    if (system::fs::info(path.string().c_str()).kind != system::fs::FileKind::file)
                        continue;
                    outBytes = system::fs::readAllBytes(path.string().c_str());
                    if (!outBytes.empty())
                    {
                        outType = textureTypeForExtension(ext);
                        outLabel = path.string();
                        return true;
                    }
                }
                return false;
            };

            // 1. Module directory first.
            if (tryDir(moduleDir))
                return true;

            // 2. Shared core asset directory.
            if (tryDir(game_core_dir()))
                return true;

            // 3. data:// fallback (original game data).
            {
                const std::string dataPrefix = std::string("data://") + std::string(logicalPath);
                for (const auto& ext : extensions)
                {
                    const std::string candidate = dataPrefix + ext;
                    const std::string resolved = system::fs::resolve(candidate.c_str());
                    if (resolved.empty())
                        continue;
                    if (system::fs::info(resolved.c_str()).kind != system::fs::FileKind::file)
                        continue;
                    outBytes = system::fs::readAllBytes(resolved.c_str());
                    if (!outBytes.empty())
                    {
                        outType = textureTypeForExtension(ext);
                        outLabel = resolved;
                        return true;
                    }
                }
            }

            return false;
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

        // Builds a render2d image from the raw compressed ADT file bytes. The
        // decoded payload is a layered 256x256 + side-panel RGB555 buffer which
        // reorgAdt restitches into a 320x240 RGB555 buffer. Some ADT files
        // (the intro pages common/data/open00..03) instead decode to a suite of
        // TIM textures packed back to back; imageFromTim above handles those.
        render2d::Image imageFromAdt(std::vector<uint8_t> bytes, int timIndex)
        {
            auto decoded = graphics::decodeAdt(bytes);
            if (decoded.empty())
            {
                logging::logWarning("[texture_loader] ADT decode failed ({})", bytes.size());
                return {};
            }

            // Some ADT files (e.g. the intro pages common/data/open00..03) are
            // not a single frame: the decompressed payload is a suite of TIM
            // textures packed back to back (detected by the "10" TIM tag). When
            // the caller selected one of them via timIndex, decode that TIM
            // instead of treating the buffer as one frame. Only considered when
            // the payload is not a known single-frame size, so a regular 320x240
            // backdrop whose first pixels happen to look like a TIM tag keeps
            // taking the reorg path below.
            if (decoded.size() != (size_t)kAdtFull * 2 && decoded.size() != (size_t)kAdtPage * 2 && decoded.size() >= 4
                && decoded[0] == 0x10 && decoded[1] == 0x00 && decoded[2] == 0x00 && decoded[3] == 0x00)
            {
                marni::Image marniImage;
                if (timIndex >= 0 && tim::decodeTimIndex(decoded.data(), decoded.size(), timIndex, marniImage))
                {
                    logging::logInfo(
                        "[texture_loader] ADT '{}' decoded as TIM suite, selected index {}", decoded.size(), timIndex);
                    return imageFromTim(marniImage);
                }
                logging::logWarning("[texture_loader] ADT TIM suite index {} failed to decode", timIndex);
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

        uint16_t readU16Le(const std::vector<uint8_t>& b, size_t off)
        {
            return (uint16_t)((uint32_t)b[off] | ((uint32_t)b[off + 1] << 8));
        }

        uint32_t readU32Le(const std::vector<uint8_t>& b, size_t off)
        {
            return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) | ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
        }

        // Counts the set bits of a 32-bit channel mask.
        constexpr int popc32(uint32_t v)
        {
            int n = 0;
            while (v != 0)
            {
                v &= v - 1;
                n++;
            }
            return n;
        }

        // Byte offset (little-endian) of a channel's mask within a pixel
        // dword, e.g. mask 0x00FF0000 lives at byte 2. 0 when the mask is 0.
        int maskShift(uint32_t mask)
        {
            for (int i = 0; i < 4; i++)
            {
                if ((mask & (0xFFu << (8 * i))) != 0)
                    return i;
            }
            return -1;
        }

        // Minimal uncompressed BMP decoder (BITMAPINFOHEADER or larger,
        // BI_RGB / BI_BITFIELDS only). Bottom-up rows are flipped to top-down.
        // 8bpp palettes map pure magenta to fully transparent; 32bpp honours
        // the header's channel masks when present and falls back to opaque
        // (magenta rule) when no alpha mask is defined. Produces a 32bpp
        // image whose pixels are native-endian 0xAARRGGBB values, which
        // render2d::decodeImageToRgba uploads directly.
        render2d::Image imageFromBmp(const std::vector<uint8_t>& bytes)
        {
            auto fail = [](const char* why) {
                logging::logWarning("[texture_loader] BMP rejected: {}", why);
                return render2d::Image{};
            };

            if (bytes.size() < 54 || bytes[0] != 'B' || bytes[1] != 'M')
                return fail("not a BMP file");

            const uint32_t dataOffset = readU32Le(bytes, 10);
            const uint32_t headerSize = readU32Le(bytes, 14);
            if (headerSize < 40 || (size_t)headerSize + 14 > bytes.size())
                return fail("unsupported BMP header");

            const int32_t width = (int32_t)readU32Le(bytes, 18);
            const int32_t storedHeight = (int32_t)readU32Le(bytes, 22);
            const uint16_t bpp = readU16Le(bytes, 28);
            const uint32_t compression = readU32Le(bytes, 30);

            if (width <= 0 || storedHeight < 0)
                return fail("top-down/negative height not supported");
            if (bpp != 8 && bpp != 32)
            {
                logging::logWarning("[texture_loader] BMP rejected: {}bpp not supported (only 8bpp / 32bpp)", bpp);
                return {};
            }
            if (compression != 0 && !(bpp == 32 && compression == 3))
                return fail("compressed BMP (e.g. RLE) not supported");

            // Channel masks (BI_BITFIELDS): stored right after the info
            // header on V2+ headers; default to standard BGRA byte order.
            uint32_t maskR = 0x00FF0000, maskG = 0x0000FF00, maskB = 0x000000FF, maskA = 0;
            if (bpp == 32)
            {
                if (compression == 3 && headerSize >= 52)
                {
                    maskR = readU32Le(bytes, 54);
                    maskG = readU32Le(bytes, 58);
                    maskB = readU32Le(bytes, 62);
                    // The alpha mask only exists on V4+ headers.
                    maskA = headerSize >= 108 ? readU32Le(bytes, 66) : 0;
                }
                if (popc32(maskR) != 8 || popc32(maskG) != 8 || popc32(maskB) != 8 || (maskA != 0 && popc32(maskA) != 8))
                    return fail("unsupported 32bpp channel masks");
            }

            // The palette sits between the info header and the pixel data;
            // its entry count follows from the pixel data offset.
            const size_t palOffset = 14 + headerSize;
            size_t palEntries = dataOffset > palOffset ? ((size_t)dataOffset - palOffset) / 4 : 0;
            if (palEntries > 256)
                palEntries = 256;
            if (bpp == 8 && palEntries == 0)
                return fail("no palette");

            const int32_t height = storedHeight;
            const size_t stride = (((size_t)width * bpp / 8) + 3) / 4 * 4;
            const size_t neededPixels = stride * (size_t)height;
            if ((size_t)dataOffset > bytes.size() || bytes.size() - (size_t)dataOffset < neededPixels)
                return fail("truncated pixel data");

            const int shiftR = maskShift(maskR), shiftG = maskShift(maskG), shiftB = maskShift(maskB),
                      shiftA = maskShift(maskA);

            render2d::Image image;
            image.width = width;
            image.height = height;
            image.depth = 32;
            image.psxFormat = false; // plain RGBA, not PSX 555
            image.pixels.assign((size_t)width * height * 4, 0);

            for (int32_t y = 0; y < height; y++)
            {
                // BMP rows are stored bottom-up: flip while copying.
                const uint8_t* src = bytes.data() + dataOffset + (size_t)(height - 1 - y) * stride;
                uint8_t* dst = image.pixels.data() + (size_t)y * width * 4;
                for (int32_t x = 0; x < width; x++)
                {
                    uint8_t r = 0, g = 0, b = 0;
                    uint8_t a = 255;
                    if (bpp == 8)
                    {
                        const uint8_t index = src[x];
                        if (index < palEntries)
                        {
                            const size_t po = palOffset + (size_t)index * 4;
                            b = bytes[po + 0];
                            g = bytes[po + 1];
                            r = bytes[po + 2];
                        }
                        // Pure magenta is the legacy transparency key.
                        a = (r == 255 && g == 0 && b == 255) ? 0 : 255;
                    }
                    else
                    {
                        const uint8_t* px = src + (size_t)x * 4;
                        r = px[shiftR];
                        g = px[shiftG];
                        b = px[shiftB];
                        a = shiftA >= 0 ? px[shiftA] : ((r == 255 && g == 0 && b == 255) ? 0 : 255);
                    }
                    // Little-endian byte order of 0xAARRGGBB, matching how
                    // decodeImageToRgba reads depth-32 pixels.
                    dst[0] = b;
                    dst[1] = g;
                    dst[2] = r;
                    dst[3] = a;
                    dst += 4;
                }
            }
            return image;
        }
    } // namespace

    ResourceCookie loadTextureResource(std::string_view path, int width, int height, int timIndex)
    {
        (void)width;
        (void)height;

        // A non-negative timIndex selects one TIM of a multi-TIM file (0 =
        // first) and keys the resource as "path#N"; a negative index decodes
        // the whole file / ADT frame and uses the bare path as key.
        std::string key(path);
        if (timIndex >= 0)
            key += "#" + std::to_string(timIndex);

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
        case TextureType::adt: image = imageFromAdt(std::move(bytes), timIndex); break;
        case TextureType::bmp:
        {
            image = imageFromBmp(bytes);
            if (image.width <= 0)
            {
                logging::logError("[texture_loader] failed to decode BMP '{}'", label);
                return 0;
            }
            break;
        }
        case TextureType::tim:
        {
            marni::Image marniImage;
            bool ok = timIndex >= 0 ? tim::decodeTimIndex(bytes.data(), bytes.size(), timIndex, marniImage)
                                    : tim::decodeTim(bytes.data(), bytes.size(), marniImage);
            if (!ok)
            {
                if (timIndex >= 0)
                    logging::logError("[texture_loader] failed to decode TIM index {} of '{}'", timIndex, label);
                else
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
                bool ok = timIndex >= 0 ? tim::decodeTimIndex(bytes.data(), bytes.size(), timIndex, marniImage)
                                        : tim::decodeTim(bytes.data(), bytes.size(), marniImage);
                if (ok)
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

        // Cache the decoded RGBA so the debug web UI can preview the texture
        // without re-decoding the source file.
        if (render2d::decodeImageToRgba(image, resource->rgba))
        {
            resource->rgbaWidth = image.width;
            resource->rgbaHeight = image.height;
        }

        const int textureHandle = resource->textureHandle;
        const int textureWidth = resource->width;
        const int textureHeight = resource->height;
        auto cookie = resmgr.addFirstRef<TextureResource>(key, std::move(resource));
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