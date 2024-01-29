#include "marni.h"
#include "interop.hpp"

namespace openre::marni
{
    // 0x00432BB0
    void unload_door_texture()
    {
        interop::call(0x00432BB0);
    }
}
