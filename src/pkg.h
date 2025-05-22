#pragma once

#include <filesystem>

namespace openre
{
    enum class KnownPackageId
    {
        unknown,
        re1,
        re2,
        re3,
        re1hd = 4 | 1,
        re2hd = 4 | 2,
        re3hd = 4 | 3,
    };

    void convertPackage(KnownPackageId id, const std::filesystem::path& dst, const std::filesystem::path& src);
    int pkgconv(int argc, const char** argv);
}
