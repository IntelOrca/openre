#include "file.h"
#include "gfx.h"
#include "interop.hpp"

#include <cstring>
#include <filesystem>

namespace openre::graphics
{
    struct AdtUnpacker
    {
    private:
        struct BitReader
        {
        private:
            const uint8_t* src{};
            size_t len{};
            size_t index{};
            uint8_t srcByte{};
            int32_t srcBitIndex{};

        public:
            BitReader(const std::vector<uint8_t>& v)
                : src(v.data())
                , len(v.size())
            {
            }

            void seek(int len, int origin)
            {
                this->index += len;
            }

            bool read(uint8_t* src, int len)
            {
                auto remaining = this->len - this->index;
                while (remaining > 0 && len > 0)
                {
                    *src++ = this->src[this->index++];
                    len--;
                    remaining--;
                }
                if (len > 0)
                {
                    while (len > 0)
                    {
                        *src++ = 0;
                        len--;
                    }
                    return false;
                }
                else
                {
                    return true;
                }
            }

            int32_t readBits(int32_t count)
            {
                int orMask = 0;
                int finalValue = this->srcByte;
                while (count > this->srcBitIndex)
                {
                    count -= this->srcBitIndex;
                    int andMask = (1 << this->srcBitIndex) - 1;
                    andMask &= finalValue;
                    andMask <<= count;
                    if (!this->read(&this->srcByte, 1))
                    {
                        this->srcByte = 0;
                    }
                    finalValue = this->srcByte;
                    this->srcBitIndex = 8;
                    orMask |= andMask;
                }
                this->srcBitIndex -= count;
                finalValue >>= this->srcBitIndex;
                finalValue = (finalValue & ((1 << count) - 1)) | orMask;
                return finalValue;
            }

            uint8_t readBit()
            {
                this->srcBitIndex--;
                if (this->srcBitIndex < 0)
                {
                    this->srcBitIndex = 7;
                    if (!this->read(&this->srcByte, 1))
                    {
                        this->srcByte = 0;
                    }
                }
                return (this->srcByte >> this->srcBitIndex) & 1;
            }

            size_t readCode()
            {
                size_t numZeroBits = 0;
                while (this->readBit() == 0)
                {
                    numZeroBits++;
                }

                size_t result = 1;
                while (numZeroBits > 0)
                {
                    result = this->readBit() + (result << 1);
                    numZeroBits--;
                }
                return result;
            }

            uint16_t readBlockLength()
            {
                uint8_t l = readBits(8);
                uint8_t h = readBits(8);
                return (h << 8) | l;
            }
        };

        template<size_t TStart, size_t TLength> struct HuffmanData
        {
            static constexpr size_t start = TStart;
            static constexpr size_t length = TLength;

            struct Range
            {
                uint32_t start;
                uint32_t length;
            };

            Range range[TLength];

        private:
            struct Node
            {
                uint32_t child[2];
            };

            Node tree[TLength * 2];

            static constexpr size_t CHILD_LEFT = 0;
            static constexpr size_t CHILD_RIGHT = 1;

        public:
            HuffmanData()
            {
                for (size_t i = 0; i < this->length; i++)
                {
                    this->range[i].start = -1;
                    this->range[i].length = -1;
                }
                for (size_t i = 0; i < this->length * 2; i++)
                {
                    this->tree[i].child[CHILD_LEFT] = -1;
                    this->tree[i].child[CHILD_RIGHT] = -1;
                }
            }

            void initialize()
            {
                uint16_t freqArray[17]{};
                for (size_t i = 0; i < this->length; i++)
                {
                    auto numValues = this->range[i].length;
                    if (numValues <= 16)
                    {
                        freqArray[numValues]++;
                    }
                }

                uint16_t tmp[18]{};
                for (size_t i = 0; i < 16; i++)
                {
                    tmp[i + 2] = (tmp[i + 1] + freqArray[i + 1]) << 1;
                }

                for (size_t i = 0; i < 18; i++)
                {
                    for (size_t j = 0; j < this->length; j++)
                    {
                        if (this->range[j].length == i)
                        {
                            this->range[j].start = tmp[i]++ & 0xFFFF;
                        }
                    }
                }
                this->initializeTree();
            }

            size_t readIndex(BitReader& src) const
            {
                size_t curIndex = this->length;
                do
                {
                    if (src.readBit())
                    {
                        curIndex = this->tree[curIndex].child[CHILD_RIGHT];
                    }
                    else
                    {
                        curIndex = this->tree[curIndex].child[CHILD_LEFT];
                    }
                } while (curIndex >= this->length);
                return curIndex;
            }

        private:
            size_t initializeTree()
            {
                auto curLength = this->length;
                auto curArrayIndex = curLength + 1;

                this->tree[curLength].child[CHILD_LEFT] = -1;
                this->tree[curLength].child[CHILD_RIGHT] = -1;
                this->tree[curArrayIndex].child[CHILD_LEFT] = -1;
                this->tree[curArrayIndex].child[CHILD_RIGHT] = -1;

                for (size_t i = 0; i < this->length; i++)
                {
                    auto curPtr8Start = this->range[i].start;
                    auto curPtr8Length = this->range[i].length;
                    curLength = this->length;
                    for (size_t j = 0; j < curPtr8Length; j++)
                    {
                        auto curMask = 1 << (curPtr8Length - j - 1);
                        auto arrayOffset = (curMask & curPtr8Start) != 0 ? CHILD_RIGHT : CHILD_LEFT;
                        if (j + 1 == curPtr8Length)
                        {
                            this->tree[curLength].child[arrayOffset] = i;
                            break;
                        }

                        if (this->tree[curLength].child[arrayOffset] == -1)
                        {
                            this->tree[curLength].child[arrayOffset] = curArrayIndex;
                            this->tree[curArrayIndex].child[CHILD_LEFT] = -1;
                            this->tree[curArrayIndex].child[CHILD_RIGHT] = -1;
                            curLength = curArrayIndex++;
                        }
                        else
                        {
                            curLength = this->tree[curLength].child[arrayOffset];
                        }
                    }
                }

                return this->length;
            }
        };

        uint8_t history[16 * 1024]{};
        size_t historyOffset{};
        HuffmanData<8, 16> huffman1;
        HuffmanData<8, 512> huffman2;
        HuffmanData<8, 16> huffman3;

    public:
        std::vector<uint8_t> decode(const std::vector<uint8_t>& input)
        {
            std::vector<uint8_t> output;

            BitReader src(input);
            src.seek(4, SEEK_CUR);

            std::memset(0, sizeof(this->history), 0);
            this->historyOffset = 0;

            uint16_t blockLength;
            while ((blockLength = src.readBlockLength()) > 0)
            {
                this->readHuffmanData1(src);
                this->readHuffmanData2(src);
                this->readHuffmanData3(src);

                for (uint16_t i = 0; i < blockLength; i++)
                {
                    auto curBitfield = this->huffman2.readIndex(src);
                    if (curBitfield < 256)
                    {
                        auto b = static_cast<uint8_t>(curBitfield);
                        this->history[this->historyOffset] = b;
                        this->historyOffset = (this->historyOffset + 1) & 0x3FFF;
                        output.push_back(b);
                    }
                    else
                    {
                        auto numValues = curBitfield - 253;
                        curBitfield = this->huffman3.readIndex(src);
                        if (curBitfield != 0)
                        {
                            auto numBits = curBitfield - 1;
                            curBitfield = src.readBits(numBits) & 0xFFFF;
                            curBitfield += 1 << numBits;
                        }

                        auto startOffset = (this->historyOffset - curBitfield - 1) & 0x3FFF;
                        for (size_t i = 0; i < numValues; i++)
                        {
                            auto b = this->history[startOffset];
                            startOffset = (startOffset + 1) & 0x3FFF;

                            this->history[this->historyOffset] = b;
                            this->historyOffset = (this->historyOffset + 1) & 0x3FFF;

                            output.push_back(b);
                        }
                    }
                }
            }
            return output;
        }

    private:
        void readHuffmanData1(BitReader& src)
        {
            size_t prevValue = 0;
            for (size_t i = 0; i < this->huffman1.length; i++)
            {
                if (src.readBit())
                {
                    this->huffman1.range[i].length = src.readCode() ^ prevValue;
                }
                else
                {
                    this->huffman1.range[i].length = prevValue;
                }
                prevValue = this->huffman1.range[i].length;
            }
            this->huffman1.initialize();
        }

        void readHuffmanData2(BitReader& src)
        {
            uint16_t tmp[512]{};
            auto bit = src.readBit();
            size_t j = 0;
            while (j < this->huffman2.length)
            {
                auto code = src.readCode();
                if (bit)
                {
                    for (size_t i = 0; i < code; i++)
                    {
                        tmp[j + i] = static_cast<uint16_t>(this->huffman1.readIndex(src));
                    }
                    j += code;
                    bit = 0;
                }
                else
                {
                    if (code > 0)
                    {
                        std::memset(&tmp[j], 0, code * sizeof(uint16_t));
                        j += code;
                    }
                    bit = 1;
                }
            }

            size_t k = 0;
            for (size_t i = 0; i < this->huffman2.length; i++)
            {
                k = k ^ tmp[i];
                this->huffman2.range[i].length = k;
            }
            this->huffman2.initialize();
        }

        void readHuffmanData3(BitReader& src)
        {
            size_t prevValue = 0;
            for (size_t i = 0; i < this->huffman3.length; i++)
            {
                if (src.readBit())
                {
                    this->huffman3.range[i].length = src.readCode() ^ prevValue;
                }
                else
                {
                    this->huffman3.range[i].length = prevValue;
                }
                prevValue = this->huffman3.range[i].length;
            }
            this->huffman3.initialize();
        }
    };

    static std::vector<uint8_t> decodeAdt(const std::vector<uint8_t>& input)
    {
        auto unpacker = std::make_unique<AdtUnpacker>();
        return unpacker->decode(input);
    }

    /**
     * Reorganizes a 256x256 + 128*64 + 128*64 RGB555 buffer to a 320x240 RGB555 buffer.
     */
    static std::vector<uint8_t> reorgAdt(const std::vector<uint8_t>& input)
    {
        std::vector<uint8_t> output(320 * 240 * 2);
        auto src = input.data();
        auto dst = output.data();
        for (uint32_t y = 0; y < 240; y++)
        {
            std::memcpy(dst, src, 256 * 2);
            src += 256 * 2;
            dst += 320 * 2;
        }

        src = input.data() + (256 * 256 * 2);
        dst = output.data() + (256 * 2);
        for (uint32_t y = 0; y < 128; y++)
        {
            std::memcpy(dst, src, 64 * 2);
            src += 128 * 2;
            dst += 320 * 2;
        }

        src = input.data() + (256 * 256 * 2) + (64 * 2);
        for (uint32_t y = 128; y < 240; y++)
        {
            std::memcpy(dst, src, 64 * 2);
            src += 128 * 2;
            dst += 320 * 2;
        }

        return output;
    }

    TextureBuffer rgb555toTextureBuffer(std::vector<uint8_t> input, uint32_t width, uint32_t height)
    {
        auto numPixels = width * height;
        std::vector<uint8_t> output(numPixels * 3);

        auto src = input.data();
        auto dst = output.data();
        for (uint32_t i = 0; i < numPixels; i++)
        {
            auto c16 = src[0] | (src[1] << 8);
            auto c32 = rgb555to8888(c16);
            *dst++ = c32 & 0xFF;
            *dst++ = (c32 >> 8) & 0xFF;
            *dst++ = (c32 >> 16) & 0xFF;
            src += 2;
        }

        TextureBuffer result;
        result.pixels = std::move(output);
        result.width = width;
        result.height = height;
        result.bpp = 24;
        return result;
    }

    TextureBuffer adt2TextureBuffer(std::vector<uint8_t> input, uint32_t width, uint32_t height)
    {
        auto buffer = openre::graphics::decodeAdt(input);
        if (buffer.empty())
            return {};

        // 320x240 background ADT files are packed as 3 smaller textures
        if (width == 320 && height == 240)
            buffer = reorgAdt(buffer);

        return rgb555toTextureBuffer(buffer, width, height);
    }

    // 0x0043C590
    int load_adt(const char* path, uint32_t* bufferSize, int mode)
    {
        return interop::call<int, const char*, uint32_t*, int>(0x0043C590, path, bufferSize, mode);
    }
}
