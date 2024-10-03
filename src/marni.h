#pragma once
#include <wchar.h> 

struct Md1;

namespace openre::marni
{
    void mapping_tmd(int workNo, Md1* pTmd, int id);
    void out(void* data = nullptr);
    void unload_door_texture();
    bool sub_442E40();
    void marni_out_hooks();
}
