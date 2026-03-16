#pragma once

#include <vector>
#include <cstdint>
#include "config/Config.hpp"
#include "resource/Types.hpp"
#include "../ImportExport.hpp"

namespace types
{
    class VF_IMPORT_API TextureCompressor
    {
    public:
        // Compress RGBA8 data to BC7
        static std::vector<unsigned char> compressBC7(
            const unsigned char* rgbaData, uint32_t width, uint32_t height,
            importConfig::TextureCompressionQuality quality);

        // Compress RGBA32F data to BC6H (converts to half-float internally)
        static std::vector<unsigned char> compressBC6H(
            const float* rgbaFloat, uint32_t width, uint32_t height,
            importConfig::TextureCompressionQuality quality);

        // Calculate compressed data size in bytes
        static uint32_t calculateCompressedSize(
            uint32_t width, uint32_t height,
            resource::TextureCompressionFormat format);

        // Pad image dimensions to block-size multiple, returns padded data
        // bpp = bytes per pixel (4 for RGBA8, 16 for RGBA32F)
        static std::vector<unsigned char> padToBlockSize(
            const unsigned char* data, uint32_t width, uint32_t height,
            uint32_t blockX, uint32_t blockY, uint32_t bpp,
            uint32_t& paddedWidth, uint32_t& paddedHeight);

        // Decompress BC7 data back to RGBA8
        static std::vector<unsigned char> decompressBC7(
            const unsigned char* compressedData, uint32_t width, uint32_t height);

        // Get block dimensions for a compression format
        static void getBlockDimensions(resource::TextureCompressionFormat format,
                                       uint32_t& blockX, uint32_t& blockY);

    private:
        // Convert float32 to float16 (IEEE 754 half-precision)
        static uint16_t floatToHalf(float value);
    };
}
