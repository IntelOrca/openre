#pragma once

struct Md1;

namespace openre::marni
{
    void mapping_tmd(int workNo, Md1* pTmd, int id);
    void out();
    void unload_door_texture();
    bool sub_442E40();
    void unload_texture_page(int page);
    void door_disp0(int doorId, int a1, int a2, int a3);
    void door_disp1(int doorId);
}
