#include "re2.h"

namespace openre::sce
{
    using SceImpl = int (*)(void*);

    static SceAotBase** gAotTable = (SceAotBase**)0x988850;
    static uint8_t& gAotCount = *((uint8_t*)0x98E528);
    static SceImpl* gScdImplTable = (SceImpl*)0x53B46C;
    static uint32_t* gDoorLocks = (uint32_t*)0x98ED2C;
    static uint32_t& gGameFlags = *((uint32_t*)0x989ED0);

    static uint32_t& dword_98A0D4 = *((uint32_t*)0x98A0D4);
    static uint8_t*& dword_98A110 = *((uint8_t**)0x98A110);
    static uint32_t& dword_989ED4 = *((uint32_t*)0x989ED4);
    static IN_DOOR_WORK*& dword_988848 = *((IN_DOOR_WORK**)0x988848);
    static uint8_t& byte_991F80 = *((uint8_t*)0x991F80);
    static uint8_t& byte_98504F = *((uint8_t*)0x98504F);
    static void*& dword_98E790 = *((void**)0x98E790);
    static uint8_t& byte_98E541 = *((uint8_t*)0x98E541);

    constexpr uint8_t KEY_LOCKED = 255;
    constexpr uint8_t KEY_UNLOCK = 254;

    // 0x00503170
    static int bitarray_get(uint32_t* bitArray, int index)
    {
        auto dwordIndex = index >> 5;
        auto bitIndex = index & 0x1F;
        auto result = bitArray[dwordIndex] & (0x80000000 >> bitIndex);
        return result;
    }

    // 0x00503120
    static void bitarray_set(uint32_t* bitArray, int index)
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

    static int sub_502660(int a0)
    {
        using sig = int (*)(int);
        auto p = (sig)0x00502660;
        return p(a0);
    }

    static int sub_4E95F0()
    {
        using sig = int (*)();
        auto p = (sig)0x004E95F0;
        return p();
    }

    // 0x4E9460
    static int sce_door(IN_DOOR_WORK* pAot)
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
        if (pAot->Key_id < 128 || (eax = bitarray_get(gDoorLocks, pAot->Key_id & 0x3F)))
        {
            byte_991F80 = 1;
            dword_988848 = pAot;
            dword_989ED4 |= 0xFF000000;
            return 0;
        }

        auto key = pAot->Key_type;
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
            auto tmp = sub_502660(key);
            if (tmp < 0)
            {
                snd_se_on(0x2160000, 0);
                show_message(0, 0x100, (int)key - 76, 0xFF000000);
                return 0;
            }

            byte_98504F = key;
            show_message(0, 0x100, MESSAGE_KIND_YOU_USED_KEY_X, 0xFF000000);
            snd_se_on(0x2250000, 0);
            dword_98E790 = &sub_4E95F0;
            byte_98E541 = tmp + 1;
        }

        bitarray_set(gDoorLocks, pAot->Key_id & 0x3F);
        return 0;
    }

    static void set_sce_hook(SceKind sce, SceImpl impl)
    {
        gScdImplTable[sce] = impl;
    }

    void sce_init_hooks()
    {
        set_sce_hook(SCE_DOOR, reinterpret_cast<SceImpl>(&sce_door));
    }
}
