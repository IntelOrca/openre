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

    // 0x004C4DE0
    void cut_check(uint8_t cut_id)
    {
        interop::call<void, uint8_t>(0x004C4DE0, cut_id);
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
    }
};