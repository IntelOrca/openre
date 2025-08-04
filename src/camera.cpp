#include "camera.h"
#include "interop.hpp"
#include "openre.h"
#include "rdt.h"

using namespace openre::rdt;

namespace openre::camera
{
    // 0x004C4E90
    static VCut* cut_search(uint8_t cut_id)
    {
        auto vcut = rdt_get_offset<VCut>(RdtOffsetKind::RVD);
        while (vcut->fCut != cut_id)
        {
            vcut++;
        }
        return vcut;
    }

    // 0x004C4E60
    VCut* cut_change(uint8_t cut_id)
    {
        gGameTable.can_draw = 0;
        gGameTable.byte_989EEA = cut_id;
        gGameTable.vcut_data[1] = cut_search(cut_id);
        return gGameTable.vcut_data[1];
    }

    // 0x004C4EC0
    bool hit_camera_switch(Vec32* pos, VCut* camSwitch)
    {
        auto ax = camSwitch->a.x;
        auto az = camSwitch->a.y;
        auto bx = camSwitch->b.x;
        auto bz = camSwitch->b.y;
        auto cx = camSwitch->c.x;
        auto cz = camSwitch->c.y;
        auto dx = camSwitch->d.x;
        auto dz = camSwitch->d.y;

        // ABD
        auto px = pos->x - ax;
        auto py = pos->z - az;
        if ((bz - az) * px < (bx - ax) * py)
        {
            return false;
        }
        if ((dx - ax) * py < (dz - az) * px)
        {
            return false;
        }

        // CDB
        px = pos->x - cx;
        py = pos->z - cz;
        if ((bx - cx) * py < (bz - cz) * px)
        {
            return false;
        }
        if ((dz - cz) * px < (dx - cx) * py)
        {
            return false;
        }

        return true;
    }

    // 0x004C4DE0
    void cut_check(uint8_t cut_id)
    {
        auto camSwitch = gGameTable.vcut_data[1];
        camSwitch++;

        if (camSwitch->fCut == gGameTable.byte_989EEA)
        {
            uint8_t fcut;
            do
            {
                if (camSwitch->be_flg)
                {
                    if (camSwitch->nFloor == 0xFF || camSwitch->nFloor == gGameTable.pl.nFloor)
                    {
                        if (hit_camera_switch(&gGameTable.pl.m.pos, camSwitch))
                        {
                            cut_change(camSwitch->tCut);
                            return;
                        }
                    }
                }
                fcut = camSwitch[1].fCut;
                ++camSwitch;
            } while (fcut == gGameTable.byte_989EEA);
        }
        if (cut_id)
        {
            cut_change(gGameTable.byte_989EEA);
        }
    }

    // 0x004E5020
    VCut* sub_4E5020(uint8_t cut_id)
    {
        gGameTable.byte_98F07B = 1;
        gGameTable.cut_old = static_cast<uint8_t>(gGameTable.current_cut);
        gGameTable.current_cut = cut_id;
        return cut_change(cut_id);
    }

    void camera_init_hooks()
    {
        interop::writeJmp(0x004E5020, &sub_4E5020);
        interop::writeJmp(0x004C4E60, &cut_change);
        interop::writeJmp(0x004C4E90, &cut_search);
        interop::writeJmp(0x004C4DE0, &cut_check);
    }
};