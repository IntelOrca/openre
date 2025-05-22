#pragma once

#include <string>
#include <string_view>

namespace openre
{
    std::string toUTF8(std::wstring_view s);
}
