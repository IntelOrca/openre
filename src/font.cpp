#include "font.h"

#include "game_module.h"
#include "logger.h"
#include "marni_renderer.h"
#include "options.h"
#include "render2d.h"
#include "resmgr.h"
#include "system_filesystem.h"
#include "tim.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace openre
{
    FontResource::~FontResource()
    {
        // The font owns one reference per distinct atlas texture (raw fonts
        // have a single atlas; .fnt fonts reference every texture listed in
        // the file). Releasing them lets the atlas unload with the last font
        // using it. Safe during releaseAll(): cookie slots are zeroed before
        // resources are destroyed, making the nested releases no-ops.
        auto& resmgr = ResourceManager::get();
        std::set<ResourceCookie> cookies;
        if (this->atlas != 0)
            cookies.insert(this->atlas);
        for (const auto& tex : this->textures)
        {
            if (tex.atlas != 0)
                cookies.insert(tex.atlas);
        }
        for (const ResourceCookie cookie : cookies)
            resmgr.release(cookie);

        // Close the TTF_Font handle for TTF-backed fonts. Must happen after
        // the page releases because (in the current SDL3_ttf) the font does
        // not own the page textures.
        if (this->ttfFont != nullptr)
            TTF_CloseFont(static_cast<TTF_Font*>(this->ttfFont));
    }
    namespace
    {
        // Reads the font file from the module dir, then the shared core dir,
        // then the data:// fallback, probing "" / ".adt" / ".tim". Returns
        // false when nothing exists.
        bool
        findAndReadFont(std::string_view logicalPath, const std::filesystem::path& moduleDir, std::vector<uint8_t>& outBytes)
        {
            // Prefer the raw .tim (a standalone 4bpp TIM that tim::decodeTim
            // understands) over any .adt wrapper (ADT = font/clut container
            // format, not a raw TIM).
            const std::vector<std::string> extensions = { "", ".tim", ".adt" };

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
                        return true;
                }
                return false;
            };

            if (tryDir(moduleDir))
                return true;
            if (tryDir(game_core_dir()))
                return true;

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
            return resmgr.addFirstRef<TextureResource>(name, std::move(atlasResource));
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

        // ------------------------------------------------------------------
        // TTF/OTF fonts (SDL3_ttf)
        // ------------------------------------------------------------------

        // One-time SDL3_ttf init (idempotent).
        bool ensureTtfInitialised()
        {
            if (TTF_WasInit() != 0)
                return true;
            if (!TTF_Init()) // TTF_Init returns bool: true on success
            {
                logging::logError("[font] TTF_Init failed: {}", SDL_GetError());
                return false;
            }
            return true;
        }

        // Strips a trailing ":NN" point/pixel-size suffix from a logical path.
        // Returns false when there is no numeric suffix (so ":small" and plain
        // paths are left alone).
        bool splitSizeSuffix(std::string& path, int& outSize)
        {
            const auto colon = path.rfind(':');
            if (colon == std::string::npos || colon + 1 >= path.size())
                return false;
            const char* s = path.c_str() + colon + 1;
            const char* q = s;
            int value = 0;
            for (; *q != '\0'; q++)
            {
                if (*q < '0' || *q > '9')
                    return false;
                value = value * 10 + (*q - '0');
            }
            outSize = value;
            path.erase(colon);
            return true;
        }

        bool hasTtfExtension(std::string_view path)
        {
            auto lower = std::string(path);
            for (auto& c : lower)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return lower.size() >= 4
                && (lower.compare(lower.size() - 4, 4, ".ttf") == 0 || lower.compare(lower.size() - 4, 4, ".otf") == 0
                    || lower.compare(lower.size() - 4, 4, ".ttc") == 0);
        }

        constexpr int kTtfPageW = 512;
        constexpr int kTtfPageH = 512;
        constexpr int kTtfPad = 1; // padding between glyphs to stop bleed

        // Appends a new atlas page (GPU dynamic texture + CPU mirror) to
        // `font` and returns its TextureResource cookie (0 on failure).
        ResourceCookie ttfAddPage(FontResource& font)
        {
            auto& renderer = render2d::SpriteRenderer::get();
            const int handle = renderer.createDynamicTexture(kTtfPageW, kTtfPageH);
            if (handle == 0)
            {
                logging::logError("[font] ttf page allocation failed (createDynamicTexture)");
                return 0;
            }

            auto tex = std::make_unique<TextureResource>();
            tex->textureHandle = handle;
            tex->width = kTtfPageW;
            tex->height = kTtfPageH;

            auto& resmgr = ResourceManager::get();
            const std::string name = "font:ttf page " + std::to_string(reinterpret_cast<uintptr_t>(&font)) + ":"
                + std::to_string(font.textures.size());
            const auto cookie = resmgr.addFirstRef<TextureResource>(name, std::move(tex));
            if (cookie == 0)
            {
                renderer.unloadTexture(handle);
                return 0;
            }

            FontResource::FntTexture ft;
            ft.width = kTtfPageW;
            ft.height = kTtfPageH;
            ft.atlas = cookie;
            font.textures.push_back(std::move(ft));

            FontResource::TtfPage page;
            page.pixels.assign(static_cast<size_t>(kTtfPageW) * kTtfPageH * 4, 0);
            page.cursorX = kTtfPad;
            page.cursorY = kTtfPad;
            page.rowH = 0;
            font.ttfPages.push_back(std::move(page));

            if (font.atlas == 0)
            {
                font.atlas = cookie;
                font.width = kTtfPageW;
                font.height = kTtfPageH;
            }
            logging::logInfo(
                "[font] ttf atlas page {} ({}x{}) allocated for cookie {}",
                font.textures.size() - 1,
                kTtfPageW,
                kTtfPageH,
                cookie);
            return cookie;
        }

        // Re-uploads a page's CPU pixels to its GPU dynamic texture and keeps
        // the debug-preview rgba in sync.
        void ttfUploadPage(FontResource& font, size_t pageIndex)
        {
            auto& renderer = render2d::SpriteRenderer::get();
            auto* tex = ResourceManager::get().fromCookie<TextureResource>(font.textures[pageIndex].atlas);
            if (tex == nullptr)
                return;
            const auto& page = font.ttfPages[pageIndex];
            renderer.updateDynamicTexture(tex->textureHandle, page.pixels.data(), tex->width, tex->height);
            tex->rgba = page.pixels;
            tex->rgbaWidth = tex->width;
            tex->rgbaHeight = tex->height;
        }

        // Copies one glyph surface into a page's CPU buffer starting at (x, y).
        // SDL_ttf blended surfaces come back premultiplied; we un-premultiply
        // so the sprite-tinting shader (which multiplies texture * vertex
        // color under src-alpha blending) does not double-darken glyph edges.
        void blitGlyphToPage(FontResource::TtfPage& page, int pageW, int pageH, SDL_Surface* surf, int x, int y)
        {
            const SDL_PixelFormatDetails* fmt = SDL_GetPixelFormatDetails(surf->format);
            const uint8_t* src = static_cast<const uint8_t*>(surf->pixels);
            uint8_t* dstBase = page.pixels.data();
            for (int row = 0; row < surf->h && (y + row) < pageH; row++)
            {
                const uint8_t* sRow = src + static_cast<size_t>(row) * surf->pitch;
                uint8_t* dRow = dstBase + (static_cast<size_t>(y + row) * pageW + static_cast<size_t>(x)) * 4;
                for (int col = 0; col < surf->w && (x + col) < pageW; col++)
                {
                    Uint32 pixel = 0;
                    std::memcpy(&pixel, sRow + static_cast<size_t>(col) * fmt->bytes_per_pixel, fmt->bytes_per_pixel);
                    Uint8 r = 0, g = 0, b = 0, a = 0;
                    SDL_GetRGBA(pixel, fmt, nullptr, &r, &g, &b, &a);
                    if (a != 0)
                    {
                        r = static_cast<Uint8>(std::min(255, (r * 255) / a));
                        g = static_cast<Uint8>(std::min(255, (g * 255) / a));
                        b = static_cast<Uint8>(std::min(255, (b * 255) / a));
                    }
                    uint8_t* d = dRow + static_cast<size_t>(col) * 4;
                    d[0] = r;
                    d[1] = g;
                    d[2] = b;
                    d[3] = a;
                }
            }
        }

        // Builds a TTF-backed FontResource from raw font bytes and registers
        // it, returning a ResourceCookie (0 on failure). `resourceKey` is the
        // stable identity used for dedup (the ":NN" size suffix is appended).
        ResourceCookie createTtfFont(const std::string& resourceKey, std::vector<uint8_t> bytes, int pixelSize)
        {
            if (pixelSize <= 0)
                pixelSize = 16;
            if (!ensureTtfInitialised())
                return 0;

            auto font = std::make_unique<FontResource>();
            font->fromTtf = true;
            // Keep the raw bytes alive: SDL3_ttf resolves glyphs lazily from
            // the IO stream, so the buffer must outlive the loader.
            font->ttfData = std::move(bytes);
            font->ttfFont = [&]() -> void* {
                auto* io = SDL_IOFromConstMem(font->ttfData.data(), font->ttfData.size());
                if (io == nullptr)
                {
                    logging::logError("[font] SDL_IOFromConstMem failed: {}", SDL_GetError());
                    return nullptr;
                }
                // closeio=true: SDL_ttf owns the (non-owning) mem stream and
                // closes it in TTF_CloseFont; the bytes themselves are owned
                // by ttfData.
                auto* f = TTF_OpenFontIO(io, true, static_cast<float>(pixelSize));
                if (f == nullptr)
                {
                    logging::logError("[font] TTF_OpenFontIO '{}' ({}px) failed: {}", resourceKey, pixelSize, SDL_GetError());
                    SDL_CloseIO(io);
                }
                return f;
            }();
            if (font->ttfFont == nullptr)
                return 0;

            font->pixelSize = pixelSize;
            auto* ttf = static_cast<TTF_Font*>(font->ttfFont);
            font->ascent = TTF_GetFontAscent(ttf);
            font->descent = TTF_GetFontDescent(ttf);
            // Line box: defaultTop is negative (ascent above the line top),
            // defaultBottom is descent below it, so lineHeight() = ascent -
            // descent (matches TTF's lineSkip, the height of every blended
            // glyph surface). left/right are filled in per-glyph by
            // ensureGlyph.
            font->defaultLeft = 0;
            font->defaultRight = 0;
            font->defaultTop = -font->ascent;
            font->defaultBottom = -font->descent;

            if (ttfAddPage(*font) == 0)
            {
                TTF_CloseFont(ttf);
                return 0;
            }

            auto& resmgr = ResourceManager::get();
            const int ascent = font->ascent;
            const int descent = font->descent;
            const int lineHeight = font->lineHeight();
            const std::string resourceName = resourceKey + ":" + std::to_string(pixelSize);
            const auto cookie = resmgr.addFirstRef<FontResource>(resourceName, std::move(font));
            logging::logInfo(
                "[font] loaded TTF '{}' ({}px) -> cookie {}, ascent {} descent {} lineHeight {}",
                resourceKey,
                pixelSize,
                cookie,
                ascent,
                descent,
                lineHeight);
            return cookie;
        }

        // Loads a TTF/OTF/TTC font file into a FontResource and registers it,
        // returning a ResourceCookie (0 on failure). `logicalPath` may carry a
        // ":NN" size suffix (default 16px). Glyphs are rasterized lazily by
        // FontResource::ensureGlyph.
        ResourceCookie loadTtfFont(std::string logicalPath)
        {
            int pixelSize = 16;
            splitSizeSuffix(logicalPath, pixelSize);
            if (pixelSize <= 0)
                pixelSize = 16;

            std::vector<uint8_t> bytes;
            if (!findAndReadFont(logicalPath, game_module_dir(), bytes))
            {
                logging::logError("[font] TTF font '{}' not found (module dir or data://)", logicalPath);
                return 0;
            }
            return createTtfFont(logicalPath, std::move(bytes), pixelSize);
        }

        // ------------------------------------------------------------------
        // Font role catalogue + fallback chain (shared by both backends)
        // ------------------------------------------------------------------

        // Sentinel glyph map key for the synthetic tofu box (PUA codepoint).
        constexpr uint32_t kTofuCodepoint = 0xF0000;

        // Best-effort path to a system font with broad Unicode coverage,
        // validated at runtime and cached. Empty when no candidate exists.
        const std::filesystem::path& systemFontFilePath()
        {
            static const std::filesystem::path path = []() -> std::filesystem::path {
                std::vector<std::filesystem::path> candidates;
#ifdef _WIN32
                const std::filesystem::path winDir
                    = std::getenv("SystemRoot") != nullptr ? std::getenv("SystemRoot") : "C:\\Windows";
                candidates = { winDir / "Fonts/msgothic.ttc", winDir / "Fonts/arialuni.ttf" };
#elif defined(__APPLE__)
                candidates = {
                    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
                    "/System/Library/Fonts/STHeiti Light.ttc",
                };
#else
                candidates = {
                    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                };
#endif
                std::error_code ec;
                for (const auto& candidate : candidates)
                {
                    if (std::filesystem::is_regular_file(candidate, ec))
                        return candidate;
                }
                return {};
            }();
            return path;
        }

        // System font opened at `pixelSize`, cached per size (0 when the
        // platform has no usable system font).
        ResourceCookie systemTtfCookie(int pixelSize)
        {
            static std::unordered_map<int, ResourceCookie> cache;
            auto it = cache.find(pixelSize);
            if (it != cache.end())
                return it->second;

            ResourceCookie cookie = 0;
            const auto& path = systemFontFilePath();
            if (!path.empty())
            {
                std::vector<uint8_t> bytes;
                if (system::fs::info(path.string().c_str()).kind == system::fs::FileKind::file)
                    bytes = system::fs::readAllBytes(path.string().c_str());
                if (!bytes.empty())
                    cookie = createTtfFont("system://" + path.string(), std::move(bytes), pixelSize);
            }
            cache[pixelSize] = cookie;
            return cookie;
        }

        // Creates (once per box size) the synthetic "last resort" font: a
        // hollow tofu box drawn programmatically into an atlas page. Returns
        // 0 only on page allocation failure.
        ResourceCookie tofuFontCookie(int boxSize)
        {
            static std::unordered_map<int, ResourceCookie> cache;
            auto it = cache.find(boxSize);
            if (it != cache.end())
                return it->second;

            const int boxW = std::max(1, boxSize * 3 / 4);
            const int boxH = std::max(1, boxSize);

            auto font = std::make_unique<FontResource>();
            if (ttfAddPage(*font) == 0)
                return 0;

            const int x = kTtfPad;
            const int y = kTtfPad;
            auto& page = font->ttfPages[0];
            auto setPixel = [&](int px, int py) {
                if (px < 0 || py < 0 || px >= kTtfPageW || py >= kTtfPageH)
                    return;
                uint8_t* d = page.pixels.data() + (static_cast<size_t>(py) * kTtfPageW + static_cast<size_t>(px)) * 4;
                d[0] = 255;
                d[1] = 255;
                d[2] = 255;
                d[3] = 255;
            };
            for (int i = 0; i < boxW; i++)
            {
                setPixel(x + i, y);
                setPixel(x + i, y + boxH - 1);
            }
            for (int i = 0; i < boxH; i++)
            {
                setPixel(x, y + i);
                setPixel(x + boxW - 1, y + i);
            }
            ttfUploadPage(*font, 0);

            // One shared record stands in for every unresolvable codepoint.
            FontGlyph g;
            g.textureIndex = 0;
            g.left = 0;
            g.top = 0;
            g.right = boxW;
            g.bottom = 0;
            g.srcX = static_cast<float>(x);
            g.srcY = static_cast<float>(y);
            g.srcW = static_cast<float>(boxW);
            g.srcH = static_cast<float>(boxH);
            font->glyphs[kTofuCodepoint] = g;

            auto& resmgr = ResourceManager::get();
            const std::string resourceName = "font:tofu " + std::to_string(boxSize);
            const auto cookie = resmgr.addFirstRef<FontResource>(resourceName, std::move(font));
            cache[boxSize] = cookie;
            logging::logInfo("[font] tofu glyph created ({}x{}) -> cookie {}", boxW, boxH, cookie);
            return cookie;
        }
    } // namespace

    const FontGlyph* FontResource::ensureGlyph(uint32_t codepoint)
    {
        if (!this->fromTtf || this->ttfFont == nullptr)
            return nullptr;

        auto it = this->glyphs.find(codepoint);
        if (it != this->glyphs.end())
            return &it->second;

        auto* ttf = static_cast<TTF_Font*>(this->ttfFont);
        FontGlyph g;
        g.textureIndex = 0;

        int minx = 0, maxx = 0, miny = 0, maxy = 0, advance = 0;
        const bool haveMetrics = TTF_GetGlyphMetrics(ttf, codepoint, &minx, &maxx, &miny, &maxy, &advance);
        if (haveMetrics)
        {
            // Horizontal: glyph bitmap left edge sits at pen_x + minx, so
            // left = -minx and right = advance - left (pen advances by
            // left + right).
            g.left = -minx;
            g.right = advance - g.left;
            // Vertical: SDL3_ttf blended glyph surfaces are always lineSkip
            // tall with the baseline at row `ascent` (ink top at ascent -
            // maxy), so the source rect already encodes the correct vertical
            // position. top = 0 places every glyph's surface top on the line
            // top, giving every glyph the same baseline (pen_y + ascent).
            // Using maxy - ascent here would shift each glyph's baseline by
            // maxy, misaligning glyphs within a line.
            g.top = 0;
        }

        if (haveMetrics && codepoint != 0x20 && TTF_FontHasGlyph(ttf, codepoint))
        {
            // Render opaque white; the per-glyph sprite color tints the final
            // output, so the atlas only ever holds coverage.
            SDL_Surface* surf = TTF_RenderGlyph_Blended(ttf, codepoint, SDL_Color{ 255, 255, 255, 255 });
            if (surf != nullptr)
            {
                const int gw = surf->w;
                const int gh = surf->h;
                if (gw > 0 && gh > 0 && gw + kTtfPad <= kTtfPageW && gh + kTtfPad <= kTtfPageH)
                {
                    bool packed = false;
                    for (size_t i = 0; i < this->ttfPages.size() && !packed; i++)
                    {
                        auto& page = this->ttfPages[i];
                        if (page.cursorX + gw + kTtfPad > kTtfPageW)
                        {
                            // Row full: drop to the next line if it fits.
                            if (page.cursorY + page.rowH + kTtfPad + gh + kTtfPad <= kTtfPageH)
                            {
                                page.cursorY += page.rowH + kTtfPad;
                                page.cursorX = kTtfPad;
                                page.rowH = 0;
                            }
                            else
                            {
                                continue; // try the next page
                            }
                        }
                        blitGlyphToPage(page, kTtfPageW, kTtfPageH, surf, page.cursorX, page.cursorY);
                        g.textureIndex = static_cast<uint16_t>(i);
                        g.srcX = static_cast<float>(page.cursorX);
                        g.srcY = static_cast<float>(page.cursorY);
                        g.srcW = static_cast<float>(gw);
                        g.srcH = static_cast<float>(gh);
                        page.rowH = std::max(page.rowH, gh);
                        page.cursorX += gw + kTtfPad;
                        ttfUploadPage(*this, i);
                        packed = true;
                    }
                    if (!packed && ttfAddPage(*this) != 0)
                    {
                        auto& page = this->ttfPages.back();
                        blitGlyphToPage(page, kTtfPageW, kTtfPageH, surf, page.cursorX, page.cursorY);
                        g.textureIndex = static_cast<uint16_t>(this->ttfPages.size() - 1);
                        g.srcX = static_cast<float>(page.cursorX);
                        g.srcY = static_cast<float>(page.cursorY);
                        g.srcW = static_cast<float>(gw);
                        g.srcH = static_cast<float>(gh);
                        page.rowH = std::max(page.rowH, gh);
                        page.cursorX += gw + kTtfPad;
                        ttfUploadPage(*this, this->ttfPages.size() - 1);
                    }
                }
                SDL_DestroySurface(surf);
            }
        }

        // Blank/missing glyphs keep srcW/srcH = 0; layout advances the pen by
        // left+right (or its fallback advance) but draws nothing.
        auto result = this->glyphs.emplace(codepoint, g);
        return &result.first->second;
    }

    int FontResource::kerning(uint32_t previous, uint32_t current) const
    {
        if (!this->fromTtf || this->ttfFont == nullptr)
            return 0;
        int value = 0;
        if (TTF_GetGlyphKerning(static_cast<TTF_Font*>(this->ttfFont), previous, current, &value))
            return value;
        return 0;
    }

    ResourceCookie loadFontResource(std::string_view path, int size)
    {
        // A ".fnt" path selects the binary OpenRE font format with explicit
        // per-character metrics and kerning. The ":small" variant suffix only
        // applies to raw atlas paths.
        std::string filePath(path);
        if (filePath.size() >= 4 && filePath.compare(filePath.size() - 4, 4, ".fnt") == 0)
            return loadFntFont(filePath);

        // A ".ttf"/".otf"/".ttc" path (optionally with a ":NN" point-size
        // suffix) selects the SDL3_ttf backend. `size` (when > 0) is
        // appended as that suffix, and both feed the resource key.
        std::string ttfProbe = filePath;
        int unusedSize = 0;
        splitSizeSuffix(ttfProbe, unusedSize);
        if (hasTtfExtension(ttfProbe))
        {
            std::string request = filePath;
            if (size > 0)
                request += ":" + std::to_string(size);
            return loadTtfFont(request);
        }

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

    // ── Role font registry (consumed by resolveGlyph below) ───────────────
    //
    // The ui/font.lua catalogue in core owns which fonts each role uses and
    // pins them with core.setRoleFont. Native code reads only the cookies
    // registered here; engine text and the swap walk never name a concrete
    // font asset. `previous` captures the pre-change cookie so FontSwapSystem
    // can remap scene text; `fallback` is the per-role glyph fallback used
    // when a .fnt primary lacks a codepoint.

    namespace
    {
        ResourceCookie s_roleCurrent[2] = {};
        ResourceCookie s_rolePrevious[2] = {};
        ResourceCookie s_roleFallback[2] = {};
    }

    // The fallback FontResource pinned for whichever role currently has
    // `font` as its primary (previous primaries still count while a swap is
    // pending, so pre-remap text keeps its fallback). nullptr when the font
    // is not a registered role primary or the role has no fallback.
    FontResource* roleFallbackFor(const FontResource& font)
    {
        auto& resmgr = ResourceManager::get();
        for (size_t i = 0; i < std::size(s_roleCurrent); i++)
        {
            auto* cur = resmgr.fromCookie<FontResource>(s_roleCurrent[i]);
            auto* prev = resmgr.fromCookie<FontResource>(s_rolePrevious[i]);
            if ((cur != nullptr && cur == &font) || (prev != nullptr && prev == &font))
                return resmgr.fromCookie<FontResource>(s_roleFallback[i]);
        }
        return nullptr;
    }

    ResolvedGlyph resolveGlyph(FontResource& font, uint32_t codepoint)
    {
        auto& resmgr = ResourceManager::get();

        // 1) The primary font's own glyphs.
        if (font.fromFnt)
        {
            auto* glyph = font.findGlyph(codepoint);
            if (glyph != nullptr && glyph->srcW > 0 && glyph->srcH > 0)
                return { glyph, font.textures[glyph->textureIndex].atlas, false };
        }
        else if (font.fromTtf)
        {
            // Rasterizes on first use; characters the font actually lacks get
            // a blank advance-only record instead of a bitmap.
            auto* glyph = font.ensureGlyph(codepoint);
            if (glyph != nullptr && glyph->srcW > 0 && glyph->srcH > 0)
                return { glyph, font.textures[glyph->textureIndex].atlas, false };
        }
        else
        {
            // Legacy grid atlases (font0/font1) have no fallback chain in v1.
            return {};
        }

        // Spaces always stay blank: never drawn, fallbacked or tofued.
        if (codepoint == 0x20)
            return { font.findGlyph(codepoint), 0, false };

        // Fallback font size mirrors the primary: TTF fonts keep their own
        // pixel size; sprite fonts use their line box (14px / 8px).
        const int size = font.fromTtf ? font.pixelSize : font.lineHeight();
        if (size <= 0)
            return {};

        // 2) The role's registered fallback font (sprite primaries only; a
        //    TTF primary skips straight to the system font). The game's Lua
        //    catalogue pins this per role via core.setRoleFont, so which font
        //    covers missing codepoints (accents, kana, kanji) is game policy,
        //    never an engine hardcode.
        if (font.fromFnt)
        {
            if (auto* fallback = roleFallbackFor(font))
            {
                auto* glyph = fallback->ensureGlyph(codepoint);
                if (glyph != nullptr && glyph->srcW > 0 && glyph->srcH > 0)
                    return { glyph, fallback->textures[glyph->textureIndex].atlas, false };
            }
        }

        // 3) System font, best effort (nil when the platform has none).
        if (auto* system = resmgr.fromCookie<FontResource>(systemTtfCookie(size)))
        {
            auto* glyph = system->ensureGlyph(codepoint);
            if (glyph != nullptr && glyph->srcW > 0 && glyph->srcH > 0)
                return { glyph, system->textures[glyph->textureIndex].atlas, false };
        }

        // 4) Tofu: a synthetic box so unrenderable codepoints visibly stand
        //    in instead of silently blanking. Box height mirrors the primary
        //    font's line box.
        if (auto* tofu = resmgr.fromCookie<FontResource>(tofuFontCookie(font.lineHeight())))
        {
            if (auto* glyph = tofu->findGlyph(kTofuCodepoint))
                return { glyph, tofu->textures[glyph->textureIndex].atlas, true };
        }

        return {};
    }

    // Generic engine defaults for the role seeds below: the line boxes the
    // engine opens the platform system font at for pre-Lua text. Any game
    // that cares pins its own fonts through core.setRoleFont at boot, so
    // these only size strictly-prelude engine text (input hints, captions).
    constexpr int kRoleSeedBigSize = 14;
    constexpr int kRoleSeedSmallSize = 8;

    // Native boot default for a role, used only before the Lua ui/font
    // catalogue has pinned the role via core.setRoleFont (and for games that
    // never call it). Generic by construction: it opens the *system* font at
    // a plain line-box size, so no game asset, font family or mode policy is
    // duplicated in native code. The choice of which fonts a role uses (and
    // at what size) belongs entirely to the Lua catalogue.
    ResourceCookie seedRoleFont(FontRole role)
    {
        static ResourceCookie cache[2] = {};
        auto& slot = cache[static_cast<int>(role)];
        if (slot != 0)
            return slot;

        switch (role)
        {
        case FontRole::Big: slot = systemTtfCookie(kRoleSeedBigSize); break;
        case FontRole::Small: slot = systemTtfCookie(kRoleSeedSmallSize); break;
        }
        return slot;
    }

    // ── Role registry accessors ──────────────────────────────────────────
    //
    // Storage lives above with roleFallbackFor (resolveGlyph consumes it);
    // these functions are the public entry points for the Lua catalogue and
    // the swap walk. The Lua catalogue keeps its handles alive, so a
    // registered role cookie always resolves; `previous` captures the
    // pre-change cookie so FontSwapSystem can remap scene text.

    ResourceCookie getRoleFont(FontRole role)
    {
        auto& resmgr = ResourceManager::get();
        const int index = static_cast<int>(role);
        if (s_roleCurrent[index] != 0)
            return s_roleCurrent[index];
        // First touch: seed from the native boot default. Nothing swaps;
        // previous points at the same cookie. The slot takes its own
        // reference so the swap walk's syncRoleSwap release of the previous
        // cookie never drops the seed cache's permanent reference - the
        // platform system font must stay resolvable for the whole run.
        const ResourceCookie seeded = seedRoleFont(role);
        s_roleCurrent[index] = seeded != 0 ? resmgr.dupRef(seeded) : 0;
        s_rolePrevious[index] = s_roleCurrent[index];
        return s_roleCurrent[index];
    }

    ResourceCookie previousRoleFont(FontRole role)
    {
        const int index = static_cast<int>(role);
        if (s_rolePrevious[index] != 0)
            return s_rolePrevious[index];
        return getRoleFont(role);
    }

    void setRoleFont(FontRole role, ResourceCookie primary, ResourceCookie fallback)
    {
        const int index = static_cast<int>(role);
        auto& resmgr = ResourceManager::get();

        // Register (or clear) the per-role glyph fallback first. The swap
        // walk does not touch it, so no previous/current dance; the catalogue
        // keeps its fallback handle alive, and the registry takes its own
        // reference so the cookie always resolves.
        if (fallback != s_roleFallback[index])
        {
            if (s_roleFallback[index] != 0)
                resmgr.release(s_roleFallback[index]);
            s_roleFallback[index] = fallback != 0 ? resmgr.dupRef(fallback) : 0;
        }

        auto* next = resmgr.fromCookie<FontResource>(primary);
        if (next == nullptr)
            return;
        auto* now = resmgr.fromCookie<FontResource>(s_roleCurrent[index]);
        if (next == now)
            return; // Same resource under a different cookie number; nothing to do.

        // Slide the roles: the current slot hands its reference to `previous`,
        // and the current slot takes a fresh reference on the new cookie. The
        // swap walk then has both resources alive until syncRoleSwap collapses
        // previous into current.
        if (s_rolePrevious[index] != 0 && s_rolePrevious[index] != s_roleCurrent[index])
            resmgr.release(s_rolePrevious[index]);
        s_rolePrevious[index] = s_roleCurrent[index];
        s_roleCurrent[index] = resmgr.dupRef(primary);
    }

    void syncRoleSwap()
    {
        auto& resmgr = ResourceManager::get();
        for (size_t i = 0; i < 2; i++)
        {
            if (s_rolePrevious[i] != 0 && s_rolePrevious[i] != s_roleCurrent[i])
                resmgr.release(s_rolePrevious[i]);
            s_rolePrevious[i] = s_roleCurrent[i];
        }
    }
}
