#include "player.h"
#include "audio.h"
#include "enemy.h"
#include "entity.h"
#include "input.h"
#include "interop.hpp"
#include "item.h"
#include "openre.h"
#include "re2.h"
#include "scd.h"
#include "sce.h"

#include <cstring>

using namespace openre::audio;
using namespace openre::sce;
using namespace openre::enemy;
using namespace openre::input;

namespace openre::player
{
    using PldType = uint8_t;

    constexpr uint8_t INVENTORY_INDEX_SPECIAL = 10;

    extern uint8_t WEAPON_INDEX_NONE = 128;

    extern uint8_t UNK_BIT_INVENTORY_SAVED = 127;

    uint8_t& gCurrentWeaponIndex = *((uint8_t*)0x691F68);
    uint8_t& gCurrentWeaponType = *((uint8_t*)0x691F6A);
    uint8_t& gSavedWeaponIndex = *((uint8_t*)0x9888DA);
    uint8_t& gSavedInventorySize = *((uint8_t*)0x9888DB);
    InventorySlot* gSavedInventory = (InventorySlot*)0x9888DC;

    static uint32_t* dword_98EB4C = (uint32_t*)0x98EB4C;
    static HudInfo& gHudInfo = *((HudInfo*)0x691F60);

    using MoveTypeFunc = void (*)(PlayerEntity*, int, int);
    static MoveTypeFunc* gMoveTypeTable = (MoveTypeFunc*)0x53A7DC;

    using MoveKeyFunc = void (*)(PlayerEntity*, uint32_t, uint32_t);
    using MoveFunc = void (*)(PlayerEntity*, Emr*, Edd*);
    static MoveKeyFunc* gMoveBrTable = (MoveKeyFunc*)0x53A7FC;
    static MoveFunc* gMoveMvTable = (MoveFunc*)0x53A82C;
    static MoveFunc* gMoveDamageTable = (MoveFunc*)0x53A85C;

    void (*br_tbl[13])(PlayerEntity* player, uint32_t key, uint32_t key_trg);
    void (*mv_tbl[13])(PlayerEntity* player, Emr* pKanPtr, Edd* pSeqPtr);
    void (*dmg_tbl[6])(PlayerEntity* player, Emr* pKanPtr, Edd* pSeqPtr);

    using MoveAimWeaponFunc = void (*)(PlayerEntity*, Emr*, Edd*, uint32_t);

    const int now_seq_0x4000 = 0x4000;

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

    enum
    {
        MOVE_TYPE_INIT = 0,
        MOVE_TYPE_MOVE = 1,
        MOVE_TYPE_DAMAGE = 2,
        MOVE_TYPE_DIE = 3,
        MOVE_TYPE_CUTSCENE = 4,
        MOVE_TYPE_EM_DAMAGE = 5,
        MOVE_TYPE_EM_DIE = 6,
        MOVE_TYPE_DEAD = 7,
    };

    enum
    {
        PICK_UP_ITEM_INTERACT_STATE_FLOOR = 0,
        PICK_UP_ITEM_INTERACT_STATE_REACHING = 1,
        PICK_UP_ITEM_INTERACT_STATE_REACHED = 2,
        PICK_UP_ITEM_INTERACT_STATE_4 = 4,
        PICK_UP_ITEM_INTERACT_STATE_5 = 5,
        PICK_UP_ITEM_INTERACT_STATE_6 = 6,
    };

    enum
    {
        BR_TBL_IDLE_IDX = 0,
        BR_TBL_FORWARD_IDX = 1,
        BR_TBL_RUN_FORWARD_IDX = 2,
        BR_TBL_BACKWARD_IDX = 3,
        BR_TBL_ROTATE_IDX = 4,
        BR_TBL_QUICKTURN_IDX = 12,
    };

    enum
    {
        PUSH_OBJ_STATE_0,
        PUSH_OBJ_STATE_PLACE_IN_FRONT,
        PUSH_OBJ_STATE_2,
        PUSH_OBJ_STATE_3,
        PUSH_OBJ_STATE_START_PUSHING,
        PUSH_OBJ_STATE_PUSHING,
        PUSH_OBJ_STATE_STOP_PUSHING,
        PUSH_OBJ_STATE_END,
    };

    enum
    {
        CLIMB_ON_STATE_0,
        CLIMB_ON_STATE_PLACE_IN_FRONT,
        CLIMB_ON_STATE_2,
        CLIMB_ON_STATE_CLIMBING,
        CLIMB_ON_STATE_END,
    };

    enum
    {
        PLAYER_LIFE_STATUS_FINE,
        PLAYER_LIFE_STATUS_CAUTION_1,
        PLAYER_LIFE_STATUS_CAUTION_2,
        PLAYER_LIFE_STATUS_DANGER,
        PLAYER_LIFE_STATUS_POISONED,
    };

    void set_routine(Routine routine)
    {
        switch (routine)
        {
        case Routine::IDLE:
            gPlayerEntity.routine_0 = 1;
            gPlayerEntity.routine_1 = 0;
            break;
        case Routine::FORWARD:
            gPlayerEntity.routine_0 = 1;
            gPlayerEntity.routine_1 = 1;
            break;
        case Routine::RUN_FORWARD:
            gPlayerEntity.routine_0 = 1;
            gPlayerEntity.routine_1 = 2;
            break;
        case Routine::BACKWARD:
            gPlayerEntity.routine_0 = 1;
            gPlayerEntity.routine_1 = 3;
            break;
        case Routine::ROTATE:
            gPlayerEntity.routine_0 = 1;
            gPlayerEntity.routine_1 = 4;
            break;
        case Routine::AIM:
            gPlayerEntity.routine_0 = 1;
            gPlayerEntity.routine_1 = 5;
            break;
        case Routine::QUICKTURN:
            gPlayerEntity.routine_0 = 1;
            gPlayerEntity.routine_1 = 0xC;
            break;
        case Routine::PUSH_OBJECT:
            gPlayerEntity.routine_0 = 1;
            gPlayerEntity.routine_1 = 0xA;
            break;
        case Routine::PICK_UP_ITEM:
            gPlayerEntity.routine_1 = 6;
            gPlayerEntity.routine_2 = 0;
            break;
        }
        gPlayerEntity.routine_2 = 0;
        gPlayerEntity.routine_3 = 0;
    }

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

    // 0x004E2680
    int sca_hit_stairs(PlayerEntity* player, int a1, int a2)
    {
        using sig = int (*)(PlayerEntity*, int, int);
        auto p = (sig)0x004E2680;
        return p(&gPlayerEntity, a1, a2);
    }

    // 0x004CDE00
    static int oma_pl_updown_ck(int a0)
    {
        using sig = int (*)(int);
        auto p = (sig)0x004CDE00;
        return p(a0);
    }

    // 0x004D49C0
    static void pl_water(PlayerEntity* player)
    {
        interop::call<void, PlayerEntity*>(0x004D49C0, player);
    }

    // 0x004CEDE0
    static void oma_ob_pull2(PlayerEntity* player, int a1, uint16_t a2, uint32_t a3)
    {
        interop::call<void, PlayerEntity*, int, uint16_t, uint32_t>(0x004CEDE0, player, a1, a2, a3);
    }

    // 0x004D9940
    static int pl_init(PlayerEntity* player)
    {
        using sig = int (*)(PlayerEntity*);
        auto p = (sig)0x004D9940;
        return p(player);
    }

    // 0x004B2B00
    static int foot_set_pl(PlayerEntity* player, int a1, int a2)
    {
        return interop::call<int, PlayerEntity*, int, int>(0x004B2B00, player, a1, a2);
    }

    // 0x004E2AE0
    static int sca_ck_hit(Vec32* vec, int a1, int a2, int a3)
    {
        return interop::call<int, Vec32*, int, int, int>(0x004E2AE0, vec, a1, a2, a3);
    }

    // 0x004D9D20
    static void pl_move(PlayerEntity* player, Emr* pKan, Edd* pSeq)
    {
        if (player->routine_1 <= BR_TBL_ROTATE_IDX)
        {
            // Logic shared in br moves from 0 to 4
            if (player->routine_2 == 0 || player->move_cnt == 0)
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
        }
        gMoveBrTable[player->routine_1](player, gGameTable.g_key, gGameTable.key_trg);
        gMoveMvTable[player->routine_1](player, pKan, pSeq);
    }

    // 0x004DC130
    static void pl_mv_damage(PlayerEntity* player, Emr* pKan, Edd* pSeq)
    {
        set_flag(FlagGroup::Status, FG_STATUS_25, true);
        gMoveDamageTable[player->routine_1](player, pKan, pSeq);
    }

    // 0x004DC850
    static void pl_mv_die(PlayerEntity* player, Emr* pKan, Edd* pSeq)
    {
        interop::call<void, PlayerEntity*, Emr*, Edd*>(0x004DC850, player, pKan, pSeq);
    }

    // 0x004F6080
    static void pl_mv_cutscene(PlayerEntity* player, Emr* pKan, Edd* pSeq)
    {
        interop::call<void, PlayerEntity*, Emr*, Edd*>(0x004F6080, player, pKan, pSeq);
    }

    static void pl_mv_em_damage_internal(PlayerEntity* player, MoveFunc* table)
    {
        set_flag(FlagGroup::Status, FG_STATUS_25, true);
        player->be_flg &= ~0x04;

        auto enemy = reinterpret_cast<EnemyEntity*>(player->pEnemy_ptr);
        auto cb = table[enemy->id];
        cb(player, player->pSub1_kan_t_ptr, player->pSub1_seq_t_ptr);
    }

    // 0x004DC930
    static void pl_mv_em_damage(PlayerEntity* player, Emr* pKan, Edd* pSeq)
    {
        pl_mv_em_damage_internal(player, reinterpret_cast<MoveFunc*>(gGameTable.em_damage_table_16 - 16));
    }

    // 0x004DC980
    static void pl_mv_em_die(PlayerEntity* player, Emr* pKan, Edd* pSeq)
    {
        pl_mv_em_damage_internal(player, reinterpret_cast<MoveFunc*>(gGameTable.em_die_table));
    }

    // 0x004DC9D0
    static void pl_mv_dead(PlayerEntity* player, Emr* pKan, Edd* pSeq)
    {
        interop::call<void, PlayerEntity*, Emr*, Edd*>(0x004DC9D0, player, pKan, pSeq);
    }

    // 0x004D97B0
    void player_move(PlayerEntity* player)
    {
        if (gGameTable.fg_stop < 0)
        {
            return;
        }
        gGameTable.word_989EEE &= 0xE0;
        set_flag(FlagGroup::Status, FG_STATUS_24, false);
        if (player->damage_cnt & 0x7F)
        {
            player->damage_cnt--;
        }

        if (player->id == PLD_TOFU)
        {
            auto v3 = static_cast<uint32_t>((player->life << 15) / (player->max_life) >> 8);
            auto partsW = player->pSin_parts_ptr;
            partsW->poly_rgb = ((v3 | (v3 << 8)) << 8) + 128;
            if (player->life < 0)
            {
                partsW->poly_rgb = 128;
            }
            if (gPoisonStatus)
            {
                if ((player->move_cnt & 1) == 0)
                {
                    gGameTable.dword_689BDC++;
                }
                gGameTable.dword_689BDC = (gGameTable.dword_689BDC & 0xFFFFFF) | 64;
            }
            else
            {
                if ((gGameTable.dword_689BDC & 0xFF) && (player->move_cnt & 1) == 0)
                {
                    gGameTable.dword_689BDC--;
                }
            }
            partsW->poly_rgb += gGameTable.dword_689BDC << 16;
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

        auto moveType = player->routine_0;
        auto pKan = player->pKan_t_ptr;
        auto pSeq = player->pSeq_t_ptr;
        switch (moveType)
        {
        case MOVE_TYPE_INIT: pl_init(player); break;
        case MOVE_TYPE_MOVE: pl_move(player, pKan, pSeq); break;
        case MOVE_TYPE_DAMAGE: pl_mv_damage(player, pKan, pSeq); break;
        case MOVE_TYPE_DIE: pl_mv_die(player, pKan, pSeq); break;
        case MOVE_TYPE_CUTSCENE: pl_mv_cutscene(player, pKan, pSeq); break;
        case MOVE_TYPE_EM_DAMAGE: pl_mv_em_damage(player, pKan, pSeq); break;
        case MOVE_TYPE_EM_DIE: pl_mv_em_die(player, pKan, pSeq); break;
        case MOVE_TYPE_DEAD: pl_mv_dead(player, pKan, pSeq); break;
        }

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

    int get_floor_sound(PlayerEntity* player)
    {
        return (*player->pNow_seq >> 13) & 1;
    }

    // 0x004EDF40
    static void snd_se_walk(int a0, int floor_sound, PlayerEntity* player)
    {
        using sig = void (*)(int, int, PlayerEntity*);
        auto p = (sig)0x004EDF40;
        return p(a0, floor_sound, player);
    }

    // 0x004B8470
    static int esp_call(int a0, int a1, Mat16 matrix, Vec16p vec)
    {
        using sig = int (*)(int, int, Mat16, Vec16p);
        auto p = (sig)0x004B8470;
        return p(a0, a1, matrix, vec);
    }

    // 0x004DAE70
    static void pl_mv_rotate(PlayerEntity* player, Emr* pKanPtr, Edd* pSeqPtr)
    {
        Vec16p pVec{ 0, 3, 6 };
        if (player->routine_2)
        {
            if (player->routine_2 != 1)
            {
                return;
            }
        }
        else
        {
            player->routine_2 = 1;
            player->spd.x = 0;
            player->spd.z = 0;
            player->move_no = (pVec.x + player->d_life_u) + 458752;
            set_flag(FlagGroup::Status, FG_STATUS_26, false);
            set_flag(FlagGroup::Status, FG_STATUS_CUTSCENE, false);
            set_flag(FlagGroup::Status, FG_STATUS_28, false);
            set_flag(FlagGroup::Status, FG_STATUS_29, false);
            set_flag(FlagGroup::Status, FG_STATUS_30, false);
            set_flag(FlagGroup::Status, FG_STATUS_31, false);
        }

        joint_move(player, pKanPtr, pSeqPtr, 512);
        if (*player->pNow_seq & now_seq_0x4000)
        {
            snd_se_walk(0, 4 + (get_floor_sound(player) * 3), player);
            gGameTable.word_989EEE |= 2;
        }

        if (player->water && (player->move_cnt & 1) != 0)
        {
            pVec = Vec16p{ 0, 300, 0 };
            auto sinPartsAddr = player->pSin_parts_ptr + 1892;
            auto sinParts = reinterpret_cast<uint8_t*>(&sinPartsAddr);

            if (player->water < static_cast<int32_t>(sinParts[24]) + 300)
            {
                auto matrix = *reinterpret_cast<Mat16*>(sinParts[72]);
                esp_call((4 * rnd() + 1548) | 0x1A000000, player->cdir.y, matrix, pVec);
            }

            sinParts = reinterpret_cast<uint8_t*>(player->pSin_parts_ptr);
            if (player->water < static_cast<int32_t>(sinParts[626]) + 300)
            {
                auto matrix = *reinterpret_cast<Mat16*>(sinParts[672]);
                esp_call((4 * rnd() + 1548) | 0x1A000000, player->cdir.y, matrix, pVec);
            }
        }
    }

    // 0x004DAFF0
    void pl_mv_pick_up_item(PlayerEntity* player, Emr* pKanPtr, Edd* pSeqPtr)
    {
        int lateFlag = 0;
        switch (player->routine_2)
        {
        case PICK_UP_ITEM_INTERACT_STATE_FLOOR:
            player->move_no = 06;
            player->move_cnt = 0;
            player->hokan_flg = 7;
            player->mplay_flg = 0;
            player->routine_2 = 1;
            player->spd.x = 0;
            set_flag(FlagGroup::Status, FG_STATUS_24, true);
            set_flag(FlagGroup::Status, FG_STATUS_25, true);
            goto LABEL_3;
        case PICK_UP_ITEM_INTERACT_STATE_REACHING:
        LABEL_3:
            lateFlag = 512;
            goto LABEL_7;
        case PICK_UP_ITEM_INTERACT_STATE_REACHED:
            gGameTable.hud_mode = 2;
            set_flag(FlagGroup::Status, FG_STATUS_SCREEN, true);
            gGameTable.byte_991F80 = 1;
            gGameTable.dword_991FC4 = gGameTable.fg_stop;
            player->routine_2 = 3;
            break;
        case PICK_UP_ITEM_INTERACT_STATE_4:
            player->move_no = 06;
            player->move_cnt = 0;
            player->hokan_flg = 7;
            player->mplay_flg = 0;
            player->routine_2 = 5;
            goto LABEL_6;
        case PICK_UP_ITEM_INTERACT_STATE_5:
        LABEL_6:
            lateFlag = 66048;
        LABEL_7:
            player->routine_2 += joint_move(player, pKanPtr, pSeqPtr, lateFlag);
            break;
        case PICK_UP_ITEM_INTERACT_STATE_6:
            player->routine_0 = 1;
            set_flag(FlagGroup::Status, FG_STATUS_24, true);
            set_flag(FlagGroup::Status, FG_STATUS_25, true);
            break;
        }
    }

    // 0x004DA6C0
    static void pl_br_backward(PlayerEntity* player, uint32_t key, uint32_t key_trg)
    {
        static uint8_t yAxisRotationSpeed[] = { 0x28, 0x10, 0x10 };
        if (key & input::KEY_TYPE_BACKWARD)
        {
            if (key & input::KEY_TYPE_LEFT)
            {
                player->cdir.y += yAxisRotationSpeed[player->d_life_u];
            }
            if (key & input::KEY_TYPE_RIGHT)
            {
                player->cdir.y -= yAxisRotationSpeed[player->d_life_u];
            }
            if (key_trg & input::KEY_TYPE_RUN_AND_CANCEL)
            {
                set_routine(Routine::QUICKTURN);
                return;
            }
            if ((key_trg & input::KEY_TYPE_128) != 0)
            {
                set_flag(FlagGroup::Status, FG_STATUS_INTERACT, true);
            }
            if (key & input::KEY_TYPE_AIM && player->type & 0xFFF)
            {
                set_routine(Routine::AIM);
            }
        }
        else
        {
            set_routine(Routine::IDLE);

            if (key & input::KEY_TYPE_ROTATE)
            {
                set_routine(Routine::ROTATE);
            }
            if (key & input::KEY_TYPE_FORWARD)
            {
                set_routine(Routine::FORWARD);
            }
        }
    }

    // no input code is required for quickturn
    void pl_br_quickturn(PlayerEntity* player, uint32_t key, uint32_t key_trg) {}

    void pl_mv_quickturn(PlayerEntity* player, Emr* pKanPtr, Edd* pSeqPtr)
    {
        switch (player->routine_2)
        {
        case 0:
            player->routine_2 = 1;
            player->timer0 = 0;

            // set base walk animation
            player->move_no = 0;
            player->move_cnt = 0;
            player->hokan_flg = 0;
            player->mplay_flg = 0x07;
            [[fallthrough]];

        case 1:
            if (player->timer0++ < 8)
            {
                player->cdir.y += 2048 / 8;
            }
            else
            {
                set_routine(Routine::IDLE);
            }
            break;
        }

        if (*player->pNow_seq & 0x4000)
        {
            snd_se_walk(0, 4 + (get_floor_sound(player) * 3), player);
            gGameTable.word_989EEE |= 2;
        }
        joint_move(player, player->pKan_t_ptr, player->pSeq_t_ptr, 512);
    }

    // 0x004D9FA0
    void pl_br_forward(PlayerEntity* player, uint32_t key, uint32_t key_trg)
    {
        static uint8_t yAxisRotationSpeed[] = { 0x28, 0x20, 0x16 };
        if ((key & input::KEY_TYPE_FORWARD) == 0)
        {
            set_routine(Routine::IDLE);

            if ((key & input::KEY_TYPE_ROTATE) != 0)
            {
                set_routine(Routine::ROTATE);
            }

            if ((key & input::KEY_TYPE_BACKWARD) != 0)
            {
                set_routine(Routine::BACKWARD);
            }

            return;
        }
        if (key & input::KEY_TYPE_LEFT)
        {
            player->cdir.y += yAxisRotationSpeed[player->d_life_u];
        }
        if (key & input::KEY_TYPE_RIGHT)
        {
            player->cdir.y -= yAxisRotationSpeed[player->d_life_u];
        }
        if (key & input::KEY_TYPE_RUN_AND_CANCEL)
        {
            set_routine(Routine::RUN_FORWARD);
        }
        if (check_flag(FlagGroup::Status, FG_STATUS_PUSH_OBJECT))
        {
            set_routine(Routine::PUSH_OBJECT);
        }
        if ((key & input::KEY_TYPE_128) == 0 && (key_trg & input::KEY_TYPE_128) == 0)
        {
            goto LABEL_31;
        }
        if (player->Sca_info & 0x100000)
        {
            sca_hit_stairs(player, 450, gGameTable.dword_695E7C);
            return;
        }
        if (oma_pl_updown_ck(player->id + 4) == 0)
        {
            if (key_trg & input::KEY_TYPE_128)
            {
                set_flag(FlagGroup::Status, FG_STATUS_INTERACT, true);
            }
        LABEL_31:
            if (key & input::KEY_TYPE_AIM && player->type & 0xFFF)
            {
                set_routine(Routine::AIM);
            }
        }
    }

    // 0x004D9D60
    void pl_br_idle(PlayerEntity* player, uint32_t key, uint32_t key_trg)
    {
        if (key & input::KEY_TYPE_ROTATE)
        {
            set_routine(Routine::ROTATE);
        }
        if (key & input::KEY_TYPE_BACKWARD)
        {
            set_routine(Routine::BACKWARD);
        }
        if (key & input::KEY_TYPE_FORWARD)
        {
            set_routine(Routine::FORWARD);
        }
        if (((key & input::KEY_TYPE_128) == 0) && ((key_trg & input::KEY_TYPE_128) == 0))
        {
            goto LABEL_25;
        }
        if (player->Sca_info & 0x100000)
        {
            sca_hit_stairs(player, 450, gGameTable.dword_695E7C);
        }
        if (oma_pl_updown_ck(player->id + 4) == 0)
        {
            if (key_trg & input::KEY_TYPE_128)
            {
                set_flag(FlagGroup::Status, FG_STATUS_INTERACT, true);
            }

        LABEL_25:
            if (key & input::KEY_TYPE_AIM && player->type & 0xFFF)
            {
                set_routine(Routine::AIM);
            }
        }
    }

    // 0x004DAD30
    void pl_br_rotate(PlayerEntity* player, uint32_t key, uint32_t key_trg)
    {
        static uint8_t yAxisRotationSpeed[] = { 0x50, 0x30, 0x20 };
        int result = 0;
        if ((key & input::KEY_TYPE_ROTATE) == 0)
        {
            set_routine(Routine::IDLE);
        }
        if (key & input::KEY_TYPE_BACKWARD)
        {
            set_routine(Routine::BACKWARD);
        }
        if (key & input::KEY_TYPE_FORWARD)
        {
            set_routine(Routine::FORWARD);
        }
        if (key & input::KEY_TYPE_LEFT)
        {
            player->cdir.y += yAxisRotationSpeed[player->d_life_u];
        }
        if (key & input::KEY_TYPE_RIGHT)
        {
            player->cdir.y -= yAxisRotationSpeed[player->d_life_u];
        }
        if (key & input::KEY_TYPE_128 || key_trg & input::KEY_TYPE_128)
        {
            if (player->Sca_info & 0x100000)
            {
                sca_hit_stairs(player, 450, gGameTable.dword_695E7C);
                return;
            }
            if (oma_pl_updown_ck(player->id + 4))
            {
                return;
            }
            if (key_trg & input::KEY_TYPE_128)
            {
                set_flag(FlagGroup::Status, FG_STATUS_INTERACT, true);
            }
        }
        if (key & input::KEY_TYPE_AIM && player->type & 0xFFF)
        {
            set_routine(Routine::AIM);
        }
    }

    // 0x004DB930
    void pl_br_step_down(PlayerEntity* player, uint32_t key, uint32_t key_trg)
    {
        if (player->routine_3 <= 4)
        {
            // Play step down animation
            return;
        }
        // End step down animation
        if (key & input::KEY_TYPE_FORWARD)
        {
            set_routine(Routine::FORWARD);
            if (key & input::KEY_TYPE_RUN_AND_CANCEL)
            {
                set_routine(Routine::RUN_FORWARD);
            }
        }
        if (key & input::KEY_TYPE_BACKWARD)
        {
            set_routine(Routine::BACKWARD);
        }
        if (key & input::KEY_TYPE_ROTATE)
        {
            set_routine(Routine::ROTATE);
        }
        if (key_trg & input::KEY_TYPE_128)
        {
            set_flag(FlagGroup::Status, FG_STATUS_INTERACT, true);
            if (player->Sca_info & 0x100000)
            {
                sca_hit_stairs(player, 450, gGameTable.dword_695E7C);
            }
        }
        if ((key & input::KEY_TYPE_AIM) && (player->type & 0xFFF))
        {
            set_routine(Routine::AIM);
        }
    }

    // 0x004DB9D0
    void pl_mv_step_down(PlayerEntity* player, Emr* emr, Edd* edd)
    {
        switch (player->routine_3)
        {
        case 0:
        {
            player->routine_3 = 1;
            player->be_flg |= 4;
            player->move_no = 7;
            player->move_cnt = 0;
            player->hokan_flg = 7;
            player->mplay_flg = 0;
            [[fallthrough]];
        }
        case 1:
        {
            player->routine_3 += joint_move(player, (Emr*)player->field_190, (Edd*)player->field_194, 512);
            if (player->id == PLD_SHERRY)
            {
                if (player->move_cnt == 26)
                {
                    player->routine_3 = 2;
                }
            }
            else if (player->move_cnt == 14)
            {
                player->routine_3 = 2;
            }
            return;
        }
        case 2:
        {
            player->damage_cnt |= 0x80;
            player->routine_3 = 3;
            player->move_no = 8;
            player->move_cnt = 0;
            player->hokan_flg = 0;
            player->mplay_flg = 0;
            player->spd.x = 1000;
            player->m.pos.y += 550;
            player->timer0 = 5;
            if (player->id == PLD_SHERRY)
            {
                player->spd.x = 600;
                player->m.pos.y -= 400;
                player->timer0 = 3;
            }
            player->spd.z = 0;
            add_speed_xz(player, 0);
            player->nFloor--;
            player->sca_old_x = player->m.pos.x;
            player->sca_old_z = player->m.pos.z;
            player->timer0 = 3;
            [[fallthrough]];
        }
        case 3:
        {
            player->m.pos.y += 30 * player->timer0++;
            player->ground = sca_ck_hit(&player->m.pos, player->atd[0].at_w, 0x8000, 0);
            player->nFloor = compute_nfloor(player->m.pos.y);

            if ((uint32_t)&gGameTable.obj_ptr > (uint32_t)&gGameTable.pOm)
            {
                auto objIdx = ((uint32_t)&gGameTable.obj_ptr - (uint32_t)&gGameTable.pOm) / sizeof(ObjectEntity);
                for (uint32_t i = 0; i < objIdx; i++)
                {
                    auto& obj = gGameTable.pOm[i];
                    if (obj.be_flg & 1 && !(obj.be_flg & 2))
                    {
                        oma_set_ofs(&obj);
                        omd_in_check(&player->m.pos, &obj, player->atd[0].at_w, 0);

                        if (player->ground == obj.atd[0].pos.y - obj.atd[0].at_h)
                        {
                            player->pOn_om = (uint32_t)&obj;
                            break;
                        }
                    }
                }
            }

            if (player->pOn_om)
            {
                oma_ob_pull2(&gGameTable.pl, player->pOn_om, static_cast<uint16_t>(player->sc_id << 8), 0x30);
            }
            joint_move(player, (Emr*)player->field_190, (Edd*)player->field_194, 512);
            if (player->water < player->ground && !player->timer3)
            {
                player->timer3 = -123;
            }
            if (player->timer3 & 0x7F)
            {
                player->timer3--;
                pl_water(player);
            }
            if (player->m.pos.y > player->ground - 300)
            {
                player->routine_3 = 4;
            }
            return;
        }
        case 4:
        {
            player->routine_3 = 5;
            player->move_no = 9;
            if ((player->routine_2 & 0xC) > 4)
            {
                player->move_no = 7;
            }
            player->move_cnt = 4;
            player->hokan_flg = 7;
            player->m.pos.y = player->ground;
            player->damage_cnt &= 0x7F;
            player->be_flg = (player->be_flg << 8) | (player->be_flg & 0xFB);
            snd_se_walk(1, 4, player);
            gGameTable.word_989EEE |= 4;
            [[fallthrough]];
        }
        case 5:
        {
            if (player->move_cnt == 6)
            {
                snd_se_walk(1, 7, player);
            }
            player->nFloor = compute_nfloor(player->m.pos.y);
            if (player->pOn_om)
            {
                oma_ob_pull2(&gGameTable.pl, player->pOn_om, static_cast<uint16_t>(player->sc_id << 8), 0x30);
            }
            player->routine_3 += joint_move(player, (Emr*)player->field_190, (Edd*)player->field_194, 512);
            return;
        }
        case 6:
        {
            set_flag(FlagGroup::Status, FG_STATUS_25, false);
            if (player->pOn_om)
            {
                oma_ob_pull2(&gGameTable.pl, player->pOn_om, static_cast<uint16_t>(player->sc_id << 8), 0x3E8);
            }
            player->routine_1 = 0;
            player->routine_2 = 0;
            player->routine_3 = 0;
            return;
        }
        }
    }

    // 0x004DBD90
    void pl_br_push_object(PlayerEntity* player, uint32_t key, uint32_t key_trg)
    {
        if ((!(key & 0x10) || !check_flag(FlagGroup::Status, FG_STATUS_PUSH_OBJECT)) && player->routine_2 == 5)
        {
            // End push object animation
            player->routine_2 = 6;
            player->move_no = 7;
            player->move_cnt = 0;
            player->hokan_flg = 7;
            snd_se_on(0x2000001, player->m.pos);
        }
    }

    // 0x004DBDE0
    void pl_mv_push_object(PlayerEntity* player, Emr* emr, Edd* edd)
    {
        static uint8_t pushSpeed[] = { 0, 3, 6 };
        switch (player->routine_2)
        {
        case PUSH_OBJ_STATE_0:
        {
            player->routine_2 = 1;
            player->move_no = pushSpeed[player->d_life_u];
            player->move_cnt = 0;
            player->hokan_flg = 7;
            player->mplay_flg = 0;
            [[fallthrough]];
        }
        case PUSH_OBJ_STATE_PLACE_IN_FRONT:
        {
            auto nowSeq = *player->pNow_seq;
            if (nowSeq & 0x4000)
            {
                snd_se_walk(1, 3 * ((nowSeq >> 13) & 1) + 4, player);
                gGameTable.word_989EEE |= 4;
            }
            auto joinMoveRes = static_cast<int32_t>(joint_move(player, player->pSub0_kan_t_ptr, player->pSub0_seq_t_ptr, 512));
            joinMoveRes = (joinMoveRes << 16) | player->cdir.y;
            if (joinMoveRes & 0x200)
            {
                player->cdir.y = joinMoveRes + ((joinMoveRes >> 2) & 0xFF);
            }
            else
            {
                player->cdir.y = joinMoveRes - ((joinMoveRes >> 2) & 0xFF);
            }
            if (!(player->cdir.y & 0x3E0))
            {
                player->routine_2 = 2;
            }
            return;
        }
        case PUSH_OBJ_STATE_2:
        {
            player->routine_2 = 3;
            player->move_no = 7;
            player->move_cnt = 0;
            player->hokan_flg = 7;
            player->mplay_flg = 0;
            [[fallthrough]];
        }
        case PUSH_OBJ_STATE_3:
        {
            player->routine_2 += joint_move(player, emr, edd, 512);
            break;
        }
        case PUSH_OBJ_STATE_START_PUSHING:
        {
            player->routine_2 = 5;
            player->move_no = 8;
            player->move_cnt = 0;
            player->hokan_flg = 7;
            player->mplay_flg = 0;
            [[fallthrough]];
        }
        case PUSH_OBJ_STATE_PUSHING:
        {
            if (player->move_no == 1)
            {
                snd_se_on(0x2040001, player->m.pos);
            }
            joint_move(player, emr, edd, 512);
            if (player->id == PLD_SHERRY)
            {
                player->spd.x = 5;
            }
            if (*player->pNow_seq & 0x2000)
            {
                foot_set_pl(player, 1, player->id == PLD_SHERRY);
            }
            player->spd.z = 0;
            add_speed_xz(player, 0);
            break;
        }
        case PUSH_OBJ_STATE_STOP_PUSHING:
        {
            player->routine_2 += joint_move(player, emr, edd, 0x10200);
            break;
        }
        case PUSH_OBJ_STATE_END:
        {
            set_routine(Routine::IDLE);
            break;
        }
        }
    }

    // 0x004DA2E0
    void pl_br_run_forward(PlayerEntity* player, uint32_t key, uint32_t key_trg)
    {
        static uint8_t yAxisRotationSpeed[] = { 0x38, 0x30, 0x18 };
        int result = player->routine_2;
        if (key & input::KEY_TYPE_FORWARD && key & input::KEY_TYPE_RUN_AND_CANCEL)
        {
            if (player->spd.x > 50)
            {
                if (key & input::KEY_TYPE_LEFT)
                {
                    player->cdir.y += yAxisRotationSpeed[player->d_life_u];
                }
                if (key & input::KEY_TYPE_RIGHT)
                {
                    player->cdir.y -= yAxisRotationSpeed[player->d_life_u];
                }
            }
            if (key & input::KEY_TYPE_128 || key_trg & input::KEY_TYPE_128)
            {
                if (player->Sca_info & 0x100000)
                {
                    sca_hit_stairs(player, 450, gGameTable.dword_695E7C);
                    return;
                }
                if (oma_pl_updown_ck(player->id + 4))
                {
                    return;
                }
                if (key_trg & input::KEY_TYPE_128)
                {
                    set_flag(FlagGroup::Status, FG_STATUS_INTERACT, true);
                }
            }
            if (key & input::KEY_TYPE_AIM && player->type & 0xFFF)
            {
                set_routine(Routine::AIM);
            }
        }
        else
        {
            set_routine(Routine::IDLE);

            if (key & input::KEY_TYPE_ROTATE)
            {
                set_routine(Routine::ROTATE);
            }
            if (key & input::KEY_TYPE_BACKWARD)
            {
                set_routine(Routine::BACKWARD);
            }
            if (key & input::KEY_TYPE_FORWARD)
            {
                set_routine(Routine::FORWARD);
            }
        }
    }

    // 0x004DB670
    static void pl_mv_climb_on(PlayerEntity* player, Emr* emr, Edd* edd)
    {
        switch (player->routine_3)
        {
        case CLIMB_ON_STATE_0:
        {
            player->routine_3 = 1;
            player->move_no = 0;
            player->move_cnt = 0;
            player->hokan_flg = 7;
            player->mplay_flg = 0;
            player->spd.x = 50;
            set_flag(FlagGroup::Status, FG_STATUS_25, true);
            player->be_flg |= 4;
            [[fallthrough]];
        }
        case CLIMB_ON_STATE_PLACE_IN_FRONT:
        {
            auto joinMoveRes = static_cast<int32_t>(joint_move(player, player->pSub0_kan_t_ptr, player->pSub0_seq_t_ptr, 512));
            joinMoveRes = (joinMoveRes << 16) | player->cdir.y;
            if (joinMoveRes & 0x200)
            {
                player->cdir.y = joinMoveRes + ((joinMoveRes >> 2) & 0xFF);
            }
            else
            {
                player->cdir.y = joinMoveRes - ((joinMoveRes >> 2) & 0xFF);
            }
            if ((player->cdir.y & 0x3E0) == 0)
            {
                player->routine_3 = 2;
                player->cdir.y &= ~0xff;
                if (player->routine_2 & 2)
                {
                    player->routine_1 = 9;
                    player->routine_3 = 0;
                }
            }
            break;
        }
        case CLIMB_ON_STATE_2:
        {
            player->routine_3 = 3;
            player->move_no = 6;
            player->hokan_flg = 3;
            [[fallthrough]];
        }
        case CLIMB_ON_STATE_CLIMBING:
        {
            if (player->pOn_om)
            {
                oma_ob_pull2(player, player->pOn_om, static_cast<uint16_t>(player->sc_id << 8), 4);
            }
            if (player->move_cnt == 17)
            {
                player->damage_cnt |= 0x80;
                player->nFloor++;
                player->be_flg |= 8;
            }
            bool v13;
            if (player->id == PLD_SHERRY)
            {
                if (player->move_cnt == 57 && (rnd() & 3))
                {
                    player->move_no = 6;
                    player->move_cnt = 0x56;
                    player->hokan_flg = 5;
                    player->mplay_flg = 0;
                }
                if (player->move_cnt == 99)
                {
                    player->spd.x = 548;
                    add_speed_xz(player, 0);
                    player->ground -= 1800;
                    player->m.pos.y -= 1800;
                }
                v13 = player->move_cnt == 119;
            }
            else
            {
                if (player->move_cnt == 29)
                {
                    player->spd.x = 1000;
                    add_speed_xz(player, 0);
                    player->ground -= 1800;
                    player->m.pos.y -= 1800;
                }
                v13 = player->move_cnt == 49;
            }
            if (v13)
            {
                player->damage_cnt &= ~0x80u;
                player->be_flg &= 0xF3;
            }
            player->routine_3 += joint_move(player, (Emr*)player->field_190, (Edd*)player->field_194, 1024);
            if (player->water < player->ground)
            {
                auto moveCnt = player->move_cnt;
                if (moveCnt < 0xF && (moveCnt & 1))
                {
                    pl_water(player);
                }
            }
            auto nowSeq = *player->pNow_seq;
            if (nowSeq & 0x4000)
            {
                snd_se_walk(1, 3 * ((nowSeq >> 13) & 1) + 4, player);
                gGameTable.word_989EEE |= 4;
            }
            break;
        }
        case CLIMB_ON_STATE_END:
        {
            if (player->pOn_om)
            {
                oma_ob_pull2(player, player->pOn_om, static_cast<uint16_t>(player->sc_id << 8), 0x3E8);
            }
            player->routine_1 = 0;
            player->routine_2 = 0;
            player->routine_3 = 0;
            gGameTable.fg_status &= 0xBF;
            break;
        }
        }
    }

    // 0x004D4310
    static void enemy_ck(PlayerEntity* player, int a1)
    {
        interop::call<void, PlayerEntity*, int>(0x004D4310, player, a1);
    }

    // 0x004D5020
    static void pl_aim_move_w1_0(PlayerEntity* player, Emr* emr, Edd* edd, uint32_t key)
    {
        if (!player->routine_3)
        {
            player->status_flg &= 0x1FFF;
            player->status_flg |= 0x4000;
            player->routine_3 = 1;
            player->move_no = 9;
            player->move_cnt = 0;
            player->hokan_flg = 7;
            player->mplay_flg = 0;

            auto weapon = player->type & 0xFFF;
            if (weapon != ITEM_TYPE_KNIFE)
            {
                enemy_ck(player, 0);
            }
            if ((gGameTable.current_stage == 3 && gGameTable.current_room != 10)
                || (gGameTable.current_stage != 6 || gGameTable.current_room != 4))
            {
                enemy_ck(player, 0x1388);
            }
            else
            {
                enemy_ck(player, 0x7530);
            }
            player->timer3 = 0;
            player->neck_flg |= 0x12;
            player->spd.x = 0;
        }
        else if (player->routine_3 != 1)
        {
            if (player->routine_3 == 2)
            {
                player->routine_2 = 1;
                player->routine_3 = 0;
            }
            return;
        }

        if (player->water)
        {
            if (player->water < player->ground)
            {
                if (player->move_cnt < 6 && player->move_cnt & 1)
                {
                    pl_water(player);
                }
            }

            auto part14 = player->pSin_parts_ptr[14];
            auto part11 = player->pSin_parts_ptr[11];
            Vec16p vec{ 0, 300, 0 };

            if (player->water < part11.workm.pos.y + 300)
            {
                esp_call((4 * rnd() + 0x60C) | 0x1A000000, player->cdir.y, part11.workm, vec);
            }
            if (player->water < part14.workm.pos.y + 300)
            {
                esp_call((4 * rnd() + 0x60C) | 0x1A000000, player->cdir.y, part14.workm, vec);
            }
        }

        // Auto aim
        if (!(gGameTable.dword_98E9C4 & 1)
            && (!check_flag(FlagGroup::System, FG_SYSTEM_ARRANGE) || !check_flag(FlagGroup::System, FG_SYSTEM_20)))
        {
            if (player != player->pEnemy_ptr)
            {
                auto pos = player->pEnemy_ptr->atd[0].pos;
                auto weapon = player->type & 0xFFF;
                if (weapon == ITEM_TYPE_HANDGUN_COLT_SAA)
                {
                    goto00(player, pos.x, pos.z, 0x120);
                }
                else
                {
                    goto00(player, pos.x, pos.z, 0xD8);
                }
            }
        }

        if (gGameTable.key_trg & 0x20 && !player->timer3)
        {
            enemy_ck(player, 0xBB8);
            player->timer3 = 1;
        }

        if (key & KEY_TYPE_RIGHT)
        {
            player->cdir.y -= 32;
        }
        if (key & KEY_TYPE_LEFT)
        {
            player->cdir.y += 32;
        }
        player->routine_3 += joint_move(player, emr, edd, 512);

        auto weapon = player->type & 0xFFF;
        auto moveIndex = weapon * 3;
        if (weapon != ITEM_TYPE_SPARKSHOT && weapon != ITEM_TYPE_ROCKET_LAUNCHER)
        {
            if (player->move_cnt > (gGameTable.byte_53A305[moveIndex + 1] & 0xF7) && key & 0x20)
            {
                player->routine_2 = 1;
                player->routine_3 = 0;
                player->status_flg &= 0x1FFF;
                player->status_flg |= 0x2000;
            }
            else if (player->move_cnt > (gGameTable.byte_53A305[moveIndex + 2] & 0xF7) && key & 0x10)
            {
                player->routine_2 = 1;
                player->routine_3 = 0;
                player->status_flg &= 0x1FFF;
                player->status_flg |= 0x2000;
            }
        }
    }

    static MoveAimWeaponFunc plAimMoveW1Table[6] = {
        pl_aim_move_w1_0,
        (MoveAimWeaponFunc)0x004D5300,
        (MoveAimWeaponFunc)0x004D54F0,
        (MoveAimWeaponFunc)0x004D69F0,
        (MoveAimWeaponFunc)0x004D6B20,
        (MoveAimWeaponFunc)0x004D6C20,
    };

    // 0x004D5000
    static void pl_aim_move_w1(PlayerEntity* player, Emr* emr, Edd* edd, uint32_t key)
    {
        plAimMoveW1Table[player->routine_2](player, emr, edd, key);
    }

    static MoveAimWeaponFunc pl_aim_move_tbl[21] = {
        nullptr,                       // 0
        (MoveAimWeaponFunc)0x004D4B20, // Knife
        (MoveAimWeaponFunc)0x004D5000, // 2
        pl_aim_move_w1,                // 3
        (MoveAimWeaponFunc)0x004D8E80, // 4
        pl_aim_move_w1,                // 5
        pl_aim_move_w1,                // 6
        pl_aim_move_w1,                // 7
        pl_aim_move_w1,                // 8
        pl_aim_move_w1,                // 9
        pl_aim_move_w1,                // 10
        pl_aim_move_w1,                // 11
        pl_aim_move_w1,                // 12
        pl_aim_move_w1,                // 13
        (MoveAimWeaponFunc)0x004D8B20, // 14
        (MoveAimWeaponFunc)0x004D8480, // 15
        (MoveAimWeaponFunc)0x004D8480, // 16
        pl_aim_move_w1,                // 17
        (MoveAimWeaponFunc)0x004D9120, // 18
        pl_aim_move_w1,                // 19
        pl_aim_move_w1                 // 20
    };

    // 0x004D5810
    static void pl_mv_aim(PlayerEntity* player, Emr* emr, Edd* edd)
    {
        gGameTable.fg_status |= 0xC0;
        auto weapon = player->type & 0xFFF;

        pl_aim_move_tbl[weapon](player, player->pSub0_kan_t_ptr, player->pSub0_seq_t_ptr, gGameTable.g_key);
        if (player->spd.x == 0 && (player->routine_2 || !player->hokan_flg))
        {
            foot_set_pl(player, 0, gGameTable.byte_53A305[weapon * 3 + (player->id & 1)] >> 7);
        }
    }

    void init_move_tables()
    {
        // fill expanded tables with old code
        std::memcpy(br_tbl, gMoveBrTable, 12 * 4);
        std::memcpy(mv_tbl, gMoveMvTable, 12 * 4);
        std::memcpy(dmg_tbl, gMoveDamageTable, 6 * 4);
        // set br hooks
        br_tbl[0] = pl_br_idle;
        br_tbl[1] = pl_br_forward;
        br_tbl[2] = pl_br_run_forward;
        br_tbl[3] = pl_br_backward;
        br_tbl[4] = pl_br_rotate;
        br_tbl[9] = pl_br_step_down;
        br_tbl[10] = pl_br_push_object;
        br_tbl[12] = pl_br_quickturn;
        // set mv hooks
        mv_tbl[5] = pl_mv_aim;
        mv_tbl[6] = pl_mv_pick_up_item;
        mv_tbl[8] = pl_mv_climb_on;
        mv_tbl[9] = pl_mv_step_down;
        mv_tbl[10] = pl_mv_push_object;
        mv_tbl[12] = pl_mv_quickturn;
        // replace old table pointers
        interop::writeMemory(0x53A7FC, br_tbl, 12 * 4);
        interop::writeMemory(0x53A82C, mv_tbl, 12 * 4);
        interop::writeMemory(0x53A85C, dmg_tbl, 6 * 4);
        gMoveBrTable = br_tbl;
        gMoveMvTable = mv_tbl;
        gMoveDamageTable = dmg_tbl;
    }

    // 0x00502530
    int player_check_life()
    {
        if (gGameTable.poison_status)
        {
            return PLAYER_LIFE_STATUS_POISONED;
        }
        if (gGameTable.pl.life > gGameTable.pl.max_life >> 1)
        {
            return PLAYER_LIFE_STATUS_FINE;
        }
        if (gGameTable.pl.life <= 20)
        {
            return PLAYER_LIFE_STATUS_DANGER;
        }
        if (gGameTable.pl.life <= 40)
        {
            return PLAYER_LIFE_STATUS_CAUTION_2;
        }
        return PLAYER_LIFE_STATUS_CAUTION_1;
    }

    // 0x004D93A0
    void player_set(PlayerEntity* player)
    {
        interop::call<void, PlayerEntity*>(0x004D93A0, player);
    }

    void player_init_hooks()
    {
        interop::writeJmp(0x00502190, &partner_switch);
        interop::writeJmp(0x00502660, &inventory_find_item);
        interop::writeJmp(0x4FC3CE, itembox_prev_slot);
        interop::writeJmp(0x4D97B0, player_move);
        interop::writeJmp(0x4D9D20, pl_move);
        interop::writeJmp(0x4DC130, pl_mv_damage);

        init_move_tables();
    }

    bool is_aiming()
    {
        return (
            check_flag(FlagGroup::Status, FG_STATUS_GAMEPLAY) && check_flag(FlagGroup::Status, FG_STATUS_25)
            && check_flag(FlagGroup::Status, FG_STATUS_24) && !check_flag(FlagGroup::Status, FG_STATUS_SCREEN));
    }
}