#include "font.h"

#include "game_module.h"
#include "logger.h"
#include "marni_renderer.h"
#include "render2d.h"
#include "resmgr.h"
#include "system_filesystem.h"
#include "tim.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace openre
{
    namespace
    {
        // Reads the font file from the module dir then the data:// fallback,
        // probing "" / ".adt" / ".tim". Returns false when nothing exists.
        bool
        findAndReadFont(std::string_view logicalPath, const std::filesystem::path& moduleDir, std::vector<uint8_t>& outBytes)
        {
            // Prefer the raw .tim (a standalone 4bpp TIM that tim::decodeTim
            // understands) over any .adt wrapper (ADT = font/clut container
            // format, not a raw TIM).
            const std::vector<std::string> extensions = { "", ".tim", ".adt" };

            if (!moduleDir.empty())
            {
                for (const auto& ext : extensions)
                {
                    auto path = moduleDir / std::string(logicalPath);
                    if (!ext.empty())
                        path += ext;
                    if (system::fs::info(path.string().c_str()).kind != system::fs::FileKind::file)
                        continue;
                    outBytes = system::fs::readAllBytes(path.string().c_str());
                    if (!outBytes.empty())
                        return true;
                }
            }

            {
                const std::string dataPrefix = std::string("data://") + std::string(logicalPath);
                for (const auto& ext : extensions)
                {
                    const std::string resolved = system::fs::resolve((dataPrefix + ext).c_str());
                    if (resolved.empty())
                        continue;
                    if (system::fs::info(resolved.c_str()).kind != system::fs::FileKind::file)
                        continue;
                    outBytes = system::fs::readAllBytes(resolved.c_str());
                    if (!outBytes.empty())
                        return true;
                }
            }

            return false;
        }

        // Font TIMs are paletted; force palette index 0 to be transparent. The
        // raw font palette uses entry 0 for an opaque colour (dark green), but
        // text rendering needs a clear glyph background. render2d::decodeToRgba
        // treats any palette entry whose value & 0x7FFF == 0 as fully
        // transparent, so zeroing the first entry is enough.
        void makePaletteIndexZeroTransparent(marni::Image& image)
        {
            if (image.palBpp != 16 || image.palette.size() < 2)
                return;
            image.palette[0] = 0;
            image.palette[1] = 0;
        }

        // Decodes and uploads font atlas pixels, creating the TextureResource.
        // Returns its cookie (0 on failure). Shared by the legacy .tim path
        // and the textures referenced from .fnt files.
        ResourceCookie
        loadAtlasTexture(const std::string& name, const std::vector<uint8_t>& bytes, int& outWidth, int& outHeight)
        {
            marni::Image marniImage;
            if (!tim::decodeTim(bytes.data(), bytes.size(), marniImage))
            {
                logging::logError("[font] failed to decode TIM for '{}'", name);
                return 0;
            }
            if (marniImage.width <= 0 || marniImage.height <= 0)
            {
                logging::logError("[font] font '{}' decoded to an empty image", name);
                return 0;
            }

            makePaletteIndexZeroTransparent(marniImage);

            render2d::Image image;
            image.width = marniImage.width;
            image.height = marniImage.height;
            image.depth = marniImage.depth;
            image.palBpp = marniImage.palBpp;
            image.palCnt = marniImage.palCnt;
            image.psxFormat = marniImage.psxFormat;
            image.pixels = std::move(marniImage.pixels);
            image.palette = std::move(marniImage.palette);

            int textureHandle = render2d::SpriteRenderer::get().loadTexture(image);
            if (textureHandle == 0)
            {
                logging::logError("[font] font '{}' failed to upload to GPU", name);
                return 0;
            }

            auto& resmgr = ResourceManager::get();
            auto atlasResource = std::make_unique<TextureResource>();
            atlasResource->textureHandle = textureHandle;
            atlasResource->width = image.width;
            atlasResource->height = image.height;
            if (render2d::decodeImageToRgba(image, atlasResource->rgba))
            {
                atlasResource->rgbaWidth = image.width;
                atlasResource->rgbaHeight = image.height;
            }
            outWidth = image.width;
            outHeight = image.height;
            return resmgr.addFirstRef<TextureResource>(name + "#atlas", std::move(atlasResource));
        }

        // Minimal bounds-checked reader over a .fnt file buffer.
        class FntReader
        {
        private:
            const uint8_t* _data;
            size_t _size;
            size_t _pos = 0;

            bool read(void* dst, size_t len)
            {
                if (this->_pos + len > this->_size)
                    return false;
                std::memcpy(dst, this->_data + this->_pos, len);
                this->_pos += len;
                return true;
            }

        public:
            FntReader(const std::vector<uint8_t>& bytes)
                : _data(bytes.data())
                , _size(bytes.size())
            {
            }

            template<typename T> bool read(T& out)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                return this->read(&out, sizeof(T));
            }

            bool skip(size_t len)
            {
                if (this->_pos + len > this->_size)
                    return false;
                this->_pos += len;
                return true;
            }

            // Reads the NUL-terminated UTF-8 string at an absolute offset.
            bool readStringAt(uint64_t offset, std::string& out)
            {
                if (offset >= this->_size)
                    return false;
                const char* begin = reinterpret_cast<const char*>(this->_data + offset);
                const auto remaining = static_cast<size_t>(this->_size - offset);
                const char* endNul = static_cast<const char*>(std::memchr(begin, '\0', remaining));
                if (endNul == nullptr)
                    return false;
                out.assign(begin, endNul - begin);
                return true;
            }
        };

        ResourceCookie loadFntFont(const std::string& filePath)
        {
            std::vector<uint8_t> bytes;
            if (!findAndReadFont(filePath, game_module_dir(), bytes))
            {
                logging::logError("[font] font '{}' not found (module dir or data://)", filePath);
                return 0;
            }

            constexpr uint32_t kMagic = 0x544E464F; // 'OFNT'
            FntReader reader(bytes);

            uint32_t magic = 0;
            uint32_t numTextures = 0;
            uint32_t numCharacters = 0;
            if (!reader.read(magic) || !reader.read(numTextures) || !reader.read(numCharacters))
            {
                logging::logError("[font] '{}' truncated header", filePath);
                return 0;
            }
            if (magic != kMagic)
            {
                logging::logError("[font] '{}' bad magic {:#x}", filePath, magic);
                return 0;
            }
            if (numTextures == 0 || numTextures > 64 || numCharacters > 65536)
            {
                logging::logError("[font] '{}' implausible texture/character count", filePath);
                return 0;
            }

            auto font = std::make_unique<FontResource>();
            font->fromFnt = true;
            if (!reader.read(font->defaultLeft) || !reader.read(font->defaultRight) || !reader.read(font->defaultTop)
                || !reader.read(font->defaultBottom) || !reader.skip(4)) // padding
            {
                logging::logError("[font] '{}' truncated header", filePath);
                return 0;
            }

            struct TexEntry
            {
                uint32_t width;
                uint32_t height;
                uint64_t pathOffset;
            };
            std::vector<TexEntry> texEntries(numTextures);
            for (auto& tex : texEntries)
            {
                if (!reader.read(tex.width) || !reader.read(tex.height) || !reader.read(tex.pathOffset))
                {
                    logging::logError("[font] '{}' truncated texture directory", filePath);
                    return 0;
                }
            }

            struct CharEntry
            {
                uint32_t codepoint;
                uint16_t textureIndex;
                // Kerning bearings are s32 in the file format.
                int32_t left;
                int32_t top;
                int32_t right;
                int32_t bottom;
                // Source rectangle components are u32 in the file format.
                uint32_t srcX;
                uint32_t srcY;
                uint32_t srcW;
                uint32_t srcH;
            };
            std::vector<CharEntry> chars(numCharacters);
            for (auto& ch : chars)
            {
                uint16_t padding = 0;
                if (!reader.read(ch.codepoint) || !reader.read(ch.textureIndex) || !reader.read(padding)
                    || !reader.read(ch.left) || !reader.read(ch.top) || !reader.read(ch.right) || !reader.read(ch.bottom)
                    || !reader.read(ch.srcX) || !reader.read(ch.srcY) || !reader.read(ch.srcW) || !reader.read(ch.srcH))
                {
                    logging::logError("[font] '{}' truncated character records", filePath);
                    return 0;
                }
                if (ch.textureIndex >= numTextures)
                {
                    logging::logError(
                        "[font] '{}' character U+{:X} references texture {} of {}",
                        filePath,
                        ch.codepoint,
                        ch.textureIndex,
                        numTextures);
                    return 0;
                }
            }

            // Load the textures. Each gets its own TextureResource cookie; the
            // first one doubles as the legacy `atlas` so grid-based fallbacks
            // keep working for glyphs missing from the map (e.g. kana).
            font->textures.resize(numTextures);
            for (uint32_t i = 0; i < numTextures; i++)
            {
                std::string texPath;
                if (!reader.readStringAt(texEntries[i].pathOffset, texPath))
                {
                    logging::logError("[font] '{}' bad texture path offset", filePath);
                    return 0;
                }
                std::vector<uint8_t> texBytes;
                if (!findAndReadFont(texPath, game_module_dir(), texBytes))
                {
                    logging::logError("[font] '{}' texture '{}' not found", filePath, texPath);
                    return 0;
                }
                int width = 0;
                int height = 0;
                auto cookie = loadAtlasTexture(texPath, texBytes, width, height);
                if (cookie == 0)
                    return 0;

                auto& tex = font->textures[i];
                tex.path = std::move(texPath);
                tex.width = width;
                tex.height = height;
                tex.atlas = cookie;
                if (i == 0)
                {
                    font->atlas = cookie;
                    font->width = width;
                    font->height = height;
                }
            }

            for (const auto& ch : chars)
            {
                FontGlyph glyph;
                glyph.textureIndex = ch.textureIndex;
                glyph.left = ch.left;
                glyph.top = ch.top;
                glyph.right = ch.right;
                glyph.bottom = ch.bottom;
                glyph.srcX = static_cast<float>(ch.srcX);
                glyph.srcY = static_cast<float>(ch.srcY);
                glyph.srcW = static_cast<float>(ch.srcW);
                glyph.srcH = static_cast<float>(ch.srcH);
                font->glyphs[ch.codepoint] = glyph;
            }

            auto& resmgr = ResourceManager::get();
            const int lineHeight = font->lineHeight();
            auto cookie = resmgr.addFirstRef<FontResource>(filePath, std::move(font));
            logging::logInfo(
                "[font] loaded '{}' -> cookie {}, {} chars, {} texture(s), lineHeight {}",
                filePath,
                cookie,
                numCharacters,
                numTextures,
                lineHeight);
            return cookie;
        }
    } // namespace

    ResourceCookie loadFontResource(std::string_view path)
    {
        // A ".fnt" path selects the binary OpenRE font format with explicit
        // per-character metrics and kerning. The ":small" variant suffix only
        // applies to raw atlas paths.
        std::string filePath(path);
        if (filePath.size() >= 4 && filePath.compare(filePath.size() - 4, 4, ".fnt") == 0)
            return loadFntFont(filePath);

        // A ":variant" suffix selects a virtual sub-font of the same atlas
        // (only ":small" is supported for now - the compact ASCII 0x20-0x7F
        // set in an 8px-cell, 32-per-row grid on font0). The suffix is
        // stripped from the file path but kept in the resource name so each
        // variant registers separately.
        bool small = false;
        if (auto pos = filePath.find(":small"); pos != std::string::npos)
        {
            small = true;
            filePath.erase(pos);
        }

        std::vector<uint8_t> bytes;
        if (!findAndReadFont(filePath, game_module_dir(), bytes))
        {
            logging::logError("[font] font '{}' not found (module dir or data://)", filePath);
            return 0;
        }

        int width = 0;
        int height = 0;
        auto atlasCookie = loadAtlasTexture(filePath, bytes, width, height);
        if (atlasCookie == 0)
            return 0;

        auto font = std::make_unique<FontResource>();
        font->atlas = atlasCookie;
        font->width = width;
        font->height = height;
        // Page 0 (font0) holds ASCII + kana; page 1 (font1) holds kanji.
        font->page = filePath.find("font1") != std::string::npos ? 1 : 0;
        font->small = small;

        auto& resmgr = ResourceManager::get();
        auto cookie = resmgr.addFirstRef<FontResource>(std::string(path), std::move(font));
        logging::logInfo(
            "[font] loaded '{}' (small={}) -> cookie {}, atlas {}, {}x{}", filePath, small, cookie, atlasCookie, width, height);
        return cookie;
    }
}
