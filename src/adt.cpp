#include "adt.h"
#include "interop.hpp"
#include "openre.h"
#include "re2.h"

#include <filesystem>

namespace openre::graphics
{
    std::vector<uint8_t> decodeAdt(const void* input, size_t length)
    {
        std::vector<uint8_t> result;
        std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "openre.adt";
        auto fp = fopen(tempPath.u8string().c_str(), "wb");
        if (fp != NULL)
        {
            auto bytesWritten = fwrite(input, 1, length, fp);
            fclose(fp);
            if (bytesWritten == length)
            {
                result.resize(0x80000);
                auto outLen = load_adt(tempPath.u8string().c_str(), (uint32_t*)result.data(), 0);
                result.resize(outLen);
            }
            std::filesystem::remove(tempPath);
        }
        return result;
    }

    // 0x0043C590
    int load_adt(const char* path, uint32_t* bufferSize, int mode)
    {
        return interop::call<int, const char*, uint32_t*, int>(0x0043C590, path, bufferSize, mode);
    }
}
