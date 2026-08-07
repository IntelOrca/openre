#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Standalone MS-ADPCM (and plain PCM) decoder for the .sap sound files used
// by the game. A .sap is a RIFF WAV container holding a "fmt " chunk and a
// "data" chunk; the data is either 8/16-bit PCM (tag 1) or MS-ADPCM (tag 2,
// the IMA-style variant with 7 predictor coefficient pairs). This module only
// decodes bytes to interleaved s16 samples - no file I/O, no audio device,
// no SDL. The game's system_audio layer feeds it raw .sap buffers.
namespace openre::msadpcm
{
    // Format parameters carried by the WAVE "fmt " chunk.
    struct WaveFormat
    {
        int formatTag = 0;       // 1 = WAVE_FORMAT_PCM, 2 = WAVE_FORMAT_ADPCM (MS-ADPCM)
        int channels = 0;        // 1 mono, 2 stereo
        int samplesPerSec = 0;   // sample rate in Hz
        int blockAlign = 0;      // bytes per MS-ADPCM block
        int bitsPerSample = 0;   // 8/16 for PCM, 4 for MS-ADPCM
        int samplesPerBlock = 0; // MS-ADPCM only: samples per block per channel
        std::vector<int16_t> coef1;
        std::vector<int16_t> coef2;
    };

    // Parses a WAVE "fmt " chunk body into `fmt`. Returns false on malformed
    // or unsupported formats (unknown tags, invalid channel counts/rates).
    bool parse_wave_fmt(const uint8_t* p, size_t size, WaveFormat& fmt);

    // Decodes a PCM data chunk into interleaved s16 samples. 16-bit passes
    // through as-is; 8-bit unsigned is widened to s16. Returns an empty vector
    // on failure.
    std::vector<int16_t> decode_pcm(const uint8_t* data, size_t size, const WaveFormat& fmt);

    // Decodes an MS-ADPCM data chunk (a sequence of blocks) into interleaved
    // s16 samples. Returns an empty vector on failure.
    std::vector<int16_t> decode_adpcm(const uint8_t* data, size_t size, const WaveFormat& fmt);

    // Result of decoding a whole .sap buffer.
    struct DecodedWave
    {
        std::vector<int16_t> samples; // interleaved s16, frames * channels
        int channels = 0;
        int samplesPerSec = 0;
        int formatTag = 0;
    };

    // Decodes a whole .sap buffer: a RIFF WAV container holding a PCM or
    // MS-ADPCM data chunk. Pure decoding - no file I/O, no audio device.
    // Returns false on malformed data or unsupported formats.
    bool decode_sap(const uint8_t* data, size_t size, DecodedWave& out);
}
