#include "itembox.h"
#include "item.h"
#include "openre.h"
#include "re2.h"
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace openre::itembox
{
    constexpr uint8_t ITEMBOX_SIZE = 64;
    constexpr uint8_t MAX_STACK_SIZE = 250;

    bool is_stackable(int itemType)
    {
        switch (itemType)
        {
        case ITEM_TYPE_AMMO_HANDGUN:
        case ITEM_TYPE_AMMO_SHOTGUN:
        case ITEM_TYPE_AMMO_BOWGUN:
        case ITEM_TYPE_AMMO_EXPLOSIVE_ROUNDS:
        case ITEM_TYPE_AMMO_ACID_ROUNDS:
        case ITEM_TYPE_AMMO_FLAME_ROUNDS:
        case ITEM_TYPE_AMMO_MAGNUM:
        case ITEM_TYPE_INK_RIBBON: return true;
        default: return false;
        }
    }

    void sort_itembox()
    {
        std::unordered_map<int, int> stackableSlotdIds{};

        int nextFreeSlot = 0;
        std::vector<ItemboxItem> sortedItems(ITEMBOX_SIZE);
        for (uint8_t i = 0; i < ITEMBOX_SIZE; i++)
        {
            auto& item = gGameTable.itembox[i];
            if (item.Type != ITEM_TYPE_NONE)
            {
                if (!is_stackable(item.Type))
                {
                    sortedItems[nextFreeSlot++] = item;
                    continue;
                }

                if (stackableSlotdIds.find(item.Type) == stackableSlotdIds.end())
                {
                    stackableSlotdIds[item.Type] = nextFreeSlot++;
                    sortedItems[stackableSlotdIds[item.Type]] = item;
                }
                else
                {
                    auto& stackedItem = sortedItems[stackableSlotdIds[item.Type]];
                    if ((stackedItem.Quantity + item.Quantity) <= MAX_STACK_SIZE)
                    {
                        stackedItem.Quantity += item.Quantity;
                    }
                    else
                    {
                        item.Quantity = (stackedItem.Quantity + item.Quantity) - MAX_STACK_SIZE;
                        stackedItem.Quantity = MAX_STACK_SIZE;

                        auto slotId = stackableSlotdIds[item.Type];
                        sortedItems.insert(sortedItems.begin() + slotId + 1, item);
                        stackableSlotdIds[item.Type]++;
                        nextFreeSlot++;
                    }
                }
            }
        }

        // Sort item by type.
        std::sort(sortedItems.begin(), sortedItems.end(), [](ItemboxItem& a, ItemboxItem& b) {
            if (a.Type == ITEM_TYPE_NONE && b.Type != ITEM_TYPE_NONE)
            {
                return false;
            }
            else if (a.Type != ITEM_TYPE_NONE && b.Type == ITEM_TYPE_NONE)
            {
                return true;
            }

            return a.Type < b.Type;
        });

        // Rotate all items two slot to the right.
        // First two point to items 62 and 63.
        std::rotate(sortedItems.rbegin(), sortedItems.rbegin() + 2, sortedItems.rend());

        std::copy(sortedItems.begin(), sortedItems.end(), gGameTable.itembox);
    }
};
