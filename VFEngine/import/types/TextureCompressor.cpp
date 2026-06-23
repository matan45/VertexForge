#include "TextureCompressor.hpp"
#include "print/Log.hpp"

#include "cpumem/CpuMemoryManager.hpp"
#include "cpumem/CpuMemoryCategories.hpp"

#include <ispc_texcomp.h>
#define BCDEC_IMPLEMENTATION
#include <bcdec.h>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace
{
    // Category id for the padding scratch: resolved once and reused lock-free.
    memory::CategoryId compressCategory()
    {
        static const memory::CategoryId id =
            memory::CpuMemoryManager::instance().registerCategory(
                memory::categories::ImportTextureCompress, memory::CategoryKind::Transient);
        return id;
    }

    // Grow-only thread-local scratch for padToBlockSize. Import decodes run on
    // worker threads so thread_local gives each thread its own buffer with zero
    // contention. The buffer is never shrunk: once it grows to the largest mip it
    // will handle, subsequent calls of equal or smaller size reuse it in-place.
    thread_local std::vector<uint8_t> gPadScratch;
}

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

        const size_t requiredBytes = static_cast<size_t>(paddedWidth) * paddedHeight * bpp;

        // No padding needed: copy source into a fresh vector (unchanged behaviour).
        if (paddedWidth == width && paddedHeight == height)
        {
            return std::vector<unsigned char>(data, data + requiredBytes);
        }

        // Grow-only pool: resize the thread-local scratch up but never shrink.
        // Record any capacity increase so the diagnostics window reflects the
        // per-thread high-water mark held by this scratch.
        const uint64_t prevCapacity = static_cast<uint64_t>(gPadScratch.capacity());
        if (requiredBytes > gPadScratch.capacity())
        {
            gPadScratch.reserve(requiredBytes);
        }
        gPadScratch.resize(requiredBytes, 0);

        const uint64_t newCapacity = static_cast<uint64_t>(gPadScratch.capacity());
        if (newCapacity > prevCapacity)
        {
            // Capacity grew: record the delta against the Transient compress category.
            // addUsage only (no matching subUsage) because the scratch is
            // thread-lifetime, not call-scoped. The category accumulates the
            // per-thread high-water mark; subUsage is never called by design.
            memory::CpuMemoryManager::instance().addUsage(
                compressCategory(), newCapacity - prevCapacity);
        }

        // Fill the padded buffer
        std::memset(gPadScratch.data(), 0, requiredBytes);
        for (uint32_t y = 0; y < paddedHeight; ++y)
        {
            uint32_t srcY = std::min(y, height - 1);
            for (uint32_t x = 0; x < paddedWidth; ++x)
            {
                uint32_t srcX = std::min(x, width - 1);
                std::memcpy(
                    gPadScratch.data() + (static_cast<size_t>(y) * paddedWidth + x) * bpp,
                    data + (static_cast<size_t>(srcY) * width + srcX) * bpp,
                    bpp);
            }
        }

        // Return a copy from the pool (callers own the returned buffer; the pool
        // retains its capacity for the next call on this thread).
        return std::vector<unsigned char>(gPadScratch.data(), gPadScratch.data() + requiredBytes);
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

    std::vector<unsigned char> TextureCompressor::decompressBC7(
        const unsigned char* compressedData, uint32_t width, uint32_t height)
    {
        uint32_t blocksX = (width + 3) / 4;
        uint32_t blocksY = (height + 3) / 4;

        std::vector<unsigned char> output(static_cast<size_t>(width) * height * 4);

        // Decode into a temporary buffer with pitch = blocksX * 4 pixels * 4 bytes
        uint32_t decodedRowPitch = blocksX * 4 * 4;
        std::vector<unsigned char> decoded(static_cast<size_t>(blocksY) * 4 * decodedRowPitch);

        for (uint32_t by = 0; by < blocksY; ++by)
        {
            for (uint32_t bx = 0; bx < blocksX; ++bx)
            {
                const void* block = compressedData + (static_cast<size_t>(by) * blocksX + bx) * BCDEC_BC7_BLOCK_SIZE;
                void* dst = decoded.data() + static_cast<size_t>(by) * 4 * decodedRowPitch + static_cast<size_t>(bx) * 4 * 4;
                bcdec_bc7(block, dst, static_cast<int>(decodedRowPitch));
            }
        }

        // Copy decoded pixels, clipping to actual image dimensions
        for (uint32_t y = 0; y < height; ++y)
        {
            std::memcpy(
                output.data() + static_cast<size_t>(y) * width * 4,
                decoded.data() + static_cast<size_t>(y) * decodedRowPitch,
                static_cast<size_t>(width) * 4);
        }

        return output;
    }

    bool TextureCompressor::decompressAllMips(resource::TextureData& textureData)
    {
        if (textureData.compressionFormat != resource::TextureCompressionFormat::BC7)
        {
            return false;
        }

        for (auto& mip : textureData.mipData)
        {
            auto decompressed = decompressBC7(mip.data.data(), mip.width, mip.height);
            if (decompressed.empty())
            {
                return false;
            }
            mip.dataSize = static_cast<uint32_t>(decompressed.size());
            mip.data = std::move(decompressed);
        }
        textureData.compressionFormat = resource::TextureCompressionFormat::Uncompressed;
        return true;
    }

}
