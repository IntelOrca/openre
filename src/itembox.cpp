#include "itembox.h"
#include "re2.h"
#include "item.h"
#include "openre.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace openre::itembox
{
    constexpr uint8_t ITEMBOX_SIZE = 64;

    void sort_itembox()
    {
        // Skip the first two items, they point to items 62 and 63
        int nextFreeSlot = 2;
        std::array<ItemboxItem, ITEMBOX_SIZE> sortedItems{};
        for (uint8_t i = 2; i < ITEMBOX_SIZE; i++)
        {
            // Adjust the index to wrap around for the first two iterations
            uint8_t adjustedIndex = i % ITEMBOX_SIZE;

            auto& item = gGameTable.itembox[adjustedIndex];
            if (item.Type != ITEM_TYPE_NONE)
            {
                sortedItems[nextFreeSlot++] = item;
            }
        }

        std::copy(sortedItems.begin(), sortedItems.end(), gGameTable.itembox);
    }
};