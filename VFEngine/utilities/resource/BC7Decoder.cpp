#include "BC7Decoder.hpp"
#include <cstring>

namespace resource
{
    namespace
    {
        static uint64_t getBits128(const uint8_t block[16], int startBit, int numBits)
        {
            uint64_t lo, hi;
            std::memcpy(&lo, block, 8);
            std::memcpy(&hi, block + 8, 8);

            uint64_t result = 0;
            if (startBit < 64)
            {
                result = lo >> startBit;
                if (startBit + numBits > 64)
                    result |= hi << (64 - startBit);
            }
            else
            {
                result = hi >> (startBit - 64);
            }
            return result & ((1ull << numBits) - 1);
        }
    }

    std::vector<unsigned char> BC7Decoder::decompress(
        const unsigned char* compressedData, uint32_t width, uint32_t height)
    {
        static const int weights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };
        static const int weights3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 };
        static const int weights2[4] = { 0, 21, 43, 64 };

        uint32_t blocksX = (width + 3) / 4;
        uint32_t blocksY = (height + 3) / 4;

        std::vector<unsigned char> output(static_cast<size_t>(width) * height * 4);

        for (uint32_t by = 0; by < blocksY; ++by)
        {
            for (uint32_t bx = 0; bx < blocksX; ++bx)
            {
                const uint8_t* block = compressedData + (by * blocksX + bx) * 16;

                uint8_t modeByte = block[0];
                int mode = -1;
                for (int i = 0; i < 8; ++i)
                {
                    if (modeByte & (1 << i))
                    {
                        mode = i;
                        break;
                    }
                }

                uint8_t pixels[16][4];

                if (mode == 6)
                {
                    // Mode 6: 7-bit RGBA endpoints, 1 p-bit per endpoint, 4-bit indices
                    uint8_t ep0[4], ep1[4];
                    ep0[0] = static_cast<uint8_t>(getBits128(block, 7, 7));
                    ep1[0] = static_cast<uint8_t>(getBits128(block, 14, 7));
                    ep0[1] = static_cast<uint8_t>(getBits128(block, 21, 7));
                    ep1[1] = static_cast<uint8_t>(getBits128(block, 28, 7));
                    ep0[2] = static_cast<uint8_t>(getBits128(block, 35, 7));
                    ep1[2] = static_cast<uint8_t>(getBits128(block, 42, 7));
                    ep0[3] = static_cast<uint8_t>(getBits128(block, 49, 7));
                    ep1[3] = static_cast<uint8_t>(getBits128(block, 56, 7));
                    uint8_t p0 = static_cast<uint8_t>(getBits128(block, 63, 1));
                    uint8_t p1 = static_cast<uint8_t>(getBits128(block, 64, 1));

                    int e0[4], e1[4];
                    for (int c = 0; c < 4; ++c)
                    {
                        e0[c] = (ep0[c] << 1) | p0;
                        e1[c] = (ep1[c] << 1) | p1;
                    }

                    uint8_t indices[16];
                    indices[0] = static_cast<uint8_t>(getBits128(block, 65, 3));
                    for (int i = 1; i < 16; ++i)
                        indices[i] = static_cast<uint8_t>(getBits128(block, 65 + 3 + (i - 1) * 4, 4));

                    for (int i = 0; i < 16; ++i)
                    {
                        int w = weights4[indices[i]];
                        for (int c = 0; c < 4; ++c)
                            pixels[i][c] = static_cast<uint8_t>((e0[c] * (64 - w) + e1[c] * w + 32) >> 6);
                    }
                }
                else if (mode == 1)
                {
                    // Mode 1: 6-bit RGB endpoints, 2 subsets, 3-bit indices
                    // Simplified: decode as average of endpoints
                    uint8_t ep[4][3];
                    ep[0][0] = static_cast<uint8_t>(getBits128(block, 2, 6));
                    ep[0][1] = static_cast<uint8_t>(getBits128(block, 8, 6));
                    ep[0][2] = static_cast<uint8_t>(getBits128(block, 14, 6));
                    ep[1][0] = static_cast<uint8_t>(getBits128(block, 20, 6));
                    ep[1][1] = static_cast<uint8_t>(getBits128(block, 26, 6));
                    ep[1][2] = static_cast<uint8_t>(getBits128(block, 32, 6));

                    for (int i = 0; i < 16; ++i)
                    {
                        pixels[i][0] = static_cast<uint8_t>((ep[0][0] + ep[1][0]) << 1);
                        pixels[i][1] = static_cast<uint8_t>((ep[0][1] + ep[1][1]) << 1);
                        pixels[i][2] = static_cast<uint8_t>((ep[0][2] + ep[1][2]) << 1);
                        pixels[i][3] = 255;
                    }
                }
                else
                {
                    // Unsupported mode — decode as mid-gray
                    for (int i = 0; i < 16; ++i)
                    {
                        pixels[i][0] = pixels[i][1] = pixels[i][2] = 128;
                        pixels[i][3] = 255;
                    }
                }

                for (int py = 0; py < 4; ++py)
                {
                    uint32_t dstY = by * 4 + py;
                    if (dstY >= height) continue;
                    for (int px = 0; px < 4; ++px)
                    {
                        uint32_t dstX = bx * 4 + px;
                        if (dstX >= width) continue;
                        size_t dstIdx = (static_cast<size_t>(dstY) * width + dstX) * 4;
                        std::memcpy(&output[dstIdx], pixels[py * 4 + px], 4);
                    }
                }
            }
        }

        return output;
    }
}
