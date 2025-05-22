#include "str.h"

#include <string>
#include <string_view>
#include <windows.h>

namespace openre
{
    std::string toUTF8(std::wstring_view s)
    {
        auto nSize = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
        std::string result(nSize, 0);
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &result[0], nSize, nullptr, nullptr);
        return result;
    }
}
