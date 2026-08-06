#pragma once

#include <cstdint>

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
    // no mmio/ACM involved) and registers it as the voice for (type, sub).
    // Returns false on malformed data or unsupported formats.
    bool load_sap(const uint8_t* data, int size, int type, int sub);

    // Registers decoded interleaved s16 PCM as the voice for (type, sub),
    // replacing any existing voice in that slot. `num_samples` is the total
    // number of samples (frames * channels). Returns an opaque handle (0 on
    // failure) that the caller stores in the matching GameTable audio_Buffer*
    // slot.
    uint32_t create_buffer(int type, int sub, const void* pcm, int num_samples, int frequency, int channels, bool loop);

    // Starts or resumes playback. If the voice had already reached the end of
    // a non-looping buffer it is rewound to the start so re-triggered one-shots
    // play again. Already-playing voices are left untouched.
    bool play(uint32_t handle);
    // Pauses playback, keeping the current position.
    bool stop(uint32_t handle);
    // Pauses every voice.
    void stop_all();
    // True while the voice is playing (DirectSound DSBSTATUS_PLAYING bit 0).
    bool get_status(uint32_t handle);
    // Sets the linear gain from a DirectSound volume in centibels (0 = full,
    // -10000 = silence, clamped). gain = 10^(centibel / 2000).
    bool set_vol(uint32_t handle, int centibel);
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
