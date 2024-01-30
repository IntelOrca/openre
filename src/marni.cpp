#include "marni.h"
#include "interop.hpp"

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
}
