#pragma once

#include <cstdint>
#include <vector>

namespace openre::system::audio
{
    // Initialises the SDL3 audio subsystem and opens the default playback
    // device with a software mixer. Safe to call more than once; subsequent
    // calls are no-ops until shutdown(). Returns false (and logs) on failure.
    // Voices can be created and played before init(); they only start to be
    // mixed once a device is open.
    bool init();
    // Stops the device, closes it, releases every voice and quits the SDL3
    // audio subsystem.
    void shutdown();

    // Sets the mixer output format. `frequency` is the sample rate in Hz and
    // `channels` is 1 (mono) or 2 (stereo); the game derives these from
    // MarniSnd_Frequency / audio_SpeakerConfig. Defaults are 44100 Hz stereo.
    // Voices store their native format and are resampled by the mixer, so this
    // may be changed at any time without reloading voices. Usually called once
    // before init().
    void set_format(int frequency, int channels);

    // Decodes a .sap file (a RIFF WAV container holding MS-ADPCM or PCM data,
    // no mmio/ACM involved) and registers it as the voice for (type, sub),
    // replacing any existing voice in that slot. Returns the opaque handle for
    // the new voice, or 0 on malformed data or unsupported formats.
    //
    // `sapSub` selects one sub-sound of a multi-sound .sap container (the
    // "file.sap#N" syntax used by scripts): masked containers carry an 8-byte
    // header followed by one RIFF chunk per set bit, and sapSub picks the Nth
    // of those. -1 keeps the legacy whole-file behaviour (decode the first
    // RIFF stream in the buffer, skipping the short container header if one is
    // present).
    uint32_t load_sap(const uint8_t* data, int size, int type, int sub, int sapSub = -1);

    // decode_sap() + create_buffer_unused(): decodes a whole .sap file and
    // registers it in the first free slot of buffer group `type`. Returns the
    // voice handle, or 0 when the group is full or decoding failed.
    uint32_t load_sap_unused(int type, const uint8_t* data, int size);
    // Decodes a .sap payload into interleaved s16 PCM without registering a
    // voice, so callers can store it however they like (e.g. as an always
    // resident sound effect). `sapSub` has the same meaning as for load_sap().
    // Returns false on malformed data or unsupported formats.
    bool
    decode_sap(const uint8_t* data, int size, int sapSub, std::vector<int16_t>& outPcm, int& outFrequency, int& outChannels);

    // Registers decoded interleaved s16 PCM as the voice for (type, sub),
    // replacing any existing voice in that slot. `num_samples` is the total
    // number of samples (frames * channels). Returns an opaque handle (0 on
    // failure) that the caller stores in the matching GameTable audio_Buffer*
    // slot.
    uint32_t create_buffer(int type, int sub, const void* pcm, int num_samples, int frequency, int channels, bool loop);

    // Registers decoded interleaved s16 PCM like create_buffer(), but instead
    // of taking an explicit sub it picks the first unused slot within buffer
    // group `type`, so independent callers can share a group without
    // coordinating on slot numbers. Returns 0 when the group is full or the
    // PCM is invalid. The voice starts non-looping; toggle with set_loop().
    uint32_t create_buffer_unused(int type, const void* pcm, int num_samples, int frequency, int channels);

    // Opens a .sap file (RIFF WAV) for streamed playback: the file stays on
    // disk and is decoded incrementally into a small read-ahead window instead
    // of being expanded to PCM in memory. Picks the first unused slot of
    // buffer group `type` like create_buffer_unused(). Returns 0 when the
    // group is full or the file is missing/malformed. The voice starts
    // non-looping; toggle with set_loop().
    uint32_t create_stream_unused(int type, const char* path);

    // Starts or resumes playback. If the voice had already reached the end of
    // a non-looping buffer it is rewound to the start so re-triggered one-shots
    // play again. Already-playing voices are left untouched.
    bool play(uint32_t handle);
    // Starts a one-shot from the beginning no matter what state the voice is
    // in (playing, paused or finished). Used for resident sound effects where
    // every trigger must be audible from its first sample.
    bool trigger(uint32_t handle);
    // Pauses playback, keeping the current position.
    bool stop(uint32_t handle);
    // Pauses every voice.
    void stop_all();
    // True while the voice is playing (DirectSound DSBSTATUS_PLAYING bit 0).
    bool get_status(uint32_t handle);
    // Sets the linear gain from a DirectSound volume in centibels (0 = full,
    // -10000 = silence, clamped). gain = 10^(centibel / 2000).
    bool set_vol(uint32_t handle, int centibel);
    // Returns the voice's current volume in DirectSound centibels (0 = full,
    // -10000 = silence), inverting set_vol. Returns 0 for an invalid handle.
    int get_vol(uint32_t handle);
    // Sets an equal-power pan from DirectSound units (-10000 = left,
    // 0 = centre, +10000 = right, clamped).
    bool set_pan(uint32_t handle, int pan);
    // Toggles looping for the voice. Needed because the original game decides
    // looping at Play time (DSBPLAY_LOOPING) rather than at buffer creation.
    void set_loop(uint32_t handle, bool loop);
    // Frees the voice's data and invalidates its handle (the slot becomes
    // reusable by the next create_buffer for the same (type, sub)).
    void unload(uint32_t handle);
}
