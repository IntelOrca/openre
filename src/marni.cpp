#include "marni.h"
#include "interop.hpp"
#include "re2.h"
#include <stdio.h>
#include <iostream>
#include <type_traits>
#include <cstring>

namespace openre::marni
{
    // 0x00443620
    void mapping_tmd(int workNo, Md1* pTmd, int id)
    {
        interop::call<void, int, Md1*, int>(0x00443620, workNo, pTmd, id);
    }

    // 0x004DBFD0
    void out(void* data)
    {
        if (data == nullptr)
        {
            return;
        }

        // Check if it's a narrow string (char*)
        if (auto cstr = reinterpret_cast<char*>(data); cstr && strlen(cstr) > 0)
        {
            std::cout << cstr << std::endl;
        }
        // Check if it's a wide string (wchar_t*)
        else if (auto wstr = reinterpret_cast<wchar_t*>(data); wstr && wcslen(wstr) > 0)
        {
            std::wcout << wstr << std::endl;
        }
        // If it's anything else, do nothing
        else
        {
        }
    }

    // 0x00432BB0
    void unload_door_texture()
    {
        interop::call(0x00432BB0);
    }

    // 0x00442E40
    bool sub_442E40()
    {
        using sig = bool (*)();
        auto p = (sig)0x00442E40;
        return p();
    }

    void marni_out_hooks()
    {
        //interop::writeJmp(0x004DBFD0, &out);
    }
}
