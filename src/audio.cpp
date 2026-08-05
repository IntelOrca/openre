#include "audio.h"
#include "file.h"
#include "interop.hpp"
#include "logger.h"
#include "openre.h"

#include <algorithm>
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
    static int ss_get_status(int type, int sub);
    static int ss_stop_all();
    static int ss_stop_group(int type, int id);
    static int ss_shutdown();
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
        interop::writeJmp(0x00434EA0, &ss_load_sap);
        interop::writeJmp(0x00435170, &ss_load_steps);
        interop::writeJmp(0x00435300, &ss_load_bgm);
        interop::writeJmp(0x00435610, &ss_init_2);
        interop::writeJmp(0x00435540, &ss_init_buffers);
        interop::writeJmp(0x00435930, &ss_create_buffer);
        interop::writeJmp(0x00436470, &ss_voice_load);
        interop::writeJmp(0x00436590, &ss_voice_parse);
        interop::writeJmp(0x00436810, &bgm_channels_init);
        interop::writeJmp(0x004ECDA0, snd_bgm_main);
        interop::writeJmp(0x004ED920, bgm_set_entry);
        // interop::writeJmp(0x004ED950, snd_se_on);
    }
}
