#include "interop.hpp"
#include "scd.h"
#include "sce.h"
#include "re2.h"
#include "openre.h"

using namespace openre::sce;

namespace openre::player
{
    extern uint8_t WEAPON_INDEX_NONE = 128;

    extern uint8_t UNK_BIT_INVENTORY_SAVED = 127;

    uint8_t& gCurrentWeaponIndex = *((uint8_t*)0x691F68);
    uint8_t& gCurrentWeaponType = *((uint8_t*)0x691F6A);
    uint8_t& gSavedWeaponIndex = *((uint8_t*)0x9888DA);
    uint8_t& gSavedInventorySize = *((uint8_t*)0x9888DB);
    InventorySlot* gSavedInventory = (InventorySlot*)0x9888DC;

    static uint32_t* dword_98EB4C = (uint32_t*)0x98EB4C;

    static const InventoryDef _initialInventoryAda[FULL_INVENTORY_SIZE] = {
        { ITEM_TYPE_HANDGUN_CLAIRE, 13, 0 },
        { ITEM_TYPE_AMMO_HANDGUN, 45, 0 },
        { ITEM_TYPE_FIRST_AID_SPRAY, 1, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_PHOTO_ADA, 1, 0 },
    };

    static const InventoryDef _initialInventorySherry[FULL_INVENTORY_SIZE] = {
        { ITEM_TYPE_FIRST_AID_SPRAY, 1, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_NONE, 0, 0 },
        { ITEM_TYPE_PHOTO_SHERRY, 1, 0 },
    };

    // 0x00502190
    static void partner_switch(PldType pldType)
    {
        auto inventory = gGameTable.inventory;
        if (pldType == PLD_ADA || pldType == PLD_SHERRY)
        {
            gSavedWeaponIndex = gCurrentWeaponIndex;
            gSavedInventorySize = gGameTable.inventory_size;
            bitarray_set(dword_98EB4C, UNK_BIT_INVENTORY_SAVED);
            const InventoryDef* srcInventory;
            if (pldType == PLD_ADA)
            {
                gCurrentWeaponType = ITEM_TYPE_HANDGUN_CLAIRE;
                gCurrentWeaponIndex = 0;
                srcInventory = _initialInventoryAda;
            }
            else
            {
                gCurrentWeaponType = ITEM_TYPE_NONE;
                gCurrentWeaponIndex = WEAPON_INDEX_NONE;
                srcInventory = _initialInventorySherry;
            }

            gGameTable.inventory_size = 8;
            for (auto i = 0; i < FULL_INVENTORY_SIZE; i++)
            {
                gSavedInventory[i] = inventory[i];
                inventory[i].Type = srcInventory[i].Type;
                inventory[i].Quantity = srcInventory[i].Quantity;
                inventory[i].Part = srcInventory[i].Part;
            }
        }
        else
        {
            if (bitarray_get(dword_98EB4C, UNK_BIT_INVENTORY_SAVED))
            {
                // TODO just unstash the special slot as well
                for (auto i = 0; i < FULL_INVENTORY_SIZE - 1; i++)
                {
                    inventory[i] = gSavedInventory[i];
                }

                gGameTable.inventory_size = gSavedInventorySize;
                gCurrentWeaponIndex = gSavedWeaponIndex;
                if (gCurrentWeaponIndex == WEAPON_INDEX_NONE)
                {
                    gCurrentWeaponType = ITEM_TYPE_NONE;
                }
                else
                {
                    gCurrentWeaponType = inventory[gCurrentWeaponIndex].Type;
                }

                // TODO this isn't necessary if you just unstash the special slot
                inventory[INVENTORY_INDEX_SPECIAL].Type = ITEM_TYPE_LOCKPICK;
                inventory[INVENTORY_INDEX_SPECIAL].Quantity = 1;
                inventory[INVENTORY_INDEX_SPECIAL].Part = 0;
                if ((pldType & 1) == 0)
                {
                    inventory[INVENTORY_INDEX_SPECIAL].Type = ITEM_TYPE_LIGHTER;
                }

                bitarray_clr(dword_98EB4C, UNK_BIT_INVENTORY_SAVED);
            }
        }
    }

    // 0x00502660
    int inventory_find_item(ItemType type)
    {
        auto inventory = gGameTable.inventory;
        for (int i = 0; i < gGameTable.inventory_size; i++)
        {
            if (inventory[i].Type == type)
            {
                return i;
            }
        }
        return -1;
    }

    void player_init_hooks()
    {
        interop::writeJmp(0x00502190, &partner_switch);
        interop::writeJmp(0x00502660, &inventory_find_item);
    }
}
