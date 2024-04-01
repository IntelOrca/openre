#include "marni.h"
#include "interop.hpp"

namespace openre::marni
{
    // 0x00443620
    void mapping_tmd(int workNo, Md1* pTmd, int id)
    {
        interop::call<void, int, Md1*, int>(0x00443620, workNo, pTmd, id);
    }

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
