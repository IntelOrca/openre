#define _CRT_SECURE_NO_WARNINGS

#include "audio.h"
#include "file.h"
#include "interop.hpp"
#include "openre.h"

using namespace openre::file;

namespace openre::audio
{
    static uint16_t* gBgmTable = (uint16_t*)0x98E9C8;

    static uint8_t& _dsoundEnable = *((uint8_t*)0x524EB6);
    static uint8_t* byte_53C5D8 = (uint8_t*)0x53C5D8;
    static uint8_t* byte_53C78F = (uint8_t*)0x53C78F;
    static uint32_t* _ssNameBgm = (uint32_t*)0x6934E0;
    static uint8_t* byte_6D730C = (uint8_t*)0x6D730C;

    static uint8_t*& current_bgm_address = *((uint8_t**)0x693E84);

    static uint8_t& _vabId = *((uint8_t*)0x692FF5);
    static uint8_t& _seqCtr = *((uint8_t*)0x693800);
    static int8_t& byte_693802 = *((int8_t*)0x693802);
    static int8_t& byte_693808 = *((int8_t*)0x693808);
    static int8_t& byte_69380A = *((int8_t*)0x69380A);
    static int8_t& byte_693810 = *((int8_t*)0x693810);
    static int8_t& byte_693812 = *((int8_t*)0x693812);
    static int8_t& _ssNameSbgm = *((int8_t*)0x693EA0);
    static int8_t& byte_693FA4 = *((int8_t*)0x693FA4);

    static uint8_t*& dword_6934B4 = *((uint8_t**)0x6934B4);
    static uint32_t& dword_693804 = *((uint32_t*)0x693804);
    static uint32_t& dword_693C4C = *((uint32_t*)0x693C4C);
    static uint32_t& _memTop = *((uint32_t*)0x988624);
    static uint32_t& dword_989E6C = *((uint32_t*)0x989E6C);

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
    static int ss_load_banks(int type, int id, int bank, int player)
    {
        using sig = int (*)(int, int, int, int);
        auto p = (sig)0x004344A0;
        return p(type, id, bank, player);
    }

    // 0x004ECDA0
    int read_bgm()
    {
        if (_dsoundEnable == 0)
            return 1;

        if ((dword_989E6C & 0x2000) != 0)
            return 1;

        dword_693C4C = 0;
        if (-1 < byte_693802)
        {
            if (_seqCtr != '\0')
            {
                auto uVar3 = ss_get_status(5, 0);
                if ((uVar3 & 1) != 0)
                {
                    ss_stop_group(5, 0xffffffff);
                }
                _seqCtr = '\0';
            }
            auto iVar4 = 0;
            auto puVar6 = _ssNameBgm;
            do
            {
                ss_unload_bgm(5, iVar4);
                *puVar6 = 0;
                puVar6 = puVar6 + 0x104;
                iVar4++;
            } while ((int)puVar6 < 0x6937ec);
            byte_693802 = -1;
        }
        if (*current_bgm_address == 0xff)
        {
            return 0xff;
        }
        if (-1 < byte_69380A)
        {
            if (byte_693808 != '\0')
            {
                auto uVar3 = ss_get_status(5, 1);
                if ((uVar3 & 1) != 0)
                {
                    ss_stop_group(6, 0);
                }
                byte_693808 = '\0';
            }
            ss_unload_bgm(6, 0);
            _ssNameSbgm = 0;
            byte_69380A = -1;
        }
        if (-1 < byte_693812)
        {
            if (byte_693810 != '\0')
            {
                auto uVar3 = ss_get_status(5, 2);
                if ((uVar3 & 1) != 0)
                {
                    ss_stop_group(6, 1);
                }
                byte_693810 = '\0';
            }
            ss_unload_bgm(6, 1);
            byte_693FA4 = 0;
            byte_693812 = -1;
        }

        auto bgmIndex = *current_bgm_address & 0x3F;
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

        dword_6934B4 = byte_6D730C + unk1;
        std::memcpy(byte_6D730C, (void*)buffer, unk2);
        dword_693C4C = *(int*)(dword_6934B4 + 12) + (uint32_t) * (uint16_t*)(dword_6934B4 + 18) * -0x200 - 0xA20;
        if (dword_693C4C < 0x38801)
        {
            _vabId = ss_load_banks(5, gGameTable.current_stage, gGameTable.current_room, bgmIndex);
            byte_693802 = _vabId;

            for (auto i = 0; i < 3; i++)
            {
                auto temp = 0;
                if (byte_53C5D8[(bgmIndex * 3) + i] == '\0')
                {
                    if ((dword_989E6C & 8) != 0)
                    {
                        temp = dword_693804 & 0xFFFF;
                    }
                }
                else
                {
                    temp = dword_693804 & 0xFFFF;
                }
                ss_set_vol(5, i, temp);
            }
            _seqCtr = 0;
            return 0;
        }
        return 0xff;
    }

    // 0x004ED920
    void bgm_set_entry(uint32_t arg0)
    {
        if (_dsoundEnable == 0)
            return;

        auto stage = arg0 >> 24;
        auto room = (arg0 >> 16) & 0xFF;
        auto tableIndex = byte_53C78F[stage] + room;
        gBgmTable[tableIndex] = arg0 & 0xFFFF;
    }

    // 0x004ED2F0
    void bgm_set_control(uint32_t arg0)
    {
        using sig = void (*)(uint32_t);
        auto p = (sig)0x004ED2F0;
        p(arg0);
    }

    // 0x004ED950
    void snd_se_on(int a0, int a1)
    {
        // using sig = void (*)(int, int);
        // auto p = (sig)0x004ED950;
        // p(a0, a1);
    }

    void bgm_init_hooks()
    {
        interop::writeJmp(0x004ECDA0, read_bgm);
        interop::writeJmp(0x004ED920, bgm_set_entry);
        interop::writeJmp(0x004ED950, snd_se_on);
    }
}
