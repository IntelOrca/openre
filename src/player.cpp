#include "player.h"
#include "interop.hpp"
#include "item.h"
#include "openre.h"
#include "re2.h"
#include "scd.h"
#include "sce.h"

#include <cstring>

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

    using MoveTypeFunc = int (*)(void*);
    static MoveTypeFunc* gMoveTypeTable = (MoveTypeFunc*)0x53A7DC;

    using MoveFunc = void (*)(PlayerEntity*, uint32_t, uint32_t);
    static MoveFunc* gMoveBrTable = (MoveFunc*)0x53A7FC;
    static MoveFunc* gMoveMvTable = (MoveFunc*)0x53A82C;

    void (*br_tbl[13])(PlayerEntity* player, uint32_t key, uint32_t key_trg);
    void (*mv_tbl[13])(PlayerEntity* player, uint32_t pKanPtr, uint32_t pSeqPtr);

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

    // 0x004DABC0
    static int pl_neck(int a1, int a2)
    {
        using sig = int (*)(PlayerEntity*, int, int);
        auto p = (sig)0x004DABC0;
        return p(&gPlayerEntity, a1, a2);
    }

    // 0x004B3540
    static int rot_neck(int a1)
    {
        using sig = int (*)(PlayerEntity*, int);
        auto p = (sig)0x004B3540;
        return p(&gPlayerEntity, a1);
    }

    // 0x004D71C0
    static int pl_bow()
    {
        using sig = int (*)(PlayerEntity*);
        auto p = (sig)0x004D71C0;
        return p(&gPlayerEntity);
    }

    // 0x004D4850
    static int g_rot()
    {
        using sig = int (*)(PlayerEntity*);
        auto p = (sig)0x004D4850;
        return p(&gPlayerEntity);
    }

    // 0x004D4910
    static int gat_rot()
    {
        using sig = int (*)(PlayerEntity*);
        auto p = (sig)0x004D4910;
        return p(&gPlayerEntity);
    }

    // 0x004D46A0
    static int mag_down()
    {
        using sig = int (*)(PlayerEntity*);
        auto p = (sig)0x004D46A0;
        return p(&gPlayerEntity);
    }

    // 0x004D97B0
    static void player_move(PlayerEntity* player)
    {
        if (gGameTable.fg_stop < 0)
        {
            return;
        }
        gGameTable.word_989EEE &= 0xE0;
        gGameTable.fg_status &= ~0x80;
        if (player->damage_cnt & 0x7F)
        {
            player->damage_cnt--;
        }

        if (player->id == 13)
        {
            auto v3 = static_cast<uint8_t>((player->life << 15) / (player->max_life) >> 8);
            auto sinParts = reinterpret_cast<uint8_t*>(&(player->pSin_parts_ptr));
            sinParts[28] = ((v3 | (v3 << 8)) << 8) + 128;
            if (player->life < 0)
            {
                sinParts[28] = 128;
            }
            if (gPoisonStatus)
            {
                if (!(player->move_cnt & 1))
                {
                    gGameTable.dword_689BDC++;
                }
                gGameTable.dword_689BDC = (gGameTable.dword_689BDC & 0xFFFFFF) | 64;
            }
            else
            {
                if (!(gGameTable.dword_689BDC << 24) && !(player->move_cnt & 1))
                {
                    gGameTable.dword_689BDC--;
                }
            }
            sinParts[28] += gGameTable.dword_689BDC << 16;
        }

        if (gPoisonStatus)
        {
            if (gPoisonTimer-- == 0)
            {
                gPoisonTimer = 30;
                if (player->life > 1)
                {
                    player->life--;
                }
            }
        }

        gMoveTypeTable[player->routine_0](player);
        pl_neck(7000, 1500);
        rot_neck(player->cdir.y);
        if ((player->type & 0xFFF) == 12)
        {
            pl_bow();
        }
        g_rot();
        gat_rot();
        mag_down();
    }

    // 0x004EDF40
    static void snd_se_walk(int a0, int a1, PlayerEntity* player)
    {
        using sig = void (*)(int, int, PlayerEntity*);
        auto p = (sig)0x004EDF40;
        return p(a0, a1, player);
    }

    // 0x004C1C30
    static void joint_move(PlayerEntity* player, int a1, int a2, int a3)
    {
        using sig = void (*)(PlayerEntity*, int, int, int);
        auto p = (sig)0x004C1C30;
        return p(player, a1, a2, a3);
    }

    // 0x004B8470
    static int esp_call(int a0, int a1, Mat16 matrix, Vec16p vec)
    {
        using sig = int (*)(int, int, Mat16, Vec16p);
        auto p = (sig)0x004B8470;
        return p(a0, a1, matrix, vec);
    }

    // 0x004DAE70
    static void player_mv_rotate()
    {
        Vec16p pVec{ 0, 3, 6 };
        if (gPlayerEntity.routine_2 && gPlayerEntity.routine_2 != 1)
        {
            return;
        }
        else
        {
            gPlayerEntity.routine_2 = 1;
            gPlayerEntity.spd.x = 0;
            gPlayerEntity.spd.z = 0;
            gPlayerEntity.move_no = *reinterpret_cast<uint32_t*>(&gPlayerEntity.d_life_u) + 458752;
            gGameTable.fg_status &= 0xffffffC0;
        }

        joint_move(&gPlayerEntity, gPlayerEntity.pKan_t_ptr, gPlayerEntity.pSeq_t_ptr, 512);

        if (*gPlayerEntity.pNow_seq & 0x4000)
        {
            snd_se_walk(0, 3 * ((*gPlayerEntity.pNow_seq >> 13) & 1) + 4, &gPlayerEntity);
            gGameTable.word_989EEE |= 2;
        }

        if (gPlayerEntity.water && (gPlayerEntity.move_cnt & 1) != 0)
        {
            pVec = Vec16p{ 0, 300, 0 };
            auto sinPartsAddr = gPlayerEntity.pSin_parts_ptr + 1892;
            auto sinParts = reinterpret_cast<uint8_t*>(&sinPartsAddr);

            if (gPlayerEntity.water < static_cast<int32_t>(sinParts[24]) + 300)
            {
                auto matrix = *reinterpret_cast<Mat16*>(sinParts[72]);
                esp_call((4 * rnd() + 1548) | 0x1A000000, gPlayerEntity.cdir.y, matrix, pVec);
            }

            sinParts = reinterpret_cast<uint8_t*>(&gPlayerEntity.pSin_parts_ptr);
            if (gPlayerEntity.water < static_cast<int32_t>(sinParts[626]) + 300)
            {
                auto matrix = *reinterpret_cast<Mat16*>(sinParts[672]);
                esp_call((4 * rnd() + 1548) | 0x1A000000, gPlayerEntity.cdir.y, matrix, pVec);
            }
        }
    }

    // 0x004D9D20
    static void pl_move(PlayerEntity* player)
    {
        auto pKan = *reinterpret_cast<uint32_t*>(&(player->pKan_t_ptr));
        auto seq = *reinterpret_cast<uint32_t*>(&(player->pSeq_t_ptr));

        gMoveBrTable[player->routine_1](player, gGameTable.g_key, gGameTable.key_trg);
        gMoveMvTable[player->routine_1](player, player->pKan_t_ptr, player->pSeq_t_ptr);
    }

    // 0x004DA6C0
    static void pl_br_03(PlayerEntity* player, uint32_t key, uint32_t key_trg)
    {
        static uint8_t tbl[] = { 0x28, 0x10, 0x10 };

        if (!player->routine_2 || !player->move_cnt)
        {
            int t = player->d_life_u;
            player->d_life_u = 0;
            if (player->life <= 100)
            {
                player->d_life_u = 1;
            }
            if (gPoisonStatus)
            {
                player->d_life_u = 1;
            }
            if (player->life <= 20)
            {
                player->d_life_u = 2;
            }
            if (t != player->d_life_u)
            {
                player->routine_2 = 0;
            }
        }
        // back is pressed
        if (key & 4)
        {
            // left
            if (key & 2)
            {
                player->cdir.y += tbl[player->d_life_u];
            }
            // right
            if (key & 8)
            {
                player->cdir.y -= tbl[player->d_life_u];
            }

            // run/cancel
            if (key_trg & 0x0200)
            {
                // trigger quickturn
                *(uint32_t*)&player->routine_0 = 0xC01;
                return;
            }
            if ((key_trg & 0x80) != 0)
            {
                player->status_flg |= 0x200000;
            }
            if (key & 0x100 && player->type & 0xFFF)
            {
                *(uint32_t*)player->routine_0 = 0x501;
            }
        }
        else
        {
            *(uint32_t*)&player->routine_0 = 1;

            if (key & 0xA)
            {
                *(uint32_t*)&player->routine_0 = 0x401;
            }

            if (key & 1)
            {
                *(uint32_t*)&player->routine_0 = 0x101;
            }
        }
    }

    // no input code is required for quickturn
    void pl_br_quickturn(PlayerEntity* player, uint32_t key, uint32_t key_trg) {}

    void pl_mv_quickturn(PlayerEntity* player, uint32_t pKanPtr, uint32_t pSeqPtr)
    {
        switch (player->routine_2)
        {
        case 0:
            player->routine_2 = 1;
            player->timer0 = 0;
            // set base walk animation
            *(uint32_t*)&player->move_no = 0x070000;
            [[fallthrough]];

        case 1:
            if (player->timer0++ < 8)
            {
                player->cdir.y += 2048 / 8;
            }
            else
            {
                *(uint32_t*)&player->routine_0 = 1;
            }
            break;
        }

        if (*player->pNow_seq & 0x4000)
        {
            snd_se_walk(0, 4 + (player_get_floor_sound(player) * 3), player);
            gGameTable.word_989EEE |= 2;
        }
        joint_move(player, player->pKan_t_ptr, player->pSeq_t_ptr, 512);
    }

    void init_quickturn_move()
    {
        // fill expanded tables with old code
        std::memcpy(br_tbl, gMoveBrTable, 12 * 4);
        std::memcpy(mv_tbl, gMoveMvTable, 12 * 4);
        // set hooks for quickturn
        br_tbl[03] = pl_br_03;
        br_tbl[12] = pl_br_quickturn;
        mv_tbl[12] = pl_mv_quickturn;
        // replace old table pointers
        gMoveBrTable = br_tbl;
        gMoveMvTable = mv_tbl;
    }

    void player_init_hooks()
    {
        interop::writeJmp(0x00502190, &partner_switch);
        interop::writeJmp(0x00502660, &inventory_find_item);
        interop::writeJmp(0x4FC3CE, itembox_prev_slot);
        interop::writeJmp(0x5024D0, set_inventory_item);
        interop::writeJmp(0x502500, set_inventory_item_quantity);
        interop::writeJmp(0x4D97B0, player_move);
        interop::writeJmp(0x4D9D20, pl_move);
        interop::writeJmp(0x4DAE70, player_mv_rotate);
        interop::writeJmp(0x004DA6C0, pl_br_03);
        init_quickturn_move();
    }

    bool is_aiming()
    {
        return (
            check_flag(FlagGroup::Status, FG_STATUS_PLAYER) && check_flag(FlagGroup::Status, FG_STATUS_GAMEPLAY)
            && check_flag(FlagGroup::Status, FG_STATUS_ITEM) && check_flag(FlagGroup::Status, FG_STATUS_24)
            && !check_flag(FlagGroup::Status, FG_STATUS_SCREEN));
    }

    bool is_picking_up_item()
    {
        return (
            check_flag(FlagGroup::Status, FG_STATUS_PLAYER) && check_flag(FlagGroup::Status, FG_STATUS_GAMEPLAY)
            && check_flag(FlagGroup::Status, FG_STATUS_ITEM) && !check_flag(FlagGroup::Status, FG_STATUS_24)
            && !check_flag(FlagGroup::Status, FG_STATUS_SCREEN));
    }
}
