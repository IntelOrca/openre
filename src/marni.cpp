#include "marni.h"
#include "interop.hpp"
#include "re2.h"

namespace openre::marni
{
    // 0x004DBFD0
    void out()
    {
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
}
