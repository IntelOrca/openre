#define _CRT_SECURE_NO_WARNINGS

#include <cstring>

#include "audio.h"
#include "file.h"
#include "interop.hpp"
#include "openre.h"

using namespace openre::file;

namespace openre::audio
{
    static uint8_t get_bgm_slot(int index, int kind)
    {
        auto entry = &gGameTable.byte_53C5D8[index];
        switch (kind)
        {
        case 0: return entry->main;
        case 1: return entry->sub0;
        case 2: return entry->sub1;
        }
        return 0;
    }

    // 0x004347B0
    static int ss_get_status(int type, int sub)
    {
        using sig = int (*)(int, int);
        auto p = (sig)0x004347B0;
        return p(type, sub);
    }

    // 0x004341E0
    static int ss_stop_group(int type, int id)
    {
        using sig = int (*)(int, int);
        auto p = (sig)0x004341E0;
        return p(type, id);
    }

    // 0x00434140
    static int ss_unload_bgm(int type, int index)
    {
        using sig = int (*)(int, int);
        auto p = (sig)0x00434140;
        return p(type, index);
    }

    // 0x00434AB0
    static int ss_set_vol(int type, int index, int vol)
    {
        using sig = int (*)(int, int, int);
        auto p = (sig)0x00434AB0;
        return p(type, index, vol);
    }

    // 0x004344A0
    static uint8_t ss_load_banks(int type, int id, int bank, int player)
    {
        using sig = int (*)(int, int, int, int);
        auto p = (sig)0x004344A0;
        return p(type, id, bank, player);
    }

    // 0x004ECDA0
    int snd_bgm_main()
    {
        if (!gGameTable.enable_dsound)
            return 1;

        if (check_flag(FlagGroup::System, FG_SYSTEM_BGM_DISABLED))
            return 1;

        gGameTable.dword_693C4C = 0;
        if (-1 < gGameTable.seq_ctr[2])
        {
            if (gGameTable.seq_ctr[0] != 0)
            {
                auto uVar3 = ss_get_status(5, 0);
                if ((uVar3 & 1) != 0)
                {
                    ss_stop_group(5, 0xffffffff);
                }
                gGameTable.seq_ctr[0] = 0;
            }
            auto iVar4 = 0;
            auto puVar6 = gGameTable.ss_name_bgm;
            do
            {
                ss_unload_bgm(5, iVar4);
                *puVar6 = 0;
                puVar6 = puVar6 + 0x104;
                iVar4++;
            } while ((int)puVar6 < 0x6937ec);
            gGameTable.seq_ctr[2] = -1;
        }
        if (*gGameTable.current_bgm_address == 0xff)
        {
            return 0xff;
        }
        if (-1 < gGameTable.byte_69380A)
        {
            if (gGameTable.byte_693808 != 0)
            {
                auto uVar3 = ss_get_status(5, 1);
                if ((uVar3 & 1) != 0)
                {
                    ss_stop_group(6, 0);
                }
                gGameTable.byte_693808 = 0;
            }
            ss_unload_bgm(6, 0);
            gGameTable.ss_name_sbgm[0] = 0;
            gGameTable.byte_69380A = -1;
        }
        if (-1 < gGameTable.byte_693812)
        {
            if (gGameTable.byte_693810 != 0)
            {
                auto uVar3 = ss_get_status(5, 2);
                if ((uVar3 & 1) != 0)
                {
                    ss_stop_group(6, 1);
                }
                gGameTable.byte_693810 = 0;
            }
            ss_unload_bgm(6, 1);
            gGameTable.byte_693FA4 = 0;
            gGameTable.byte_693812 = -1;
        }

        auto bgmIndex = *gGameTable.current_bgm_address & 0x3F;
        char path[260];
        std::sprintf(path, "common\\sound\\bgm\\main%02x.bgm", bgmIndex);

        auto buffer = (uint8_t*)((_memTop + 16) & 0xFFFFFFF0);
        auto numBytes = read_file_into_buffer(path, (char*)buffer, 1);
        if (numBytes == 0)
        {
            file_error();
            return 1;
        }
        if (numBytes == -1)
        {
            return 0xff;
        }

        auto unk1 = *((int32_t*)&buffer[numBytes - 8]);
        auto unk2 = *((int32_t*)&buffer[numBytes - 12]);

        gGameTable.dword_6934B4 = gGameTable.byte_6D730C + unk1;
        std::memcpy(gGameTable.byte_6D730C, (void*)buffer, unk2);
        gGameTable.dword_693C4C
            = *(int*)(gGameTable.dword_6934B4 + 12) + (uint32_t) * (uint16_t*)(gGameTable.dword_6934B4 + 18) * -0x200 - 0xA20;
        if (gGameTable.dword_693C4C < 0x38801)
        {
            auto id = ss_load_banks(5, gGameTable.current_stage, gGameTable.current_room, bgmIndex);
            gGameTable.vab_id[5] = id;
            gGameTable.seq_ctr[2] = id;

            for (auto i = 0; i < 3; i++)
            {
                auto temp = 0;
                if (get_bgm_slot(bgmIndex, i) == 0)
                {
                    if (check_flag(FlagGroup::System, FG_SYSTEM_4TH_SURVIVOR))
                    {
                        temp = gGameTable.dword_693804 & 0xFFFF;
                    }
                }
                else
                {
                    temp = gGameTable.dword_693804 & 0xFFFF;
                }
                ss_set_vol(5, i, temp);
            }
            gGameTable.seq_ctr[0] = 0;
            return 0;
        }
        return 0xff;
    }

    // 0x004ED920
    void bgm_set_entry(uint32_t arg0)
    {
        if (!gGameTable.enable_dsound)
            return;

        auto stage = arg0 >> 24;
        auto room = (arg0 >> 16) & 0xFF;
        auto tableIndex = gGameTable.byte_53C78F[stage] + room;
        gGameTable.bgm_table[tableIndex] = arg0 & 0xFFFF;
    }

    // 0x004ED2F0
    void bgm_set_control(uint32_t arg0)
    {
        using sig = void (*)(uint32_t);
        auto p = (sig)0x004ED2F0;
        p(arg0);
    }

    // 0x004ED950
    static void snd_se_on(int a0, const Vec32* a1)
    {
        using sig = void (*)(int, const Vec32*);
        auto p = (sig)0x004ED950;
        p(a0, a1);
    }

    void snd_se_on(int a0, const Vec32& a1)
    {
        snd_se_on(a0, &a1);
    }

    void snd_se_on(int a0)
    {
        snd_se_on(a0, nullptr);
    }

    // 0x004EC450
    void snd_load_core(uint8_t a0, uint8_t a1)
    {
        interop::call<void, uint8_t, uint8_t>(0x004EC450, a0, a1);
    }

    // 0x004EC410
    void snd_sys_init_sub2()
    {
        interop::call(0x004EC410);
    }

    // 0x004ECBE0
    void snd_bgm_ck()
    {
        interop::call(0x004ECBE0);
    }

    // 0x004EC7D0
    void snd_room_load()
    {
        interop::call(0x004EC7D0);
    }

    void bgm_init_hooks()
    {
        interop::writeJmp(0x004ECDA0, snd_bgm_main);
        interop::writeJmp(0x004ED920, bgm_set_entry);
        // interop::writeJmp(0x004ED950, snd_se_on);
    }
}
