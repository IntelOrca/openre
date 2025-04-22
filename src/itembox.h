#pragma once

#include "openre.h"
#include "re2.h"

namespace openre::itembox
{
    void sort_itembox();

    inline void set_itembox_item(int slotId, ItemType type, int quantity, int part)
    {
        gGameTable.itembox[slotId].Type = type;
        gGameTable.itembox[slotId].Quantity = quantity;
        gGameTable.itembox[slotId].Part = part;
    }
};