#include "system_audio.h"
#include "logger.h"
#include "msadpcm.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
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
// decoded by the standalone decoder in msadpcm.cpp (7 header bytes per
// channel, 2 nibbles per byte for mono, left/right nibbles packed per byte
// for stereo, Jayant adaptation table with a delta floor of 16). No
// mmio/ACM is used.
//
// Debug dumps: when the OPENRE_DUMP_AUDIO environment variable is set,
// load_sap() additionally writes each decoded .sap out as a 16-bit PCM WAV
// file so the sound can be inspected outside the game (see dump_wav()).

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

        // Optional mixer-output dump (OPENRE_DUMP_MIX): writes the exact
        // post-mix float buffer (before SDL_PutAudioStreamData) to
        // <dir>/mix_output.wav as 16-bit PCM so rendered output can be compared
        // against the clean decoded input from OPENRE_DUMP_AUDIO. Set the env
        // var to a directory, or to "1" for the default dump/audio.
        void wr_le16(std::ofstream& file, uint16_t v);
        void wr_le32(std::ofstream& file, uint32_t v);
        std::ofstream gMixFile;
        uint32_t gMixFrames = 0;
        void dump_mix_init()
        {
            const char* env = std::getenv("OPENRE_DUMP_MIX");
            if (!env)
                return;
            std::string dir = env;
            if (dir.empty() || dir == "1")
                dir = "dump/audio";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            std::filesystem::path path = std::filesystem::path(dir) / "mix_output.wav";
            gMixFile.open(path, std::ios::binary | std::ios::trunc);
            if (gMixFile)
            {
                // placeholder RIFF header, patched on close
                char hdr[44] = {};
                gMixFile.write(hdr, 44);
                gMixFrames = 0;
                logging::logInfo("system_audio: [MIX DUMP] writing {} ({} Hz, {} ch)", path.string(), gFrequency, gChannels);
            }
        }
        void dump_mix_append(const float* frames, int count)
        {
            if (!gMixFile.is_open())
                return;
            for (int i = 0; i < count * gChannels; i++)
            {
                float v = frames[i];
                if (v > 1.0f)
                    v = 1.0f;
                if (v < -1.0f)
                    v = -1.0f;
                int16_t s = (int16_t)(v * 32767.0f);
                char b[2] = { (char)(s & 0xFF), (char)((s >> 8) & 0xFF) };
                gMixFile.write(b, 2);
            }
            gMixFrames += (uint32_t)count;
        }
        void dump_mix_close()
        {
            if (!gMixFile.is_open())
                return;
            gMixFile.seekp(0);
            const uint32_t dataSize = gMixFrames * (uint32_t)gChannels * 2;
            const uint32_t byteRate = (uint32_t)gFrequency * (uint32_t)gChannels * 2;
            const uint16_t blockAlign = (uint16_t)(gChannels * 2);
            gMixFile.write("RIFF", 4);
            wr_le32(gMixFile, 36 + dataSize);
            gMixFile.write("WAVE", 4);
            gMixFile.write("fmt ", 4);
            wr_le32(gMixFile, 16);
            wr_le16(gMixFile, 1);
            wr_le16(gMixFile, (uint16_t)gChannels);
            wr_le32(gMixFile, (uint32_t)gFrequency);
            wr_le32(gMixFile, byteRate);
            wr_le16(gMixFile, blockAlign);
            wr_le16(gMixFile, 16);
            gMixFile.write("data", 4);
            wr_le32(gMixFile, dataSize);
            gMixFile.close();
            logging::logInfo("system_audio: [MIX DUMP] closed mix_output.wav ({} frames)", gMixFrames);
        }
        // --- END mixer-output dump ---
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
                    int i2 = (i + 1 < v.total) ? i + 1 : (v.loop ? 0 : i);

                    float sample[2];
                    if (v.channels == 1)
                    {
                        float s = (float)v.data[i] * inv32768;
                        sample[0] = s + ((float)v.data[i2] * inv32768 - s) * (float)frac;
                        sample[1] = sample[0];
                    }
                    else
                    {
                        float l = (float)v.data[i * 2] * inv32768;
                        float r = (float)v.data[i * 2 + 1] * inv32768;
                        sample[0] = l + ((float)v.data[i2 * 2] * inv32768 - l) * (float)frac;
                        sample[1] = r + ((float)v.data[i2 * 2 + 1] * inv32768 - r) * (float)frac;
                    }

                    acc[0] += sample[0] * v.gain * v.panL;
                    acc[1] += sample[1] * v.gain * v.panR;
                    v.pos += (double)v.frequency * stepScale;
                }

                // Clamp the summed mix to full scale, matching DirectSound's
                // behaviour when several voices overlap; the DAC can't play
                // values beyond [-1, 1] anyway.
                if (acc[0] > 1.0f)
                    acc[0] = 1.0f;
                else if (acc[0] < -1.0f)
                    acc[0] = -1.0f;
                if (acc[1] > 1.0f)
                    acc[1] = 1.0f;
                else if (acc[1] < -1.0f)
                    acc[1] = -1.0f;

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
                dump_mix_append(gScratch.data(), frames);
                remaining -= frames * frameBytes;
            }
            SDL_UnlockMutex(gMutex);
        }

        // ------------------------------------------------------------ dumps
        void wr_le16(std::ofstream& file, uint16_t v)
        {
            char b[2] = { (char)(v & 0xFF), (char)(v >> 8) };
            file.write(b, 2);
        }

        void wr_le32(std::ofstream& file, uint32_t v)
        {
            char b[4] = {
                (char)(v & 0xFF), (char)((v >> 8) & 0xFF), (char)((v >> 16) & 0xFF), (char)((v >> 24) & 0xFF),
            };
            file.write(b, 4);
        }

        // Writes the decoded PCM of a voice to a 16-bit PCM WAV file so it can
        // be inspected outside the game. Only active when the OPENRE_DUMP_AUDIO
        // environment variable is set: its value is the output directory, or a
        // value of "1" / an empty value selects <working dir>\dump\audio. The
        // directory is created if missing. Filenames are <name><type>_<sub>.wav
        // where <name> is derived from the buffer group ("bgm", "voice", ...);
        // re-dumping the same slot appends a counter so earlier files survive.
        void dump_wav(const std::vector<int16_t>& samples, int channels, int samplesPerSec, int type, int sub)
        {
            if (samples.empty() || channels < 1 || samplesPerSec <= 0)
                return;

            const char* env = std::getenv("OPENRE_DUMP_AUDIO");
            if (!env)
                return;

            std::string dir = env;
            if (dir.empty() || dir == "1")
                dir = "dump/audio";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            if (ec)
            {
                logging::logWarning("system_audio: [AUDIO DUMP] cannot create directory {}: {}", dir, ec.message());
                return;
            }

            static const char* kTypeNames[] = { "door", "weapon", "room", "enemy", "core", "bgm", "sbgm", "voice" };
            constexpr int kTypeNameCount = (int)(sizeof(kTypeNames) / sizeof(kTypeNames[0]));
            const char* name = (type >= 0 && type < kTypeNameCount) ? kTypeNames[type] : "buf";

            // First dump of a slot is <name><type>_<sub>.wav; re-dumps append a
            // counter so earlier files are never overwritten.
            std::filesystem::path path;
            for (int counter = 0;; counter++)
            {
                std::string fileName = std::string(name) + std::to_string(type) + "_" + std::to_string(sub);
                if (counter > 0)
                    fileName += "_" + std::to_string(counter);
                fileName += ".wav";
                path = std::filesystem::path(dir) / fileName;
                std::error_code existsEc;
                if (!std::filesystem::exists(path, existsEc))
                    break;
            }

            std::ofstream file(path, std::ios::binary);
            if (!file)
            {
                logging::logWarning("system_audio: [AUDIO DUMP] cannot open {}", path.string());
                return;
            }

            const uint32_t dataSize = (uint32_t)(samples.size() * sizeof(int16_t));
            const uint32_t byteRate = (uint32_t)samplesPerSec * (uint32_t)channels * 2;
            const uint16_t blockAlign = (uint16_t)(channels * 2);

            file.write("RIFF", 4);
            wr_le32(file, 36 + dataSize);
            file.write("WAVE", 4);
            file.write("fmt ", 4);
            wr_le32(file, 16);
            wr_le16(file, 1); // WAVE_FORMAT_PCM
            wr_le16(file, (uint16_t)channels);
            wr_le32(file, (uint32_t)samplesPerSec);
            wr_le32(file, byteRate);
            wr_le16(file, blockAlign);
            wr_le16(file, 16); // bits per sample
            file.write("data", 4);
            wr_le32(file, dataSize);
            file.write(reinterpret_cast<const char*>(samples.data()), (std::streamsize)(samples.size() * sizeof(int16_t)));
            file.close();

            logging::logInfo("[AUDIO DUMP] wrote {}", path.string());
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
        if (std::getenv("OPENRE_DUMP_MIX"))
        {
            // Log the actual SDL stream + device formats so the captured mix
            // output can be interpreted (which conversions SDL applies
            // downstream of the mixer).
            SDL_AudioSpec src, dst;
            if (SDL_GetAudioStreamFormat(gStream, &src, &dst))
                logging::logInfo(
                    "system_audio: [MIX DUMP] stream src {} Hz {} ch fmt {} -> dst {} Hz {} ch fmt {}",
                    src.freq,
                    src.channels,
                    (int)src.format,
                    dst.freq,
                    dst.channels,
                    (int)dst.format);
            SDL_AudioDeviceID devid = SDL_GetAudioStreamDevice(gStream);
            if (devid)
            {
                SDL_AudioSpec dev;
                if (SDL_GetAudioDeviceFormat(devid, &dev, nullptr))
                    logging::logInfo(
                        "system_audio: [MIX DUMP] device format {} Hz {} ch fmt {}",
                        dev.freq,
                        dev.channels,
                        (int)dev.format);
            }
        }
        dump_mix_init();
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
        dump_mix_close();

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

    uint32_t load_sap(const uint8_t* data, int size, int type, int sub)
    {
        if (!data || size < 12)
            return 0;

        msadpcm::DecodedWave decoded;
        if (!msadpcm::decode_sap(data, (size_t)size, decoded) || decoded.samples.empty())
            return 0;

        // decoded.samples holds frames * channels interleaved samples.
        int frames = (int)(decoded.samples.size() / (size_t)decoded.channels);
        uint32_t handle = create_buffer(
            type, sub, decoded.samples.data(), (int)decoded.samples.size(), decoded.samplesPerSec, decoded.channels, false);
        if (handle == 0)
            return 0;
        logging::logInfo(
            "system_audio: loaded sap type={} sub={} ({} Hz, {} ch, {} frames, tag {})",
            type,
            sub,
            decoded.samplesPerSec,
            decoded.channels,
            frames,
            decoded.formatTag);
        dump_wav(decoded.samples, decoded.channels, decoded.samplesPerSec, type, sub);
        return handle;
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
        else if (!v->loop)
        {
            // The voice is still playing. DirectSound treated Play() on an
            // already-playing one-shot as a no-op, which silently swallowed
            // re-triggered one-shots (e.g. footsteps whose trigger arrived
            // before the previous step had finished, dropping to ~1 step/s
            // while running in rooms with short samples). Restart the one-shot
            // so every trigger is audible; looping voices are left untouched.
            v->pos = 0.0;
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

    int get_vol(uint32_t handle)
    {
        ScopedLock lock(get_mutex());
        Voice* v = voice_at(handle);
        if (!v)
            return 0;
        // Invert set_vol's gain = 10^(centibel / 2000) -> centibel =
        // 2000 * log10(gain). A zero gain (silence) saturates at -10000.
        if (v->gain <= 0.0f)
            return -10000;
        int cb = (int)std::lround(2000.0 * std::log10((double)v->gain));
        if (cb > 0)
            cb = 0;
        if (cb < -10000)
            cb = -10000;
        return cb;
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
