#include "TextureCompressor.hpp"
#include "print/Log.hpp"

#include <ispc_texcomp.h>
#include <astcenc.h>
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
            case resource::TextureCompressionFormat::ASTC_4x4:
                blockX = 4; blockY = 4; break;
            case resource::TextureCompressionFormat::ASTC_6x6:
                blockX = 6; blockY = 6; break;
            case resource::TextureCompressionFormat::ASTC_8x8:
                blockX = 8; blockY = 8; break;
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
        return blocksX * blocksY * 16; // All BC/ASTC formats use 16 bytes per block
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

    std::vector<unsigned char> TextureCompressor::compressASTC(
        const unsigned char* rgbaData, uint32_t width, uint32_t height,
        uint32_t blockX, uint32_t blockY,
        importConfig::TextureCompressionQuality quality)
    {
        // Pad to block alignment
        uint32_t paddedW, paddedH;
        auto padded = padToBlockSize(rgbaData, width, height, blockX, blockY, 4, paddedW, paddedH);

        // Configure astc-encoder
        float astcQuality;
        switch (quality)
        {
            case importConfig::TextureCompressionQuality::Fast:
                astcQuality = ASTCENC_PRE_FAST;
                break;
            case importConfig::TextureCompressionQuality::Balanced:
                astcQuality = ASTCENC_PRE_MEDIUM;
                break;
            case importConfig::TextureCompressionQuality::Quality:
                astcQuality = ASTCENC_PRE_THOROUGH;
                break;
        }

        astcenc_config config;
        astcenc_status status = astcenc_config_init(
            ASTCENC_PRF_LDR_SRGB, blockX, blockY, 1,
            astcQuality, 0, &config);

        if (status != ASTCENC_SUCCESS)
        {
            vfLogError("ASTC config init failed: {}", astcenc_get_error_string(status));
            return {};
        }

        astcenc_context* context = nullptr;
        status = astcenc_context_alloc(&config, 1, &context);
        if (status != ASTCENC_SUCCESS)
        {
            vfLogError("ASTC context alloc failed: {}", astcenc_get_error_string(status));
            return {};
        }

        // Setup image - astcenc wants row pointers
        std::vector<void*> rowPointers(paddedH);
        for (uint32_t y = 0; y < paddedH; ++y)
        {
            rowPointers[y] = padded.data() + static_cast<size_t>(y) * paddedW * 4;
        }

        astcenc_image image;
        image.dim_x = paddedW;
        image.dim_y = paddedH;
        image.dim_z = 1;
        image.data_type = ASTCENC_TYPE_U8;
        image.data = rowPointers.data();

        astcenc_swizzle swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};

        // Allocate output
        uint32_t blocksXCount = (paddedW + blockX - 1) / blockX;
        uint32_t blocksYCount = (paddedH + blockY - 1) / blockY;
        size_t outputSize = static_cast<size_t>(blocksXCount) * blocksYCount * 16;
        std::vector<unsigned char> compressed(outputSize);

        status = astcenc_compress_image(context, &image, &swizzle,
                                        compressed.data(), outputSize, 0);

        astcenc_context_free(context);

        if (status != ASTCENC_SUCCESS)
        {
            vfLogError("ASTC compression failed: {}", astcenc_get_error_string(status));
            return {};
        }

        return compressed;
    }
}
