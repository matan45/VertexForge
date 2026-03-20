#include "BC7Encoder.hpp"
#include <cstring>
#include <algorithm>
#include <cmath>

namespace resource
{
    namespace
    {
        // BC7 Mode 6 interpolation weights (4-bit, 16 values)
        static const int weights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

        void setBits128(uint8_t block[16], int startBit, int numBits, uint64_t value)
        {
            for (int i = 0; i < numBits; ++i)
            {
                int bit = startBit + i;
                int byteIdx = bit / 8;
                int bitIdx = bit % 8;
                if (value & (1ull << i))
                    block[byteIdx] |= (1 << bitIdx);
            }
        }

        // Find best two RGBA endpoints for a 4x4 block using min/max per channel
        void findEndpoints(const uint8_t pixels[16][4], uint8_t ep0[4], uint8_t ep1[4])
        {
            uint8_t minC[4] = {255, 255, 255, 255};
            uint8_t maxC[4] = {0, 0, 0, 0};

            for (int i = 0; i < 16; ++i)
            {
                for (int c = 0; c < 4; ++c)
                {
                    if (pixels[i][c] < minC[c]) minC[c] = pixels[i][c];
                    if (pixels[i][c] > maxC[c]) maxC[c] = pixels[i][c];
                }
            }

            for (int c = 0; c < 4; ++c)
            {
                ep0[c] = minC[c];
                ep1[c] = maxC[c];
            }
        }

        // Find best 4-bit index for a pixel given two endpoints
        uint8_t findBestIndex(const uint8_t pixel[4], const int e0[4], const int e1[4])
        {
            int bestIdx = 0;
            int bestError = INT32_MAX;

            for (int idx = 0; idx < 16; ++idx)
            {
                int w = weights4[idx];
                int error = 0;
                for (int c = 0; c < 4; ++c)
                {
                    int interp = (e0[c] * (64 - w) + e1[c] * w + 32) >> 6;
                    int diff = static_cast<int>(pixel[c]) - interp;
                    error += diff * diff;
                }
                if (error < bestError)
                {
                    bestError = error;
                    bestIdx = idx;
                }
            }
            return static_cast<uint8_t>(bestIdx);
        }

        // Encode a single 4x4 block as BC7 Mode 6
        void encodeBlockMode6(const uint8_t pixels[16][4], uint8_t block[16])
        {
            std::memset(block, 0, 16);

            // Mode 6 bit: bit 6 = 1 (mode byte = 0x40)
            block[0] = 0x40;

            uint8_t ep0_8[4], ep1_8[4];
            findEndpoints(pixels, ep0_8, ep1_8);

            // Mode 6: 7-bit endpoints + 1 p-bit each
            // Convert 8-bit endpoints to 7-bit + p-bit
            uint8_t ep0_7[4], ep1_7[4];
            uint8_t p0 = 0, p1 = 0;

            for (int c = 0; c < 4; ++c)
            {
                ep0_7[c] = ep0_8[c] >> 1;
                ep1_7[c] = ep1_8[c] >> 1;
            }
            p0 = ep0_8[0] & 1; // Use red channel LSB as p-bit
            p1 = ep1_8[0] & 1;

            // Reconstruct 8-bit endpoints with p-bits for index selection
            int e0[4], e1[4];
            for (int c = 0; c < 4; ++c)
            {
                e0[c] = (ep0_7[c] << 1) | p0;
                e1[c] = (ep1_7[c] << 1) | p1;
            }

            // Write endpoints: R0(7) G0(7) B0(7) A0(7) R1(7) G1(7) B1(7) A1(7)
            // Starting at bit 7 (after mode bits)
            setBits128(block, 7,  7, ep0_7[0]);  // R0
            setBits128(block, 14, 7, ep1_7[0]);  // R1
            setBits128(block, 21, 7, ep0_7[1]);  // G0
            setBits128(block, 28, 7, ep1_7[1]);  // G1
            setBits128(block, 35, 7, ep0_7[2]);  // B0
            setBits128(block, 42, 7, ep1_7[2]);  // B1
            setBits128(block, 49, 7, ep0_7[3]);  // A0
            setBits128(block, 56, 7, ep1_7[3]);  // A1

            // P-bits at bits 63-64
            setBits128(block, 63, 1, p0);
            setBits128(block, 64, 1, p1);

            // Compute indices
            uint8_t indices[16];
            for (int i = 0; i < 16; ++i)
            {
                indices[i] = findBestIndex(pixels[i], e0, e1);
            }

            // Anchor index (pixel 0) is 3-bit (MSB implicitly 0)
            // Ensure anchor index MSB is 0; if not, swap endpoints
            if (indices[0] >= 8)
            {
                // Swap endpoints
                for (int c = 0; c < 4; ++c)
                {
                    std::swap(ep0_7[c], ep1_7[c]);
                    std::swap(e0[c], e1[c]);
                }
                std::swap(p0, p1);

                // Rewrite endpoints
                std::memset(block, 0, 16);
                block[0] = 0x40;
                setBits128(block, 7,  7, ep0_7[0]);
                setBits128(block, 14, 7, ep1_7[0]);
                setBits128(block, 21, 7, ep0_7[1]);
                setBits128(block, 28, 7, ep1_7[1]);
                setBits128(block, 35, 7, ep0_7[2]);
                setBits128(block, 42, 7, ep1_7[2]);
                setBits128(block, 49, 7, ep0_7[3]);
                setBits128(block, 56, 7, ep1_7[3]);
                setBits128(block, 63, 1, p0);
                setBits128(block, 64, 1, p1);

                // Recompute indices with swapped endpoints
                for (int i = 0; i < 16; ++i)
                    indices[i] = findBestIndex(pixels[i], e0, e1);
            }

            // Write anchor index (3 bits) at bit 65
            setBits128(block, 65, 3, indices[0] & 0x7);

            // Write remaining 15 indices (4 bits each) starting at bit 68
            for (int i = 1; i < 16; ++i)
                setBits128(block, 65 + 3 + (i - 1) * 4, 4, indices[i]);
        }
    }

    std::vector<uint8_t> BC7Encoder::compress(const uint8_t* rgbaData,
                                                uint32_t width, uint32_t height)
    {
        uint32_t blocksX = (width + 3) / 4;
        uint32_t blocksY = (height + 3) / 4;
        uint32_t totalBlocks = blocksX * blocksY;

        std::vector<uint8_t> output(static_cast<size_t>(totalBlocks) * 16);

        for (uint32_t by = 0; by < blocksY; ++by)
        {
            for (uint32_t bx = 0; bx < blocksX; ++bx)
            {
                uint8_t pixels[16][4];

                for (int py = 0; py < 4; ++py)
                {
                    uint32_t srcY = by * 4 + py;
                    if (srcY >= height) srcY = height - 1;

                    for (int px = 0; px < 4; ++px)
                    {
                        uint32_t srcX = bx * 4 + px;
                        if (srcX >= width) srcX = width - 1;

                        size_t srcIdx = (static_cast<size_t>(srcY) * width + srcX) * 4;
                        std::memcpy(pixels[py * 4 + px], &rgbaData[srcIdx], 4);
                    }
                }

                encodeBlockMode6(pixels, &output[(by * blocksX + bx) * 16]);
            }
        }

        return output;
    }
}
