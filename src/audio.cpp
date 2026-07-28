#include "audio.h"
#include "file.h"
#include "interop.hpp"
#include "logger.h"
#include "openre.h"

#include <cstring>
#include <string>
#include <windows.h>
#include <dsound.h>
#include <mmsystem.h>

#include <mmreg.h>
#include <msacm.h>

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

    // 0x00433870
    static int ss_set_coop_level(int mode)
    {
        if (!gGameTable.audio_pMarniSnd)
            return 1;

        auto ds = (LPDIRECTSOUND)gGameTable.audio_pMarniSnd;
        DWORD level = (mode == 1) ? DSSCL_NORMAL : DSSCL_EXCLUSIVE;
        if (ds->SetCooperativeLevel((HWND)gGameTable.hwnd, level))
            return 0;
        return 1;
    }

    // 0x004338B0
    static int ss_set_stereo_mono(int is_mono)
    {
        if (!gGameTable.audio_pMarniSnd)
            return 1;

        auto ds = (LPDIRECTSOUND)gGameTable.audio_pMarniSnd;
        DWORD config = (is_mono == 1) ? DSSPEAKER_MONO : DSSPEAKER_STEREO;
        return (ds->SetSpeakerConfig(config) == DS_OK) ? 1 : 0;
    }

    // 0x00433740
    static int ss_init()
    {
        if (!gGameTable.enable_dsound)
        {
            gGameTable.audio_pMarniSnd = nullptr;
            return 1;
        }

        if (DirectSoundCreate(nullptr, (LPDIRECTSOUND*)&gGameTable.audio_pMarniSnd, nullptr))
        {
            gGameTable.audio_pMarniSnd = nullptr;
            return 0;
        }

        int result = ss_set_coop_level((~gGameTable.pMarni->gpu_flag >> 10) & 1);
        if (!result)
            return result;

        ss_set_stereo_mono(gGameTable.audio_SpeakerConfig);
        memset(gGameTable.audio_BufferArms, 0, sizeof(gGameTable.audio_BufferArms));
        memset(gGameTable.audio_BufferCore, 0, sizeof(gGameTable.audio_BufferCore));
        memset(gGameTable.audio_BufferEnemy, 0, sizeof(gGameTable.audio_BufferEnemy));
        gGameTable.audio_BufferDoor[0] = 0;
        gGameTable.audio_BufferDoor[1] = 0;
        memset(gGameTable.audio_BufferRoom, 0, sizeof(gGameTable.audio_BufferRoom));
        gGameTable.audio_BufferDoor[2] = 0;
        gGameTable.audio_BufferBgm[0] = 0;
        gGameTable.audio_BufferDoor[3] = 0;
        gGameTable.audio_BufferBgm[1] = 0;
        gGameTable.audio_BufferSBgm[0] = 0;
        gGameTable.audio_BufferVoice[0] = 0;
        gGameTable.audio_BufferBgm[2] = 0;
        gGameTable.audio_BufferSBgm[1] = 0;
        gGameTable.audio_BufferVoice[1] = 0;
        return 1;
    }

    // 0x00435930
    static int ss_create_buffer(HMMIO hmmio, DWORD type, int sub)
    {
        return interop::call<int, HMMIO, DWORD, int>(0x435930, hmmio, type, sub);
    }

    // 0x00435540
    static MMRESULT ss_init_buffers(DWORD type)
    {
        auto& ss = gGameTable.ss_file_string;
        logging::logInfo("[AUDIO OPEN] {}", ss.data);
        HMMIO hmmio = mmioOpenA(ss.data, nullptr, MMIO_ALLOCBUF);
        if (!hmmio)
            return mmioClose(nullptr, 0);

        int32_t mask0, mask1;
        if (mmioRead(hmmio, (HPSTR)&mask0, 4) != 4 || mmioRead(hmmio, (HPSTR)&mask1, 4) != 4)
            return mmioClose(hmmio, 0);

        for (int i = 0; i < 32; i++)
        {
            if ((mask0 >> i) & 1)
                ss_create_buffer(hmmio, type, i);
        }
        for (int j = 0; j < 16; j++)
        {
            if ((mask1 >> j) & 1)
                ss_create_buffer(hmmio, type, j + 32);
        }

        return mmioClose(hmmio, 0);
    }

    // 0x00433f10

    // 0x00434140
    static int ss_unload_bgm(int type, int index)
    {
        using sig = int (*)(int, int);
        auto p = (sig)0x00434140;
        return p(type, index);
    }

    // 0x004341E0
    static int ss_stop_group(int type, int id)
    {
        using sig = int (*)(int, int);
        auto p = (sig)0x004341E0;
        return p(type, id);
    }

    // 0x004344A0
    static uint8_t ss_load_banks(int type, int id, int bank, int player)
    {
        using sig = int (*)(int, int, int, int);
        auto p = (sig)0x004344A0;
        return p(type, id, bank, player);
    }

    // 0x004347B0
    static int ss_get_status(int type, int sub)
    {
        using sig = int (*)(int, int);
        auto p = (sig)0x004347B0;
        return p(type, sub);
    }

    // 0x004348f0

    // 0x00434AB0
    static int ss_set_vol(int type, int index, int vol)
    {
        using sig = int (*)(int, int, int);
        auto p = (sig)0x00434AB0;
        return p(type, index, vol);
    }

    // START SND

    // 0x004EC220
    void snd_sys_init()
    {
        if (gGameTable.enable_dsound)
        {
            ss_init();
            gGameTable.cd_vol_0 = 120;
            using Snd_sys_init_sub_t = void (*)();
            auto Snd_sys_init_sub = (Snd_sys_init_sub_t)0x004EC350;
            Snd_sys_init_sub();
        }
    }

    // Stereo channel registry table (0x517468): groups of {offset, data, count}
    namespace
    {
        struct StereoEntry
        {
            int32_t offset;
            const uint8_t* data;
            int32_t count;
        };

        // Stereo channel index tables (from 0x00524F0C etc.)
        constexpr uint8_t kStereoChannels0[] = {
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
            0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D,
            0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C,
            0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B,
        };
        constexpr uint8_t kStereoChannels1[] = {
            0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
            0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
        };
        constexpr uint8_t kStereoChannels2[] = {
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
            0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
        };
        constexpr uint8_t kStereoChannels3[] = {
            0x00, 0x00, 0x03, 0x04, 0x05, 0x01, 0x02, 0x01, 0x01, 0x01, 0x01, 0x06, 0x07, 0x01, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
            0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
        };

        const StereoEntry s_stereo_config[] = {
            { 0, kStereoChannels0, 60 },
            { 2, kStereoChannels1, 35 },
            { 3, kStereoChannels2, 32 },
            { 4, kStereoChannels3, 38 },
        };
    }

    // 0x00442E60
    void snd_sys_stereo()
    {
        for (auto& entry : s_stereo_config)
        {
            using Set_registry_flg_t = void (*)(int, uint8_t);
            auto Set_registry_flg = (Set_registry_flg_t)0x00442EA0;
            for (int j = 0; j < entry.count; j++)
            {
                Set_registry_flg(entry.offset, entry.data[j]);
            }
        }
    }

    // 0x004EC250
    void snd_sys_init2()
    {
        interop::call(0x004EC250);
    }

    // 0x004ec340

    // 0x004ec350

    // 0x004EC410
    void snd_sys_init_sub2()
    {
        interop::call(0x004EC410);
    }

    // 0x004EC450
    void snd_load_core(uint8_t a0, uint8_t a1)
    {
        interop::call<void, uint8_t, uint8_t>(0x004EC450, a0, a1);
    }

    // 0x004ec6d0

    // 0x004EC7D0
    void snd_room_load()
    {
        interop::call(0x004EC7D0);
    }

    // 0x004EC8A0
    void snd_load_enemy()
    {
        interop::call(0x004EC8A0);
    }

    // 0x004ec990

    // 0x004EC9C0
    void snd_bgm_set()
    {
        interop::call(0x004EC9C0);
    }

    // 0x004ECBE0
    void snd_bgm_ck()
    {
        interop::call(0x004ECBE0);
    }

    // 0x004ECCE0
    void snd_bgm_play_ck()
    {
        interop::call(0x004ECCE0);
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

        auto buffer = (uint8_t*)(((uintptr_t)gGameTable.mem_top + 16) & 0xFFFFFFF0);
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
            = *(int*)(gGameTable.dword_6934B4 + 12) + (uint32_t)*(uint16_t*)(gGameTable.dword_6934B4 + 18) * -0x200 - 0xA20;
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

    // 0x004ed050

    // 0x004ed260

    // 0x004ED2F0
    void bgm_set_control(uint32_t arg0)
    {
        using sig = void (*)(uint32_t);
        auto p = (sig)0x004ED2F0;
        p(arg0);
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

    static MMRESULT acmDriverDetailsA_proxy(HACMDRIVERID hadid, LPACMDRIVERDETAILSA padd, DWORD fdwDetails)
    {
        return interop::stdcall<MMRESULT, HACMDRIVERID, LPACMDRIVERDETAILSA, DWORD>(0x0050C7B2, hadid, padd, fdwDetails);
    }

    // 0x004329B0
    static BOOL __stdcall acmDriverEnumCallback(HACMDRIVERID hadid, DWORD dwInstance, DWORD fdwSupport)
    {
        ACMDRIVERDETAILSA padd{};
        padd.cbStruct = sizeof(ACMDRIVERDETAILSA);

        acmDriverDetailsA_proxy(hadid, &padd, 0);

        if (dwInstance)
        {
            auto v3 = strstr(padd.szShortName, "MS-PCM");
            if (!v3)
                return TRUE;
        }
        else
        {
            auto v3 = strstr(padd.szShortName, "MS-ADPCM");
            if (!v3)
                return TRUE;
        }

        gGameTable.hadid = hadid;
        return FALSE;
    }

    void bgm_init_hooks()
    {
        interop::writeJmp(0x004329B0, &acmDriverEnumCallback);
        interop::writeJmp(0x00435540, &ss_init_buffers);
        interop::writeJmp(0x004ECDA0, snd_bgm_main);
        interop::writeJmp(0x004ED920, bgm_set_entry);
        // interop::writeJmp(0x004ED950, snd_se_on);
    }
}
