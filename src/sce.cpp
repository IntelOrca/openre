#include "re2.h"
#include "openre.h"
#include "player.h"

using namespace openre::player;

namespace openre::sce
{
    enum {
        ITEMBOX_INTERACT_STATE_INIT,
        ITEMBOX_INTERACT_STATE_OPENING,
        ITEMBOX_INTERACT_STATE_OPENED,
        ITEMBOX_INTERACT_STATE_CLOSING,
    };

    using SceImpl = int (*)(void*);

    static SceAotBase** gAotTable = (SceAotBase**)0x988850;
    static uint8_t& gAotCount = *((uint8_t*)0x98E528);
    static SceImpl* gScdImplTable = (SceImpl*)0x53B46C;
    static uint8_t& _questionState = *((uint8_t*)0x98E542);

    static uint8_t& byte_6805B1 = *((uint8_t*)0x6805B1);
    static uint8_t& byte_6805B3 = *((uint8_t*)0x6805B3);
    static int16_t& _itemBoxSpeed = *((int16_t*)0x689C90);
    static int16_t& _itemBoxAcceleration = *((int16_t*)0x689C98);
    static Unknown689CA8*& _itemBoxObj = *((Unknown689CA8**)0x689CA8);
    static uint8_t& gHudMode = *((uint8_t*)0x691F70);
    static uint8_t& byte_691F74 = *((uint8_t*)0x691F74);
    static uint16_t& word_6949F0 = *((uint16_t*)0x6949F0);
    static uint16_t& word_6949F4 = *((uint16_t*)0x6949F4);
    static Unknown6949F8*& dword_6949F8 = *((Unknown6949F8**)0x6949F8);
    static uint8_t& gQuestionFlags = *((uint8_t*)0x98504C);
    static uint8_t& gPickupItemName = *((uint8_t*)0x98504F);
    static Unknown988628*& dword_988628 = *((Unknown988628**)0x988628);
    static SceAotDoorData*& dword_988848 = *((SceAotDoorData**)0x988848);
    static Unknown6949F8*& dword_9888D0 = *((Unknown6949F8**)0x9888D0);
    static uint32_t& dword_989E6C = *((uint32_t*)0x989E6C);
    static uint32_t& dword_989ED4 = *((uint32_t*)0x989ED4);
    static uint32_t& dword_98A0D4 = *((uint32_t*)0x98A0D4);
    static uint8_t*& dword_98A110 = *((uint8_t**)0x98A110);
    static uint8_t& _itemBoxObjIndex = *((uint8_t*)0x98E533);
    static uint8_t& byte_98E541 = *((uint8_t*)0x98E541);
    static void*& dword_98E790 = *((void**)0x98E790);
    static Action& dword_98E794 = *((Action*)0x98E794);
    static uint8_t& byte_98E9A7 = *((uint8_t*)0x98E9A7);
    static uint16_t& word_98EAE4 = *((uint16_t*)0x98EAE4);
    static uint16_t& word_98EAE6 = *((uint16_t*)0x98EAE6);
    static uint8_t& byte_989EF5 = *((uint8_t*)0x989EF5);
    static uint8_t& byte_989EF6 = *((uint8_t*)0x989EF6);
    static Unknown689CA8* data_0098A61C = (Unknown689CA8*)0x98A61C;
    static uint8_t& gPickupItem = *((uint8_t*)0x98E529);
    static uint8_t& byte_991F80 = *((uint8_t*)0x991F80);
    static uint32_t& dword_991FC4 = *((uint32_t*)0x991FC4);

    constexpr uint8_t KEY_LOCKED = 255;
    constexpr uint8_t KEY_UNLOCK = 254;

    constexpr uint8_t QUESTION_FLAG_ANSWER_NO = 1 << 0;
    constexpr uint8_t QUESTION_FLAG_IS_WAITING = 1 << 7;

    // 0x00503170
    int bitarray_get(uint32_t* bitArray, int index)
    {
        auto dwordIndex = index >> 5;
        auto bitIndex = index & 0x1F;
        auto result = bitArray[dwordIndex] & (0x80000000 >> bitIndex);
        return result;
    }

    // 0x00503120
    void bitarray_set(uint32_t* bitArray, int index)
    {
        auto dwordIndex = index >> 5;
        auto bitIndex = index & 0x1F;
        bitArray[dwordIndex] |= 0x80000000 >> bitIndex;
    }

    // 0x00503140
    void bitarray_clr(uint32_t* bitArray, int index)
    {
        auto dwordIndex = index >> 5;
        auto bitIndex = index & 0x1F;
        bitArray[dwordIndex] |= 0x80000000 >> bitIndex;
    }

    // 0x004C89B2
    static void show_message(int a0, int a1, int a2, int a3)
    {
        using sig = void (*)(int, int, int, int);
        auto p = (sig)0x004C89B2;
        p(a0, a1, a2, a3);
    }

    // 0x004ED950
    static void snd_se_on(int a0, int a1)
    {
        using sig = void (*)(int, int);
        auto p = (sig)0x004ED950;
        p(a0, a1);
    }

    static int sub_4E95F0()
    {
        using sig = int (*)();
        auto p = (sig)0x004E95F0;
        return p();
    }

    // 0x004E9930
    static void sce_save_callback()
    {
        constexpr uint8_t STATE_QUESTION = 0;
        constexpr uint8_t STATE_ANSWER = 1;

        if (_questionState == STATE_QUESTION)
        {
            auto inventoryIndex = inventory_find_item(ITEM_TYPE_INK_RIBBON);
            if (inventoryIndex < 0)
            {
                dword_989ED4 = dword_991FC4;
                show_message(0, 0x100, MESSAGE_KIND_INK_RIBBON_REQUIRED_TO_SAVE, 0xFF000000);
                dword_98E794 = nullptr;
                _questionState = STATE_QUESTION;
            }
            else
            {
                show_message(0, 0x100, MESSAGE_KIND_WILL_YOU_USE_USE_INK_RIBBON, 0xFF000000);
                _questionState = STATE_ANSWER;
            }
        }
        else if (_questionState == STATE_ANSWER && !(gQuestionFlags & QUESTION_FLAG_IS_WAITING))
        {
            dword_98E794 = nullptr;
            _questionState = STATE_QUESTION;
            if (gQuestionFlags & QUESTION_FLAG_ANSWER_NO)
            {
                dword_989ED4 = dword_991FC4;
            }
            else
            {
                byte_991F80 = 1;
                dword_989E6C |= 0x40000;
            }
        }
    }

    // 0x004E9A20
    static void sce_itembox_callback()
    {
        switch (_questionState) {
        case ITEMBOX_INTERACT_STATE_INIT:
            dword_991FC4 = dword_989ED4;
            dword_989ED4 |= 0x7F000000;
            _itemBoxSpeed = 1;
            _itemBoxAcceleration = 3;
            snd_se_on(0x2150000, 0);
            _questionState = ITEMBOX_INTERACT_STATE_OPENING;
            _itemBoxObj = &data_0098A61C[_itemBoxObjIndex];
            [[fallthrough]];
        case ITEMBOX_INTERACT_STATE_OPENING:
            _itemBoxObj->var_78 -= _itemBoxSpeed;
            _itemBoxSpeed += _itemBoxAcceleration;
            if (_itemBoxObj->var_78 < -399)
            {
                _questionState = ITEMBOX_INTERACT_STATE_OPENED;
                _itemBoxAcceleration = -_itemBoxAcceleration;
            }
            break;
        case ITEMBOX_INTERACT_STATE_OPENED:
            _itemBoxObj->var_78 -= _itemBoxSpeed;
            if (_itemBoxSpeed + _itemBoxAcceleration > 0)
            {
                _itemBoxSpeed += _itemBoxAcceleration;
                break;
            }
            gHudMode = HUD_MODE_ITEM_BOX;
            gGameFlags |= GAME_FLAG_15;
            byte_991F80 = 1;
            _itemBoxSpeed = 0;
            _questionState = ITEMBOX_INTERACT_STATE_CLOSING;
            [[fallthrough]];
        case ITEMBOX_INTERACT_STATE_CLOSING:
            _itemBoxSpeed++;
            if (_itemBoxSpeed > 4)
            {
                _itemBoxObj->var_78 = 0;
                _questionState = 0;
                dword_98E794 = nullptr;
            }
            break;
        }
    }

    // 0x004E9440
    static void sce_auto(void*)
    {
        word_98EAE4 = word_6949F0;
        word_98EAE6 = word_6949F4;
    }

    // 0x004E9460
    static int sce_door(SceAotDoorData* data)
    {
        if (dword_98A0D4 != 0)
            return 0;

        if ((gGameFlags & GAME_FLAG_IS_PLAYER_1) &&
            (gGameFlags & GAME_FLAG_HAS_PARTNER) &&
            (dword_98A110[0] & 1) &&
            (dword_98A110[0x21D] & 0x20))
        {
            show_message(0, 0x100, MESSAGE_KIND_LEAVE_SHERRY_BEHIND, 0xFF000000);
            return 0;
        }

        int eax;
        if (data->LockId < 128 || (eax = bitarray_get(gGameTable.door_locks, data->LockId & 0x3F)))
        {
            byte_991F80 = 1;
            dword_988848 = data;
            dword_989ED4 |= 0xFF000000;
            return 0;
        }

        auto key = data->KeyType;
        if (key == KEY_UNLOCK)
        {
            show_message(eax, 0x100, MESSAGE_KIND_YOU_UNLOCKED_IT, 0xFF000000);
            snd_se_on(0x2260000, 0);
        }
        else if (key == KEY_LOCKED)
        {
            snd_se_on(0x2160000, 0);
            show_message(0, 0x100, MESSAGE_KIND_LOCKED_FROM_OTHER_SIDE, 0xFF000000);
            return 0;
        }
        else
        {
            auto inventoryIndex = inventory_find_item(key);
            if (inventoryIndex < 0)
            {
                snd_se_on(0x2160000, 0);
                show_message(0, 0x100, (int)key - 76, 0xFF000000);
                return 0;
            }

            gPickupItemName = key;
            show_message(0, 0x100, MESSAGE_KIND_YOU_USED_KEY_X, 0xFF000000);
            snd_se_on(0x2250000, 0);
            dword_98E790 = &sub_4E95F0;
            byte_98E541 = inventoryIndex + 1;
        }

        bitarray_set(gGameTable.door_locks, data->LockId & 0x3F);
        return 0;
    }

    // 0x004E96C0
    static void sce_item(SceAotItemData* data)
    {
        gPickupItemName = data->type;
        dword_9888D0 = dword_6949F8;
        gPickupItem = data->type;
        if ((byte_6805B3 != 0) && (data->type == ITEM_TYPE_INK_RIBBON))
        {
            data->amount = 2;
        }
        if (byte_6805B1 != 0)
        {
            switch (data->type)
            {
            case ITEM_TYPE_AMMO_HANDGUN:
                data->amount = data->amount == 15 ? 10 : 20;
                break;
            case ITEM_TYPE_AMMO_SHOTGUN:
                data->amount = 5;
                break;
            case ITEM_TYPE_AMMO_MAGNUM:
                data->amount = 6;
                break;
            case ITEM_TYPE_AMMO_EXPLOSIVE_ROUNDS:
                data->amount = 4;
                break;
            case ITEM_TYPE_AMMO_FLAME_ROUNDS:
                data->amount = 4;
                break;
            case ITEM_TYPE_AMMO_ACID_ROUNDS:
                data->amount = 4;
                break;
            case ITEM_TYPE_AMMO_BOWGUN:
                data->amount = 12;
                break;
            }
        }

        if ((data->action & 1) != 0)
        {
            // Pick up from floor animation
            byte_989EF5 = 6;
            byte_989EF6 = 0;
        }
        else
        {
            // No pick up animation
            byte_991F80 = 1;
            dword_991FC4 = dword_989ED4;
            gHudMode = HUD_MODE_PICKUP_ITEM;
            gGameFlags |= GAME_FLAG_15;
        }
    }

    // 0x004E97C0
    static void sce_normal(void*)
    {
    }

    // 0x004E97D0
    static void sce_message(SceAotMessageData* data)
    {
        show_message(0, data->var_02 + 768, data->var_00, data->var_04 << 16);
    }

    // 0x004E9880
    static void sce_water(uint16_t* data)
    {
        auto unk = dword_988628;
        unk->var_10C = *data;
    }

    // 0x004E98F0
    static void sce_save(void*)
    {
        dword_98E794 = &sce_save_callback;
        _questionState = 0;
        dword_991FC4 = dword_989ED4;
        dword_989ED4 |= 0xFF000000;
        byte_98E9A7 = dword_6949F8->var_0C;
    }

    // 0x004E99F0
    static void sce_itembox(void*)
    {
        _itemBoxObjIndex = dword_6949F8->var_0C;
        byte_691F74 = dword_6949F8->var_0E;
        _questionState = ITEMBOX_INTERACT_STATE_INIT;
        dword_98E794 = &sce_itembox_callback;
    }

    static void set_sce_hook(SceKind sce, SceImpl impl)
    {
        gScdImplTable[sce] = impl;
    }

    void sce_init_hooks()
    {
        set_sce_hook(SCE_AUTO, reinterpret_cast<SceImpl>(&sce_auto));
        set_sce_hook(SCE_DOOR, reinterpret_cast<SceImpl>(&sce_door));
        set_sce_hook(SCE_ITEM, reinterpret_cast<SceImpl>(&sce_item));
        set_sce_hook(SCE_NORMAL, reinterpret_cast<SceImpl>(&sce_normal));
        set_sce_hook(SCE_MESSAGE, reinterpret_cast<SceImpl>(&sce_message));
        set_sce_hook(SCE_WATER, reinterpret_cast<SceImpl>(&sce_water));
        set_sce_hook(SCE_SAVE, reinterpret_cast<SceImpl>(&sce_save));
        set_sce_hook(SCE_ITEMBOX, reinterpret_cast<SceImpl>(&sce_itembox));
    }
}
