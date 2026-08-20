#include "font.h"

#include "game_module.h"
#include "logger.h"
#include "marni_renderer.h"
#include "render2d.h"
#include "resmgr.h"
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
    } // namespace

    ResourceCookie loadFontResource(std::string_view path)
    {
        // A ":variant" suffix selects a virtual sub-font of the same atlas
        // (only ":small" is supported for now - the compact ASCII 0x20-0x7F
        // set in an 8px-cell, 32-per-row grid on font0). The suffix is
        // stripped from the file path but kept in the resource name so each
        // variant registers separately.
        bool small = false;
        std::string filePath(path);
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

        marni::Image marniImage;
        if (!tim::decodeTim(bytes.data(), bytes.size(), marniImage))
        {
            logging::logError("[font] failed to decode TIM for '{}'", filePath);
            return 0;
        }
        if (marniImage.width <= 0 || marniImage.height <= 0)
        {
            logging::logError("[font] font '{}' decoded to an empty image", filePath);
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
        image.pixels = marniImage.pixels;
        image.palette = marniImage.palette;

        int textureHandle = render2d::SpriteRenderer::get().loadTexture(image);
        if (textureHandle == 0)
        {
            logging::logError("[font] font '{}' failed to upload to GPU", filePath);
            return 0;
        }

        auto& resmgr = ResourceManager::get();

        // The font atlas is exposed through a TextureResource so glyph sprite
        // components can reference it by cookie. All variants share the one
        // atlas for the file (deduped by the base path).
        auto atlasResource = std::make_unique<TextureResource>();
        atlasResource->textureHandle = textureHandle;
        atlasResource->width = image.width;
        atlasResource->height = image.height;
        auto atlasCookie = resmgr.addFirstRef<TextureResource>(filePath + "#atlas", std::move(atlasResource));

        auto font = std::make_unique<FontResource>();
        font->atlas = atlasCookie;
        font->width = image.width;
        font->height = image.height;
        // Page 0 (font0) holds ASCII + kana; page 1 (font1) holds kanji.
        font->page = filePath.find("font1") != std::string::npos ? 1 : 0;
        font->small = small;

        auto cookie = resmgr.addFirstRef<FontResource>(std::string(path), std::move(font));
        logging::logInfo(
            "[font] loaded '{}' (small={}) -> cookie {}, atlas {}, {}x{}",
            filePath,
            small,
            cookie,
            atlasCookie,
            image.width,
            image.height);
        return cookie;
    }
}
