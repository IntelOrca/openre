#include "player.h"
#include "interop.hpp"
#include "item.h"
#include "openre.h"
#include "re2.h"
#include "scd.h"
#include "sce.h"

using namespace openre::sce;

namespace openre::player
{
    using PldType = uint8_t;

    constexpr uint8_t INVENTORY_INDEX_SPECIAL = 10;
    constexpr uint8_t FULL_INVENTORY_SIZE = 11;

    extern uint8_t WEAPON_INDEX_NONE = 128;

    extern uint8_t UNK_BIT_INVENTORY_SAVED = 127;

    uint8_t& gCurrentWeaponIndex = *((uint8_t*)0x691F68);
    uint8_t& gCurrentWeaponType = *((uint8_t*)0x691F6A);
    uint8_t& gSavedWeaponIndex = *((uint8_t*)0x9888DA);
    uint8_t& gSavedInventorySize = *((uint8_t*)0x9888DB);
    InventorySlot* gSavedInventory = (InventorySlot*)0x9888DC;

    static uint32_t* dword_98EB4C = (uint32_t*)0x98EB4C;
    static uint8_t*& byte_98ED39 = *((uint8_t**)0x98ED39);
    static HudInfo& gHudInfo = *((HudInfo*)0x691F60);

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

    // 0x004FC3FD
    static void loc_4FC3FD()
    {
        using sig = void (*)();
        auto p = (sig)0x004FC3FD;
        return p();
    }

    // 0x004FC3CE
    static void itembox_prev_slot()
    {
        gGameTable.byte_691F85 = 0;
        gHudInfo.var_24 = 0;
        gGameTable.itembox_slot_id--;
        gGameTable.itembox_slot_id &= 0x3F;
        loc_4FC3FD();
    }

    // 0x005024D0
    static int set_inventory_item(int slotId, int type, int quantity, int part)
    {
        gGameTable.inventory[slotId].Type = type;
        gGameTable.inventory[slotId].Quantity = quantity;
        gGameTable.inventory[slotId].Part = part;
        return slotId;
    }

    // 0x00502500
    static void set_inventory_item_quantity(int slotId, int quantity)
    {
        gGameTable.inventory[slotId].Quantity = quantity;

        auto part = gGameTable.inventory[slotId].Part;
        if (part == 1)
        {
            byte_98ED39[4 * slotId] = quantity;
        }
        if (part == 2)
        {
            gGameTable.inventory[slotId].Quantity = quantity;
        }
    }

    void player_init_hooks()
    {
        interop::writeJmp(0x00502190, &partner_switch);
        interop::writeJmp(0x00502660, &inventory_find_item);
        interop::writeJmp(0x4FC3CE, itembox_prev_slot);
        interop::writeJmp(0x5024D0, set_inventory_item);
        interop::writeJmp(0x502500, set_inventory_item_quantity);
    }
}
