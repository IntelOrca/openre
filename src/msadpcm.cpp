#include "msadpcm.h"

#include <cstring>

namespace openre::msadpcm
{
    namespace
    {
        uint16_t rd_u16(const uint8_t* p)
        {
            return (uint16_t)(p[0] | (p[1] << 8));
        }

        uint32_t rd_u32(const uint8_t* p)
        {
            return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        }

        // Jayant step-size adaptation table and delta floor for MS-ADPCM.
        constexpr int16_t kAdaptation[16] = { 230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230 };
        constexpr int kMinDelta = 16;

        // Decodes one 4-bit codeword with the given channel state. `s1` is the
        // most recent sample, `s2` the one before it; both are updated in place
        // along with the adaptive step size `delta`.
        int decode_nibble(int nibble, int& s1, int& s2, int& delta, int16_t coef1, int16_t coef2)
        {
            // Sign-extend the 4-bit codeword to -8..7.
            int snib = (nibble & 8) ? (nibble - 16) : nibble;
            // predictor = ((coef1*s1 + coef2*s2) >> 8) + snib*delta. The sum is
            // shifted (not divided) so negative results round towards -inf,
            // matching the reference decoder this code was verified against.
            long long pred = ((long long)coef1 * s1 + (long long)coef2 * s2) >> 8;
            pred += (long long)snib * delta;
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
    }

    bool parse_wave_fmt(const uint8_t* p, size_t size, WaveFormat& fmt)
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

    std::vector<int16_t> decode_pcm(const uint8_t* data, size_t size, const WaveFormat& fmt)
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

    // Decodes one or more consecutive MS-ADPCM blocks into interleaved s16
    // frames. Mono preamble: predictor(1) delta(2) sample1(2) sample2(2);
    // stereo preamble is interleaved (see below). The first output samples
    // are sample2 then sample1. Mono blocks store two codewords per byte
    // (upper nibble first); stereo blocks store the left channel in the upper
    // nibble and right in the lower nibble of each byte.
    std::vector<int16_t> decode_adpcm(const uint8_t* data, size_t size, const WaveFormat& fmt)
    {
        const int ch = fmt.channels;
        const int spb = fmt.samplesPerBlock;
        const size_t header = (size_t)ch * 7;

        std::vector<int16_t> out;
        if (fmt.blockAlign <= 0 || fmt.coef1.empty() || fmt.coef1.size() != fmt.coef2.size() || spb <= 0)
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

    bool decode_sap(const uint8_t* data, size_t size, DecodedWave& out)
    {
        if (!data || size < 12)
            return false;
        if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0)
            return false;

        // Walk the RIFF chunk list: "fmt " then "data", in any order.
        size_t pos = 12;
        const uint8_t* pcm = nullptr;
        size_t pcmSize = 0;
        WaveFormat fmt;
        while (pos + 8 <= size)
        {
            const uint8_t* h = data + pos;
            size_t ckSize = rd_u32(h + 4);
            if (ckSize > size - (pos + 8))
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
            decoded = decode_adpcm(pcm, pcmSize, fmt);
        else
            decoded = decode_pcm(pcm, pcmSize, fmt);
        if (decoded.empty())
            return false;

        out.samples = std::move(decoded);
        out.channels = fmt.channels;
        out.samplesPerSec = fmt.samplesPerSec;
        out.formatTag = fmt.formatTag;
        return true;
    }
}
