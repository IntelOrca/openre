#include "interop.hpp"
#include "scd.h"
#include "sce.h"
#include "re2.h"

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
    uint8_t& gInventorySize = *((uint8_t*)0x98E9A4);
    InventorySlot* gInventory = (InventorySlot*)0x98ED34;


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
        if (pldType == PLD_ADA || pldType == PLD_SHERRY)
        {
            gSavedWeaponIndex = gCurrentWeaponIndex;
            gSavedInventorySize = gInventorySize;
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

            gInventorySize = 8;
            for (auto i = 0; i < FULL_INVENTORY_SIZE; i++)
            {
                gSavedInventory[i] = gInventory[i];
                gInventory[i].Type = srcInventory[i].Type;
                gInventory[i].Quantity = srcInventory[i].Quantity;
                gInventory[i].Part = srcInventory[i].Part;
            }
        }
        else
        {
            if (bitarray_get(dword_98EB4C, UNK_BIT_INVENTORY_SAVED))
            {
                // TODO just unstash the special slot as well
                for (auto i = 0; i < FULL_INVENTORY_SIZE - 1; i++)
                {
                    gInventory[i] = gSavedInventory[i];
                }

                gInventorySize = gSavedInventorySize;
                gCurrentWeaponIndex = gSavedWeaponIndex;
                if (gCurrentWeaponIndex == WEAPON_INDEX_NONE)
                {
                    gCurrentWeaponType = ITEM_TYPE_NONE;
                }
                else
                {
                    gCurrentWeaponType = gInventory[gCurrentWeaponIndex].Type;
                }

                // TODO this isn't necessary if you just unstash the special slot
                gInventory[INVENTORY_INDEX_SPECIAL].Type = ITEM_TYPE_LOCKPICK;
                gInventory[INVENTORY_INDEX_SPECIAL].Quantity = 1;
                gInventory[INVENTORY_INDEX_SPECIAL].Part = 0;
                if ((pldType & 1) == 0)
                {
                    gInventory[INVENTORY_INDEX_SPECIAL].Type = ITEM_TYPE_LIGHTER;
                }

                bitarray_clr(dword_98EB4C, UNK_BIT_INVENTORY_SAVED);
            }
        }
    }

    // 0x00502660
    int inventory_find_item(ItemType type)
    {
        for (int i = 0; i < gInventorySize; i++)
        {
            if (gInventory[i].Type == type)
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
