#include "sce.h"
#include "audio.h"
#include "entity.h"
#include "hud.h"
#include "interop.hpp"
#include "item.h"
#include "openre.h"
#include "player.h"
#include "rdt.h"
#include "re2.h"
#include "relua.h"
#include "scd.h"

#include <cstring>

using namespace openre::audio;
using namespace openre::hud;
using namespace openre::lua;
using namespace openre::player;
using namespace openre::rdt;
using namespace openre::scd;

namespace openre::sce
{
    enum
    {
        ITEMBOX_INTERACT_STATE_INIT,
        ITEMBOX_INTERACT_STATE_OPENING,
        ITEMBOX_INTERACT_STATE_OPENED,
        ITEMBOX_INTERACT_STATE_CLOSING,
    };

    using SceImpl = int (*)(void*);

    static SceImpl* gScdImplTable = (SceImpl*)0x53B46C;

    constexpr uint8_t KEY_LOCKED = 255;
    constexpr uint8_t KEY_UNLOCK = 254;

    constexpr uint32_t MESSAGE_KIND_INK_RIBBON_REQUIRED_TO_SAVE = 0;
    constexpr uint32_t MESSAGE_KIND_WILL_YOU_USE_USE_INK_RIBBON = 1;
    constexpr uint32_t MESSAGE_KIND_YOU_USED_KEY_X = 5;
    constexpr uint32_t MESSAGE_KIND_YOU_UNLOCKED_IT = 10;
    constexpr uint32_t MESSAGE_KIND_LOCKED_FROM_OTHER_SIDE = 11;
    constexpr uint32_t MESSAGE_KIND_LEAVE_SHERRY_BEHIND = 8;
    constexpr uint32_t MESSAGE_KIND_DISCARD_USELESS_KEY = 9;

    PlayerEntity* GetPlayerEntity()
    {
        return gGameTable.player_work;
    }

    Entity* GetPartnerEntity()
    {
        return gGameTable.splayer_work;
    }

    Entity* GetEnemyEntity(int index)
    {
        return gGameTable.enemies[index];
    }

    ObjectEntity* GetObjectEntity(int index)
    {
        return &gGameTable.pOm[index];
    }

    DoorEntity* GetDoorEntity(int index)
    {
        return gGameTable.doors[index];
    }

    // 0x00503170
    int bitarray_get(uint32_t* bitArray, int index)
    {
        auto dwordIndex = index >> 5;
        auto bitIndex = index & 0x1F;
        auto result = bitArray[dwordIndex] & (0x80000000 >> bitIndex);
        return result;
    }

    void bitarray_set(uint32_t* bitArray, int index, bool value)
    {
        auto dwordIndex = index >> 5;
        auto bitIndex = index & 0x1F;
        auto mask = 0x80000000 >> bitIndex;
        if (value)
            bitArray[dwordIndex] |= mask;
        else
            bitArray[dwordIndex] &= ~mask;
    }

    // 0x00503120
    void bitarray_set(uint32_t* bitArray, int index)
    {
        bitarray_set(bitArray, index, true);
    }

    // 0x00503140
    void bitarray_clr(uint32_t* bitArray, int index)
    {
        bitarray_set(bitArray, index, false);
    }

    // 0x004E3F40
    static void sce_aot_init()
    {
        for (auto i = 0; i < 32; i++)
        {
            set_aot_entry(i, nullptr);
        }
        gGameTable.aot_count = 0;
    }

    // 0x004E3DA0
    void sce_work_clr()
    {
        gGameTable.scd_variables_00 = 0xFFFF;
        gGameTable.word_98EAE6 = 0xFFFF;
        gGameTable.last_used_item = 0xFFFF;
        gGameTable.word_98EAEA = -1;
        gGameTable.fg_tick = 0;
        gGameTable.fg_use = 0;
        gGameTable.dword_989EDC = 0;
        gGameTable.dword_989EE0 = 0;
        gGameTable.dword_989EE4 = 0;
        gGameTable.mizu_div_ctr = 0;
    }

    // 0x004E3DE0
    void sce_work_clr_at()
    {
        interop::call(0x004E3DE0);
    }

    // 0x004E3E50
    void sce_work_clr_set()
    {
        scd_init_tasks();
        sce_aot_init();
        set_flag(FlagGroup::Status, FG_STATUS_20, false);
        set_flag(FlagGroup::Status, FG_STATUS_21, false);
        gGameTable.cc_work.ctex_old = 0xFF;
        gGameTable.c_id = 0xFF;
        gGameTable.c_model_type = 0xFF;
        gGameTable.c_kind = 0xFF;
        gGameTable.byte_695E72 = 0xFF;
        gGameTable.fg_room = 0;
        gGameTable.fg_room_enemy = 0;
        gGameTable.word_989EEE = 0;
        gGameTable.word_98EB26 = 0xFFFF;
        gGameTable.word_98EB28 = 0xFFFF;
        std::memset(gGameTable.pri_be_flg, 0xFF, sizeof(gGameTable.pri_be_flg));
        gGameTable.mizu_div_max = 0;
        gGameTable.mizu_div_ctr = 0;
        gGameTable.rbj_reset_flg = 0;
        gGameTable.cc_work.ccol_no = 0;
        gGameTable.se_tmp0 = 0;
        gGameTable.byte_695E71 = 0;
        gGameTable.c_em = GetEnemyEntity(0);
        gGameTable.cd_vol_0 = 120;
        gGameTable.pl.be_flg &= ~0x0400;
        gGameTable.pl.type &= 0x0FFF;
    }

    // 0x004E3AE0
    static void sce_se_set()
    {
        interop::call(0x004E3AE0);
    }

    // 0x004E4180
    static void sce_col_chg_init()
    {
        interop::call(0x004E4180);
    }

    // 0x004E41C0
    static void sce_mirror_init()
    {
        interop::call(0x004E41C0);
    }

    // 0x004E4250
    static void sce_kirakira_set()
    {
        interop::call(0x004E4250);
    }

    // 0x004E4040
    static int sce_get_map_flg(int stage, int room)
    {
        switch (stage)
        {
        case 0: return 0 + room;
        case 1: return 30 + room;
        case 2: return 58 + room;
        case 3: return 72 + room;
        case 4: return 89 + room;
        case 5: return 99 + room;
        case 6: return 123 + room;
        default: return stage + room;
        }
    }

    // 0x004E3BD0
    void sce_rnd_set()
    {
        if (check_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE))
        {
            gGameTable.rng = rnd();
        }
        else
        {
            auto rb0 = gGameTable.random_base;
            auto rb1 = rb0 * 2;
            auto rbN = (((rb1 >> 16) + rb0) ^ rb1) & 0xFFFF;
            gGameTable.random_base = rbN ^ rb1;
            gGameTable.rng = gGameTable.random_base & 0xFFFF;
        }
    }

    // 0x004E3C20
    void sce_model_init()
    {
        interop::call(0x004E3C20);
    }

    // 0x004E40D0
    void sce_scheduler_set()
    {
        if (check_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE))
        {
            auto mapFlag = sce_get_map_flg(gGameTable.current_stage, gGameTable.current_room);
            auto state = check_flag(FlagGroup::Map, mapFlag);
            set_flag(FlagGroup::Common, 255, !state);
        }
        sce_rnd_set();
        sce_work_clr();
        sce_work_clr_set();

        // Begin init scd procedure
        gGameTable.sce_type = SCE_TYPE_MAIN;
        gGameTable.scd = rdt_get_offset<uint8_t>(RdtOffsetKind::SCD_INIT);
        scd_event_exec(TASK_ID_RESERVED_0, EVT_MAIN);

        // Begin main scd procedure
        gGameTable.scd = rdt_get_offset<uint8_t>(RdtOffsetKind::SCD_MAIN);
        scd_event_exec(TASK_ID_RESERVED_1, EVT_MAIN);

        sce_scheduler_main();
        sce_se_set();
        sce_col_chg_init();
        sce_mirror_init();
        sce_kirakira_set();
    }

    // 0x004E42D0
    static void sce_scheduler()
    {
        if (!check_flag(FlagGroup::Stop, FG_STOP_06))
        {
            sce_rnd_set();
            gGameTable.sce_type = SCE_TYPE_MAIN;
            gGameTable.scd = rdt_get_offset<uint8_t>(RdtOffsetKind::SCD_MAIN);
            scd_event_exec(TASK_ID_RESERVED_0, EVT_FRAME);
            sce_scheduler_main();
            relua_call_hooks(HookKind::Tick);
        }
    }

    void set_aot_entry(AotId id, SceAotBase* aot)
    {
        auto& entry = gGameTable.aot_table[id];
        if (entry == nullptr && aot != nullptr)
        {
            gGameTable.aot_count++;
        }
        else if (entry != nullptr && aot == nullptr)
        {
            gGameTable.aot_count--;
        }
        entry = aot;
    }

    // 0x004E95F0
    static void use_key()
    {
        auto& inventory = gGameTable.inventory;

        if (gGameTable.byte_98E541)
        {
            gGameTable.dword_689CA0 = gGameTable.byte_98E541 - 1;
            if (gGameTable.fg_message >= 0)
            {
                // Key still working in other doors
                if (--inventory[gGameTable.dword_689CA0].Quantity)
                {
                    gGameTable.dword_98E790 = 0;
                    gGameTable.byte_98E541 = 0;
                }
                else // Useless key, discard it
                {
                    show_message(0, 0x100, MESSAGE_KIND_DISCARD_USELESS_KEY, 0xFF000000);
                    gGameTable.byte_98E541 = 0;
                    gGameTable.fg_stop |= 0xFF000000;
                }
            }
        }
        else if (gGameTable.fg_message >= 0)
        {
            if (gGameTable.fg_message & 1)
            {
                // Keep useless key in the inventory
                inventory[gGameTable.dword_689CA0].Quantity = 1;
            }
            else
            {
                // Discard key
                inventory[gGameTable.dword_689CA0].Type = ITEM_TYPE_NONE;
                inventory[gGameTable.dword_689CA0].Part = 0;
                sort_inventory();
            }
            gGameTable.dword_98E790 = 0;
        }
    }

    // 0x004E9930
    static void sce_save_callback()
    {
        constexpr uint8_t STATE_QUESTION = 0;
        constexpr uint8_t STATE_ANSWER = 1;

        if (gGameTable.question_state == STATE_QUESTION)
        {
            auto inventoryIndex = inventory_find_item(ITEM_TYPE_INK_RIBBON);
            if (inventoryIndex < 0)
            {
                gGameTable.fg_stop = gGameTable.dword_991FC4;
                show_message(0, 0x100, MESSAGE_KIND_INK_RIBBON_REQUIRED_TO_SAVE, 0xFF000000);
                gGameTable.action_fn = nullptr;
                gGameTable.question_state = STATE_QUESTION;
            }
            else
            {
                show_message(0, 0x100, MESSAGE_KIND_WILL_YOU_USE_USE_INK_RIBBON, 0xFF000000);
                gGameTable.question_state = STATE_ANSWER;
            }
        }
        else if (gGameTable.question_state == STATE_ANSWER && !(check_flag(FlagGroup::Message, FG_MESSAGE_WAITING_ANSWER)))
        {
            gGameTable.action_fn = nullptr;
            gGameTable.question_state = STATE_QUESTION;
            if (check_flag(FlagGroup::Message, FG_MESSAGE_ANSWER_NO))
            {
                gGameTable.fg_stop = gGameTable.dword_991FC4;
            }
            else
            {
                gGameTable.byte_991F80 = 1;
                gGameTable.fg_system |= 0x40000;
            }
        }
    }

    // 0x004E9A20
    static void sce_itembox_callback()
    {
        switch (gGameTable.question_state)
        {
        case ITEMBOX_INTERACT_STATE_INIT:
            gGameTable.dword_991FC4 = gGameTable.fg_stop;
            gGameTable.fg_stop |= 0x7F000000;
            gGameTable.itembox_speed = 1;
            gGameTable.itembox_acceleration = 3;
            snd_se_on(0x2150000);
            gGameTable.question_state = ITEMBOX_INTERACT_STATE_OPENING;
            gGameTable.itembox_obj = GetObjectEntity(gGameTable.itembox_obj_index);
            [[fallthrough]];
        case ITEMBOX_INTERACT_STATE_OPENING:
            gGameTable.itembox_obj->cdir.z -= gGameTable.itembox_speed;
            gGameTable.itembox_speed += gGameTable.itembox_acceleration;
            if (gGameTable.itembox_obj->cdir.z < -399)
            {
                gGameTable.question_state = ITEMBOX_INTERACT_STATE_OPENED;
                gGameTable.itembox_acceleration = -gGameTable.itembox_speed;
            }
            break;
        case ITEMBOX_INTERACT_STATE_OPENED:
            gGameTable.itembox_obj->cdir.z -= gGameTable.itembox_speed;
            if (gGameTable.itembox_speed + gGameTable.itembox_acceleration > 0)
            {
                gGameTable.itembox_speed += gGameTable.itembox_acceleration;
                break;
            }
            gGameTable.hud_mode = HUD_MODE_ITEM_BOX;
            set_flag(FlagGroup::Status, FG_STATUS_SCREEN, true);
            gGameTable.byte_991F80 = 1;
            gGameTable.itembox_speed = 0;
            gGameTable.question_state = ITEMBOX_INTERACT_STATE_CLOSING;
            [[fallthrough]];
        case ITEMBOX_INTERACT_STATE_CLOSING:
            gGameTable.itembox_speed++;
            if (gGameTable.itembox_speed > 4)
            {
                gGameTable.itembox_obj->cdir.z = 0;
                gGameTable.question_state = 0;
                gGameTable.action_fn = nullptr;
            }
            break;
        }
    }

    // 0x004E9440
    static void sce_auto(const void*)
    {
        gGameTable.scd_variables_00 = gGameTable.word_6949F0;
        gGameTable.word_98EAE6 = gGameTable.word_6949F4;
    }

    // 0x004E9460
    static int sce_door(SceAotDoorData* data)
    {
        if (gPlayerEntity.pOn_om != 0)
            return 0;

        auto isClaire = check_flag(FlagGroup::Status, FG_STATUS_PLAYER);
        auto hasPartner = check_flag(FlagGroup::Status, FG_STATUS_PARTNER);
        auto partner = gGameTable.splayer_work;
        if (isClaire && hasPartner && (partner->be_flg & 1) && (partner->var_21D & 0x20))
        {
            show_message(0, 0x100, MESSAGE_KIND_LEAVE_SHERRY_BEHIND, 0xFF000000);
            return 0;
        }

        int eax;
        if (data->LockId < 128 || (eax = bitarray_get(&gGameTable.door_locks, data->LockId & 0x3F)))
        {
            gGameTable.byte_991F80 = 1;
            gGameTable.door_aot_data = data;
            set_flag(FlagGroup::Stop, FG_STOP_00, true);
            set_flag(FlagGroup::Stop, FG_STOP_01, true);
            set_flag(FlagGroup::Stop, FG_STOP_02, true);
            set_flag(FlagGroup::Stop, FG_STOP_03, true);
            set_flag(FlagGroup::Stop, FG_STOP_04, true);
            set_flag(FlagGroup::Stop, FG_STOP_05, true);
            set_flag(FlagGroup::Stop, FG_STOP_06, true);
            set_flag(FlagGroup::Stop, FG_STOP_DISABLE_INPUT, true);
            set_flag(FlagGroup::Stop, FG_STOP_08, true);
            return 0;
        }

        auto key = data->KeyType;
        if (key == KEY_UNLOCK)
        {
            show_message(eax, 0x100, MESSAGE_KIND_YOU_UNLOCKED_IT, 0xFF000000);
            snd_se_on(0x2260000);
        }
        else if (key == KEY_LOCKED)
        {
            snd_se_on(0x2160000);
            show_message(0, 0x100, MESSAGE_KIND_LOCKED_FROM_OTHER_SIDE, 0xFF000000);
            return 0;
        }
        else
        {
            auto inventoryIndex = inventory_find_item(key);
            if (inventoryIndex < 0)
            {
                snd_se_on(0x2160000);
                show_message(0, 0x100, (int)key - 76, 0xFF000000);
                return 0;
            }

            gGameTable.pickup_item_type = key;
            show_message(0, 0x100, MESSAGE_KIND_YOU_USED_KEY_X, 0xFF000000);
            snd_se_on(0x2250000);
            gGameTable.dword_98E790 = (uint32_t)&use_key;
            gGameTable.byte_98E541 = inventoryIndex + 1;
        }

        bitarray_set(&gGameTable.door_locks, data->LockId & 0x3F);
        return 0;
    }

    static int transform_item_amount(const SceAotItemData& data)
    {
        switch (data.type)
        {
        case ITEM_TYPE_AMMO_HANDGUN: return data.amount == 15 ? 10 : 20;
        case ITEM_TYPE_AMMO_SHOTGUN: return 5;
        case ITEM_TYPE_AMMO_MAGNUM: return 6;
        case ITEM_TYPE_AMMO_EXPLOSIVE_ROUNDS: return 4;
        case ITEM_TYPE_AMMO_FLAME_ROUNDS: return 4;
        case ITEM_TYPE_AMMO_ACID_ROUNDS: return 4;
        case ITEM_TYPE_AMMO_BOWGUN: return 12;
        default: return data.amount;
        }
    }

    // 0x004E96C0
    static void sce_item(SceAotItemData* data)
    {
        gGameTable.pickup_item_type = data->type;
        gGameTable.dword_9888D0 = gGameTable.dword_6949F8;
        gGameTable.pickup_item = data->type;
        if (gGameTable.censorship_off && (data->type == ITEM_TYPE_INK_RIBBON))
        {
            data->amount = 2;
        }
        if (gGameTable.hard_mode != 0)
        {
            data->amount = transform_item_amount(*data);
        }

        if ((data->action & 1) != 0)
        {
            player::set_routine(Routine::PICK_UP_ITEM);
        }
        else
        {
            // No pick up animation
            gGameTable.byte_991F80 = 1;
            gGameTable.dword_991FC4 = gGameTable.fg_stop;
            gGameTable.hud_mode = HUD_MODE_PICKUP_ITEM;
            set_flag(FlagGroup::Status, FG_STATUS_SCREEN, true);
        }
    }

    // 0x004E97C0
    static void sce_normal(const void*) {}

    // 0x004E97D0
    static void sce_message(const SceAotMessageData* data)
    {
        show_message(0, data->var_02 + 768, data->var_00, data->var_04 << 16);
    }

    // 0x004E9800
    static void sce_event(const SceAotEventData* data)
    {
        if (!check_flag(FlagGroup::Stop, FG_STOP_06))
        {
            scd_event_exec(data->task_index, data->event_index);
        }
    }

    // 0x004E9880
    static void sce_water(const uint16_t* data)
    {
        auto entity = gGameTable.actor_entity;
        entity->water = *data;
    }

    // 0x004E98F0
    static void sce_save(const void*)
    {
        gGameTable.action_fn = &sce_save_callback;
        gGameTable.question_state = 0;
        gGameTable.dword_991FC4 = gGameTable.fg_stop;
        gGameTable.fg_stop |= 0xFF000000;
        gGameTable.byte_98E9A7 = gGameTable.dword_6949F8->var_0C;
    }

    // 0x004E99F0
    static void sce_itembox(const void*)
    {
        gGameTable.itembox_obj_index = gGameTable.dword_6949F8->var_0C;
        gGameTable.byte_691F74 = gGameTable.dword_6949F8->var_0E;
        gGameTable.question_state = ITEMBOX_INTERACT_STATE_INIT;
        gGameTable.action_fn = &sce_itembox_callback;
    }

    // 0x004E9B70
    static int sce_damage(int* a0)
    {
        auto& entity = gGameTable.actor_entity;

        if (entity->status_flg & ENTITY_STATUS_FLAG_2)
        {
            entity->status_flg &= ~ENTITY_STATUS_FLAG_2;
        }

        entity->damage_cnt |= 0x80;
        entity->life -= *a0 >> 16;
        if (entity->life >= 0)
        {
            entity->routine_0 = 2;
            entity->routine_1 = *a0 & 0xFF;
            entity->routine_2 = 0;
            entity->routine_3 = 0;

            auto direction = direction_check(
                                 gGameTable.dword_6949F8->var_04 + (gGameTable.dword_6949F8->var_08 >> 1),
                                 gGameTable.dword_6949F8->var_06 + (gGameTable.dword_6949F8->var_0A >> 1),
                                 entity->m.pos.x,
                                 entity->m.pos.z)
                - entity->cdir.y;
            entity->spd.x = 200;
            add_speed_xz(entity, direction);
        }
        else if (entity->routine_0 != 3)
        {
            entity->routine_0 = 3;
            entity->routine_1 = 0;
            entity->routine_2 = 0;
            entity->routine_3 = 0;
        }

        return 1;
    }

    static void set_sce_hook(SceKind sce, SceImpl impl)
    {
        gScdImplTable[sce] = impl;
    }

    void sce_init_hooks()
    {
        interop::writeJmp(0x004E40D0, &sce_scheduler_set);
        interop::writeJmp(0x004E42D0, &sce_scheduler);
        set_sce_hook(SCE_AUTO, reinterpret_cast<SceImpl>(&sce_auto));
        set_sce_hook(SCE_DOOR, reinterpret_cast<SceImpl>(&sce_door));
        set_sce_hook(SCE_ITEM, reinterpret_cast<SceImpl>(&sce_item));
        set_sce_hook(SCE_NORMAL, reinterpret_cast<SceImpl>(&sce_normal));
        set_sce_hook(SCE_MESSAGE, reinterpret_cast<SceImpl>(&sce_message));
        set_sce_hook(SCE_EVENT, reinterpret_cast<SceImpl>(&sce_event));
        set_sce_hook(SCE_WATER, reinterpret_cast<SceImpl>(&sce_water));
        set_sce_hook(SCE_SAVE, reinterpret_cast<SceImpl>(&sce_save));
        set_sce_hook(SCE_ITEMBOX, reinterpret_cast<SceImpl>(&sce_itembox));
        set_sce_hook(SCE_DAMAGE, reinterpret_cast<SceImpl>(&sce_damage));
    }
}
