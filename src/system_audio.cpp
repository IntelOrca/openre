#include "system_audio.h"
#include "logger.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include <SDL3/SDL.h>

// Design notes
// ------------
// Format: the mixer produces interleaved float32 (SDL_AUDIO_F32) at the rate
// and channel count configured with set_format() (defaults: 44100 Hz stereo).
// Float is used because mixing in float needs no intermediate clipping and SDL
// converts to the hardware format downstream; the stream created by
// SDL_OpenAudioDeviceStream() is our mixer's output format.
//
// Voices keep their decoded PCM in its native s16 format, rate and channel
// count. The mixer resamples per voice with linear interpolation, so
// set_format() may be called at any time without reloading voices and loading
// a .sap does not depend on the target format (the original game resampled at
// load time via ACM; here that cost is moved into the mixer).
//
// Threading: the SDL callback runs on the audio device thread, game logic on
// its own thread. A single SDL mutex guards the voice store; the callback
// holds it for the whole mix (only a few milliseconds at most). To avoid
// deadlocks the game thread never calls SDL functions while holding the
// mutex: SDL_SetAudioStreamFormat() / SDL_DestroyAudioStream() are invoked
// without the lock (SDL waits for any in-flight callback itself, and our
// callback only ever waits on the mutex, never on the game thread).
//
// MS-ADPCM: the .sap files are RIFF WAV containers. Format tag 1 (PCM, 8 or
// 16 bit) passes through (8-bit is widened to s16); tag 2 (MS-ADPCM) is
// decoded with a hand-rolled block decoder (7 header bytes per channel, 2
// nibbles per byte for mono, left/right nibbles packed per byte for stereo,
// Jayant adaptation table with a delta floor of 16). No mmio/ACM is used.

namespace openre::system::audio
{
    namespace
    {
        // ------------------------------------------------------------- voice store
        // The 8 buffer groups mirror the GameTable audio_Buffer* arrays. A
        // voice handle is the 1-based index of its slot in this layout, so the
        // game can store handles directly in the audio_Buffer* slots.
        constexpr int kGroupCount = 8;
        constexpr int kGroupCapacity[kGroupCount] = { 4, 32, 48, 32, 22, 3, 2, 2 };
        constexpr int kGroupBase[kGroupCount] = { 0, 4, 36, 84, 116, 138, 141, 143 };
        constexpr int kVoiceCount = kGroupBase[kGroupCount - 1] + kGroupCapacity[kGroupCount - 1];

        constexpr float kPanCenter = 0.70710678f; // 1/sqrt(2), equal-power centre
        constexpr size_t kScratchFrames = 8192;   // max frames mixed per callback chunk

        struct Voice
        {
            std::vector<int16_t> data; // interleaved s16 PCM at the voice's native rate
            int total = 0;             // frame count (data.size() / channels)
            int frequency = 0;         // native sample rate
            int channels = 1;          // 1 mono, 2 stereo
            bool loop = false;
            bool playing = false;
            double pos = 0.0;  // playback position in source frames
            float gain = 1.0f; // linear volume, 0..1
            float panL = kPanCenter;
            float panR = kPanCenter;
        };

        SDL_AudioStream* gStream = nullptr;
        SDL_Mutex* gMutex = nullptr;
        bool gInitialized = false;
        int gFrequency = 44100;
        int gChannels = 2;
        std::array<Voice, kVoiceCount> gVoices;
        std::vector<float> gScratch; // mix output scratch (gChannels interleaved floats)
        class ScopedLock
        {
        public:
            explicit ScopedLock(SDL_Mutex* mutex)
                : mMutex(mutex)
            {
                if (mMutex)
                    SDL_LockMutex(mMutex);
            }

            ~ScopedLock()
            {
                if (mMutex)
                    SDL_UnlockMutex(mMutex);
            }

            ScopedLock(const ScopedLock&) = delete;
            ScopedLock& operator=(const ScopedLock&) = delete;

        private:
            SDL_Mutex* mMutex;
        };

        SDL_Mutex* get_mutex()
        {
            if (!gMutex)
                gMutex = SDL_CreateMutex();
            return gMutex;
        }

        Voice* voice_at(uint32_t handle)
        {
            if (handle == 0 || handle > kVoiceCount)
                return nullptr;
            return &gVoices[handle - 1];
        }

        // Maps a (type, sub) pair to a voice slot, or -1 when out of range.
        int group_slot(int type, int sub)
        {
            if (type < 0 || type >= kGroupCount)
                return -1;
            if (sub < 0 || sub >= kGroupCapacity[type])
                return -1;
            return kGroupBase[type] + sub;
        }

        // --------------------------------------------------------------- mixing
        static void SDLCALL mix_callback(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount);

        void mix_frames(float* out, int frames)
        {
            const float inv32768 = 1.0f / 32768.0f;
            const double stepScale = (gFrequency > 0) ? 1.0 / (double)gFrequency : 0.0;

            for (int f = 0; f < frames; f++)
            {
                float acc[2] = { 0.0f, 0.0f };
                for (Voice& v : gVoices)
                {
                    if (!v.playing || v.total <= 0 || v.data.empty())
                        continue;

                    // Advance/wrap the source position; non-looping voices that
                    // reach the end stop contributing immediately.
                    if (v.pos >= (double)v.total)
                    {
                        if (!v.loop)
                        {
                            v.playing = false;
                            continue;
                        }
                        v.pos = std::fmod(v.pos, (double)v.total);
                    }

                    int i = (int)v.pos;
                    double frac = v.pos - (double)i;
                    int i2 = (i + 1 < v.total) ? i + 1 : i;

                    float sample[2];
                    if (v.channels == 1)
                    {
                        float s = (float)v.data[i] * inv32768;
                        sample[0] = s + ((float)v.data[i2] - s) * (float)frac;
                        sample[1] = sample[0];
                    }
                    else
                    {
                        float l = (float)v.data[i * 2] * inv32768;
                        float r = (float)v.data[i * 2 + 1] * inv32768;
                        sample[0] = l + ((float)v.data[i2 * 2] - l) * (float)frac;
                        sample[1] = r + ((float)v.data[i2 * 2 + 1] - r) * (float)frac;
                    }

                    acc[0] += sample[0] * v.gain * v.panL;
                    acc[1] += sample[1] * v.gain * v.panR;
                    v.pos += (double)v.frequency * stepScale;
                }

                if (gChannels == 1)
                    out[f] = (acc[0] + acc[1]) * 0.5f;
                else
                {
                    out[f * 2] = acc[0];
                    out[f * 2 + 1] = acc[1];
                }
            }
        }

        static void SDLCALL mix_callback(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount)
        {
            (void)userdata;
            (void)total_amount;
            if (additional_amount <= 0 || !gStream)
                return;

            SDL_LockMutex(gMutex);
            const int frameBytes = (int)sizeof(float) * gChannels;
            int remaining = additional_amount;
            while (remaining >= frameBytes)
            {
                int chunkBytes = remaining;
                int maxChunk = (int)(gScratch.size() * sizeof(float));
                if (chunkBytes > maxChunk)
                    chunkBytes = maxChunk;
                int frames = chunkBytes / frameBytes;
                if (frames <= 0)
                    break;
                mix_frames(gScratch.data(), frames);
                SDL_PutAudioStreamData(stream, gScratch.data(), frames * frameBytes);
                remaining -= frames * frameBytes;
            }
            SDL_UnlockMutex(gMutex);
        }

        // -------------------------------------------------------------- RIFF/WAV
        uint16_t rd_u16(const uint8_t* p)
        {
            return (uint16_t)(p[0] | (p[1] << 8));
        }

        uint32_t rd_u32(const uint8_t* p)
        {
            return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        }

        struct SapFormat
        {
            int formatTag = 0; // 1 = PCM, 2 = MS-ADPCM
            int channels = 0;
            int samplesPerSec = 0;
            int blockAlign = 0;
            int bitsPerSample = 0;
            int samplesPerBlock = 0; // MS-ADPCM only, per channel
            std::vector<int16_t> coef1;
            std::vector<int16_t> coef2;
        };

        bool parse_wave_fmt(const uint8_t* p, size_t size, SapFormat& fmt)
        {
            if (size < 16)
                return false;
            fmt.formatTag = rd_u16(p + 0);
            fmt.channels = rd_u16(p + 2);
            fmt.samplesPerSec = (int)rd_u32(p + 4);
            fmt.blockAlign = rd_u16(p + 12);
            fmt.bitsPerSample = rd_u16(p + 14);

            if (fmt.channels < 1 || fmt.channels > 2 || fmt.samplesPerSec <= 0)
                return false;

            if (fmt.formatTag == 1) // WAVE_FORMAT_PCM
            {
                return fmt.bitsPerSample == 8 || fmt.bitsPerSample == 16;
            }

            if (fmt.formatTag == 2) // WAVE_FORMAT_ADPCM (MS-ADPCM)
            {
                if (fmt.blockAlign < fmt.channels * 7)
                    return false;

                // cbSize + wSamplesPerBlock + wNumCoef + aCoef pairs.
                size_t extra = 0;
                const uint8_t* ex = nullptr;
                if (size >= 18)
                {
                    ex = p + 18;
                    extra = rd_u16(p + 16);
                    if (extra > size - 18)
                        extra = size - 18;
                }
                if (extra >= 4)
                {
                    fmt.samplesPerBlock = rd_u16(ex + 0);
                    int numCoef = rd_u16(ex + 2);
                    int maxCoef = (int)((extra - 4) / 4);
                    if (numCoef > maxCoef)
                        numCoef = maxCoef;
                    for (int i = 0; i < numCoef; i++)
                    {
                        fmt.coef1.push_back((int16_t)rd_u16(ex + 4 + i * 4));
                        fmt.coef2.push_back((int16_t)rd_u16(ex + 4 + i * 4 + 2));
                    }
                }
                if (fmt.coef1.empty())
                {
                    // Missing/truncated coefficient table: fall back to the 7
                    // built-in predictor pairs.
                    static const int16_t kBuiltinCoef1[7] = { 256, 512, 0, 192, 240, 460, 392 };
                    static const int16_t kBuiltinCoef2[7] = { 0, -256, 0, 64, 0, -208, -232 };
                    fmt.coef1.assign(kBuiltinCoef1, kBuiltinCoef1 + 7);
                    fmt.coef2.assign(kBuiltinCoef2, kBuiltinCoef2 + 7);
                }
                if (fmt.samplesPerBlock <= 0)
                {
                    // Derive from the block alignment: 7 header bytes per
                    // channel, then 2 encoded samples per nibble byte (mono)
                    // or 1 per channel (stereo).
                    fmt.samplesPerBlock = (fmt.blockAlign / fmt.channels - 7) * 2 + 2;
                }
                return fmt.samplesPerBlock > 0;
            }
            return false;
        }

        // ---------------------------------------------------------------- decode
        // Decodes a WAV data chunk into interleaved s16 frames. Returns an
        // empty vector on failure.
        std::vector<int16_t> decode_pcm(const uint8_t* data, size_t size, const SapFormat& fmt)
        {
            if (fmt.formatTag == 1 && fmt.bitsPerSample == 16)
            {
                size_t n = size / 2;
                std::vector<int16_t> out(n);
                for (size_t i = 0; i < n; i++)
                    out[i] = (int16_t)rd_u16(data + i * 2);
                return out;
            }
            if (fmt.formatTag == 1 && fmt.bitsPerSample == 8)
            {
                std::vector<int16_t> out(size);
                for (size_t i = 0; i < size; i++)
                    out[i] = (int16_t)(((int)data[i] - 128) << 8);
                return out;
            }
            return {};
        }

        // Jayant step-size adaptation table and delta floor for MS-ADPCM.
        constexpr int16_t kAdaptation[16] = { 230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230 };
        constexpr int kMinDelta = 16;

        // Decodes one 4-bit nibble with the given channel state. `s1` is the
        // most recent sample, `s2` the one before it; both are updated in place.
        int decode_nibble(int nibble, int& s1, int& s2, int& delta, int16_t coef1, int16_t coef2)
        {
            int snib = (nibble & 8) ? (nibble - 16) : nibble;
            long long pred = ((long long)coef1 * s1 + (long long)coef2 * s2) / 256 + (long long)snib * delta;
            if (pred > 32767)
                pred = 32767;
            else if (pred < -32768)
                pred = -32768;
            s2 = s1;
            s1 = (int)pred;
            delta = (kAdaptation[nibble] * delta) / 256;
            if (delta < kMinDelta)
                delta = kMinDelta;
            return (int)pred;
        }

        // Decodes one or more consecutive MS-ADPCM blocks into interleaved s16
        // frames. Mono preamble: predictor(1) delta(2) sample1(2) sample2(2);
        // stereo preamble is interleaved (see below). The first output samples
        // are sample2 then sample1. Mono blocks store two samples per byte
        // (upper nibble first); stereo blocks store the left channel in the
        // upper nibble and right in the lower nibble of each byte.
        std::vector<int16_t> decode_msadpcm(const uint8_t* data, size_t size, const SapFormat& fmt)
        {
            const int ch = fmt.channels;
            const int spb = fmt.samplesPerBlock;
            const size_t header = (size_t)ch * 7;

            std::vector<int16_t> out;
            if (fmt.blockAlign <= 0)
                return out;
            out.reserve((size / (size_t)fmt.blockAlign) * (size_t)spb * (size_t)ch + (size_t)ch * 2 + 16);

            size_t pos = 0;
            while (pos + header <= size)
            {
                size_t blockSize = (size_t)fmt.blockAlign;
                if (blockSize > size - pos)
                    blockSize = size - pos;

                int pred[2] = { 0, 0 };
                int delta[2] = { 0, 0 };
                int s1[2] = { 0, 0 };
                int s2[2] = { 0, 0 };
                const uint8_t* h = data + pos;
                if (ch == 1)
                {
                    // Mono preamble: predictor(1) delta(2) sample1(2) sample2(2).
                    int p = h[0];
                    if (p >= (int)fmt.coef1.size())
                        p = 0;
                    pred[0] = p;
                    delta[0] = (int)rd_u16(h + 1);
                    s1[0] = (int16_t)rd_u16(h + 3);
                    s2[0] = (int16_t)rd_u16(h + 5);
                }
                else
                {
                    // Stereo preamble is interleaved: predL, predR, deltaL,
                    // deltaR, sample1L, sample1R, sample2L, sample2R.
                    for (int c = 0; c < 2; c++)
                    {
                        int p = h[c];
                        if (p >= (int)fmt.coef1.size())
                            p = 0;
                        pred[c] = p;
                    }
                    delta[0] = (int)rd_u16(h + 2);
                    delta[1] = (int)rd_u16(h + 4);
                    s1[0] = (int16_t)rd_u16(h + 6);
                    s1[1] = (int16_t)rd_u16(h + 8);
                    s2[0] = (int16_t)rd_u16(h + 10);
                    s2[1] = (int16_t)rd_u16(h + 12);
                }
                for (int c = 0; c < ch; c++)
                {
                    if (delta[c] < kMinDelta)
                        delta[c] = kMinDelta;
                }

                // Output the two header samples: sample2 first, then sample1,
                // interleaved across channels.
                for (int c = 0; c < ch; c++)
                    out.push_back((int16_t)s2[c]);
                for (int c = 0; c < ch; c++)
                    out.push_back((int16_t)s1[c]);

                // Encoded samples per channel = samplesPerBlock - 2 (the two
                // header samples). Data bytes produce 2 samples per byte per
                // channel for mono, 1 per byte per channel for stereo.
                int encoded = spb - 2;
                size_t dataBytes = blockSize - header;
                int perChannel = (int)(dataBytes * 2 / (size_t)ch);
                if (perChannel > encoded)
                    perChannel = encoded;

                if (ch == 1)
                {
                    for (int i = 0; i + 1 < perChannel; i += 2)
                    {
                        int b = data[pos + header + (size_t)(i / 2)];
                        out.push_back(
                            (int16_t)decode_nibble(b >> 4, s1[0], s2[0], delta[0], fmt.coef1[pred[0]], fmt.coef2[pred[0]]));
                        out.push_back(
                            (int16_t)decode_nibble(b & 0x0F, s1[0], s2[0], delta[0], fmt.coef1[pred[0]], fmt.coef2[pred[0]]));
                    }
                    if (perChannel & 1)
                    {
                        int b = data[pos + header + (size_t)(perChannel / 2)];
                        out.push_back(
                            (int16_t)decode_nibble(b >> 4, s1[0], s2[0], delta[0], fmt.coef1[pred[0]], fmt.coef2[pred[0]]));
                    }
                }
                else
                {
                    for (int i = 0; i < perChannel; i++)
                    {
                        int b = data[pos + header + (size_t)i];
                        out.push_back(
                            (int16_t)decode_nibble(b >> 4, s1[0], s2[0], delta[0], fmt.coef1[pred[0]], fmt.coef2[pred[0]]));
                        out.push_back(
                            (int16_t)decode_nibble(b & 0x0F, s1[1], s2[1], delta[1], fmt.coef1[pred[1]], fmt.coef2[pred[1]]));
                    }
                }
                pos += blockSize;
            }
            return out;
        }
    }

    // --------------------------------------------------------------- public API
    bool init()
    {
        if (gInitialized)
            return true;

        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            logging::logWarning("system_audio: SDL_InitSubSystem(SDL_INIT_AUDIO) failed: {}", SDL_GetError());
            return false;
        }

        // The mutex must exist before the stream does: the callback locks it
        // and it also guards format/state changes made by the game thread.
        if (!get_mutex())
        {
            logging::logWarning("system_audio: SDL_CreateMutex failed: {}", SDL_GetError());
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return false;
        }

        SDL_AudioSpec spec;
        spec.format = SDL_AUDIO_F32;
        spec.channels = gChannels;
        spec.freq = gFrequency;
        gStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, mix_callback, nullptr);
        if (!gStream)
        {
            logging::logWarning("system_audio: SDL_OpenAudioDeviceStream failed: {}", SDL_GetError());
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return false;
        }

        gScratch.assign(kScratchFrames * (size_t)gChannels, 0.0f);
        if (!SDL_ResumeAudioStreamDevice(gStream))
            logging::logWarning("system_audio: SDL_ResumeAudioStreamDevice failed: {}", SDL_GetError());

        gInitialized = true;
        logging::logInfo("system_audio: initialised, {} Hz, {} channel(s)", gFrequency, gChannels);
        return true;
    }

    void shutdown()
    {
        if (!gStream && !gInitialized)
            return;

        // Stop the audio thread first, without holding the lock. Destroying
        // the stream also closes the device and waits for any in-flight
        // callback to finish, so the voice store can be cleared safely below.
        if (gStream)
        {
            SDL_DestroyAudioStream(gStream);
            gStream = nullptr;
        }

        if (gMutex)
        {
            SDL_LockMutex(gMutex);
            gInitialized = false;
            for (Voice& v : gVoices)
                v = Voice();
            gScratch.clear();
            SDL_UnlockMutex(gMutex);
            SDL_DestroyMutex(gMutex);
            gMutex = nullptr;
        }
        else
        {
            gInitialized = false;
        }

        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        logging::logInfo("system_audio: shut down");
    }

    void set_format(int frequency, int channels)
    {
        if (frequency <= 0)
            frequency = 44100;
        if (channels != 1 && channels != 2)
            channels = 2;
        if (frequency == gFrequency && channels == gChannels)
            return;

        // Reconfigure the stream's input format. This must not happen while
        // holding the mutex: SDL waits for in-flight callbacks itself and our
        // callback never waits on the game thread.
        if (gStream)
        {
            SDL_AudioSpec src;
            src.format = SDL_AUDIO_F32;
            src.channels = channels;
            src.freq = frequency;
            SDL_SetAudioStreamFormat(gStream, &src, nullptr);
        }

        SDL_LockMutex(get_mutex());
        gFrequency = frequency;
        gChannels = channels;
        gScratch.assign(kScratchFrames * (size_t)channels, 0.0f);
        SDL_UnlockMutex(gMutex);
        logging::logInfo("system_audio: format set to {} Hz, {} channel(s)", frequency, channels);
    }

    bool load_sap(const uint8_t* data, int size, int type, int sub)
    {
        if (!data || size < 12)
            return false;
        if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0)
            return false;

        // Walk the RIFF chunk list: "fmt " then "data", in any order.
        size_t pos = 12;
        const uint8_t* pcm = nullptr;
        size_t pcmSize = 0;
        SapFormat fmt;
        while (pos + 8 <= (size_t)size)
        {
            const uint8_t* h = data + pos;
            size_t ckSize = rd_u32(h + 4);
            if (ckSize > (size_t)size - (pos + 8))
                return false; // truncated chunk
            const uint8_t* body = h + 8;
            if (std::memcmp(h, "fmt ", 4) == 0)
            {
                if (!parse_wave_fmt(body, ckSize, fmt))
                    return false;
            }
            else if (std::memcmp(h, "data", 4) == 0)
            {
                pcm = body;
                pcmSize = ckSize;
            }
            pos += 8 + ckSize + (ckSize & 1);
        }

        if (!pcm || pcmSize == 0 || fmt.samplesPerSec <= 0 || fmt.channels < 1 || fmt.channels > 2)
            return false;

        std::vector<int16_t> decoded;
        if (fmt.formatTag == 2)
            decoded = decode_msadpcm(pcm, pcmSize, fmt);
        else
            decoded = decode_pcm(pcm, pcmSize, fmt);
        if (decoded.empty())
            return false;

        // decoded holds frames * channels interleaved samples.
        int frames = (int)(decoded.size() / (size_t)fmt.channels);
        uint32_t handle = create_buffer(type, sub, decoded.data(), (int)decoded.size(), fmt.samplesPerSec, fmt.channels, false);
        if (handle == 0)
            return false;
        logging::logInfo(
            "system_audio: loaded sap type={} sub={} ({} Hz, {} ch, {} frames, tag {})",
            type,
            sub,
            fmt.samplesPerSec,
            fmt.channels,
            frames,
            fmt.formatTag);
        return true;
    }

    uint32_t create_buffer(int type, int sub, const void* pcm, int num_samples, int frequency, int channels, bool loop)
    {
        int slot = group_slot(type, sub);
        if (slot < 0)
            return 0;
        const int16_t* src = static_cast<const int16_t*>(pcm);
        if (!src || num_samples <= 0 || frequency <= 0 || (channels != 1 && channels != 2))
            return 0;

        ScopedLock lock(get_mutex());
        Voice& v = gVoices[slot];
        v = Voice();
        v.data.assign(src, src + (size_t)num_samples);
        v.total = num_samples / channels; // frames
        v.frequency = frequency;
        v.channels = channels;
        v.loop = loop;
        return (uint32_t)(slot + 1);
    }

    bool play(uint32_t handle)
    {
        ScopedLock lock(get_mutex());
        Voice* v = voice_at(handle);
        if (!v || v->data.empty() || v->total <= 0)
            return false;
        if (!v->playing)
        {
            // Resume a paused voice; rewind a voice that already played to the
            // end so re-triggered one-shots sound again.
            if (v->pos >= (double)v->total)
                v->pos = 0.0;
            v->playing = true;
        }
        return true;
    }

    bool stop(uint32_t handle)
    {
        ScopedLock lock(get_mutex());
        Voice* v = voice_at(handle);
        if (!v)
            return false;
        v->playing = false;
        return true;
    }

    void stop_all()
    {
        ScopedLock lock(get_mutex());
        for (Voice& v : gVoices)
            v.playing = false;
    }

    bool get_status(uint32_t handle)
    {
        ScopedLock lock(get_mutex());
        Voice* v = voice_at(handle);
        return v != nullptr && v->playing;
    }

    bool set_vol(uint32_t handle, int centibel)
    {
        ScopedLock lock(get_mutex());
        Voice* v = voice_at(handle);
        if (!v)
            return false;
        float gain = 1.0f;
        if (centibel < 0)
        {
            // DirectSound centibels: 0 = full volume, -10000 = -100 dB.
            // gain = 10^(centibel/2000), linear in amplitude.
            gain = std::pow(10.0f, (float)centibel / 2000.0f);
        }
        v->gain = gain;
        return true;
    }

    bool set_pan(uint32_t handle, int pan)
    {
        ScopedLock lock(get_mutex());
        Voice* v = voice_at(handle);
        if (!v)
            return false;

        float l;
        float r;
        if (pan <= -10000)
        {
            l = 1.0f;
            r = 0.0f;
        }
        else if (pan >= 10000)
        {
            l = 0.0f;
            r = 1.0f;
        }
        else
        {
            // Equal-power pan: angle sweeps 0..pi/2 across -10000..10000.
            float angle = (float)(pan + 10000) * 3.14159265358979f / 40000.0f;
            l = std::cos(angle);
            r = std::sin(angle);
        }
        v->panL = l;
        v->panR = r;
        return true;
    }

    void set_loop(uint32_t handle, bool loop)
    {
        ScopedLock lock(get_mutex());
        if (Voice* v = voice_at(handle))
            v->loop = loop;
    }

    void unload(uint32_t handle)
    {
        ScopedLock lock(get_mutex());
        Voice* v = voice_at(handle);
        if (!v)
            return;
        v->playing = false;
        v->data.clear();
        v->data.shrink_to_fit();
        v->total = 0;
    }
}
