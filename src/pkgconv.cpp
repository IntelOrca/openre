#include "pkg.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace openre
{
    class PackageConverter
    {
    private:
        fs::path srcBasePath;
        fs::path dstBasePath;

    public:
        PackageConverter(const fs::path& dst, const fs::path& src)
            : srcBasePath(src)
            , dstBasePath(dst)
        {
        }

        void correct(std::string_view subdir)
        {
            auto fullSubir = srcBasePath / subdir;
            if (fs::is_directory(fullSubir))
            {
                srcBasePath = fullSubir;
            }
        }

        void import(std::string_view dst, std::string_view src)
        {
            auto srcFull = (srcBasePath / src).lexically_normal();
            auto dstFull = (dstBasePath / dst).lexically_normal();
            fs::create_directories(dstFull.parent_path());
            fs::copy_file(srcFull, dstFull, fs::copy_options::overwrite_existing);
            std::printf("%s\n", dstFull.u8string().c_str());
        }

        void importSound(std::string_view dstPath, std::string_view srcPath, int index)
        {
            auto fullSrcPath = (srcBasePath / srcPath).lexically_normal();
            auto fullDstPath = (dstBasePath / dstPath).lexically_normal();
            fs::create_directories(fullDstPath.parent_path());

            auto bytes = readFile(fullSrcPath);

            // Skip SAP header
            const auto* src = bytes.data() + 8;

            // WAVE header
            auto currentIndex = 0;
            const uint8_t* waveStart;
            do
            {
                waveStart = src;
                src += 4;
                uint32_t fileSize;
                std::memcpy(&fileSize, src, sizeof(uint32_t));
                src += fileSize;
                src += 4;
            } while (currentIndex++ < index);
            writeFile(fullDstPath, waveStart, src - waveStart);
            std::printf("%s\n", fullDstPath.u8string().c_str());
        }

    private:
        std::vector<uint8_t> readFile(const fs::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                throw std::runtime_error("Could not open file: " + path.string());

            file.seekg(0, std::ios::end);
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);

            std::vector<uint8_t> buffer(static_cast<size_t>(size));
            if (!file.read((char*)buffer.data(), size))
                throw std::runtime_error("Error reading file: " + path.string());

            return buffer;
        }

        void writeFile(const fs::path& path, const void* buffer, size_t len)
        {
            std::ofstream file(path, std::ios::binary);
            if (!file)
                throw std::runtime_error("Could not open file for writing: " + path.string());

            file.write(static_cast<const char*>(buffer), len);
            if (!file)
                throw std::runtime_error("Error writing to file: " + path.string());
        }
    };

    static void convertPackageRe2(PackageConverter& c)
    {
        c.correct("data");
        c.import("font/font1.tim", "common/data/font0.tim");
        c.import("font/font2.tim", "common/data/font0.tim");
        c.import("texture/splashbg.adt", "common/data/gw2.adt");
        c.import("texture/savebg.adt", "common/data/type00.adt");
        c.import("texture/titlebg.adt", "common/data/title_bg.adt");
        c.importSound("sound/apply.wav", "common/sound/core/core22.sap", 2);
        c.importSound("sound/back.wav", "common/sound/core/core22.sap", 1);
        c.importSound("sound/biohazard.wav", "common/sound/core/core16.sap", 0);
        c.importSound("sound/residentevil.wav", "common/sound/core/core17.sap", 0);
        c.importSound("sound/select.wav", "common/sound/core/core22.sap", 0);
        c.import("movie/intro.mpg", "pl0/zmovie/title_l.bin");
    }

    static void convertPackageRe2hd(PackageConverter& c)
    {
        c.correct("hires");
        c.import("font/font1.webp", "misc/AD08D2F9.webp");
        c.import("font/font2.webp", "misc/AD08D2F9.webp");
        c.import("texture/splashbg.webp", "bgd/6A04CF1C.webp");
        c.import("texture/logo1.webp", "misc/7D7B2C69.webp");
        c.import("texture/logo2.webp", "misc/2498232B.webp");
        c.import("texture/savebg.webp", "bgd/2AC72113.webp");
        c.import("texture/titlebg.webp", "bgd/2C05BCDB.webp");
    }

    /**
     * Converts an existing original game installation or CR mod to
     * an OpenRE compatible game or mod package.
     */
    void convertPackage(KnownPackageId id, const fs::path& dst, const fs::path& src)
    {
        PackageConverter c(dst, src);
        switch (id)
        {
        case KnownPackageId::re2: return convertPackageRe2(c);
        case KnownPackageId::re2hd: return convertPackageRe2hd(c);
        default: throw std::invalid_argument("Unknown package id");
        }
    }

    int pkgconv(int argc, const char** argv)
    {
        if (argc < 5)
        {
            std::printf("usage: openre pkgconv <id> <source> <target>\n");
            return 1;
        }

        auto id = static_cast<KnownPackageId>(std::atoi(argv[2]));
        auto src = fs::u8path(argv[3]);
        auto dst = fs::u8path(argv[4]);
        try
        {
            convertPackage(id, dst, src);
            return 0;
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "%s\n", e.what());
            return 2;
        }
    }
}
