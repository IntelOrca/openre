#include "audio.h"
#include "file.h"
#include "interop.hpp"
#include "logger.h"
#include "openre.h"
#include "scheduler.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <dsound.h>
#include <malloc.h>
#include <mmsystem.h>
#include <string>
#include <windows.h>

#include <mmreg.h>
#include <msacm.h>

using namespace openre::file;

namespace openre::audio
{
    namespace
    {
        // Standalone globals (not present in GameTable): set when a BGM/SBGM
        // buffer's current position is non-zero (i.e. it has been started).
        uint32_t* dword_689DCC = (uint32_t*)0x689DCC;
        uint32_t* dword_689DD0 = (uint32_t*)0x689DD0;
        int* dword_693B30 = (int*)0x693B30;   // BGM per-slot volume cache, indexed 0..2
        int* dword_6941C8 = (int*)0x6941C8;   // SBGM per-slot volume cache, indexed 0..1
        int* dword_6941CC = (int*)0x6941CC;   // SBGM[1] volume cache

        // Standalone globals used by Snd_sys_init_sub (0x004EC350). All fall
        // in GameTable pad regions.
        uint32_t* rev_vol = (uint32_t*)0x6940A8;    // SND_VOL {int16 left; int16 right;}
        uint32_t* revd_vol = (uint32_t*)0x693350;   // SND_VOL {int16 left; int16 right;}
        uint32_t* main_vol = (uint32_t*)0x69334C;   // SND_VOL {int16 left; int16 right;}
        uint32_t* cd_vol = (uint32_t*)0x6934CC;     // SND_VOL {int16 left; int16 right;}
        int8_t* fade_rtn = (int8_t*)0x693E8C;
        int8_t* byte_693E8D = (int8_t*)0x693E8D; // byte right after fade_rtn (0x693E8C)
        int8_t* fade_time = (int8_t*)0x69346F;
        int32_t* pEdt_adr = (int32_t*)0x693480;     // 6 ints
        int32_t* dword_693B24 = (int32_t*)0x693B24; // SEQ channel table base
        uint16_t* word_693030 = (uint16_t*)0x693030;
        uint16_t* se_pri = (uint16_t*)0x693000;     // 48-byte array (24 words); loop end sentinel

        // Standalone globals used by Snd_load_core (0x004EC450). The byte
        // buffers hold the loaded core .edh data; dword_6934A0/B0 point to the
        // decoded voice/sample data inside them.
        uint8_t* byte_6DFC0C = (uint8_t*)0x6DFC0C;  // core .edh buffer 1 (pEdt_adr[0])
        uint8_t* byte_6DE21C = (uint8_t*)0x6DE21C;  // core .edh buffer 2 (pEdt_adr[4])
        int32_t* dword_6934A0 = (int32_t*)0x6934A0; // decoded core data ptr (buffer 1)
        int32_t* dword_6934B0 = (int32_t*)0x6934B0; // decoded core data ptr (buffer 2)

        // Standalone globals used by Snd_load_arms (0x004EC6D0). The byte
        // buffer holds the loaded arms .edh data; dword_6934A4 points to the
        // decoded voice/sample data inside it. Unlike the core buffers, this
        // one is used unaligned (directly, no & 0xFFFFFFF0).
        uint8_t* byte_6DEF0C = (uint8_t*)0x6DEF0C;  // arms .edh buffer
        int32_t* dword_6934A4 = (int32_t*)0x6934A4; // decoded arms data ptr

        // Standalone globals used by Snd_se_on (0x004ED950).
        int16_t* vol_3d_l = (int16_t*)0x693C44;
        int16_t* vol_3d_r = (int16_t*)0x693C46;
        int16_t* vol_3d_pan = (int16_t*)0x689DE4;

        // Standalone globals used by Snd_se_3D (0x004EE780).
        int32_t* sesz = (int32_t*)0x693818;        // SE 3D distance-scaled value
        uint16_t* word_693B3C = (uint16_t*)0x693B3C; // SE 3D direction delta

        // Standalone globals used by Snd_se_call (0x004EE350).
        int* ss_timer = (int*)0x6934C0;          // SE fade countdown timers (3 entries)
        int* ss_vol = (int*)0x693468;            // SE base volume table (indexed 0..2)
        uint32_t* dword_689DD8 = (uint32_t*)0x689DD8; // set when the BGM fade completes
        uint32_t* dword_689DDC = (uint32_t*)0x689DDC; // set when the SBGM[0] fade completes
        uint32_t* dword_689DE0 = (uint32_t*)0x689DE0; // set when the SBGM[1] fade completes

        // Standalone globals used by the Xa_* voice playback functions (0x004EEC30 etc.).
        int32_t* dword_693464 = (int32_t*)0x693464; // XA voice result / load handle
        uint8_t* byte_69346E = (uint8_t*)0x69346E;  // XA voice active flag
        uint8_t* byte_693470 = (uint8_t*)0x693470;  // XA voice active flag

        // SEQCTR / SoundVolume types used by Snd_sys_init_sub2 (0x004EC410).
        // The 3-entry SEQCTR table lives at 0x693800 with an 8-byte stride,
        // overlapping the GameTable seq_ctr / dword_693804 fields, so it is
        // addressed here via a standalone pointer.
        struct SoundVolume
        {
            int16_t left;
            int16_t right;
        };
        struct SeqCtr
        {
            int8_t flg;      // +0
            int8_t ctrl;     // +1
            int8_t seq_no;   // +2
            int8_t vab_id;   // +3
            SoundVolume vol; // +4
        };

        // Standalone globals used by Snd_sys_init_sub2 (0x004EC410). All fall
        // in GameTable pad regions.
        uint32_t* dword_693B20 = (uint32_t*)0x693B20;
        uint8_t* bgm_main = (uint8_t*)0x693498;    // Bgm.Main byte
        uint8_t* bgm_sub = (uint8_t*)0x693499;     // Bgm.Sub byte
        SeqCtr* seq_ctr_table = (SeqCtr*)0x69381C; // SEQCTR table base (3 entries)

        // Standalone globals used by Snd_room_load (0x004EC7D0).
        int32_t* dword_6934A8 = (int32_t*)0x6934A8; // room VAB data ptr (same value as rdt->offsets[1])
        uint8_t* byte_6941D0 = (uint8_t*)0x6941D0;  // room reverb level cache

        // Standalone globals used by Snd_load_em (0x004EC8A0). The byte
        // buffer holds the loaded enemy .edh data (used aligned);
        // dword_6934AC points to the decoded voice/sample data inside it.
        uint8_t* byte_6DD31C = (uint8_t*)0x6DD31C;  // enemy .edh buffer
        int32_t* dword_6934AC = (int32_t*)0x6934AC; // decoded enemy data ptr

        // ---- Constant LUTs used by SsLoadBanks (0x004344A0) ------------------------
        // All dumped verbatim from the read-only data segment of bio2 1.10.exe.

        // mainbmg_name_tbl @ 0x522470: 64 main-BGM names. Entries 50 and 58 are
        // invalid (the original pointers reference a 0xFFFFFFFF sentinel at 0x669F4C).
        constexpr const char* kMainBgmNameTbl[] = {
            "main00_1", "main01", "main02", "main03", "main04", "main05", "main06", "main07",
            "main08",   "main09", "main0a", "main00", "main0c", "main0d", "main0e", "main0f",
            "main10",   "main11", "main12", "main13", "main14", "main15", "main16", "main17",
            "main18",   "main19", "main1a", "main1b", "main1c", "main1d", "main1e", "main1f",
            "main20",   "main21", "main22", "main23", "main24", "main25", "main26", "main27",
            "main28",   "main29", "main2a", "main2b", "main2c", "main2d", "main2e", "main2f",
            "main30",   "main31", nullptr, "main33", "main34", "main35", "main36", "main37",
            "main38",   "main39", nullptr, "main00_2", "main04_1", "main0f_1", "main14_1", "main1b_1",
        };

        // subbgm_name_tbl @ 0x522628: 70 sub-BGM names. Entries 25 and 37 are invalid
        // (0xFFFFFFFF sentinel at 0x669F4C); entry 69 is NULL.
        constexpr const char* kSubBgmNameTbl[] = {
            "sub00",    "sub01",  "sub02",  "sub03",  "sub04",  "sub05",  "sub2f_1", "main16",
            "main04_1", "sub09",  "sub0a",  "sub0b",  "sub0c",  "sub0d",  "sub0e",   "sub0f",
            "sub10",    "sub11",  "sub12",  "sub13",  "sub14",  "sub15",  "sub16",   "sub17",
            "sub18",    nullptr,  "sub1a",  "sub1b",  "sub1c",  "main0f", "sub1e",   "sub1f",
            "sub20",    "sub21",  "sub22",  "sub23",  "sub24",  nullptr,  "sub26",   "sub27",
            "sub28",    "sub29",  "sub2a",  "main14", "main1b", "sub2d",  "sub2e",   "sub2f",
            "sub30",    "main0a", "sub32",  "sub33",  "sub34",  "sub35",  "sub36",   "sub37",
            "sub36",    "sub39",  "sub3a",  "sub3b",  "main12", "sub20",  "sub3e",   "sub3f",
            "sub40",    "sub41",  "sub42",  "sub43",  "sub44",  nullptr,
        };

        // fs_name_tbl @ 0x522130: 54 footstep-set names.
        constexpr const char* kFsNameTbl[] = {
            "fs00", "fs01", "fs02", "fs03", "fs04", "fs05", "fs06", "fs07", "fs08", "fs09",
            "fs10", "fs11", "fs12", "fs13", "fs14", "fs15", "fs16", "fs17", "fs18", "fs19",
            "fs20", "fs21", "fs22", "fs23", "fs24", "fs25", "fs26", "fs27", "fs28", "fs29",
            "fs30", "fs31", "fs32", "fs33", "fs34", "fs35", "fs36", "fs37", "fs38", "fs39",
            "fs40", "fs41", "fs42", "fs43", "fs44", "fs45", "fs46", "fs47", "fs48", "fs49",
            "fs50", "fs51", "fs52", "fs53",
        };

        // bgm_lut @ 0x522570: 61 main-BGM LUT entries (one per BGM slot), each 3
        // signed bytes (BGM_MAIN_TBL) indexing kMainBgmNameTbl, or -1.
        struct MainBgmLutEntry
        {
            int8_t field_0;
            int8_t field_1;
            int8_t field_2;
        };
        constexpr MainBgmLutEntry kMainBgmLut[] = {
            {0, 11, 59}, {1, -1, -1}, {2, -1, -1}, {3, -1, -1}, {4, 60, -1}, {5, -1, -1},
            {6, -1, -1}, {7, -1, -1}, {8, -1, -1}, {-1, -1, -1}, {10, -1, -1}, {0, 59, -1},
            {12, -1, -1}, {13, -1, -1}, {14, -1, -1}, {15, 61, -1}, {16, -1, -1}, {17, -1, -1},
            {18, -1, -1}, {19, -1, -1}, {20, 62, -1}, {21, -1, -1}, {22, -1, -1}, {23, -1, -1},
            {24, -1, -1}, {25, -1, -1}, {26, -1, -1}, {27, -1, -1}, {28, -1, -1}, {29, -1, -1},
            {30, -1, -1}, {31, -1, -1}, {32, -1, -1}, {33, -1, -1}, {34, -1, -1}, {35, -1, -1},
            {36, -1, -1}, {37, -1, -1}, {38, -1, -1}, {39, -1, -1}, {40, -1, -1}, {41, -1, -1},
            {42, -1, -1}, {43, -1, -1}, {44, -1, -1}, {45, -1, -1}, {46, -1, -1}, {47, -1, -1},
            {48, -1, -1}, {49, -1, -1}, {50, -1, -1}, {51, -1, -1}, {52, -1, -1}, {53, -1, -1},
            {54, -1, -1}, {55, -1, -1}, {56, -1, -1}, {57, -1, -1}, {58, -1, -1}, {0, 0, 0},
            {0, 0, 0},
        };

        // sbgm_lut @ 0x522740: 56 sub-BGM LUT entries (one per SBGM slot), each 3
        // signed bytes (BGM_LUT) indexing kSubBgmNameTbl, or -1.
        struct SubBgmLutEntry
        {
            int8_t field_0[3];
        };
        constexpr SubBgmLutEntry kSubBgmLut[] = {
            {{0, -1, -1}}, {{1, -1, -1}}, {{2, -1, -1}}, {{3, -1, -1}}, {{4, -1, -1}}, {{5, -1, -1}},
            {{6, -1, -1}}, {{7, -1, -1}}, {{8, 9, -1}}, {{10, 11, -1}}, {{10, 12, -1}}, {{13, -1, -1}},
            {{14, -1, -1}}, {{15, -1, -1}}, {{16, -1, -1}}, {{17, 18, -1}}, {{19, -1, -1}}, {{20, 21, -1}},
            {{22, 23, -1}}, {{20, 24, -1}}, {{26, -1, -1}}, {{27, -1, -1}}, {{32, 28, 29}}, {{35, 43, -1}},
            {{30, 31, -1}}, {{32, 33, -1}}, {{30, 34, -1}}, {{29, -1, -1}}, {{36, -1, -1}}, {{37, -1, -1}},
            {{38, -1, -1}}, {{40, -1, -1}}, {{39, 46, -1}}, {{41, -1, -1}}, {{42, -1, -1}}, {{44, -1, -1}},
            {{45, -1, -1}}, {{47, 48, 6}}, {{49, -1, -1}}, {{50, -1, -1}}, {{50, 51, -1}}, {{50, 52, -1}},
            {{56, 53, -1}}, {{54, -1, -1}}, {{62, -1, -1}}, {{57, -1, -1}}, {{63, -1, -1}}, {{59, 55, -1}},
            {{60, -1, -1}}, {{58, -1, -1}}, {{20, 64, -1}}, {{65, 66, -1}}, {{67, -1, -1}}, {{68, -1, -1}},
            {{37, 11, -1}}, {{0, 0, 0}},
        };

        // footstep_tbl @ 0x522208: 205 footstep entries (FOOTSTEP_TBL), indexed as
        // [Id * 29 + Bank] for room Id and sound bank 0..2. Each byte is an index
        // into kFsNameTbl, or -1.
        struct FootstepTblEntry
        {
            int8_t field_0;
            int8_t field_1;
            int8_t field_2;
        };
        constexpr FootstepTblEntry kFootstepTbl[] = {
            {0, 1, 2}, {-1, -1, 3}, {-1, 4, 2}, {5, 6, 7}, {-1, 1, 2}, {-1, 8, 2},
            {-1, 9, 10}, {-1, 11, 7}, {-1, 12, 10}, {-1, 14, 13}, {-1, 3, 18}, {-1, 15, 16},
            {17, 16, 18}, {-1, 19, 3}, {-1, 16, 3}, {-1, -1, 16}, {-1, -1, 20}, {-1, 17, 21},
            {-1, 17, 18}, {-1, -1, 22}, {-1, 23, 21}, {-1, 24, 25}, {12, 11, 7}, {-1, -1, 20},
            {26, 27, 28}, {-1, -1, 2}, {-1, 23, 21}, {-1, 27, 2}, {-1, 12, 10}, {20, 29, 29},
            {-1, -1, 25}, {-1, 30, 25}, {-1, 32, 31}, {-1, 30, 25}, {-1, 36, 33}, {18, 37, 25},
            {-1, -1, 28}, {-1, 31, 3}, {-1, -1, 33}, {-1, -1, 38}, {-1, 12, 25}, {-1, 15, 16},
            {39, 21, 10}, {-1, -1, 40}, {-1, 41, 40}, {-1, -1, 40}, {-1, -1, 28}, {-1, -1, 28},
            {-1, 42, 28}, {-1, -1, 28}, {-1, -1, 28}, {-1, -1, 28}, {-1, -1, -1}, {-1, -1, -1},
            {-1, -1, 28}, {0, 3, 33}, {-1, 16, 3}, {-1, -1, -1}, {-1, -1, 28}, {-1, -1, 28},
            {-1, 49, 28}, {-1, 42, 28}, {42, 49, 28}, {7, 42, 28}, {42, 50, 28}, {42, 28, 6},
            {-1, -1, 28}, {-1, -1, 26}, {0, 49, 28}, {-1, -1, 28}, {-1, -1, 28}, {-1, -1, 28},
            {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
            {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
            {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, 49, 28}, {-1, 42, 28}, {-1, 49, 28},
            {-1, 42, 28}, {-1, 51, 42}, {-1, 52, 28}, {-1, -1, -1}, {-1, 52, 28}, {42, -1, 28},
            {42, -1, 28}, {-1, 42, 28}, {-1, -1, 28}, {-1, 28, 42}, {-1, 53, 26}, {28, 53, 26},
            {-1, -1, 28}, {-1, 52, 28}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
            {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
            {-1, -1, -1}, {-1, -1, -1}, {-1, -1, 34}, {-1, -1, 34}, {-1, 42, 34}, {-1, -1, 35},
            {-1, 42, 34}, {-1, -1, 42}, {-1, 42, 34}, {-1, 42, 26}, {-1, 42, 34}, {-1, -1, 42},
            {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
            {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
            {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
            {-1, -1, -1}, {-1, -1, 42}, {-1, -1, 43}, {26, 42, 28}, {-1, -1, 42}, {26, 44, -1},
            {26, 44, 42}, {-1, 42, 34}, {-1, -1, 35}, {-1, 26, 45}, {-1, -1, 43}, {-1, -1, 43},
            {-1, -1, 28}, {-1, 44, 28}, {-1, -1, 46}, {-1, 26, -1}, {36, 0, 34}, {-1, -1, 28},
            {-1, 26, 45}, {-1, -1, 47}, {-1, -1, 48}, {26, 42, 28}, {-1, 26, 28}, {28, 42, 47},
            {-1, 26, 28}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
            {26, 42, 28}, {12, 42, 28}, {-1, 0, 28}, {26, 42, 28}, {-1, 42, 28}, {-1, -1, -1},
            {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
            {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
            {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1},
            {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {0, 0, 0},
            {0, 0, 0},
        };

        // Releases a memory block previously obtained from GlobalAlloc/GlobalLock.
        // Matches the original double GlobalHandle()/GlobalUnlock/GlobalFree idiom.
        void free_hglobal_pointer(void* p)
        {
            if (p == nullptr)
                return;
            HGLOBAL h = ::GlobalHandle(p);
            ::GlobalUnlock(h);
            h = ::GlobalHandle(p);
            ::GlobalFree(h);
        }
    }

    // Forward declarations of functions defined later in this file.
    static LPDIRECTSOUNDBUFFER ss_get_status(int type, int sub);
    static int ss_stop_all();
    static int ss_stop_group(int type, int id);
    static int ss_shutdown();
    static int ss_load_sap(DWORD type, int id, int bank, int player);
    static int ss_load_steps(const char* name, int a2);
    static int ss_load_bgm(const char* name, DWORD type, int sample);
    static BOOL CALLBACK acmDriverEnumCallback(HACMDRIVERID hadid, DWORD_PTR dwInstance, DWORD fdwSupport);

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

    // 0x00433830
    static int ss_close()
    {
        if (!gGameTable.audio_pMarniSnd)
            return 1;

        int result = ss_stop_all();
        if (result)
        {
            result = ss_shutdown();
            if (result)
            {
                auto ds = (LPDIRECTSOUND)gGameTable.audio_pMarniSnd;
                if (ds->Release())
                    return 0;
                gGameTable.audio_pMarniSnd = nullptr;
                return 1;
            }
        }
        return result;
    }

    // 0x00433C40
    static int ss_stop_all()
    {
        if (!gGameTable.audio_pMarniSnd)
            return 1;

        auto is_playing = [](uint32_t buf) {
            auto pDSB = (LPDIRECTSOUNDBUFFER)buf;
            DWORD status = 0;
            pDSB->GetStatus(&status);
            return (status & DSBSTATUS_PLAYING) != 0;
        };

        // BufferArms [32] — return 0 as soon as a playing buffer is found.
        for (int i = 0; i < 32; i++)
        {
            if (gGameTable.audio_BufferArms[i] && is_playing(gGameTable.audio_BufferArms[i]))
                return 0;
        }
        // BufferCore [22]
        for (int i = 0; i < 22; i++)
        {
            if (gGameTable.audio_BufferCore[i] && is_playing(gGameTable.audio_BufferCore[i]))
                return 0;
        }
        // BufferDoor [4]
        for (int i = 0; i < 4; i++)
        {
            if (gGameTable.audio_BufferDoor[i] && is_playing(gGameTable.audio_BufferDoor[i]))
                return 0;
        }
        // BufferEnemy [32]
        for (int i = 0; i < 32; i++)
        {
            if (gGameTable.audio_BufferEnemy[i] && is_playing(gGameTable.audio_BufferEnemy[i]))
                return 0;
        }
        // BufferRoom [48]
        for (int i = 0; i < 48; i++)
        {
            if (gGameTable.audio_BufferRoom[i] && is_playing(gGameTable.audio_BufferRoom[i]))
                return 0;
        }
        // BufferBgm [3] — also flag non-zero current positions.
        for (int i = 0; i < 3; i++)
        {
            auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferBgm[i];
            if (pDSB)
            {
                DWORD v10 = 0;
                pDSB->GetCurrentPosition((LPDWORD)&v10, nullptr);
                if (v10)
                    *dword_689DCC = 1;
                if (is_playing(gGameTable.audio_BufferBgm[i]))
                    return 0;
            }
        }
        // BufferSBgm [2]
        for (int i = 0; i < 2; i++)
        {
            auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferSBgm[i];
            if (pDSB)
            {
                DWORD v10 = 0;
                pDSB->GetCurrentPosition((LPDWORD)&v10, nullptr);
                if (v10)
                    dword_689DD0[i] = 1;
                if (is_playing(gGameTable.audio_BufferSBgm[i]))
                    return 0;
            }
        }
        // BufferVoice [2]
        for (int i = 0; i < 2; i++)
        {
            if (gGameTable.audio_BufferVoice[i] && is_playing(gGameTable.audio_BufferVoice[i]))
                return 0;
        }
        return 1;
    }

    // 0x00433DC0
    static int ss_shutdown()
    {
        if (!gGameTable.audio_pMarniSnd)
            return 1;

        if (!ss_stop_all())
            return 0;

        // BufferArms [32] — ends at BufferVoice.
        for (int i = 0; i < 32; i++)
        {
            if (gGameTable.audio_BufferArms[i])
            {
                auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferArms[i];
                if (pDSB->Release())
                    return 0;
                gGameTable.audio_BufferArms[i] = 0;
            }
        }
        // BufferCore [22] — ends at BufferArms.
        for (int i = 0; i < 22; i++)
        {
            if (gGameTable.audio_BufferCore[i])
            {
                auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferCore[i];
                if (pDSB->Release())
                    return 0;
                gGameTable.audio_BufferCore[i] = 0;
            }
        }
        // BufferDoor [4] — ends at BufferEnemy.
        for (int i = 0; i < 4; i++)
        {
            if (gGameTable.audio_BufferDoor[i])
            {
                auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferDoor[i];
                if (pDSB->Release())
                    return 0;
                gGameTable.audio_BufferDoor[i] = 0;
            }
        }
        // BufferEnemy [32] — ends at BufferBgm.
        for (int i = 0; i < 32; i++)
        {
            if (gGameTable.audio_BufferEnemy[i])
            {
                auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferEnemy[i];
                if (pDSB->Release())
                    return 0;
                gGameTable.audio_BufferEnemy[i] = 0;
            }
        }
        // BufferRoom [48] — ends at BufferCore.
        for (int i = 0; i < 48; i++)
        {
            if (gGameTable.audio_BufferRoom[i])
            {
                auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferRoom[i];
                if (pDSB->Release())
                    return 0;
                gGameTable.audio_BufferRoom[i] = 0;
            }
        }
        // BufferBgm [3] — ends at MarniSnd_Frequency.
        for (int i = 0; i < 3; i++)
        {
            if (gGameTable.audio_BufferBgm[i])
            {
                auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferBgm[i];
                if (pDSB->Release())
                    return 0;
                gGameTable.audio_BufferBgm[i] = 0;
            }
        }
        // BufferSBgm [2] — ends at SpeakerConfig.
        for (int i = 0; i < 2; i++)
        {
            if (gGameTable.audio_BufferSBgm[i])
            {
                auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferSBgm[i];
                if (pDSB->Release())
                    return 0;
                gGameTable.audio_BufferSBgm[i] = 0;
            }
        }
        // BufferVoice [2] — ends at MarniSnd_SoundDepth.
        for (int i = 0; i < 2; i++)
        {
            if (gGameTable.audio_BufferVoice[i])
            {
                auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferVoice[i];
                if (pDSB->Release())
                    return 0;
                gGameTable.audio_BufferVoice[i] = 0;
            }
        }
        return 1;
    }

    // 0x004EF0D0
    static int room_ck_room70a()
    {
        using sig = int (*)();
        auto p = (sig)0x004EF0D0;
        return p();
    }

    // 0x004EEF30
    static int bgm_ck_room112()
    {
        using sig = int (*)();
        auto p = (sig)0x004EEF30;
        return p();
    }

    // 0x004EEF50
    static int bgm_ck_room115()
    {
        using sig = int (*)();
        auto p = (sig)0x004EEF50;
        return p();
    }

    // 0x004338F0
    static void ss_play(int type, int id, int dwFlags)
    {
        if (!gGameTable.audio_pMarniSnd)
            return;

        switch (type)
        {
        case 0: // door (0..3)
        {
            if ((unsigned int)id < 4)
            {
                auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferDoor[id];
                if (pDSB)
                {
                    // GetStatus writes the DirectSound status bits back into `id`;
                    // bit 0 (DSBSTATUS_PLAYING) decides whether to restart the buffer.
                    pDSB->GetStatus((LPDWORD)&id);
                    if ((id & 1) == 0 || (!pDSB->Stop() && !pDSB->SetCurrentPosition(0)))
                        pDSB->Play(0, 0, dwFlags);
                }
            }
            break;
        }
        case 1: // arms (0..0x1F)
        {
            if ((unsigned int)id < 0x20)
            {
                auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferArms[id];
                if (pDSB)
                {
                    pDSB->GetStatus((LPDWORD)&id);
                    if ((id & 1) == 0 || (!pDSB->Stop() && !pDSB->SetCurrentPosition(0)))
                        pDSB->Play(0, 0, dwFlags);
                }
            }
            break;
        }
        case 2: // room (0..0x2F)
        {
            int v7 = id;
            if ((unsigned int)id < 0x30)
            {
                if (room_ck_room70a())
                {
                    if (v7 < 34)
                        v7 += 6;
                    if (v7 == 17)
                        ss_play(2, 12, 0);
                }
                auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferRoom[v7];
                if (pDSB)
                {
                    pDSB->GetStatus((LPDWORD)&id);
                    if ((id & 1) == 0 || (!pDSB->Stop() && !pDSB->SetCurrentPosition(0)))
                        pDSB->Play(0, 0, dwFlags);
                }
            }
            break;
        }
        case 3: // enemy (0..0x1F)
        {
            if ((unsigned int)id < 0x20)
            {
                auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferEnemy[id];
                if (pDSB)
                {
                    pDSB->GetStatus((LPDWORD)&id);
                    if ((id & 1) == 0 || (!pDSB->Stop() && !pDSB->SetCurrentPosition(0)))
                        pDSB->Play(0, 0, dwFlags);
                }
                else
                {
                    bgm_ck_room112();
                }
            }
            break;
        }
        case 4: // core (0..0x15)
        {
            if ((unsigned int)id <= 0x15)
            {
                auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferCore[id];
                if (pDSB)
                {
                    pDSB->GetStatus((LPDWORD)&id);
                    if ((id & 1) == 0 || (!pDSB->Stop() && !pDSB->SetCurrentPosition(0)))
                        pDSB->Play(0, 0, dwFlags);
                }
            }
            break;
        }
        case 5: // bgm (0..2)
        {
            if ((unsigned int)id <= 2)
            {
                auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferBgm[id];
                if (pDSB)
                    pDSB->Play(0, 0, dwFlags);
            }
            break;
        }
        case 6: // sbgm (0..1)
        {
            if ((unsigned int)id <= 1)
            {
                auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferSBgm[id];
                if (pDSB)
                    pDSB->Play(0, 0, dwFlags);
            }
            break;
        }
        case 7: // voice (XA_idx)
        {
            auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferVoice[gGameTable.XA_idx];
            if (pDSB)
                pDSB->Play(0, 0, 0);
            break;
        }
        default:
            return;
        }
    }

    // 0x00435930
    static int ss_create_buffer(HMMIO hmmio, DWORD type, DWORD sub)
    {
        if (!gGameTable.audio_pMarniSnd)
            return 1;

        uint32_t* pbuffer = nullptr;
        void* readBuffer = nullptr;   // intermediate buffer holding compressed source data
        BYTE* decompressed = nullptr; // stage 1 (ADPCM->PCM) decoded audio buffer
        void* resampled = nullptr;    // stage 2 (resample to target) output buffer
        HACMSTREAM phas = nullptr;    // stage 1 ACM stream handle
        HACMSTREAM has = nullptr;     // stage 2 ACM stream handle

        // Select the DirectSoundBuffer slot for this (type, sub).
        switch (type)
        {
        case 0:
            if (sub >= 4)
                return 0;
            pbuffer = &gGameTable.audio_BufferDoor[sub];
            break;
        case 1:
            if (sub >= 0x20)
                return 0;
            pbuffer = &gGameTable.audio_BufferArms[sub];
            break;
        case 2:
            if (sub >= 0x30)
                return 0;
            pbuffer = &gGameTable.audio_BufferRoom[sub];
            break;
        case 3:
            if (sub >= 0x20)
                return 0;
            pbuffer = &gGameTable.audio_BufferEnemy[sub];
            break;
        case 4:
            if (sub > 0x15)
                return 0;
            pbuffer = &gGameTable.audio_BufferCore[sub];
            break;
        case 5:
            if (sub > 2)
                return 0;
            pbuffer = &gGameTable.audio_BufferBgm[sub];
            break;
        case 6:
            if (sub > 1)
                return 0;
            pbuffer = &gGameTable.audio_BufferSBgm[sub];
            break;
        case 7:
        {
            if (sub > 1)
                return 0;
            int v4 = 0;
            for (int i = 0; i < 2; i++)
                v4 += (int)ss_get_status(7, i) << i;
            switch (v4)
            {
            case 3: gGameTable.XA_idx = gGameTable.XA_idx == 0; break;
            case 0:
            case 2: gGameTable.XA_idx = 0; break;
            case 1: gGameTable.XA_idx = 1; break;
            }
            pbuffer = &gGameTable.audio_BufferVoice[gGameTable.XA_idx];
            break;
        }
        default:
            // Original falls through here with pbuffer == nullptr (degenerate case
            // the game never triggers; subsequent DirectSound calls would crash).
            break;
        }

        // Alias references to the global ACM driver handles so writes go straight
        // into the GameTable slots, matching the original `&had`/`&phad` usage.
        auto had_ptr = reinterpret_cast<HACMDRIVER*>(&gGameTable.had);
        auto phad_ptr = reinterpret_cast<HACMDRIVER*>(&gGameTable.phad);

        MMCKINFO ckwav{};
        MMCKINFO ckdata{};

        ckwav.fccType = mmioFOURCC('W', 'A', 'V', 'E');
        if (mmioDescend(hmmio, &ckwav, nullptr, MMIO_FINDRIFF))
            return 0; // no WAVE chunk; nothing allocated yet, so just bail
        if (mmioDescend(hmmio, &ckdata, &ckwav, 0))
        {
            // WAVE chunk empty (no sub-chunks); nothing to decode.
            mmioAscend(hmmio, &ckwav, 0);
            return 1;
        }

        LPWAVEFORMATEX pwfxSrc = nullptr;           // set when a 'fmt ' chunk is parsed
        DWORD finalBytes = 0;                       // size used for lock/create
        const WAVEFORMATEX* fmtForBuffer = nullptr; // pwfx passed to CreateSoundBuffer
        const void* audioForBuffer = nullptr;       // source audio buffer for the lock copy
        bool needResample = false;

        do
        {
            DWORD cksize = ckdata.cksize;

            // Bounds check: chunk must not extend past the WAVE chunk's end.
            if (ckdata.cksize + ckdata.dwDataOffset > ckwav.dwDataOffset + ckwav.cksize)
                goto fail_cleanup;

            if (ckdata.ckid == mmioFOURCC('f', 'm', 't', ' '))
            {
                // Store the source WAVEFORMATEX into a stack-allocated scratch
                // area large enough to hold the chunk (rounded up to a 4-byte
                // multiple to keep esp aligned).
                size_t aligned = (cksize + 3) & ~size_t{ 3 };
                pwfxSrc = (LPWAVEFORMATEX)_alloca(aligned);
                if (mmioRead(hmmio, (HPSTR)pwfxSrc, cksize) != (LONG)cksize)
                    goto fail_cleanup;
            }
            else if (ckdata.ckid == mmioFOURCC('d', 'a', 't', 'a'))
            {
                // (1) Discover the appropriate ACM driver for this source format:
                //     MS-ADPCM for compressed streams (wFormatTag != 1),
                //     MS-PCM for already-decoded PCM streams (wFormatTag == 1).
                bool want_pcm = (pwfxSrc != nullptr && pwfxSrc->wFormatTag == WAVE_FORMAT_PCM);
                acmDriverEnum(acmDriverEnumCallback, want_pcm ? 1u : 0u, 0);
                if (acmDriverOpen(had_ptr, reinterpret_cast<HACMDRIVERID>(gGameTable.hadid), 0))
                {
                    MessageBoxA(0, "Error. OpenDriver.", 0, 0);
                    return 0;
                }

                // (2) Ask the driver for the maximum destination-format size so we
                //     can allocate a scratch WAVEFORMATEX to receive its suggestion.
                DWORD pMetric = 0;
                acmMetrics(nullptr, ACM_METRIC_MAX_SIZE_FORMAT, &pMetric);
                size_t alignedMetric = (size_t)((pMetric + 3) & ~3u);

                // (3) Stage 1: build a PCM destination format biased towards the
                //     target depth and let acmFormatSuggest fill the rest in.
                auto wfx1 = (LPWAVEFORMATEX)_alloca(alignedMetric);
                wfx1->wFormatTag = WAVE_FORMAT_PCM;
                wfx1->cbSize = 0;
                wfx1->wBitsPerSample = gGameTable.MarniSnd_SoundDepth;
                if (acmFormatSuggest(*had_ptr, pwfxSrc, wfx1, pMetric, 0x90000u))
                    goto fail_cleanup;
                if (acmStreamOpen(&phas, *had_ptr, pwfxSrc, wfx1, nullptr, 0, 0, 0))
                {
                    MessageBoxA(0, "StreamOpen Error.", 0, 0);
                    goto fail_cleanup;
                }

                // (4) Determine the size of the decompressed output buffer.
                DWORD decodedSize = 0;
                if (acmStreamSize(phas, cksize, &decodedSize, 0))
                {
                    MessageBoxA(0, "StreamSize Error.", 0, 0);
                    goto fail_cleanup;
                }

                // (5) Decide whether a second-stage resample is needed: if the
                //     suggested PCM rate differs from MarniSnd's target rate, or
                //     the target channel count (clamped to SpeakerConfig) is below
                //     the source's, we need another PCM->PCM conversion pass.
                WORD suggestedChannels = wfx1->nChannels;
                DWORD targetChannels = (DWORD)gGameTable.audio_SpeakerConfig;
                if (targetChannels > suggestedChannels)
                    targetChannels = suggestedChannels;
                if ((DWORD)gGameTable.MarniSnd_Frequency != wfx1->nSamplesPerSec || targetChannels != suggestedChannels)
                    needResample = true;

                // (6) Allocate source (compressed) and destination (decompressed)
                //     buffers via GlobalAlloc.
                HGLOBAL hg = GlobalAlloc(0x42, cksize);
                void* srcPtr = GlobalLock(hg);
                readBuffer = srcPtr;

                hg = GlobalAlloc(0x42, decodedSize);
                decompressed = (BYTE*)GlobalLock(hg);

                // (7) Configure and run the stage 1 ACM stream conversion.
                ACMSTREAMHEADER ash1{};
                ash1.cbStruct = sizeof(ash1);
                ash1.fdwStatus = 0x10000;
                ash1.pbSrc = (LPBYTE)srcPtr;
                ash1.cbSrcLength = cksize;
                ash1.dwSrcUser = cksize;
                ash1.pbDst = decompressed;
                ash1.cbDstLength = decodedSize;
                ash1.dwDstUser = decodedSize;

                acmStreamPrepareHeader(phas, &ash1, 0);
                mmioRead(hmmio, (HPSTR)srcPtr, cksize);
                if (acmStreamConvert(phas, &ash1, ACM_STREAMCONVERTF_BLOCKALIGN))
                {
                    acmStreamUnprepareHeader(phas, &ash1, 0);
                    goto fail_cleanup;
                }

                decodedSize = ash1.cbDstLengthUsed;
                free_hglobal_pointer(srcPtr);
                readBuffer = nullptr;
                acmStreamUnprepareHeader(phas, &ash1, 0);

                // (8) Optional stage 2: resample/channel-convert to the target PCM
                //     format (MarniSnd_Frequency and target channels).
                if (needResample)
                {
                    acmDriverEnum(acmDriverEnumCallback, 1u, 0);
                    if (acmDriverOpen(phad_ptr, reinterpret_cast<HACMDRIVERID>(gGameTable.hadid), 0))
                    {
                        MessageBoxA(0, "Error. OpenDriver.", 0, 0);
                        return 0;
                    }

                    auto wfx2 = (LPWAVEFORMATEX)_alloca(alignedMetric);
                    wfx2->wFormatTag = WAVE_FORMAT_PCM;
                    wfx2->cbSize = 0;
                    wfx2->wBitsPerSample = gGameTable.MarniSnd_SoundDepth;
                    wfx2->nSamplesPerSec = (DWORD)gGameTable.MarniSnd_Frequency;
                    wfx2->nChannels = (WORD)targetChannels;
                    if (acmFormatSuggest(*phad_ptr, wfx1, wfx2, pMetric, 0xF0000u))
                        goto fail_cleanup;
                    if (acmStreamOpen(&has, nullptr, wfx1, wfx2, nullptr, 0, 0, 0))
                    {
                        MessageBoxA(0, "StreamOpen Error.", 0, 0);
                        goto fail_cleanup;
                    }

                    DWORD resampledSize = 0;
                    if (acmStreamSize(has, decodedSize, &resampledSize, 0))
                    {
                        MessageBoxA(0, "StreamSize Error.", 0, 0);
                        goto fail_cleanup;
                    }

                    hg = GlobalAlloc(0x42, resampledSize);
                    auto dst = (BYTE*)GlobalLock(hg);
                    resampled = dst;

                    ACMSTREAMHEADER ash2{};
                    ash2.cbStruct = sizeof(ash2);
                    ash2.fdwStatus = 0x10000;
                    ash2.pbSrc = decompressed;
                    ash2.cbSrcLength = decodedSize;
                    ash2.dwSrcUser = decodedSize;
                    ash2.pbDst = dst;
                    ash2.cbDstLength = resampledSize;
                    ash2.dwDstUser = resampledSize;
                    acmStreamPrepareHeader(has, &ash2, 0);
                    if (acmStreamConvert(has, &ash2, ACM_STREAMCONVERTF_BLOCKALIGN))
                    {
                        acmStreamUnprepareHeader(has, &ash2, 0);
                        goto fail_cleanup;
                    }
                    resampledSize = ash2.cbDstLengthUsed;
                    free_hglobal_pointer(decompressed);
                    decompressed = nullptr;
                    acmStreamUnprepareHeader(has, &ash2, 0);

                    finalBytes = resampledSize;
                    fmtForBuffer = wfx2;
                    audioForBuffer = resampled;
                }
                else
                {
                    finalBytes = decodedSize;
                    fmtForBuffer = wfx1;
                    audioForBuffer = decompressed;
                }

                // (9) Create the DirectSound buffer using the chosen PCM format.
                DSBUFFERDESC ddesc{};
                ddesc.dwSize = sizeof(DSBUFFERDESC);
                ddesc.dwFlags = 0xE0; // CTRLFREQUENCY | CTRLPAN | CTRLVOLUME
                ddesc.dwBufferBytes = finalBytes;
                ddesc.lpwfxFormat = const_cast<LPWAVEFORMATEX>(fmtForBuffer);

                auto ds = (LPDIRECTSOUND)gGameTable.audio_pMarniSnd;
                if (ds->CreateSoundBuffer(&ddesc, (LPDIRECTSOUNDBUFFER*)pbuffer, nullptr))
                {
                    MessageBoxA(0, "CreateSoundBuffer Error.", 0, 0);
                    goto fail_cleanup;
                }

                // (10) Lock the buffer and copy the PCM data into DirectSound,
                //      restoring-and-retrying once on DSERR_BUFFERLOST before finally
                //      bailing out if the second attempt also loses the buffer.
                auto pDSB = (LPDIRECTSOUNDBUFFER)*pbuffer;
                LPVOID audioPtr1 = nullptr;
                LPVOID audioPtr2 = nullptr;
                DWORD audioBytes1 = 0;
                DWORD audioBytes2 = 0;

                HRESULT hr = pDSB->Lock(0, finalBytes, &audioPtr1, &audioBytes1, &audioPtr2, &audioBytes2, 0);
                if (hr == DSERR_BUFFERLOST)
                {
                    pDSB->Restore();
                    hr = pDSB->Lock(0, finalBytes, &audioPtr1, &audioBytes1, &audioPtr2, &audioBytes2, 0);
                    if (hr == DSERR_BUFFERLOST)
                        goto fail_cleanup;
                }
                if (hr == DS_OK)
                {
                    auto src = (const BYTE*)audioForBuffer;
                    if (audioPtr1 != nullptr && audioBytes1 != 0)
                        memcpy(audioPtr1, src, audioBytes1);
                    if (audioPtr2 != nullptr && audioBytes2 != 0)
                        memcpy(audioPtr2, src + audioBytes1, audioBytes2);
                }
                if (pDSB->Unlock(audioPtr1, audioBytes1, audioPtr2, audioBytes2))
                    goto fail_cleanup;

                // (11) Free intermediate source buffers that are no longer needed.
                if (needResample)
                {
                    free_hglobal_pointer(resampled);
                    resampled = nullptr;
                }
                else
                {
                    free_hglobal_pointer(decompressed);
                    decompressed = nullptr;
                }
            }

            mmioAscend(hmmio, &ckdata, 0);

            // Close any streams/drivers used for this chunk so the globals stay
            // balanced across iterations; the next chunk will reopen as needed.
            if (phas)
            {
                acmStreamClose(phas, 0);
                phas = nullptr;
            }
            if (needResample && has)
            {
                acmStreamClose(has, 0);
                has = nullptr;
            }
            if (*had_ptr)
            {
                acmDriverClose(*had_ptr, 0);
                *had_ptr = nullptr;
            }
            if (needResample && *phad_ptr)
            {
                acmDriverClose(*phad_ptr, 0);
                *phad_ptr = nullptr;
            }

        } while (!mmioDescend(hmmio, &ckdata, &ckwav, 0));

        // Success: ascend the WAVE chunk, free any remaining intermediates.
        mmioAscend(hmmio, &ckwav, 0);
        free_hglobal_pointer(readBuffer);
        free_hglobal_pointer(decompressed);
        free_hglobal_pointer(resampled);
        return 1;

    fail_cleanup:
        mmioAscend(hmmio, &ckdata, 0);
        mmioAscend(hmmio, &ckwav, 0);
        if (*had_ptr)
        {
            acmDriverClose(*had_ptr, 0);
            *had_ptr = nullptr;
        }
        if (*phad_ptr)
        {
            acmDriverClose(*phad_ptr, 0);
            *phad_ptr = nullptr;
        }
        if (pbuffer && (LPDIRECTSOUNDBUFFER)*pbuffer)
        {
            auto pDSB = (LPDIRECTSOUNDBUFFER)*pbuffer;
            pDSB->Release();
            *pbuffer = 0;
        }
        free_hglobal_pointer(readBuffer);
        free_hglobal_pointer(decompressed);
        free_hglobal_pointer(resampled);
        return 0;
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

    // 0x00433F10
    static int ss_unload_group(int type)
    {
        if (!gGameTable.audio_pMarniSnd)
            return 1;

        switch (type)
        {
        case 0: // door — ends at BufferEnemy.
            if (!ss_stop_group(0, -1))
                return 0;
            // BufferDoor [4]
            for (int i = 0; i < 4; i++)
            {
                if (gGameTable.audio_BufferDoor[i])
                {
                    auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferDoor[i];
                    if (pDSB->Release())
                        return 0;
                    gGameTable.audio_BufferDoor[i] = 0;
                }
            }
            return 1;
        case 1: // arms — ends at BufferVoice.
            if (!ss_stop_group(1, -1))
                return 0;
            // BufferArms [32]
            for (int i = 0; i < 32; i++)
            {
                if (gGameTable.audio_BufferArms[i])
                {
                    auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferArms[i];
                    if (pDSB->Release())
                        return 0;
                    gGameTable.audio_BufferArms[i] = 0;
                }
            }
            return 1;
        case 2: // room — ends at BufferCore.
            if (!ss_stop_group(2, -1))
                return 0;
            // BufferRoom [48]
            for (int i = 0; i < 48; i++)
            {
                if (gGameTable.audio_BufferRoom[i])
                {
                    auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferRoom[i];
                    if (pDSB->Release())
                        return 0;
                    gGameTable.audio_BufferRoom[i] = 0;
                }
            }
            return 1;
        case 3: // enemy — ends at BufferBgm.
            if (!ss_stop_group(3, -1))
                return 0;
            // BufferEnemy [32]
            for (int i = 0; i < 32; i++)
            {
                if (gGameTable.audio_BufferEnemy[i])
                {
                    auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferEnemy[i];
                    if (pDSB->Release())
                        return 0;
                    gGameTable.audio_BufferEnemy[i] = 0;
                }
            }
            return 1;
        case 4: // core — ends at BufferArms.
            if (!ss_stop_group(4, -1))
                return 0;
            // BufferCore [22]
            for (int i = 0; i < 22; i++)
            {
                if (gGameTable.audio_BufferCore[i])
                {
                    auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferCore[i];
                    if (pDSB->Release())
                        return 0;
                    gGameTable.audio_BufferCore[i] = 0;
                }
            }
            return 1;
        case 5: // bgm — ends at MarniSnd_Frequency.
            if (!ss_stop_group(5, -1))
                return 0;
            // BufferBgm [3]
            for (int i = 0; i < 3; i++)
            {
                if (gGameTable.audio_BufferBgm[i])
                {
                    auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferBgm[i];
                    if (pDSB->Release())
                        return 0;
                    gGameTable.audio_BufferBgm[i] = 0;
                }
            }
            return 1;
        case 6: // sbgm — ends at SpeakerConfig.
            if (!ss_stop_group(6, -1))
                return 0;
            // BufferSBgm [2]
            for (int i = 0; i < 2; i++)
            {
                if (gGameTable.audio_BufferSBgm[i])
                {
                    auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferSBgm[i];
                    if (pDSB->Release())
                        return 0;
                    gGameTable.audio_BufferSBgm[i] = 0;
                }
            }
            return 1;
        case 7: // voice — ends at MarniSnd_SoundDepth.
            if (!ss_stop_group(7, -1))
                return 0;
            // BufferVoice [2]
            for (int i = 0; i < 2; i++)
            {
                if (gGameTable.audio_BufferVoice[i])
                {
                    auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferVoice[i];
                    if (pDSB->Release())
                        return 0;
                    gGameTable.audio_BufferVoice[i] = 0;
                }
            }
            return 1;
        default:
            return 1;
        }
    }

    // 0x00434140
    static int ss_unload_bgm(int type, int index)
    {
        if (!gGameTable.audio_pMarniSnd)
            return 1;

        if (type == 5)
        {
            if ((unsigned int)index <= 2 && ss_stop_group(5, index))
            {
                auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferBgm[index];
                if (!pDSB)
                    return 1;
                if (pDSB->Release())
                    return 0;
                gGameTable.audio_BufferBgm[index] = 0;
                return 1;
            }
        }
        else
        {
            if (type != 6)
                return 1;
            if ((unsigned int)index <= 1 && ss_stop_group(6, -1))
            {
                auto pDSB = (LPDIRECTSOUNDBUFFER)gGameTable.audio_BufferSBgm[index];
                if (pDSB)
                {
                    if (pDSB->Release())
                        return 0;
                    gGameTable.audio_BufferSBgm[index] = 0;
                    return 1;
                }
                return 1;
            }
        }
        return 0;
    }

    // 0x004341E0
    static int ss_stop_group(int type, int id)
    {
        if (!gGameTable.audio_pMarniSnd)
            return 1;

        auto is_playing = [](uint32_t buf) {
            auto pDSB = (LPDIRECTSOUNDBUFFER)buf;
            DWORD status = 0;
            pDSB->GetStatus(&status);
            return (status & DSBSTATUS_PLAYING) != 0;
        };

        // Buffer selected by a valid specific Id (shared check below).
        uint32_t buf = 0;

        switch (type)
        {
        case 0: // door [4] — ends at BufferEnemy.
            if (id == -1)
            {
                // Return 0 as soon as a playing buffer is found.
                for (int i = 0; i < 4; i++)
                {
                    if (gGameTable.audio_BufferDoor[i] && is_playing(gGameTable.audio_BufferDoor[i]))
                        return 0;
                }
                return 1;
            }
            if ((unsigned int)id >= 4)
                return 0;
            buf = gGameTable.audio_BufferDoor[id];
            break;
        case 1: // arms [32] — ends at BufferVoice.
            if (id == -1)
            {
                for (int i = 0; i < 32; i++)
                {
                    if (gGameTable.audio_BufferArms[i] && is_playing(gGameTable.audio_BufferArms[i]))
                        return 0;
                }
                return 1;
            }
            if ((unsigned int)id >= 0x20)
                return 0;
            buf = gGameTable.audio_BufferArms[id];
            break;
        case 2: // room [48] — ends at BufferCore.
            if (id == -1)
            {
                for (int i = 0; i < 48; i++)
                {
                    if (gGameTable.audio_BufferRoom[i] && is_playing(gGameTable.audio_BufferRoom[i]))
                        return 0;
                }
                return 1;
            }
            if ((unsigned int)id >= 0x30)
                return 0;
            buf = gGameTable.audio_BufferRoom[id];
            break;
        case 3: // enemy [32] — ends at BufferBgm.
            if (id == -1)
            {
                for (int i = 0; i < 32; i++)
                {
                    if (gGameTable.audio_BufferEnemy[i] && is_playing(gGameTable.audio_BufferEnemy[i]))
                        return 0;
                }
                return 1;
            }
            if ((unsigned int)id >= 0x20)
                return 0;
            buf = gGameTable.audio_BufferEnemy[id];
            break;
        case 4: // core [22] — ends at BufferArms.
            if (id == -1)
            {
                for (int i = 0; i < 22; i++)
                {
                    if (gGameTable.audio_BufferCore[i] && is_playing(gGameTable.audio_BufferCore[i]))
                        return 0;
                }
                return 1;
            }
            if ((unsigned int)id > 0x15)
                return 0;
            buf = gGameTable.audio_BufferCore[id];
            break;
        case 5: // bgm [3] — ends at MarniSnd_Frequency.
            if (id == -1)
            {
                for (int i = 0; i < 3; i++)
                {
                    if (gGameTable.audio_BufferBgm[i] && is_playing(gGameTable.audio_BufferBgm[i]))
                        return 0;
                }
                return 1;
            }
            if ((unsigned int)id > 2)
                return 0;
            buf = gGameTable.audio_BufferBgm[id];
            break;
        case 6: // sbgm [2] — ends at SpeakerConfig.
            if (id == -1)
            {
                for (int i = 0; i < 2; i++)
                {
                    if (gGameTable.audio_BufferSBgm[i] && is_playing(gGameTable.audio_BufferSBgm[i]))
                        return 0;
                }
                return 1;
            }
            if ((unsigned int)id > 1)
                return 0;
            buf = gGameTable.audio_BufferSBgm[id];
            break;
        case 7: // voice [2] — ends at MarniSnd_SoundDepth.
            if (id == -1)
            {
                for (int i = 0; i < 2; i++)
                {
                    if (gGameTable.audio_BufferVoice[i] && is_playing(gGameTable.audio_BufferVoice[i]))
                        return 0;
                }
                return 1;
            }
            if ((unsigned int)id > 1)
                return 0;
            buf = gGameTable.audio_BufferVoice[id];
            break;
        default:
            return 1;
        }

        // LABEL_67: a non-null buffer that is still playing blocks the group.
        if (buf && is_playing(buf))
            return 0;
        return 1;
    }

    // 0x00436370
    static int ss_is_dual_bgm(int type, int id)
    {
        using sig = int (*)(int, int);
        auto p = (sig)0x00436370;
        return p(type, id);
    }

    // 0x00436420
    static int ss_load_hack(int type, int id)
    {
        using sig = int (*)(int, int);
        auto p = (sig)0x00436420;
        return p(type, id);
    }

    // 0x004EEF70
    static int room_fs_ck()
    {
        using sig = int (*)();
        auto p = (sig)0x004EEF70;
        return p();
    }

    // 0x004344A0
    static int ss_load_banks(int type, int id, int bank, int player)
    {
        if (!gGameTable.audio_pMarniSnd)
            return 1;

        switch (type)
        {
        case 0: // ST_DOOR
            return ss_load_sap(0, id, bank, 0);

        case 1: // ST_ARMS
            return ss_load_sap(1, id, bank, 0);

        case 2: // ST_ROOM
        {
            int result = ss_load_sap(2, id, bank, 0);
            if (!result)
                return result;

            // Load up to three footstep sets for this room/bank combination.
            for (int i = 0; i < 3; i++)
            {
                int step = (&kFootstepTbl[id * 29 + bank].field_0)[i];
                if (step != -1 && !ss_load_steps(kFsNameTbl[step], i))
                    return 0;
            }

            if (room_fs_ck())
                ss_load_sap(2, -1, 0, 0);
            return 1;
        }

        case 3: // ST_ENEMY
            return ss_load_sap(3, id, bank, 0);

        case 4: // ST_CORE
        {
            int result = ss_load_sap(4, id, 0, 0);
            if (!result)
                return result;
            return id == 21 || ss_load_sap(4, 22, 0, 0);
        }

        case 5: // ST_BGM
        {
            for (int i = 0; i < 3; i++)
            {
                int v8 = (&kMainBgmLut[player].field_0)[i];
                if (v8 == -1)
                    continue;

                int v11;
                if (ss_is_dual_bgm(5, v8) == 1)
                {
                    int savedSpeaker = gGameTable.audio_SpeakerConfig;
                    gGameTable.audio_SpeakerConfig = 1;
                    if (ss_load_hack(5, v8) == 1)
                    {
                        int savedDepth = gGameTable.MarniSnd_SoundDepth;
                        int savedFreq = gGameTable.MarniSnd_Frequency;
                        gGameTable.MarniSnd_SoundDepth = 8;
                        gGameTable.MarniSnd_Frequency = 11025; // 0x2B11
                        v11 = ss_load_bgm(kMainBgmNameTbl[v8], 5, i);
                        gGameTable.MarniSnd_Frequency = savedFreq;
                        gGameTable.MarniSnd_SoundDepth = (uint16_t)savedDepth;
                    }
                    else
                    {
                        v11 = ss_load_bgm(kMainBgmNameTbl[v8], 5, i);
                    }
                    gGameTable.audio_SpeakerConfig = savedSpeaker;
                }
                else
                {
                    v11 = ss_load_bgm(kMainBgmNameTbl[v8], 5, i);
                }

                if (!v11)
                    return 0;
            }
            return 1;
        }

        case 6: // ST_SBGM
        {
            int bankb = 0;
            for (int i = 0; i < 2; i++)
            {
                int v13 = kSubBgmLut[id].field_0[i];
                if (v13 == -1)
                    continue;

                int bgm;
                if (ss_is_dual_bgm(6, v13) == 1)
                {
                    int savedSpeaker = gGameTable.audio_SpeakerConfig;
                    if (ss_load_hack(6, v13) == 1)
                    {
                        int savedDepth = gGameTable.MarniSnd_SoundDepth;
                        int savedFreq = gGameTable.MarniSnd_Frequency;
                        bgm = ss_load_bgm(kSubBgmNameTbl[v13], 6, i);
                        gGameTable.MarniSnd_Frequency = savedFreq;
                        gGameTable.MarniSnd_SoundDepth = (uint16_t)savedDepth;
                    }
                    else
                    {
                        bgm = ss_load_bgm(kSubBgmNameTbl[v13], 6, i);
                    }
                    gGameTable.audio_SpeakerConfig = savedSpeaker;
                }
                else
                {
                    bgm = ss_load_bgm(kSubBgmNameTbl[v13], 6, i);
                }

                bankb += bgm << i;
            }
            return bankb;
        }

        case 7: // ST_VOICE
            return ss_load_sap(7, id, bank, player);

        default:
            return 1;
        }
    }

    // 0x004347B0
    static LPDIRECTSOUNDBUFFER ss_get_status(int type, int sub)
    {
        if (!gGameTable.audio_pMarniSnd)
            return nullptr;

        uint32_t* pbuffer = nullptr;
        switch (type)
        {
        case 0: // door (0..3)
            if ((unsigned int)sub >= 4)
                return nullptr;
            pbuffer = &gGameTable.audio_BufferDoor[sub];
            break;
        case 1: // arms (0..0x1F)
            if ((unsigned int)sub >= 0x20)
                return nullptr;
            pbuffer = &gGameTable.audio_BufferArms[sub];
            break;
        case 2: // room (0..0x2F)
            if ((unsigned int)sub >= 0x30)
                return nullptr;
            pbuffer = &gGameTable.audio_BufferRoom[sub];
            break;
        case 3: // enemy (0..0x1F)
            if ((unsigned int)sub >= 0x20)
                return nullptr;
            pbuffer = &gGameTable.audio_BufferEnemy[sub];
            break;
        case 4: // core (0..0x15)
            if ((unsigned int)sub > 0x15)
                return nullptr;
            pbuffer = &gGameTable.audio_BufferCore[sub];
            break;
        case 5: // bgm (0..2)
            if ((unsigned int)sub > 2)
                return nullptr;
            pbuffer = &gGameTable.audio_BufferBgm[sub];
            break;
        case 6: // sbgm (0..1)
            if ((unsigned int)sub > 1)
                return nullptr;
            pbuffer = &gGameTable.audio_BufferSBgm[sub];
            break;
        case 7: // voice (0..1)
            if ((unsigned int)sub > 1)
                return nullptr;
            pbuffer = &gGameTable.audio_BufferVoice[sub];
            break;
        default:
            return nullptr;
        }

        if (!pbuffer)
            return nullptr;

        auto result = (LPDIRECTSOUNDBUFFER)*pbuffer;
        if (result)
        {
            // GetStatus writes the DirectSound status bits back into `sub`; on
            // success (S_OK) that status value is returned cast to a pointer so
            // callers can test the low bits (e.g. DSBSTATUS_PLAYING), else 0.
            HRESULT hr = result->GetStatus((LPDWORD)&sub);
            result = hr == 0 ? (LPDIRECTSOUNDBUFFER)sub : nullptr;
        }
        return result;
    }

    // 0x004EF070
    static int bgm_ck_room(int a, int b, int c)
    {
        using sig = int (*)(int, int, int);
        auto p = (sig)0x004EF070;
        return p(a, b, c);
    }

    // 0x004348F0
    static LPDIRECTSOUNDBUFFER ss_set_pan(int type, unsigned int index, int pan)
    {
        if (!gGameTable.audio_pMarniSnd)
            return (LPDIRECTSOUNDBUFFER)1;

        // Clamp pan to [-10000, 10000] (the original scales by 23 first).
        int v4 = 23 * pan;
        if (v4 >= -10000)
        {
            if (v4 > 10000)
                v4 = 10000;
        }
        else
        {
            v4 = -10000;
        }

        // Mono speaker configuration forces pan to center.
        if (gGameTable.audio_SpeakerConfig == 1)
            v4 = 0;

        uint32_t* v5 = nullptr;
        switch (type)
        {
        case 0: // door (0..3)
            if (index >= 4)
                return nullptr;
            v5 = &gGameTable.audio_BufferDoor[index];
            break;
        case 1: // arms (0..0x1F)
            if (index >= 0x20)
                return nullptr;
            v5 = &gGameTable.audio_BufferArms[index];
            break;
        case 2: // room (0..0x2F)
            if (index >= 0x30)
                return nullptr;
            v5 = &gGameTable.audio_BufferRoom[index];
            break;
        case 3: // enemy (0..0x1F)
            if (index >= 0x20)
                return nullptr;
            v5 = &gGameTable.audio_BufferEnemy[index];
            break;
        case 4: // core (0..0x15)
            if (index > 0x15)
                return nullptr;
            v5 = &gGameTable.audio_BufferCore[index];
            break;
        case 5: // bgm (0..2)
            if (index <= 2)
            {
                v5 = &gGameTable.audio_BufferBgm[index];
                break;
            }
            if (bgm_ck_room(0, 8, -1) == 1)
                return nullptr;
            bgm_ck_room(0, 9, -1);
            return nullptr;
        case 6: // sbgm (0..1)
            if (index > 1)
            {
                if (bgm_ck_room(3, 0, -1) == 1)
                    return nullptr;
                if (bgm_ck_room(0, 9, -1) != 1)
                    return nullptr;
                v5 = &gGameTable.audio_BufferSBgm[1];
            }
            else
            {
                v5 = &gGameTable.audio_BufferSBgm[index];
            }
            break;
        case 7: // voice (0..1)
            if (index > 1)
                return nullptr;
            v5 = &gGameTable.audio_BufferVoice[index];
            break;
        default:
            return nullptr;
        }

        // Shared tail: all paths that resolve a buffer land here.
        if (!v5)
            return nullptr;

        auto result = (LPDIRECTSOUNDBUFFER)*v5;
        if (result)
        {
            // SetPan lives at vtable offset 0x40; S_OK (0) is returned as
            // pointer value 1 so callers can test truthiness.
            HRESULT hr = result->SetPan(v4);
            result = (LPDIRECTSOUNDBUFFER)(hr == 0);
        }
        return result;
    }

    // 0x00434AB0
    static int ss_set_vol(int type, unsigned int index, int vol)
    {
        if (!gGameTable.audio_pMarniSnd)
            return 1;

        // Scale the requested volume by the master volume for the channel.
        // BGM/SBGM channels additionally attenuate by 15 while room 115 is
        // active (Bgm_ck_room115 returns non-zero).
        int v4;
        if (type == 5 || type == 6)
        {
            int v6 = vol;
            if (bgm_ck_room115())
            {
                v6 = vol - 15;
                if (vol - 15 < 0)
                    v6 = 0;
            }
            v4 = v6 * (unsigned __int8)gGameTable.bgm_vol / 100;
        }
        else if (type == 7)
        {
            v4 = vol;
        }
        else
        {
            v4 = vol * (unsigned __int8)gGameTable.sfx_vol / 100;
        }

        // Convert the linear 0..255 volume into a DirectSound attenuation in
        // hundredths of a decibel, clamped to [-10000, 0].
        int v7;
        if (v4 >= 32)
            v7 = 2 * (9 * v4 - 1143);
        else
            v7 = 259 * v4 - 10000;
        if (v7 >= -10000)
        {
            if (v7 > 0)
                v7 = 0;
        }
        else
        {
            v7 = -10000;
        }

        uint32_t* v8 = nullptr;
        switch (type)
        {
        case 0: // door (0..3)
            if (index >= 4)
                return 0;
            v8 = &gGameTable.audio_BufferDoor[index];
            break;
        case 1: // arms (0..0x1F)
            if (index >= 0x20)
                return 0;
            v8 = &gGameTable.audio_BufferArms[index];
            break;
        case 2: // room (0..0x2F)
            if (index >= 0x30)
                return 0;
            v8 = &gGameTable.audio_BufferRoom[index];
            break;
        case 3: // enemy (0..0x1F)
            if (index >= 0x20)
                return 0;
            v8 = &gGameTable.audio_BufferEnemy[index];
            break;
        case 4: // core (0..0x15)
            if (index > 0x15)
                return 0;
            v8 = &gGameTable.audio_BufferCore[index];
            break;
        case 5: // bgm (0..2)
            if (index <= 2)
            {
                dword_693B30[index] = v4;
                v8 = &gGameTable.audio_BufferBgm[index];
                break;
            }
            bgm_ck_room(0, 8, -1);
            return 0;
        case 6: // sbgm (0..1)
            if (index > 1)
            {
                if (bgm_ck_room(3, 0, -1) == 1)
                    return 0;
                if (bgm_ck_room(0, 9, -1) != 1)
                    return 0;
                *dword_6941CC = v4;
                v8 = &gGameTable.audio_BufferSBgm[1];
            }
            else
            {
                dword_6941C8[index] = v4;
                v8 = &gGameTable.audio_BufferSBgm[index];
            }
            break;
        case 7: // voice (0..1)
            if (index > 1)
                return 0;
            v8 = &gGameTable.audio_BufferVoice[index];
            break;
        default:
            return 0;
        }

        // Shared tail: all paths that resolve a buffer land here. The original
        // NULL-checks v8 (LABEL_44); it can never be NULL for the fixed array
        // slots above, but it is kept for fidelity.
        if (!v8)
            return 0;

        auto result = (LPDIRECTSOUNDBUFFER)*v8;
        if (result)
        {
            // SetVolume lives at vtable offset 0x3C; S_OK (0) is returned as
            // value 1 so callers can test truthiness.
            HRESULT hr = result->SetVolume(v7);
            return hr == 0;
        }
        return 0;
    }

    // 0x00434CF0
    static int ss_get_volume(int type, unsigned int index)
    {
        if (!gGameTable.audio_pMarniSnd)
            return 1;

        uint32_t* v3 = nullptr;
        switch (type)
        {
        case 0: // door (0..3)
            if (index >= 4)
                return 0;
            v3 = &gGameTable.audio_BufferDoor[index];
            break;
        case 1: // arms (0..0x1F)
            if (index >= 0x20)
                return 0;
            v3 = &gGameTable.audio_BufferArms[index];
            break;
        case 2: // room (0..0x2F)
            if (index >= 0x30)
                return 0;
            v3 = &gGameTable.audio_BufferRoom[index];
            break;
        case 3: // enemy (0..0x1F)
            if (index >= 0x20)
                return 0;
            v3 = &gGameTable.audio_BufferEnemy[index];
            break;
        case 4: // core (0..0x15)
            if (index > 0x15)
                return 0;
            v3 = &gGameTable.audio_BufferCore[index];
            break;
        case 5: // bgm (0..2)
            if (index > 2)
                return 0;
            v3 = &gGameTable.audio_BufferBgm[index];
            break;
        case 6: // sbgm (0..1)
            if (index > 1)
                return 0;
            v3 = &gGameTable.audio_BufferSBgm[index];
            break;
        case 7: // voice (0..1)
            if (index > 1)
                return 0;
            v3 = &gGameTable.audio_BufferVoice[index];
            break;
        default:
            return 0;
        }

        // Shared tail: all paths that resolve a buffer land here. The original
        // NULL-checks v3 (loc_434DE5); it can never be NULL for the fixed array
        // slots above, but it is kept for fidelity.
        if (!v3)
            return 0;

        uint32_t result = *v3;
        if (!result)
            return 0;

        auto buffer = (LPDIRECTSOUNDBUFFER)result;
        // GetVolume lives at vtable offset 0x18 and writes the current
        // attenuation in hundredths of a decibel back into `index`; any
        // HRESULT other than S_OK is treated as failure (0).
        HRESULT hr = buffer->GetVolume((LPLONG)&index);
        if (hr != 0)
            return 0;

        // Convert the DirectSound attenuation back into the linear 0..255
        // volume scale, inverting the conversion done by ss_set_vol (the
        // -1971 threshold is where that function's two branches meet).
        int v4;
        if ((int)index > -1971)
            v4 = (int)(index + 2286) / 18;
        else
            v4 = (int)(index + 10000) / 259;

        // BGM/SBGM channels scale against the BGM master volume, all other
        // channels against the SFX master volume.
        if (type == 5 || type == 6)
            return 100 * v4 / (unsigned __int8)gGameTable.bgm_vol;
        else
            return 100 * v4 / (unsigned __int8)gGameTable.sfx_vol;
    }

    // 0x00434EA0
    static int ss_load_sap(DWORD type, int id, int bank, int player)
    {
        if (!gGameTable.audio_pMarniSnd)
            return 1;

        char path[260];
        int mode = 1;

        switch (type)
        {
        case 0:
            wsprintfA(path, "common\\sound\\door\\door%02d.sap", id);
            strcpy(gGameTable.ss_name_door, path);
            break;
        case 1:
            wsprintfA(path, "common\\sound\\arms\\weapon%02d.sap", id);
            strcpy(gGameTable.ss_name_arms, path);
            break;
        case 2:
            wsprintfA(path, "common\\sound\\room\\room%d%02x.sap", id + 1, bank);
            if (id + 1 > 0)
                strcpy(gGameTable.ss_name_room, path);
            break;
        case 3:
            wsprintfA(path, "common\\sound\\enemy\\enemy%02d.sap", id);
            strcpy(gGameTable.ss_name_enemy, path);
            break;
        case 4:
            wsprintfA(path, "common\\sound\\core\\core%02d.sap", id);
            if (id != 22)
                strcpy(gGameTable.ss_name_core, path);
            break;
        case 7:
            if (player)
                wsprintfA(path, "pl1\\voice\\stage%d\\v%03d.sap", id, bank);
            else
                wsprintfA(path, "pl0\\voice\\stage%d\\v%03d.sap", id, bank);
            mode = 8;
            break;
        default: return 1;
        }

        if (!file_exists(path, mode))
        {
            gGameTable.error_no = 2;
            return 0;
        }

        HMMIO hmmio = mmioOpenA(gGameTable.ss_file_string.data, nullptr, MMIO_ALLOCBUF);
        if (!hmmio)
            return 0;

        int32_t mask0, mask1;
        if (mmioRead(hmmio, (HPSTR)&mask0, 4) != 4 || mmioRead(hmmio, (HPSTR)&mask1, 4) != 4)
        {
            mmioClose(hmmio, 0);
            return 0;
        }

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

        mmioClose(hmmio, 0);
        return 1;
    }

    // 0x00435170
    static int ss_load_steps(const char* name, int a2)
    {
        if (!gGameTable.audio_pMarniSnd || !*name)
            return 1;

        char path[260];
        sprintf(path, "common\\sound\\room\\%s.sap", name);

        strcpy(&gGameTable.ss_name_step[260 * a2], path);

        if (!file_exists(path, 1))
        {
            gGameTable.error_no = 2;
            return 0;
        }

        auto& ss = gGameTable.ss_file_string;
        HMMIO hmmio = mmioOpenA(ss.data, nullptr, MMIO_ALLOCBUF);
        if (!hmmio)
        {
            mmioClose(0, 0);
            interop::call<void>(0x004DBFD0, "mmioOpen ERROR!", "dsound.cpp");
            return 0;
        }

        int base;
        switch (a2)
        {
        case 0: base = 23; break;
        case 1: base = 26; break;
        case 2: base = 29; break;
        default: base = 0; break; // unreachable
        }

        int bitmask;
        if (mmioRead(hmmio, (HPSTR)&bitmask, 4) != 4)
        {
            mmioClose(hmmio, 0);
            return 0;
        }

        char unused[4];
        if (mmioRead(hmmio, unused, 4) != 4)
        {
            mmioClose(hmmio, 0);
            return 0;
        }

        for (int i = 0; i < 32; i++)
        {
            if ((bitmask >> i) & 1)
                ss_create_buffer(hmmio, 2, i + base);
        }

        mmioClose(hmmio, 0);
        return 1;
    }

    // 0x00435300
    static int ss_load_bgm(const char* name, DWORD type, int sample)
    {
        if (!gGameTable.audio_pMarniSnd || !*name)
            return 1;

        char path[260];
        sprintf(path, "common\\sound\\bgm\\%s.sap", name);

        if (type == 5)
            strcpy(&gGameTable.ss_name_bgm[260 * sample], path);
        else if (type == 6)
            strcpy(&gGameTable.ss_name_sbgm[260 * sample], path);

        if (!file_exists(path, 1))
        {
            gGameTable.error_no = 2;
            return 0;
        }

        auto& ss = gGameTable.ss_file_string;
        HMMIO hmmio = mmioOpenA(ss.data, nullptr, MMIO_ALLOCBUF);
        if (!hmmio)
            return 0;

        ss_create_buffer(hmmio, type, sample);
        mmioClose(hmmio, 0);
        return 1;
    }

    // 0x004EEE40
    static void sub_4eee40()
    {
        interop::call(0x004EEE40);
    }

    // 0x00435610
    static int ss_init_2()
    {
        ss_init();

        // Door (type 0)
        if (gGameTable.ss_name_door[0])
        {
            if (!file_exists(gGameTable.ss_name_door, 1))
            {
                gGameTable.error_no = 2;
                return 0;
            }
            ss_init_buffers(0);
        }

        // Room (type 2)
        if (gGameTable.ss_name_room[0])
        {
            if (!file_exists(gGameTable.ss_name_room, 1))
            {
                gGameTable.error_no = 2;
                return 0;
            }
            ss_init_buffers(2);
        }

        // Steps (3 entries, type 2, base sub offsets {23, 26, 29})
        static const int stepBase[3] = { 23, 26, 29 };
        for (int i = 0; i < 3; i++)
        {
            char* name = &gGameTable.ss_name_step[260 * i];
            if (!*name)
                continue;

            if (!file_exists(name, 1))
            {
                gGameTable.error_no = 2;
                return 0;
            }

            HMMIO hmmio = mmioOpenA(gGameTable.ss_file_string.data, nullptr, MMIO_ALLOCBUF);
            if (!hmmio)
                return 0;

            int32_t mask0 = 0;
            if (mmioRead(hmmio, (HPSTR)&mask0, 4) != 4)
            {
                mmioClose(hmmio, 0);
                return 0;
            }

            int32_t unusedMask1 = 0;
            if (mmioRead(hmmio, (HPSTR)&unusedMask1, 4) != 4)
            {
                mmioClose(hmmio, 0);
                return 0;
            }

            int base = stepBase[i];
            for (int j = 0; j < 32; j++)
            {
                if (((int32_t)mask0 >> j) & 1)
                    ss_create_buffer(hmmio, 2, j + base);
            }

            mmioClose(hmmio, 0);
        }

        // Arms (type 1)
        if (gGameTable.ss_name_arms[0])
        {
            if (!file_exists(gGameTable.ss_name_arms, 1))
            {
                gGameTable.error_no = 2;
                return 0;
            }
            ss_init_buffers(1);
        }

        // Core (type 4)
        if (gGameTable.ss_name_core[0])
        {
            if (!file_exists(gGameTable.ss_name_core, 1))
            {
                gGameTable.error_no = 2;
                return 0;
            }
            ss_init_buffers(4);
            ss_load_sap(4, 22, 0, 0);
        }

        // Enemy (type 3)
        if (gGameTable.ss_name_enemy[0])
        {
            if (!file_exists(gGameTable.ss_name_enemy, 1))
            {
                gGameTable.error_no = 2;
                return 0;
            }
            ss_init_buffers(3);
        }

        // BGM (type 5, up to 3 entries)
        for (int i = 0; i < 3; i++)
        {
            char* name = &gGameTable.ss_name_bgm[260 * i];
            if (!*name)
                continue;

            if (!file_exists(name, 1))
            {
                gGameTable.error_no = 2;
                return 0;
            }

            HMMIO hmmio = mmioOpenA(gGameTable.ss_file_string.data, nullptr, MMIO_ALLOCBUF);
            if (!hmmio)
                return 0;

            ss_create_buffer(hmmio, 5, i);
            mmioClose(hmmio, 0);
        }

        // SBGM (type 6, up to 2 entries)
        for (int i = 0; i < 2; i++)
        {
            char* name = &gGameTable.ss_name_sbgm[260 * i];
            if (!*name)
                continue;

            if (!file_exists(name, 1))
            {
                gGameTable.error_no = 2;
                return 0;
            }

            HMMIO hmmio = mmioOpenA(gGameTable.ss_file_string.data, nullptr, MMIO_ALLOCBUF);
            if (!hmmio)
                return 0;

            ss_create_buffer(hmmio, 6, i);
            mmioClose(hmmio, 0);
        }

        sub_4eee40();

        return 1;
    }

    static float ss_voice_parse(HMMIO hmmio);

    // 0x00436470
    static int ss_voice_load(int room_id, int voice)
    {
        char path[260];
        int player = get_player_num();
        sprintf(path, "pl%d\\voice\\stage%d\\v%03d.sap", player, room_id, voice);

        if (!file_exists(path, 8))
        {
            gGameTable.error_no = 2;
            return 0;
        }

        auto& ss = gGameTable.ss_file_string;
        HMMIO hmmio = mmioOpenA(ss.data, nullptr, MMIO_ALLOCBUF);
        if (!hmmio)
        {
            mmioClose(0, 0);
            interop::call<void>(0x004DBFD0, "mmioOpen ERROR!", "dsound.cpp");
            return 0;
        }

        int pch;
        char unused[4];
        if (mmioRead(hmmio, (HPSTR)&pch, 4) == 4 && mmioRead(hmmio, (HPSTR)unused, 4) == 4)
        {
            float duration = ss_voice_parse(hmmio);
            int fps = 60 >> gGameTable.vsync_rate;
            mmioClose(hmmio, 0);
            return (int)(fps * duration);
        }

        mmioClose(hmmio, 0);
        return 0;
    }

    // 0x00436590
    static float ss_voice_parse(HMMIO hmmio)
    {
        float cbInput = 0.0f;
        WAVEFORMATEX* wf = nullptr;

        MMCKINFO pmmcki = {};
        pmmcki.fccType = mmioFOURCC('W', 'A', 'V', 'E');

        if (mmioDescend(hmmio, &pmmcki, nullptr, MMIO_FINDRIFF))
            return 0.0f;

        MMCKINFO v15 = {};
        if (mmioDescend(hmmio, &v15, &pmmcki, 0))
            goto ascend_parent;

        while (1)
        {
            DWORD cksize = v15.cksize;
            if (cksize + v15.dwDataOffset > pmmcki.dwDataOffset + pmmcki.cksize)
                break;

            if (v15.ckid == mmioFOURCC('f', 'm', 't', ' '))
            {
                DWORD allocSize = (std::max<DWORD>(cksize, sizeof(WAVEFORMATEX)) + 3) & ~3u;
                void* buf = _alloca(allocSize);
                wf = (WAVEFORMATEX*)buf;
                if (!wf || mmioRead(hmmio, (HPSTR)wf, cksize) != (LRESULT)cksize)
                    break;
            }
            else if (v15.ckid == mmioFOURCC('d', 'a', 't', 'a'))
            {
                cbInput = (float)cksize;
                if (wf && wf->wFormatTag == WAVE_FORMAT_PCM)
                {
                    int64_t num = (16 / wf->nBlockAlign) * (int64_t)cksize;
                    int32_t den = wf->wBitsPerSample * wf->nSamplesPerSec;
                    cbInput = (float)((double)num / (double)den);
                }
                else if (wf)
                {
                    acmDriverEnum(acmDriverEnumCallback, 0, 0);
                    auto had_ptr = reinterpret_cast<HACMDRIVER*>(&gGameTable.had);
                    auto hadid = reinterpret_cast<HACMDRIVERID>(gGameTable.hadid);
                    if (acmDriverOpen(had_ptr, hadid, 0))
                        break;

                    DWORD pMetric = 0;
                    acmMetrics(nullptr, ACM_METRIC_MAX_SIZE_FORMAT, &pMetric);
                    DWORD allocSize = (pMetric + 3) & ~3u;
                    void* buf = _alloca(allocSize);
                    auto wfxDst = (WAVEFORMATEX*)buf;

                    wfxDst->wFormatTag = WAVE_FORMAT_PCM;
                    wfxDst->cbSize = 0;
                    wfxDst->wBitsPerSample = 16;

                    HACMSTREAM phas = nullptr;
                    DWORD pdwOutputBytes = 0;
                    bool acmFailed = false;

                    if (acmFormatSuggest(*had_ptr, wf, wfxDst, pMetric, ACM_FORMATSUGGESTF_WFORMATTAG)
                        || acmStreamOpen(&phas, *had_ptr, wf, wfxDst, nullptr, 0, 0, 0)
                        || acmStreamSize(phas, (DWORD)cbInput, &pdwOutputBytes, 0))
                    {
                        acmFailed = true;
                    }

                    if (phas)
                    {
                        acmStreamClose(phas, 0);
                        phas = nullptr;
                    }
                    if (*had_ptr)
                    {
                        acmDriverClose(*had_ptr, 0);
                        *had_ptr = nullptr;
                    }

                    if (acmFailed)
                        break;

                    int64_t num = (16 / wfxDst->nBlockAlign) * (int64_t)pdwOutputBytes;
                    int32_t den = wfxDst->nSamplesPerSec * wfxDst->wBitsPerSample;
                    cbInput = (float)((double)num / (double)den);
                }
            }

            mmioAscend(hmmio, &v15, 0);
            if (mmioDescend(hmmio, &v15, &pmmcki, 0))
                goto ascend_parent;
        }

        // Break/error: ascend child before parent
        mmioAscend(hmmio, &v15, 0);

    ascend_parent:
        mmioAscend(hmmio, &pmmcki, 0);

        // Cleanup global ACM driver handle
        auto had_ptr = reinterpret_cast<HACMDRIVER*>(&gGameTable.had);
        if (*had_ptr)
        {
            acmDriverClose(*had_ptr, 0);
            *had_ptr = nullptr;
        }

        return cbInput;
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

    namespace
    {
        // 0x004EC250
        static char snd_sys_init2()
        {
            char v0 = (char)gGameTable.enable_dsound;
            if (gGameTable.enable_dsound)
            {
                ss_stop_all();
                v0 = (char)ss_shutdown();

                // Seq_ctr is a 3 x {int8, ?, int8} table at 0x693800 with an
                // 8-byte stride between slots.
                auto seq_ctr = (int8_t*)gGameTable.seq_ctr;
                for (int i = 0; i < 3; i++)
                {
                    if (seq_ctr[8 * i])
                    {
                        v0 = (char)(uintptr_t)ss_get_status(5, i);
                        if ((v0 & 1) != 0)
                            v0 = (char)ss_stop_group(5, i);
                        seq_ctr[8 * i] = 0;
                    }
                    if (seq_ctr[8 * i + 2] > -1)
                    {
                        ss_unload_group(5);
                        // Clears ss_name_bgm and the 260-byte slot at
                        // byte_6935E4 (first byte of each, like the original).
                        char* p = gGameTable.ss_name_bgm;
                        do
                        {
                            *p = 0;
                            p += 260;
                        } while ((int)p < (int)(gGameTable.pad_6935E4 + 260));
                        seq_ctr[8 * i + 2] = -1;
                    }
                }
                for (int j = 0; j < 7; j++)
                {
                    if ((int8_t)gGameTable.vab_id[j] > -1)
                    {
                        v0 = (char)ss_unload_group(j);
                        gGameTable.vab_id[j] = 0xFF;
                    }
                }
                gGameTable.ss_name_enemy[0] = 0;
                gGameTable.ss_name_room[0] = 0;
                gGameTable.ss_name_door[0] = 0;
                gGameTable.ss_name_core[0] = 0;
                gGameTable.ss_name_arms[0] = 0;
                gGameTable.ss_name_step[0] = 0;
                gGameTable.ss_name_step[260] = 0;
                gGameTable.ss_name_step[520] = 0;
                gGameTable.ss_name_bgm[0] = 0;
                gGameTable.pad_6935E4[0] = 0;
                gGameTable.pad_6935E4[260] = 0;
                gGameTable.ss_name_sbgm[0] = 0;
                gGameTable.byte_693FA4 = 0;
            }
            return v0;
        }

        // Handle to reach the implementation from the enclosing namespace:
        // `snd_sys_init2` is also the name of the public wrapper declared in
        // audio.h, so an unqualified reference from openre::audio would find
        // that wrapper rather than this function.
        char (*const snd_sys_init2_impl)() = &snd_sys_init2;
    }

    // Public wrapper declared in audio.h; used by C++ callers in other
    // translation units (openre.cpp, title.cpp). Original-binary callers
    // (Die_move_end, Game_loop, ExBattle_init, ExBattle_exit, Capcom_logo)
    // reach the implementation above via the hook on 0x004EC250.
    void snd_sys_init2()
    {
        snd_sys_init2_impl();
    }

    // 0x004ec340

    // 0x004EC350
    static void snd_sys_init_sub()
    {
        if (!gGameTable.enable_dsound)
            return;

        *rev_vol = 0x400040;   // SND_VOL { left = 0x40, right = 0x40 }
        *revd_vol = 0x400040;
        *main_vol = 0x7F007F;  // SND_VOL { left = 0x7F, right = 0x7F }
        *cd_vol = 0x7F007F;

        gGameTable.vab_id[3] = gGameTable.vab_id[2] = gGameTable.vab_id[1] = gGameTable.vab_id[0] =
            gGameTable.vab_id[6] = gGameTable.vab_id[5] = gGameTable.vab_id[4] = 0xFF;

        *fade_rtn = 0;
        *fade_time = 0;

        // Zero the SEQ/VAB channel tables. v1 steps from 0x693030 down by 2
        // (24 words) while v0 steps from 0x693B24 down by 0x20 (32 bytes),
        // writing one word plus eight dwords per iteration. The compare
        // happens BEFORE the writes, so when v1 reaches Se_pri (0x693000) the
        // writes are skipped.
        auto v0 = dword_693B24;  // int32_t*
        auto v1 = word_693030;   // uint16_t*
        do
        {
            v1 -= 1;
            v0 -= 8;
            *v1 = 0;
            *(v0 - 1) = 0;       // [eax-4]
            v0[0] = 0;
            v0[1] = 0;
            v0[2] = 0;
            v0[3] = 0;
            v0[4] = 0;
            v0[5] = 0;
            v0[6] = 0;
        } while (v1 != se_pri);

        pEdt_adr[0] = (int32_t)0x6DFC0C;
        pEdt_adr[1] = (int32_t)0x6DEF0C;
        pEdt_adr[5] = 0;
        pEdt_adr[3] = 0;
        pEdt_adr[2] = 0;
    }

    namespace
    {
        // 0x004EC410
        static SeqCtr* snd_sys_init_sub2()
        {
            if (!gGameTable.enable_dsound)
                return nullptr;

            *dword_693B20 = 0;
            gGameTable.dword_693C4C = 0;

            *bgm_sub = 0xFF;   // Bgm.Sub = -1
            *bgm_main = 0xFF;  // Bgm.Main = -1

            // Walk the SEQCTR table backwards from 0x69381C down to 0x693804
            // (compare happens BEFORE the writes, so the entry at 0x693804 is
            // still written). Each iteration stores a Vol dword (0xFFFFFF00)
            // at [result-8+4] and a Flg dword (0x006E006E) at [result].
            // 3 iterations: entries at bases 0x693800, 0x693808, 0x693810.
            auto result = seq_ctr_table; // 0x69381C
            do
            {
                result -= 1; // -8 bytes
                *(uint32_t*)&result[-1].vol = 0xFFFFFF00;
                *(uint32_t*)&result->flg = 0x006E006E;
            } while ((char*)result != (char*)0x693804);
            return result;
        }

        // Handle to reach the implementation from the enclosing namespace:
        // `snd_sys_init_sub2` is also the name of the public wrapper declared
        // in audio.h, so an unqualified reference from openre::audio would find
        // that wrapper rather than this function.
        SeqCtr* (*const snd_sys_init_sub2_impl)() = &snd_sys_init_sub2;
    }

    // Public wrapper declared in audio.h; used by C++ callers in other
    // translation units (title.cpp). Original-binary callers (Title_game_init)
    // reach the implementation above via the hook on 0x004EC410.
    void snd_sys_init_sub2()
    {
        snd_sys_init_sub2_impl();
    }

    namespace
    {
        // 0x004EC450
        static void snd_load_core(uint8_t id, int a2)
        {
            // Core-id LUT: maps a logical core slot id to the .edh file number
            // (high nibble) / sub-bank id (low nibble). Slots 0-10 alternate
            // between core files 0 and 1; slots 11-21 map to files 11-21.
            // The original also did a dead strcpy of " !\"#$%&" into a local
            // v12 buffer that was never read again, so it is omitted here.
            const uint8_t core_id_lut[24] = {
                0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0,
                11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                0, 0,
            };

            char mem[32];
            std::strcpy(mem, "common\\sound\\core\\core00.   ");

            if (!gGameTable.enable_dsound)
                return;

            // v2 is the .edh file index; mem[22]/mem[23] (the "00" in
            // "core00") are overwritten with its two hex digits, and
            // mem[25..27] (spaces) with "edh" to form "coreXY.edh".
            auto v2 = core_id_lut[id];
            if (a2 == 2)
            {
                ss_unload_group(0);
                gGameTable.ss_name_door[0] = 0;
                ss_unload_group(4);
                auto v3 = v2 & 0xF;
                gGameTable.ss_name_core[0] = 0;
                pEdt_adr[0] = (int32_t)(((uintptr_t)byte_6DFC0C + 16) & 0xFFFFFFF0);
                gGameTable.vab_id[0] = 0;
                gGameTable.vab_id[4] = 0;
                mem[22] = (char)((v2 >> 4) + 48);
                char v4;
                if ((v2 & 0xF) >= 0xA)
                    v4 = (char)(v3 + 87);
                else
                    v4 = (char)(v3 + 48);
                mem[23] = v4;
                std::memcpy(&mem[25], "edh", 3);

                auto buffer = (uint8_t*)(((uintptr_t)byte_6DFC0C + 16) & 0xFFFFFFF0);
                auto v7 = read_file_into_buffer(mem, (char*)buffer, 1);
                if (v7 == 0)
                {
                    file_error();
                    return;
                }
                if (v7 != (size_t)-1)
                {
                    *dword_6934A0 = (int32_t)(buffer + *(int32_t*)(buffer + v7 - 8));
                    gGameTable.vab_id[0] = ss_load_banks(4, v2, 0, 0);
                }
            }
            else
            {
                ss_unload_group(4);
                auto v5 = v2 & 0xF;
                gGameTable.ss_name_core[0] = 0;
                pEdt_adr[4] = (int32_t)((uintptr_t)byte_6DE21C & 0xFFFFFFF0);
                gGameTable.vab_id[4] = 0;
                mem[22] = (char)((v2 >> 4) + 48);
                char v6;
                if ((v2 & 0xF) >= 0xA)
                    v6 = (char)(v5 + 87);
                else
                    v6 = (char)(v5 + 48);
                mem[23] = v6;
                std::memcpy(&mem[25], "edh", 3);

                auto buffer = (uint8_t*)((uintptr_t)byte_6DE21C & 0xFFFFFFF0);
                auto v8 = read_file_into_buffer(mem, (char*)buffer, 1);
                if (v8 == 0)
                {
                    file_error();
                    return;
                }
                if (v8 != (size_t)-1)
                {
                    auto v9 = buffer + *(int32_t*)(buffer + v8 - 8);
                    *dword_6934B0 = (int32_t)v9;
                    // Skip the load when this is a door-core (a2 == 1) whose
                    // decoded data size exceeds the expected range.
                    if (a2 != 1 || ((*(uint32_t*)(v9 + 12) - (*(uint16_t*)(v9 + 18) << 9) - 2576) & 0xFFFFFFF0) <= 0xA780)
                        gGameTable.vab_id[4] = ss_load_banks(4, v2, 0, 0);
                }
            }
        }

        // Handle to reach the implementation from the enclosing namespace:
        // `snd_load_core` is also the name of the public wrapper declared in
        // audio.h, so an unqualified reference from openre::audio would find
        // that wrapper rather than this function.
        void (*const snd_load_core_impl)(uint8_t, int) = &snd_load_core;
    }

    // Public wrapper declared in audio.h; used by C++ callers in other
    // translation units (room.cpp, title.cpp). Original-binary callers reach
    // the implementation above via the hook on 0x004EC450.
    void snd_load_core(uint8_t a0, uint8_t a1)
    {
        snd_load_core_impl(a0, a1);
    }

    namespace
    {
        // 0x004EC6D0
        static void snd_load_arms(uint8_t id)
        {
            char mem[32];
            std::strcpy(mem, "common\\sound\\arms\\arms00.   ");

            if (!gGameTable.enable_dsound)
                return;

            ss_unload_group(1);
            gGameTable.ss_name_arms[0] = 0;
            gGameTable.vab_id[1] = 0xFF;
            if (!id)
            {
                id = 1;
            }
            // mem[22] is the '0' in "arms00"; adding the tens digit to it (and
            // writing the ones hex digit below) forms "armsXY.edh".
            mem[22] += id >> 4;
            char v3;
            if ((id & 0xF) >= 0xA)
                v3 = (char)((id & 0xF) + 87);
            else
                v3 = (char)((id & 0xF) + 48);
            mem[23] = v3;
            std::memcpy(&mem[25], "edh", 3);

            auto v4 = read_file_into_buffer(mem, (char*)byte_6DEF0C, 1);
            if (v4)
            {
                if (v4 != (size_t)-1)
                {
                    auto v9 = byte_6DEF0C + *(int32_t*)(byte_6DEF0C + v4 - 8);
                    *dword_6934A4 = (int32_t)v9;
                    // Skip the load when the decoded data size exceeds the
                    // expected range (threshold 0x8CA0 for arms).
                    if (((*(uint32_t*)(v9 + 12) - (*(uint16_t*)(v9 + 18) << 9) - 2576) & 0xFFFFFFF0) <= 0x8CA0)
                        gGameTable.vab_id[1] = ss_load_banks(1 /* ST_ARMS */, id, 0, 0);
                }
            }
            else
            {
                file_error();
            }
        }
    }

    namespace
    {
        // 0x004EC7D0
        static char snd_room_load()
        {
            // Loads the room BGM bank (group 2) and enemy BGM bank (group 3)
            // for the current room, then loads the room VAB if the room data
            // provides one. Returns the low byte of the SsLoadBanks result.
            char result = 0;

            if (!gGameTable.enable_dsound)
                return result;

            ss_unload_group(2);
            gGameTable.ss_name_room[0] = 0;

            // Zero the first byte of each footstep-name slot (0x104 bytes
            // apart) up to the Main_vol global.
            auto v1 = gGameTable.ss_name_step;
            do
            {
                *v1 = 0;
                v1 += 260;
            } while ((uintptr_t)v1 < (uintptr_t)main_vol);

            ss_unload_group(3);
            gGameTable.ss_name_enemy[0] = 0;
            gGameTable.vab_id[2] = 0;
            gGameTable.vab_id[3] = 0;

            auto v0 = gGameTable.rdt->offsets[1];
            if (v0)
            {
                *byte_6941D0 = (uint8_t)(gGameTable.rdt->header.reverb_lv * 4);
                *dword_693B20 =
                    (*(uint32_t*)((uint8_t*)v0 + 12) - (*(uint16_t*)((uint8_t*)v0 + 18) << 9) - 2592) & 0xFFFFFFF0;
                auto rdt2 = (int32_t)(uintptr_t)gGameTable.rdt->offsets[0];
                pEdt_adr[5] = rdt2;
                pEdt_adr[2] = rdt2;
                *dword_6934A8 = (int32_t)(uintptr_t)v0;
                result = (char)ss_load_banks(2 /* ST_ROOM */, gGameTable.current_stage, gGameTable.current_room, 0);
                gGameTable.vab_id[2] = (uint8_t)result;
            }
            else
            {
                *dword_693B20 = 0;
            }

            return result;
        }

        // Handle to reach the implementation from the enclosing namespace:
        // `snd_room_load` is also the name of the public wrapper declared in
        // audio.h, so an unqualified reference from openre::audio would find
        // that wrapper rather than this function.
        char (*const snd_room_load_impl)() = &snd_room_load;
    }

    // Public wrapper declared in audio.h; used by C++ callers in other
    // translation units (room.cpp, title.cpp). Original-binary callers
    // (Result_init, Set_room, Title) reach the implementation above via the
    // hook on 0x004EC7D0.
    void snd_room_load()
    {
        snd_room_load_impl();
    }

    namespace
    {
        // 0x004EC8A0
        static void snd_load_em()
        {
            char mem[32];
            std::strcpy(mem, "common\\sound\\enemy\\enem00.   ");

            if (!gGameTable.enable_dsound)
                return;

            auto v0 = gGameTable.pad_98E543[0];
            ss_unload_group(3);
            gGameTable.ss_name_enemy[0] = 0;
            gGameTable.vab_id[3] = 0;
            if (v0 == 0xFF)
                return;

            // The .edh buffer is used aligned: byte_6DD31C = 0x6DD31C, so the
            // aligned base is 0x6DD310. mem[23]/mem[24] are the two hex digits
            // of the enemy id in "enem00.   ", and mem[26..28] get "edh" to
            // form "enemXY.edh".
            auto buffer = (uint8_t*)((uintptr_t)byte_6DD31C & 0xFFFFFFF0);
            auto v1 = v0 & 0xF;
            pEdt_adr[3] = (int32_t)buffer;
            mem[23] = (char)((v0 >> 4) + 48);
            char v2;
            if (v1 >= 0xA)
                v2 = (char)(v1 + 87);
            else
                v2 = (char)(v1 + 48);
            mem[24] = v2;
            std::memcpy(&mem[26], "edh", 3);

            auto v3 = read_file_into_buffer(mem, (char*)buffer, 1);
            if (v3)
            {
                // Unusual enemy ids 37/38 ("enem37.edh"/"enem38.edh") reuse a
                // single set of 61 extra banks when the room reverb level is 4.
                if ((v0 == 37 || v0 == 38) && *byte_6941D0 == 4)
                    v0 += 61;
                *dword_6934AC = (int32_t)(buffer + *(int32_t*)(buffer + v3 - 8));
                gGameTable.vab_id[3] = ss_load_banks(3 /* ST_ENEMY */, v0, 0, 0);
            }
            else
            {
                file_error();
            }
        }

        // Handle to reach the implementation from the enclosing namespace:
        // the public audio.h wrapper for this slot is `snd_load_enemy`, so an
        // unqualified reference from openre::audio would find that wrapper
        // rather than this function.
        void (*const snd_load_enemy_impl)() = &snd_load_em;
    }

    // Public wrapper declared in audio.h; used by C++ callers in other
    // translation units (room.cpp). Original-binary callers reach the
    // implementation above via the hook on 0x004EC8A0.
    void snd_load_enemy()
    {
        snd_load_enemy_impl();
    }

    // 0x004ec990

    // snd_bgm_main (0x004ECDA0) is defined later in this file; Snd_bgm_set
    // calls it, so declare it here.
    int snd_bgm_main();

    namespace
    {
        // Standalone globals used by Snd_bgm_set (0x004EC9C0). They fall in
        // the standalone BGM-control area around 0x689DB6/0x689DC0 or in
        // GameTable pad regions, so they are addressed here directly.
        uint8_t* byte_53C790 = (uint8_t*)0x53C790;    // stage -> BGM-table offset LUT
        uint16_t* main_bgm_id = (uint16_t*)0x689DB6;  // requested main BGM id
        uint16_t* subb_bmg_id = (uint16_t*)0x689DC0;  // requested sub BGM id
        int32_t* dword_689DC4 = (int32_t*)0x689DC4;   // 0 = BGM ids unchanged, 1 = main id changed
        void** dword_689DC8 = (void**)0x689DC8;       // decrescendo data pointer
        uint32_t* dword_6934B8 = (uint32_t*)0x6934B8; // SBGM decrescendo data pointer
        uint8_t* byte_6D130C = (uint8_t*)0x6D130C;    // SBGM bank data load area
        uint16_t* dword_69380C = (uint16_t*)0x69380C; // SBGM per-slot volumes, stride 8 bytes

        // 0x004ED050
        static int snd_bgm_sub()
        {
            if (!gGameTable.enable_dsound)
                return 1;

            if (check_flag(FlagGroup::System, FG_SYSTEM_DEMO))
                return 0;

            // Unload the two sub-BGM slots and the shared VAB when they are in
            // use (the control bytes hold -1/0xFF while free).
            if (gGameTable.byte_69380A > -1)
            {
                if (gGameTable.byte_693808)
                {
                    if (((DWORD)ss_get_status(6, 0) & 1) != 0)
                        ss_stop_group(6, 0);
                    gGameTable.byte_693808 = 0;
                }
                ss_unload_bgm(6, 0);
                gGameTable.ss_name_sbgm[0] = 0;
                gGameTable.byte_69380A = -1;
            }
            if ((int8_t)gGameTable.byte_693812 > -1)
            {
                if (gGameTable.byte_693810)
                {
                    if (((DWORD)ss_get_status(6, 1) & 1) != 0)
                        ss_stop_group(6, 1);
                    gGameTable.byte_693810 = 0;
                }
                ss_unload_bgm(6, 1);
                gGameTable.byte_693FA4 = 0;
                gGameTable.byte_693812 = 0xFF;
            }
            if ((int8_t)gGameTable.vab_id[6] > -1)
            {
                ss_unload_group(6);
                gGameTable.ss_name_sbgm[0] = 0;
                gGameTable.byte_693FA4 = 0;
                gGameTable.vab_id[6] = 0xFF;
            }

            // The sub-BGM id is the second byte of the current BGM-table
            // entry; 0xFF (-1) means no sub-BGM to play.
            auto v1 = gGameTable.current_bgm_address[1];
            if (v1 == 0xFF)
                return -1;

            // Build the file name "common\sound\bgm\sub_XX.bgm" from the low
            // 6 bits of the id (tens/ones hex digits).
            char mem[28];
            std::strcpy(mem, "common\\sound\\bgm\\sub_00.bgm");
            mem[21] = (char)(mem[21] + ((v1 & 0x3F) >> 4));
            auto ones = v1 & 0xF;
            mem[22] = ones >= 10 ? (char)(ones + 87) : (char)(ones + 48);

            auto buffer = (uint8_t*)(((uintptr_t)gGameTable.mem_top + 16) & 0xFFFFFFF0);
            auto numBytes = read_file_into_buffer(mem, (char*)buffer, 1);
            if (numBytes == -1)
                return -1;

            // The bank file header points at the sequence data to play; latch
            // it for the decrescendo logic used by Snd_bgm_set.
            *dword_6934B8 = (uint32_t)(byte_6D130C + *(int32_t*)(buffer + numBytes - 12));
            gGameTable.vab_id[6] = (uint8_t)ss_load_banks(6 /* ST_SBGM */, v1 & 0x3F, 0, 0);

            // Slot control bytes live in two 8-byte-strided pairs.
            for (auto i = 0; i < 2; i++)
            {
                *((uint8_t*)&gGameTable.byte_69380A + 8 * i)
                    = (uint8_t)((gGameTable.vab_id[6] >> i) & 1);
                if (bgm_ck_room(0, 4, -1) == 1 || bgm_ck_room(0, 0, -1) == 1)
                    ss_set_vol(6, i, dword_69380C[4 * i]);
                else
                    ss_set_vol(6, i, 0);
                *((uint8_t*)&gGameTable.byte_693808 + 8 * i) = 0;
            }
            return 0;
        }

        // 0x004EEE00
        static void ss_seq_set_decrescendo(int index, int a, int b)
        {
            using sig = void (*)(int, int, int);
            auto p = (sig)0x004EEE00;
            p(index, a, b);
        }

        // 0x004EC9C0
        static void snd_bgm_set()
        {
            if (!gGameTable.enable_dsound)
                return;

            if (((uint8_t*)gGameTable.ctcb)[15] != 1)
            {
                if (check_flag(FlagGroup::System, FG_SYSTEM_DEMO))
                    return;

                // Resolve the current stage/room to its BGM-table entry and
                // latch the requested main/sub ids from it.
                gGameTable.current_bgm_address = (uint8_t*)&gGameTable
                                                     .bgm_table[gGameTable.current_room
                                                         + byte_53C790[gGameTable.current_stage]];
                *main_bgm_id = *((uint8_t*)gGameTable.current_bgm_address);
                *subb_bmg_id = *((uint8_t*)gGameTable.current_bgm_address + 1);
                ((uint8_t*)gGameTable.ctcb)[15] = 1;
            }

            if (*fade_rtn)
            {
                task_sleep(1);
                return;
            }

            if ((*main_bgm_id & 0x3F) == (*bgm_main & 0x3F))
            {
                auto v1 = 0;
                *dword_689DC4 = 0;
                if ((*main_bgm_id & 0xFFC0) != 0)
                {
                    if (gGameTable.seq_ctr[0] && gGameTable.seq_ctr[0] != -1)
                    {
                        *dword_689DC8 = gGameTable.dword_6934B4;
                        ss_seq_set_decrescendo(0, 127, 90);
                        gGameTable.seq_ctr[0] = 15;
                    }
                }
                else if (gGameTable.seq_ctr[0] == 2)
                {
                    if (gGameTable.seq_ctr[2] > -1)
                    {
                        do
                            ss_set_vol(5, v1++, (uint16_t)gGameTable.dword_693804);
                        while (v1 < 3);
                    }
                    gGameTable.seq_ctr[1] = 0;
                }
            }
            else
            {
                *dword_689DC4 = 1;
                if (snd_bgm_main())
                {
                    gGameTable.seq_ctr[1] = -1;
                }
                else
                {
                    gGameTable.seq_ctr[1] = (int8_t)(*main_bgm_id >> 6);
                }
                if (((uint8_t*)gGameTable.ctcb)[19])
                    return;
            }

            if ((*subb_bmg_id & 0x3F) == (*bgm_sub & 0x3F) && *dword_689DC4 != 1)
            {
                if ((*subb_bmg_id & 0x40) != 0 && gGameTable.byte_693808)
                {
                    *dword_689DC8 = (void*)*dword_6934B8;
                    ss_seq_set_decrescendo(1, 127, 90);
                }
                if ((*subb_bmg_id & 0xFF80) != 0)
                {
                    if (gGameTable.byte_693810)
                    {
                        *dword_689DC8 = (void*)*dword_6934B8;
                        ss_seq_set_decrescendo(2, 127, 90);
                    }
                }
            }
            else
            {
                if (snd_bgm_sub())
                {
                    gGameTable.pad_693809[0] = -1;
                    gGameTable.pad_693811[0] = -1;
                }
                else
                {
                    gGameTable.pad_693809[0] = (*subb_bmg_id & 0x40) != 0;
                    gGameTable.pad_693811[0] = (uint8_t)(*subb_bmg_id >> 7);
                }
                if (((uint8_t*)gGameTable.ctcb)[19])
                    return;
            }

            *bgm_main = *((uint8_t*)gGameTable.current_bgm_address);
            *bgm_sub = *((uint8_t*)gGameTable.current_bgm_address + 1);
            ((uint8_t*)gGameTable.ctcb)[15] = 0;
        }

        // Handle to reach the implementation from the enclosing namespace:
        // `snd_bgm_set` is also the name of the public wrapper declared in
        // audio.h, so an unqualified reference from openre::audio would find
        // that wrapper rather than this function.
        void (*const snd_bgm_set_impl)() = &snd_bgm_set;

        // 0x004ED260
        static char snd_bgm_fade_on(uint8_t a1, char a2)
        {
            char result = (char)gGameTable.enable_dsound;
            if (gGameTable.enable_dsound)
            {
                result = (char)gGameTable.fg_system;
                if (!check_flag(FlagGroup::System, FG_SYSTEM_DEMO))
                {
                    *byte_693E8D = a2;
                    *fade_rtn = 1;
                    *fade_time = (int8_t)a1;
                    if (gGameTable.seq_ctr[0] == 1)
                        ss_seq_set_decrescendo(0, 127, a1);
                    if (gGameTable.byte_693808 == 1)
                        ss_seq_set_decrescendo(1, 127, (uint8_t)*fade_time);
                    result = (char)gGameTable.byte_693810;
                    if (gGameTable.byte_693810 == 1)
                        ss_seq_set_decrescendo(2, 127, (uint8_t)*fade_time);
                }
            }
            return result;
        }

        // 0x004ECBE0
        static void snd_bgm_ck()
        {
            if (!gGameTable.dword_99CF6C || !gGameTable.enable_dsound)
                return;

            if (((uint8_t*)gGameTable.ctcb)[15] == 1)
            {
                ((uint8_t*)gGameTable.ctcb)[15] = 0;
            }
            else if (!check_flag(FlagGroup::System, FG_SYSTEM_DEMO))
            {
                // Resolve the current stage/room to its BGM-table entry (same
                // index pattern as Snd_bgm_set) and check whether the main/sub
                // ids in the table still match what is currently playing.
                auto v0 = (uint8_t*)&gGameTable
                              .bgm_table[gGameTable.current_room
                                  + byte_53C790[gGameTable.current_stage]];
                gGameTable.current_bgm_address = v0;

                if (((*bgm_main ^ v0[0]) & 0x3F) != 0)
                {
                    snd_bgm_fade_on(0x5A, 22);
                }
                else
                {
                    if (((*bgm_sub ^ v0[1]) & 0x3F) != 0)
                    {
                        if (gGameTable.byte_693808 == 1
                            && ((DWORD)ss_get_status(6, 0) & 1) != 0)
                        {
                            ss_seq_set_decrescendo(1, 127, 90);
                            gGameTable.byte_693808 = 50;
                        }
                        if (gGameTable.byte_693810 == 1
                            && ((DWORD)ss_get_status(6, 1) & 1) != 0)
                        {
                            ss_seq_set_decrescendo(2, 127, 90);
                            gGameTable.byte_693810 = 50;
                        }
                    }
                    ((uint8_t*)gGameTable.ctcb)[15] = 1;
                    task_sleep(90);
                }
            }
        }

        // Handle to reach the implementation from the enclosing namespace:
        // `snd_bgm_ck` is also the name of the public wrapper declared in
        // audio.h, so an unqualified reference from openre::audio would find
        // that wrapper rather than this function.
        void (*const snd_bgm_ck_impl)() = &snd_bgm_ck;

        // 0x004ECCE0
        static void snd_bgm_play_ck()
        {
            if (gGameTable.dword_99CF6C && gGameTable.enable_dsound
                && !check_flag(FlagGroup::System, FG_SYSTEM_DEMO))
            {
                if (!gGameTable.seq_ctr[1] && gGameTable.seq_ctr[2] > -1)
                {
                    for (int i = 0; i < 3; ++i)
                        ss_play(5, i, 1);
                    gGameTable.seq_ctr[0] = 1;
                }
                if (!gGameTable.pad_693809[0] && gGameTable.byte_69380A > -1)
                {
                    ss_play(6, 0, 1);
                    gGameTable.byte_693808 = 1;
                }
                if (!gGameTable.pad_693811[0] && gGameTable.byte_693812 > -1)
                {
                    ss_play(6, 1, 1);
                    gGameTable.byte_693810 = 1;
                }
            }
        }

        // Handle to reach the implementation from the enclosing namespace:
        // `snd_bgm_play_ck` is also the name of the public wrapper declared in
        // audio.h, so an unqualified reference from openre::audio would find
        // that wrapper rather than this function.
        void (*const snd_bgm_play_ck_impl)() = &snd_bgm_play_ck;

        // 0x004ED2F0
        static char snd_bgm_ctr(uint32_t a1)
        {
            char result = (char)gGameTable.enable_dsound;
            if (!gGameTable.enable_dsound || check_flag(FlagGroup::System, FG_SYSTEM_DEMO))
                return result;

            // Per-slot volume overrides for the Raccoon City police station
            // lobby (stage 0, room 8) during the intro (cut 0) and the
            // helicopter crash (cut 13) cutscenes.
            const uint8_t v20[3] = { 72, 57, 29 };
            const uint8_t v21[3] = { 0, 69, 29 };

            int v2 = 1;                 // loop flag passed to SsPlay
            uint8_t v19 = (uint8_t)a1;  // pan byte (low byte of a1)
            int v3 = (a1 >> 28) & 0xFF; // BGM slot index (0..15)

            // Stage/room switch: decides whether the SsPlay loop flag (v2) is
            // cleared for the selected BGM slot. Every non-matching stage/room
            // combination leaves v2 at its default of 1.
            switch (gGameTable.current_stage)
            {
            case 0:
                switch (gGameTable.current_room)
                {
                case 19:
                case 20:
                case 21:
                    if (v3 == 1)
                        v2 = 0;
                    break;
                default:
                    break;
                }
                break;
            case 1:
                switch (gGameTable.current_room)
                {
                case 6:
                case 27:
                    if (v3 == 2)
                        v2 = 0;
                    break;
                case 25:
                    v2 = 0;
                    break;
                default:
                    break;
                }
                break;
            case 2:
                switch (gGameTable.current_room)
                {
                case 0:
                    if (v3 == 2)
                        v2 = 0;
                    else if (v3 == 0)
                    {
                        if (!check_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE))
                            v2 = 0;
                    }
                    break;
                case 1:
                    v2 = 0;
                    break;
                case 4:
                case 8:
                case 10:
                case 11:
                    if (v3 == 1)
                        v2 = 0;
                    break;
                default:
                    break;
                }
                break;
            case 3:
                switch (gGameTable.current_room)
                {
                case 0:
                    if (v3 == 2 && (uint8_t)gGameTable.byte_98E9A6 < 0xA
                        && (gGameTable.byte_98E9A6 & 1) != 0)
                        v2 = 0;
                    break;
                case 7:
                    if (v3 == 2)
                        v2 = 0;
                    break;
                default:
                    break;
                }
                break;
            case 5:
                switch (gGameTable.current_room)
                {
                case 12:
                    if (v3 == 1 || v3 == 2)
                        v2 = 0;
                    break;
                case 20:
                    if (v3 == 2)
                        v2 = 0;
                    break;
                default:
                    break;
                }
                break;
            default:
                break;
            }

            // Common path for every stage/room outcome.
            result = (char)bgm_ck_room(0, 11, 13);
            if (result == 1)
            {
                *(uint16_t*)&gGameTable.dword_693804 = 63;
                v19 = 64;
            }
            if (check_flag(FlagGroup::System, FG_SYSTEM_10) && v3 == 1)
                v2 = 0;
            int v6 = v3;
            auto seq_ctr = (int8_t*)gGameTable.seq_ctr;
            if (seq_ctr[8 * v6 + 1] != -1 && seq_ctr[8 * v6 + 2] != -1)
            {
                // Base of the per-slot volume/pan table: the SBGM sequence
                // data (dword_6934B8 holds the loaded sequence pointer) or
                // the main BGM data.
                uint8_t* v18 = v3 ? (uint8_t*)*dword_6934B8 : gGameTable.dword_6934B4;

                switch ((a1 >> 24) & 0xF)
                {
                case 1: // play
                    if (v3) // SBGM (type 6)
                    {
                        ss_set_vol(6, v6 - 1, ((uint16_t*)&gGameTable.dword_693804)[4 * v6]);
                        ss_play(6, v6 - 1, v2);
                    }
                    else // main BGM (type 5)
                    {
                        for (int i = 0; i < 3; i++)
                        {
                            ss_set_vol(5, 0, (uint16_t)gGameTable.dword_693804);
                            ss_play(5, i, v2);
                        }
                    }
                    seq_ctr[8 * v6] = 1;
                    break;

                case 2: // resume/continue (only while the slot was started)
                    if (seq_ctr[8 * v6] != 0)
                    {
                        if (v3) // SBGM
                        {
                            if (((uintptr_t)ss_get_status(6, v6 - 1) & 1) != 0)
                            {
                                ss_set_vol(6, v6 - 1, ((uint16_t*)&gGameTable.dword_693804)[4 * v6]);
                                ss_stop_group(6, v6 - 1);
                            }
                        }
                        else if (((uintptr_t)ss_get_status(5, 0) & 1) != 0)
                        {
                            for (int j = 0; j < 3; j++)
                                ss_set_vol(5, j, (uint16_t)gGameTable.dword_693804);
                            ss_stop_group(5, -1);
                        }
                        seq_ctr[8 * v6] = 2;
                    }
                    break;

                case 3: // play (restart)
                    if (v3)
                    {
                        ss_set_vol(6, v6 - 1, ((uint16_t*)&gGameTable.dword_693804)[4 * v6]);
                        ss_play(6, v6 - 1, 0);
                    }
                    else
                    {
                        for (int k = 0; k < 3; k++)
                        {
                            ss_set_vol(5, k, (uint16_t)gGameTable.dword_693804);
                            ss_play(5, k, 1);
                        }
                    }
                    seq_ctr[8 * v6] = 1;
                    break;

                case 4: // stop
                    if (v3)
                    {
                        ss_set_vol(6, v6 - 1, 0);
                        ss_stop_group(6, v6 - 1);
                    }
                    else
                    {
                        for (int m = 0; m < 3; m++)
                        {
                            ss_set_vol(5, m, 0);
                            ss_stop_group(5, m);
                        }
                    }
                    seq_ctr[8 * v6] = 4;
                    break;

                case 5: // decrescendo
                    ss_seq_set_decrescendo((uint8_t)v3, 127, 90);
                    seq_ctr[8 * v6] = 50;
                    break;

                default:
                    break;
                }

                // Volume/pan update, runs after every switch case (including
                // the default). `result` mirrors AL: each SsSetVol/SsSetPan
                // return value (low byte) is what gets returned.
                result = (char)((a1 >> 8) & 0xFF);
                if (v3) // SBGM (type 6)
                {
                    if (((a1 >> 8) & 0xFF) != 0)
                    {
                        uint8_t a1low = (uint8_t)((a1 >> 8) & 0xFF) - 1;
                        if (((a1 >> 16) & 0xFF) != 0)
                        {
                            v18[16 * ((a1 >> 16) & 0xFF) + 17] = a1low;
                            result = (char)ss_set_vol(6, ((a1 >> 16) & 0xFF) - 1, a1low);
                        }
                        else
                        {
                            v18[24] = a1low;
                            for (int i = 0; i < 2; i++)
                                result = (char)ss_set_vol(6, i, a1low);
                        }
                    }
                    if (v19 != 0)
                    {
                        if (((a1 >> 16) & 0xFF) != 0)
                        {
                            v18[16 * ((a1 >> 16) & 0xFF) + 20] = v19 - 1;
                            result = (char)(uint8_t)(uintptr_t)ss_set_pan(6, ((a1 >> 16) & 0xFF) - 1, v19 - 1);
                        }
                        else
                        {
                            v18[25] = v19 - 1;
                            for (int i = 0; i < 2; i++)
                                result = (char)(uint8_t)(uintptr_t)ss_set_pan(6, i, v19 - 1);
                        }
                    }
                }
                else // main BGM (type 5)
                {
                    if (((a1 >> 8) & 0xFF) != 0)
                    {
                        uint8_t a1low = (uint8_t)((a1 >> 8) & 0xFF) - 1;
                        if (((a1 >> 16) & 0xFF) != 0)
                        {
                            v18[16 * ((a1 >> 16) & 0xFF) + 17] = a1low;
                            result = (char)ss_set_vol(5, ((a1 >> 16) & 0xFF) - 1, a1low);
                        }
                        else
                        {
                            v18[24] = a1low;
                            for (int i = 0; i < 3; i++)
                            {
                                if (gGameTable.current_stage == 0 && gGameTable.current_room == 8)
                                {
                                    if (gGameTable.current_cut == 0)
                                        a1low = v20[i];
                                    if (gGameTable.current_cut == 13)
                                        a1low = v21[i];
                                }
                                result = (char)ss_set_vol(5, i, a1low);
                            }
                        }
                    }
                    if (v19 != 0)
                    {
                        if (((a1 >> 16) & 0xFF) != 0)
                        {
                            v18[16 * ((a1 >> 16) & 0xFF) + 20] = v19 - 1;
                            result = (char)(uint8_t)(uintptr_t)ss_set_pan(5, ((a1 >> 16) & 0xFF) - 1, v19 - 1);
                        }
                        else
                        {
                            v18[25] = v19 - 1;
                            for (int i = 0; i < 3; i++)
                                result = (char)(uint8_t)(uintptr_t)ss_set_pan(5, i, v19 - 1);
                        }
                    }
                }
            }
            return result;
        }

        // Handle to reach the implementation from the enclosing namespace.
        char (*const bgm_set_control_impl)(uint32_t) = &snd_bgm_ctr;
    }

    // Public wrapper declared in audio.h; used by C++ callers in other
    // translation units (room.cpp). Original-binary callers reach the
    // implementation above via the hook on 0x004EC9C0.
    void snd_bgm_set()
    {
        snd_bgm_set_impl();
    }

    // 0x004ECBE0
    void snd_bgm_ck()
    {
        snd_bgm_ck_impl();
    }

    // 0x004ECCE0
    void snd_bgm_play_ck()
    {
        snd_bgm_play_ck_impl();
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
                auto uVar3 = (DWORD)ss_get_status(5, 0);
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
                auto uVar3 = (DWORD)ss_get_status(5, 1);
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
                auto uVar3 = (DWORD)ss_get_status(5, 2);
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
        snd_bgm_ctr(arg0);
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

    // 0x451780
    static unsigned int square_root0(int a1)
    {
        using sig = unsigned int (*)(int);
        auto p = (sig)0x451780;
        return p(a1);
    }

    // 0x4E3440
    static int sca_ck_line(int* a1, int* a2, unsigned int a3, int a4)
    {
        using sig = int (*)(int*, int*, unsigned int, int);
        auto p = (sig)0x4E3440;
        return p(a1, a2, a3, a4);
    }

    // 0x450930 (catan - pure math atan2, implemented inline; not on the checklist)
    static int64_t catan(int a1)
    {
        return (int64_t)(atan2((double)a1 * 0.000244140625, 1.0) * 2048.0 * 0.3184713375796178);
    }

    // 0x004EEBD0
    static int16_t snd_se_dir_ck(int a1, int a2, int a3, int a4)
    {
        int v4 = a4 - a2;
        if (a3 == a1)
            return (int16_t)(((v4 > 0) << 11) + 1024);
        return (int16_t)((-(int16_t)catan((v4 << 12) / (a3 - a1)) - ((a3 - a1 < 0) << 11)) & 0xFFF);
    }

    // 0x004EEDD0 (Xa_set_volume - temp thunk, will be replaced by a later agent)
    static uint8_t xa_set_volume()
    {
        using sig = uint8_t (*)();
        auto p = (sig)0x004EEDD0;
        return p();
    }

    // 0x004EEC30
    static void xa_play(int mode, int no)
    {
        int v2;
        if (mode)
            v2 = 0;
        else
            v2 = (int)gGameTable.current_stage + 1;

        *dword_693464 = 0;

        if (gGameTable.enable_dsound)
        {
            ss_load_banks(7 /* ST_VOICE */, v2, no, (int)(gGameTable.fg_status >> 31));
            *byte_693470 = 1;
            *byte_69346E = 1;
            xa_set_volume();
            gGameTable.fg_status |= 0x20u;
            ss_play(7, 0, 0);
        }
        else
        {
            *dword_693464 = ss_voice_load(v2, no);
            *byte_693470 = 1;
            *byte_69346E = 1;
        }
    }

    // 0x004EECD0
    static uint8_t xa_stop()
    {
        uint8_t result = gGameTable.enable_dsound;
        if (gGameTable.enable_dsound)
        {
            ss_stop_group(7, -1);
            result = xa_set_volume();
            *byte_69346E = 3;
        }
        return result;
    }

    // 0x004EED10
    static char xa_control_stop()
    {
        *byte_693470 = 0;
        *byte_69346E = 0;
        uint32_t v0 = gGameTable.fg_status;
        gGameTable.fg_status = v0 & ~0x20u; // clear XA voice playing flag in low byte
        return (char)v0;
    }

    // 0x004EED30
    static void xa_control_init()
    {
        *byte_69346E = 2;
    }

    // 0x004EED40 (Xa_control_play - temp thunk, will be replaced by a later agent)
    static int xa_control_play()
    {
        using sig = int (*)();
        auto p = (sig)0x004EED40;
        return p();
    }

    // 0x004EED80 (Xa_control_end - temp thunk, will be replaced by a later agent)
    static char xa_control_end()
    {
        using sig = char (*)();
        auto p = (sig)0x004EED80;
        return p();
    }

    // 0x004EED00
    static int xa_control()
    {
        int result = 0;
        switch (*byte_69346E)
        {
        case 0:
            result = (uint8_t)xa_control_stop();
            break;
        case 1:
            xa_control_init();
            break;
        case 2:
            result = (uint8_t)xa_control_play();
            break;
        case 3:
            result = (uint8_t)xa_control_end();
            break;
        }
        return result;
    }

    // 0x004EE780
    static int snd_se_3d(const Vec32* pos, int a2)
    {
        if (!gGameTable.enable_dsound)
            return 0;

        int32_t* v4 = (int32_t*)((uint8_t*)gGameTable.rdt->offsets[7] + 32 * (int)(int16_t)gGameTable.current_cut);
        int32_t* v5 = v4 + 1;

        int v35 = std::abs(v4[1] - pos->x);
        int v36 = std::abs(v4[2] - pos->y);
        int v37 = std::abs(v4[3] - pos->z);

        // Function-scope variables used by both branches; declared here so the
        // mono-branch `goto label_31` does not skip their initialization.
        int v17, v18, v19, v20, v21, v22, v23, v24, v25;

        if (gGameTable.audio_SpeakerConfig == 1)
        {
            unsigned int v11 = square_root0(v35 * v35 + v37 * v37);
            unsigned int v12 = square_root0(v35 * v35 + v37 * v37);
            int v13 = (int)square_root0(v36 * v36 + v12 * v11) / 250;
            *sesz = v13;
            if (v13 > 127)
            {
                v13 = 127;
                *sesz = 127;
            }
            *vol_3d_pan = 0;
            int64_t v13wide = 166658258613LL * (int64_t)v13;
            int v14 = (int)(((uint64_t)v13wide) >> 32) >> 6;
            *vol_3d_l = (int16_t)(127 - ((v14 >> 31) + v14));
            *vol_3d_r = *vol_3d_l;

            int point[3];
            point[0] = pos->x;
            point[1] = pos->y - 1500;
            point[2] = pos->z;
            if (a2 == 2 || !sca_ck_line(v5, point, 0x8400, 1))
                goto label_31;

            v17 = 65 * (uint16_t)*vol_3d_l / 100;
            v18 = 65 * *vol_3d_r; // NOTE: signed Vol_3D_r here, NOT uint16 cast
            *vol_3d_l = (int16_t)v17;
            *vol_3d_r = (int16_t)(v18 / 100);
            goto label_31;
        }

        *sesz = (int)square_root0((unsigned int)(v36 * v36 + v37 * v37)) / 250;
        if (*sesz > 127)
            *sesz = 127;

        v19 = snd_se_dir_ck(*v5, v4[3], pos->x, pos->z);
        v20 = snd_se_dir_ck(*v5, v4[3], v4[4], v4[6]);
        v21 = snd_se_dir_ck(0, 0, *v5, v4[3]);
        v22 = v4[3];
        v23 = 0;
        v24 = v19 - v21;
        v25 = v20 - snd_se_dir_ck(0, 0, *v5, v22);
        int16_t v26;
        if (v24 >= v25)
            v26 = (int16_t)(v24 - v25);
        else
            v26 = (int16_t)(v24 - v25 + 4096);
        *word_693B3C = (uint16_t)v26;

        if (!v26 || v26 == 4096 || v26 == 2048)
        {
            *vol_3d_pan = 0;
        }
        else
        {
            int v27, v28, v29, v30;
            if ((v26 & 0x800) != 0)
            {
                v27 = 1;
            }
            else
            {
                v27 = 0;
                v23 = 1;
            }
            v28 = v26 & 0x7FF;
            v29 = 1;
            if ((v28 & 0x400) != 0)
            {
                v29 = 0;
                v28 = 2048 - v28;
            }
            v30 = v28 / 8;
            *word_693B3C = (uint16_t)v30;

            double angle = (128.0 - (double)v30) * 0.6666666865348816 * 0.01745329238474369;
            double factor;
            switch (v27 + 2 * (v23 + 2 * v29))
            {
            case 1:
            case 5:
                factor = -std::cos(angle);
                break;
            case 2:
            case 6:
                factor = std::cos(angle);
                break;
            default:
                factor = *(float*)&a2; // unreachable
                break;
            }
            *vol_3d_pan = (int16_t)((int)(*sesz * factor));
        }

        *vol_3d_l = (int16_t)(127 - (int16_t)(77 * *sesz) / 127);
        *vol_3d_r = *vol_3d_l;

        int point[3];
        point[0] = pos->x;
        point[1] = pos->y - 1500;
        point[2] = pos->z;
        if (a2 == 2 || !sca_ck_line(v5, point, 0x8400, 1))
            goto label_31;

        v17 = 85 * (uint16_t)*vol_3d_l / 100;
        v18 = 85 * *vol_3d_r; // signed Vol_3D_r
        *vol_3d_l = (int16_t)v17;
        *vol_3d_r = (int16_t)(v18 / 100);

    label_31:
        if ((uint16_t)*vol_3d_l > 0x7F)
            *vol_3d_l = 127;
        if ((uint16_t)*vol_3d_r > 0x7F)
            *vol_3d_r = 127;
        return 127;
    }

    // 0x004ED950
    static void snd_se_on_impl(int a1, const Vec32* a2)
    {
        if (!gGameTable.enable_dsound)
            return;

        int v2 = (a1 >> 24) & 0xFF;      // HIBYTE(a1)
        int v3 = (a1 >> 16) & 0xFF;      // BYTE2(a1)
        int a1a = a1 & 0xFF;
        int8_t v4 = (int8_t)gGameTable.vab_id[v2];

        uint8_t* v5 = (uint8_t*)pEdt_adr[v2];
        if (!v5)
            return;

        int32_t v6 = *(int32_t*)(v5 + 4 * v3);
        if (gGameTable.dword_99CF6C)
        {
            if (v6 == -1 || !v4)
                return;
        }
        else
        {
            if ((v6 == -1 || !v4) && v3 == 8 && v2 == 4)
            {
                v5[4 * v3 + 0] = 0;
                v5[4 * v3 + 1] = 0;
                v5[4 * v3 + 2] = 0x53;
                v5[4 * v3 + 3] = 6;
            }
        }

        int v8 = v5[4 * v3 + 1] & 0x7F;
        int32_t v7 = dword_6934A0[v2];
        if (!v7)
            return;

        uint8_t* v9 = (uint8_t*)(v7 + 32 * ((v5[4 * v3 + 2] >> 4) + 16 * v8 + 0x41));

        if (!gGameTable.dword_99CF6C && v2 == 4)
            v9[2] = 110;

        if (!check_flag(FlagGroup::System, FG_SYSTEM_EX_BATTLE) || v2 != 0)
        {
            if (v2 == 2 && v3 == 20)
                v9[2] = 110;
        }
        else if (bgm_ck_room(4, 4, 7) || bgm_ck_room(2, 10, 5) || bgm_ck_room(2, 4, 3))
        {
            v9[2] = 110;
        }

        if (gGameTable.current_stage == 3 && gGameTable.current_room == 4 && gGameTable.pl.id == 14 && v2 == 2)
        {
            if (v3 >= 0xA && v3 <= 0xE)
                a1a = 1;
            if (v3 >= 0xF && v3 <= 0x11)
                v9[2] = 110;
        }

        int v10, v11;
        if (a1a)
        {
            snd_se_3d(a2, a1a);
            if (v2 == 1 && !v8)
            {
                *vol_3d_l += 10;
                *vol_3d_r += 10;
            }
            if (bgm_ck_room(0, 10, -1) && v3 == 10)
            {
                *vol_3d_l += 20;
                v10 = *vol_3d_r + 20;
                *vol_3d_r = (int16_t)v10;
            }
            else
            {
                v10 = *vol_3d_r;
            }
            v11 = *vol_3d_l;
            if ((uint16_t)*vol_3d_l > 0x7F)
            {
                v11 = 127;
                *vol_3d_l = 127;
            }
            if (v10 > 0x7F)
            {
                v10 = 127;
                goto label_48;
            }
        }
        else
        {
            v11 = (int16_t)(int8_t)v9[2];
            *vol_3d_l = v11;
            v10 = (int16_t)(int8_t)v9[2];
            *vol_3d_pan = 0;
            goto label_48;
        }

    label_48:
        *vol_3d_r = (int16_t)v10;

        if (!gGameTable.sfx_vol)
            return;

        int v12 = (v10 <= v11) ? (uint16_t)*vol_3d_l : v10;
        ss_set_vol(v2, v3, v12);
        ss_set_pan(v2, v3, *vol_3d_pan);

        int v13 = 0;
        if (gGameTable.current_stage == 0 && gGameTable.current_room == 18 && v2 == 2 && v3 == 7)
            ss_play(2, 7, 1);
        if (gGameTable.current_stage == 3 && gGameTable.current_room == 8 && v2 == 2 && v3 == 15)
            ss_play(2, 47, 1);
        if (gGameTable.current_stage == 5)
        {
            if (gGameTable.current_room == 1)
            {
                if (v2 == 2 && v3 == 13)
                    ss_play(2, 47, 1);
            }
            else if (gGameTable.current_room == 12 && v2 == 2 && v3 == 13)
            {
                v13 = 1;
            }
        }
        ss_play(v2, v3, v13);

        if (gGameTable.current_stage == 1)
        {
            if (gGameTable.current_room != 0)
                goto label_79;
            if (v2 != 2 || v3 != 18)
                return;
            ss_set_vol(2, 0x13, 127);
            ss_set_pan(2, 0x13, 0);
            ss_play(2, 0x13, 0);
            // scd_var_stage == 1 is always true here, fall through to LABEL_79
        label_79:
            if (gGameTable.current_room != 26 || v2 != 2 || v3 != 14)
                return;
            int v14 = (*vol_3d_r <= *vol_3d_l) ? *vol_3d_l : *vol_3d_r;
            ss_set_vol(2, 0x0A, v14);
            ss_set_pan(2, 0x0A, (int16_t)((uint16_t)*vol_3d_r - (uint16_t)*vol_3d_l));
            ss_play(2, 0x0A, 0);
        }

        if (gGameTable.current_stage == 0 && gGameTable.current_room == 18 && v2 == 2 && v3 == 8)
            ss_stop_group(2, 7);
        if (gGameTable.current_stage == 3)
        {
            if (gGameTable.current_room != 8 || v2 != 2)
                return;
            if (v3 == 14)
            {
                ss_stop_group(2, 47);
                return;
            }
            if (v3 != 16)
                return;
            ss_stop_group(2, 47);
        }
        if (gGameTable.current_stage == 5)
        {
            if (gGameTable.current_room == 1)
            {
                if (v2 != 2 || v3 != 14)
                    return;
                ss_stop_group(2, 47);
            }
            if (gGameTable.current_room == 12 && v2 == 2 && v3 == 14)
                ss_stop_group(2, 13);
        }
    }

    void snd_se_on(int a0, const Vec32& a1)
    {
        snd_se_on_impl(a0, &a1);
    }

    void snd_se_on(int a0)
    {
        snd_se_on_impl(a0, nullptr);
    }

    // 0x004EE440
    static void snd_bgm_fade()
    {
        if (gGameTable.enable_dsound && !check_flag(FlagGroup::System, FG_SYSTEM_DEMO))
        {
            if (*fade_rtn)
            {
                if (*fade_rtn == 1)
                {
                    --*fade_time;
                    auto* mv = (SoundVolume*)main_vol;
                    if (*byte_693E8D == 24)
                    {
                        mv->left = (int16_t)((int16_t)(98 * mv->left) / 100);
                        int v2 = 0;
                        mv->right = (int16_t)((int16_t)(98 * mv->right) / 100);
                        do
                        {
                            ss_set_vol(5, v2, ((uint16_t)mv->left + (uint16_t)mv->right) / 2);
                            ++v2;
                        } while (v2 < 3);
                        ss_set_vol(6, 0, ((uint16_t)mv->left + (uint16_t)mv->right) / 2);
                        ss_set_vol(6, 1, ((uint16_t)mv->left + (uint16_t)mv->right) / 2);
                    }
                    if (*fade_time == 1)
                    {
                        if (gGameTable.seq_ctr[0])
                        {
                            gGameTable.seq_ctr[0] = 0;
                            for (int i = 0; i < 3; ++i)
                            {
                                if (((DWORD)ss_get_status(5, i) & 1) != 0)
                                    ss_stop_group(5, i);
                            }
                        }
                        uint8_t* p693808 = (uint8_t*)&gGameTable.byte_693808;
                        for (int j = 0; j < 2; ++j)
                        {
                            if (p693808[8 * j])
                            {
                                p693808[8 * j] = 0;
                                if (((DWORD)ss_get_status(6, j) & 1) != 0)
                                    ss_stop_group(6, j);
                            }
                        }
                        if (*byte_693E8D == 24)
                        {
                            uint16_t left = 127;
                            int v6 = 0;
                            *main_vol = 8323199;  // 0x7F007F: left=0x7F, right=0x7F
                            do
                            {
                                ss_set_vol(5, v6, (left + (uint16_t)mv->right) / 2);
                                left = mv->left;
                                ++v6;
                            } while (v6 < 3);
                            ss_set_vol(6, 0, ((uint16_t)mv->left + (uint16_t)mv->right) / 2);
                            ss_set_vol(6, 1, ((uint16_t)mv->left + (uint16_t)mv->right) / 2);
                        }
                        *fade_rtn = 2;
                    }
                }
                else if (*fade_rtn == 2)
                {
                    ss_unload_group(5);
                    for (char* v0 = (char*)gGameTable.ss_name_bgm; (uintptr_t)v0 < 0x6937ECu; v0 += 260)
                        *v0 = 0;
                    ss_unload_group(6);
                    for (char* v1 = (char*)gGameTable.ss_name_sbgm; (uintptr_t)v1 < (uintptr_t)rev_vol; v1 += 260)
                        *v1 = 0;
                    *fade_time = 0;
                    *fade_rtn = 0;
                }
            }
            else
            {
                if ((uint8_t)gGameTable.seq_ctr[0] > 5 && --gGameTable.seq_ctr[0] == 5)
                {
                    gGameTable.seq_ctr[0] = 0;
                    for (int k = 0; k < 3; ++k)
                    {
                        if (((DWORD)ss_get_status(5, k) & 1) != 0)
                            ss_stop_group(5, k);
                        *dword_689DD8 = 0;
                    }
                }
                if (gGameTable.byte_693808 > 5 && --gGameTable.byte_693808 == 5)
                {
                    gGameTable.byte_693808 = 0;
                    if (((DWORD)ss_get_status(6, 0) & 1) != 0)
                    {
                        ss_stop_group(6, 0);
                        *dword_689DDC = 0;
                    }
                }
                if ((uint8_t)gGameTable.byte_693810 > 5 && --gGameTable.byte_693810 == 5)
                {
                    gGameTable.byte_693810 = 0;
                    if (((DWORD)ss_get_status(6, 1) & 1) != 0)
                    {
                        ss_stop_group(6, 1);
                        *dword_689DE0 = 0;
                    }
                }
            }
        }
    }

    // 0x004EE350
    static void snd_se_call()
    {
        if (gGameTable.enable_dsound)
        {
            for (int i = 0; i < 3; ++i)
            {
                int v1 = ss_timer[i];
                if (v1)
                {
                    ss_timer[i] = v1 - 1;
                    if (i)
                    {
                        if (i == 1)
                        {
                            int v3 = ss_get_volume(6, 0) - ss_vol[1];
                            if (v3 < 0)
                                v3 = 0;
                            if (ss_set_vol(6, 0, v3) == 1)
                                *dword_689DDC = 1;
                        }
                        else if (i == 2)
                        {
                            int v2 = ss_get_volume(6, 1) - ss_vol[2];
                            if (v2 < 0)
                                v2 = 0;
                            if (ss_set_vol(6, 1, v2) == 1)
                                *dword_689DE0 = 1;
                        }
                    }
                    else
                    {
                        for (int j = 0; j < 3; ++j)
                        {
                            int v5 = ss_get_volume(5, j) - ss_vol[0];
                            if (v5 < 0)
                                v5 = 0;
                            ss_set_vol(5, j, v5);
                        }
                        *dword_689DD8 = 1;
                    }
                }
            }
            snd_bgm_fade();
        }
    }

    // 0x004329B0
    static BOOL __stdcall acmDriverEnumCallback(HACMDRIVERID hadid, DWORD dwInstance, DWORD fdwSupport)
    {
        ACMDRIVERDETAILSA padd{};
        padd.cbStruct = sizeof(ACMDRIVERDETAILSA);

        acmDriverDetailsA(hadid, &padd, 0);

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

    // 0x00436810
    static void bgm_channels_init()
    {
        interop::call<void>(0x00436820);
        atexit([] { interop::call<void>(0x004368A0); });
    }

    void bgm_init_hooks()
    {
        interop::writeJmp(0x004329B0, &acmDriverEnumCallback);
        interop::writeJmp(0x00433830, &ss_close);
        interop::writeJmp(0x004338F0, &ss_play);
        interop::writeJmp(0x00433C40, &ss_stop_all);
        interop::writeJmp(0x00433DC0, &ss_shutdown);
        interop::writeJmp(0x00433F10, &ss_unload_group);
        interop::writeJmp(0x00434140, &ss_unload_bgm);
        interop::writeJmp(0x004341E0, &ss_stop_group);
        interop::writeJmp(0x004344A0, &ss_load_banks);
        interop::writeJmp(0x004347B0, &ss_get_status);
        interop::writeJmp(0x004348F0, &ss_set_pan);
        interop::writeJmp(0x00434AB0, &ss_set_vol);
        interop::writeJmp(0x00434CF0, &ss_get_volume);
        interop::writeJmp(0x00434EA0, &ss_load_sap);
        interop::writeJmp(0x00435170, &ss_load_steps);
        interop::writeJmp(0x00435300, &ss_load_bgm);
        interop::writeJmp(0x00435610, &ss_init_2);
        interop::writeJmp(0x00435540, &ss_init_buffers);
        interop::writeJmp(0x00435930, &ss_create_buffer);
        interop::writeJmp(0x00436470, &ss_voice_load);
        interop::writeJmp(0x00436590, &ss_voice_parse);
        interop::writeJmp(0x00436810, &bgm_channels_init);
        // snd_sys_init2_impl: the hook targets the static implementation
        // (snd_sys_init2) via its handle, since the name snd_sys_init2 is the
        // public audio.h wrapper in this scope.
        interop::writeJmp(0x004EC250, snd_sys_init2_impl);
        interop::writeJmp(0x004EC350, &snd_sys_init_sub);
        // snd_sys_init_sub2_impl: the hook targets the static implementation
        // via its handle, since the name snd_sys_init_sub2 is the public
        // audio.h wrapper in this scope.
        interop::writeJmp(0x004EC410, snd_sys_init_sub2_impl);
        // snd_load_core_impl: the hook targets the static implementation via
        // its handle, since the name snd_load_core is the public audio.h
        // wrapper in this scope.
        interop::writeJmp(0x004EC450, snd_load_core_impl);
        interop::writeJmp(0x004EC6D0, &snd_load_arms);
        interop::writeJmp(0x004EC7D0, snd_room_load_impl);
        // snd_load_enemy_impl: the hook targets the static implementation via
        // its handle, since the name snd_load_enemy is the public audio.h
        // wrapper in this scope.
        interop::writeJmp(0x004EC8A0, snd_load_enemy_impl);
        // snd_bgm_set_impl: the hook targets the static implementation via
        // its handle, since the name snd_bgm_set is the public audio.h
        // wrapper in this scope.
        interop::writeJmp(0x004EC9C0, snd_bgm_set_impl);
        // snd_bgm_ck_impl: the hook targets the static implementation via its
        // handle, since the name snd_bgm_ck is the public audio.h wrapper in
        // this scope.
        interop::writeJmp(0x004ECBE0, snd_bgm_ck_impl);
        // snd_bgm_play_ck_impl: the hook targets the static implementation via
        // its handle, since the name snd_bgm_play_ck is the public audio.h
        // wrapper in this scope.
        interop::writeJmp(0x004ECCE0, snd_bgm_play_ck_impl);
        interop::writeJmp(0x004ECDA0, snd_bgm_main);
        interop::writeJmp(0x004ED050, &snd_bgm_sub);
        interop::writeJmp(0x004ED260, &snd_bgm_fade_on);
        interop::writeJmp(0x004ED2F0, bgm_set_control_impl);
        interop::writeJmp(0x004ED920, bgm_set_entry);
        interop::writeJmp(0x004ED950, &snd_se_on_impl);
        interop::writeJmp(0x004EE350, &snd_se_call);
        interop::writeJmp(0x004EE440, &snd_bgm_fade);
        interop::writeJmp(0x004EE780, &snd_se_3d);
        interop::writeJmp(0x004EEBD0, &snd_se_dir_ck);
        interop::writeJmp(0x004EEC30, &xa_play);
        interop::writeJmp(0x004EECD0, &xa_stop);
        interop::writeJmp(0x004EED00, &xa_control);
        interop::writeJmp(0x004EED10, &xa_control_stop);
        interop::writeJmp(0x004EED30, &xa_control_init);
    }
}
