#include "TextureCompressor.hpp"
#include "print/Log.hpp"

#include <ispc_texcomp.h>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace types
{
    uint16_t TextureCompressor::floatToHalf(float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));

        uint32_t sign = (bits >> 16) & 0x8000;
        int32_t exponent = ((bits >> 23) & 0xFF) - 127 + 15;
        uint32_t mantissa = bits & 0x7FFFFF;

        if (exponent <= 0)
        {
            if (exponent < -10)
                return static_cast<uint16_t>(sign);
            mantissa |= 0x800000;
            uint32_t shift = static_cast<uint32_t>(1 - exponent);
            mantissa >>= shift;
            return static_cast<uint16_t>(sign | (mantissa >> 13));
        }
        if (exponent >= 31)
        {
            return static_cast<uint16_t>(sign | 0x7C00); // Inf
        }
        return static_cast<uint16_t>(sign | (exponent << 10) | (mantissa >> 13));
    }

    void TextureCompressor::getBlockDimensions(resource::TextureCompressionFormat format,
                                               uint32_t& blockX, uint32_t& blockY)
    {
        switch (format)
        {
            case resource::TextureCompressionFormat::BC7:
            case resource::TextureCompressionFormat::BC6H:
                blockX = 4; blockY = 4; break;
            default:
                blockX = 1; blockY = 1; break;
        }
    }

    uint32_t TextureCompressor::calculateCompressedSize(
        uint32_t width, uint32_t height, resource::TextureCompressionFormat format)
    {
        uint32_t blockX, blockY;
        getBlockDimensions(format, blockX, blockY);

        if (format == resource::TextureCompressionFormat::Uncompressed)
            return width * height * 4;

        uint32_t blocksX = (width + blockX - 1) / blockX;
        uint32_t blocksY = (height + blockY - 1) / blockY;
        return blocksX * blocksY * 16; // All BC formats use 16 bytes per block
    }

    std::vector<unsigned char> TextureCompressor::padToBlockSize(
        const unsigned char* data, uint32_t width, uint32_t height,
        uint32_t blockX, uint32_t blockY, uint32_t bpp,
        uint32_t& paddedWidth, uint32_t& paddedHeight)
    {
        paddedWidth = ((width + blockX - 1) / blockX) * blockX;
        paddedHeight = ((height + blockY - 1) / blockY) * blockY;

        if (paddedWidth == width && paddedHeight == height)
        {
            // No padding needed
            return std::vector<unsigned char>(data, data + static_cast<size_t>(width) * height * bpp);
        }

        std::vector<unsigned char> padded(static_cast<size_t>(paddedWidth) * paddedHeight * bpp, 0);

        for (uint32_t y = 0; y < paddedHeight; ++y)
        {
            uint32_t srcY = std::min(y, height - 1);
            for (uint32_t x = 0; x < paddedWidth; ++x)
            {
                uint32_t srcX = std::min(x, width - 1);
                std::memcpy(
                    padded.data() + (static_cast<size_t>(y) * paddedWidth + x) * bpp,
                    data + (static_cast<size_t>(srcY) * width + srcX) * bpp,
                    bpp);
            }
        }

        return padded;
    }

    std::vector<unsigned char> TextureCompressor::compressBC7(
        const unsigned char* rgbaData, uint32_t width, uint32_t height,
        importConfig::TextureCompressionQuality quality)
    {
        // Pad to 4x4 block alignment
        uint32_t paddedW, paddedH;
        auto padded = padToBlockSize(rgbaData, width, height, 4, 4, 4, paddedW, paddedH);

        // Setup surface
        rgba_surface surface;
        surface.ptr = padded.data();
        surface.width = static_cast<int32_t>(paddedW);
        surface.height = static_cast<int32_t>(paddedH);
        surface.stride = static_cast<int32_t>(paddedW * 4);

        // Setup BC7 settings based on quality
        bc7_enc_settings settings;
        switch (quality)
        {
            case importConfig::TextureCompressionQuality::Fast:
                GetProfile_fast(&settings);
                break;
            case importConfig::TextureCompressionQuality::Balanced:
                GetProfile_basic(&settings);
                break;
            case importConfig::TextureCompressionQuality::Quality:
                GetProfile_slow(&settings);
                break;
        }

        // Allocate output
        uint32_t blocksX = paddedW / 4;
        uint32_t blocksY = paddedH / 4;
        std::vector<unsigned char> compressed(static_cast<size_t>(blocksX) * blocksY * 16);

        CompressBlocksBC7(&surface, compressed.data(), &settings);

        return compressed;
    }

    std::vector<unsigned char> TextureCompressor::compressBC6H(
        const float* rgbaFloat, uint32_t width, uint32_t height,
        importConfig::TextureCompressionQuality quality)
    {
        // Convert float32 RGBA to float16 RGBA for BC6H input
        uint32_t paddedW, paddedH;

        // First convert to half-float
        size_t pixelCount = static_cast<size_t>(width) * height;
        std::vector<uint16_t> halfData(pixelCount * 4);
        for (size_t i = 0; i < pixelCount * 4; ++i)
        {
            halfData[i] = floatToHalf(rgbaFloat[i]);
        }

        // Pad to 4x4 block alignment (bpp = 8 for RGBA half-float)
        auto padded = padToBlockSize(
            reinterpret_cast<const unsigned char*>(halfData.data()),
            width, height, 4, 4, 8, paddedW, paddedH);

        // Setup surface for BC6H (half-float data)
        rgba_surface surface;
        surface.ptr = padded.data();
        surface.width = static_cast<int32_t>(paddedW);
        surface.height = static_cast<int32_t>(paddedH);
        surface.stride = static_cast<int32_t>(paddedW * 8); // 8 bytes per pixel (4 x float16)

        // Setup BC6H settings
        bc6h_enc_settings settings;
        switch (quality)
        {
            case importConfig::TextureCompressionQuality::Fast:
                GetProfile_bc6h_fast(&settings);
                break;
            case importConfig::TextureCompressionQuality::Balanced:
                GetProfile_bc6h_basic(&settings);
                break;
            case importConfig::TextureCompressionQuality::Quality:
                GetProfile_bc6h_slow(&settings);
                break;
        }

        // Allocate output
        uint32_t blocksX = paddedW / 4;
        uint32_t blocksY = paddedH / 4;
        std::vector<unsigned char> compressed(static_cast<size_t>(blocksX) * blocksY * 16);

        CompressBlocksBC6H(&surface, compressed.data(), &settings);

        return compressed;
    }

    // ========================================================================
    // BC7 Mode 6 decoder
    // Reads 128-bit blocks, extracts endpoints/indices, interpolates to RGBA8
    // ========================================================================

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

        uint64_t mask = (numBits >= 64) ? ~0ULL : ((1ULL << numBits) - 1);
        return result & mask;
    }

    std::vector<unsigned char> TextureCompressor::decompressBC7(
        const unsigned char* compressedData, uint32_t width, uint32_t height)
    {
        // BC7 interpolation weights for 4-bit indices
        static const int weights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

        uint32_t blocksX = (width + 3) / 4;
        uint32_t blocksY = (height + 3) / 4;

        std::vector<unsigned char> output(static_cast<size_t>(width) * height * 4);

        for (uint32_t by = 0; by < blocksY; ++by)
        {
            for (uint32_t bx = 0; bx < blocksX; ++bx)
            {
                const uint8_t* block = compressedData + (by * blocksX + bx) * 16;

                // Detect mode from lowest set bit
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

                    // Reconstruct 8-bit endpoints
                    int e0[4], e1[4];
                    for (int c = 0; c < 4; ++c)
                    {
                        e0[c] = (ep0[c] << 1) | p0;
                        e1[c] = (ep1[c] << 1) | p1;
                    }

                    // Read indices: anchor is 3-bit, rest are 4-bit
                    uint8_t indices[16];
                    indices[0] = static_cast<uint8_t>(getBits128(block, 65, 3));
                    for (int i = 1; i < 16; ++i)
                        indices[i] = static_cast<uint8_t>(getBits128(block, 65 + 3 + (i - 1) * 4, 4));

                    // Interpolate
                    for (int i = 0; i < 16; ++i)
                    {
                        int w = weights4[indices[i]];
                        for (int c = 0; c < 4; ++c)
                            pixels[i][c] = static_cast<uint8_t>((e0[c] * (64 - w) + e1[c] * w + 32) >> 6);
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

                // Write pixels to output (clip to actual image dimensions)
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
