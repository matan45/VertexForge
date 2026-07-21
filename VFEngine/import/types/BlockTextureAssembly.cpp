#include "BlockTextureAssembly.hpp"
#include "Texture.hpp"
#include "print/Log.hpp"

// bcdec: prototypes only. BCDEC_IMPLEMENTATION is defined once in
// TextureCompressor.cpp (same DLL), so the definitions link in without an ODR
// clash. Do NOT define BCDEC_IMPLEMENTATION / BCDEC_STATIC here.
#include <bcdec.h>

#include <cstring>
#include <algorithm>

namespace types
{
    uint32_t bcBlockBytes(BcKind kind)
    {
        switch (kind)
        {
        case BcKind::BC1:
        case BcKind::BC4_UNORM:
            return 8;
        default:
            return 16;
        }
    }

    uint32_t bcLevelByteSize(uint32_t width, uint32_t height, BcKind kind)
    {
        const uint32_t blocksX = (width + 3) / 4;
        const uint32_t blocksY = (height + 3) / 4;
        return blocksX * blocksY * bcBlockBytes(kind);
    }

    std::vector<unsigned char> decodeBcToRgba8(const unsigned char* blocks,
                                               uint32_t width, uint32_t height, BcKind kind)
    {
        if (!blocks || width == 0 || height == 0)
            return {};

        const uint32_t blocksX = (width + 3) / 4;
        const uint32_t blocksY = (height + 3) / 4;
        const uint32_t blockSize = bcBlockBytes(kind);

        // Native bytes-per-pixel bcdec emits: BC1/2/3 -> RGBA8 (4), BC4 -> R (1),
        // BC5 -> RG (2). Decode into a padded (block-aligned) buffer, then clip.
        const uint32_t nativeBpp = (kind == BcKind::BC4_UNORM) ? 1u
                                 : (kind == BcKind::BC5_UNORM) ? 2u
                                 : 4u;
        const uint32_t rowPitch = blocksX * 4 * nativeBpp; // bytes per padded row
        std::vector<unsigned char> decoded(static_cast<size_t>(blocksY) * 4 * rowPitch);

        for (uint32_t by = 0; by < blocksY; ++by)
        {
            for (uint32_t bx = 0; bx < blocksX; ++bx)
            {
                const void* block = blocks + (static_cast<size_t>(by) * blocksX + bx) * blockSize;
                void* dst = decoded.data() + static_cast<size_t>(by) * 4 * rowPitch
                          + static_cast<size_t>(bx) * 4 * nativeBpp;
                switch (kind)
                {
                case BcKind::BC1: bcdec_bc1(block, dst, static_cast<int>(rowPitch)); break;
                case BcKind::BC2: bcdec_bc2(block, dst, static_cast<int>(rowPitch)); break;
                case BcKind::BC3: bcdec_bc3(block, dst, static_cast<int>(rowPitch)); break;
                case BcKind::BC4_UNORM: bcdec_bc4(block, dst, static_cast<int>(rowPitch)); break;
                case BcKind::BC5_UNORM: bcdec_bc5(block, dst, static_cast<int>(rowPitch)); break;
                default: return {};
                }
            }
        }

        // Clip the padded native buffer to a tight width*height RGBA8 image.
        std::vector<unsigned char> rgba(static_cast<size_t>(width) * height * 4);
        for (uint32_t y = 0; y < height; ++y)
        {
            const unsigned char* srcRow = decoded.data() + static_cast<size_t>(y) * rowPitch;
            unsigned char* dstRow = rgba.data() + static_cast<size_t>(y) * width * 4;
            for (uint32_t x = 0; x < width; ++x)
            {
                const unsigned char* s = srcRow + static_cast<size_t>(x) * nativeBpp;
                unsigned char* d = dstRow + static_cast<size_t>(x) * 4;
                if (nativeBpp == 4)
                {
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                }
                else if (nativeBpp == 1) // BC4 -> grayscale
                {
                    d[0] = d[1] = d[2] = s[0];
                    d[3] = 255;
                }
                else // BC5 -> RG, B=0
                {
                    d[0] = s[0]; d[1] = s[1]; d[2] = 0; d[3] = 255;
                }
            }
        }
        return rgba;
    }

    float halfToFloat(uint16_t h)
    {
        const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
        uint32_t exp = (h & 0x7C00u) >> 10;
        uint32_t mant = (h & 0x03FFu);
        uint32_t bits;

        if (exp == 0)
        {
            if (mant == 0)
            {
                bits = sign; // +/- zero
            }
            else
            {
                // Subnormal half -> normalized float.
                exp = 1;
                while ((mant & 0x0400u) == 0)
                {
                    mant <<= 1;
                    --exp;
                }
                mant &= 0x03FFu;
                bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
            }
        }
        else if (exp == 0x1F)
        {
            bits = sign | 0x7F800000u | (mant << 13); // inf / nan
        }
        else
        {
            bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
        }

        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }

    // ---- Texture write-assembly members (reuse the private .vfImage/.vfHdr writers) ----

    void Texture::writeBlockMips(std::string_view fileName, std::string_view location,
                                 std::vector<resource::MipLevelData> levels, uint32_t width, uint32_t height,
                                 resource::TextureCompressionFormat format) const
    {
        if (levels.empty())
        {
            vfLogError("KTX/DDS: no block mip levels to write for '{}'", fileName);
            return;
        }

        if (format == resource::TextureCompressionFormat::BC6H)
        {
            resource::HDRData hdrData;
            hdrData.headerFileType = resource::FileType::HDR;
            hdrData.width = width;
            hdrData.height = height;
            hdrData.numbersOfChannels = 4;
            hdrData.compressionFormat = resource::TextureCompressionFormat::BC6H;
            hdrData.mipLevels = static_cast<uint32_t>(levels.size());
            hdrData.mipData = std::move(levels);
            saveToFileHDRWithMips(fileName, location, hdrData);
        }
        else
        {
            resource::TextureData textureData;
            textureData.headerFileType = resource::FileType::TEXTURE;
            textureData.width = width;
            textureData.height = height;
            textureData.numbersOfChannels = 4;
            textureData.compressionFormat = format; // BC7
            textureData.mipLevels = static_cast<uint32_t>(levels.size());
            textureData.mipData = std::move(levels);
            saveToFileTextureWithMips(fileName, location, textureData);
        }
    }

    void Texture::writeRgba8Mips(std::string_view fileName, std::string_view location,
                                 std::vector<resource::MipLevelData> rgbaLevels, uint32_t width, uint32_t height,
                                 importConfig::TextureCompressionMode mode,
                                 importConfig::TextureCompressionQuality quality) const
    {
        if (rgbaLevels.empty())
        {
            vfLogError("KTX/DDS: no RGBA8 mip levels to write for '{}'", fileName);
            return;
        }

        resource::TextureData textureData;
        textureData.headerFileType = resource::FileType::TEXTURE;
        textureData.width = width;
        textureData.height = height;
        textureData.numbersOfChannels = 4;
        textureData.mipData = std::move(rgbaLevels);

        if (textureData.mipData.size() == 1)
        {
            // Only a base level was supplied — regenerate the chain (engine policy,
            // same as PNG/embedded import). Sets mipLevels + appends mips.
            generateMipmaps(textureData);
        }
        else
        {
            // Multi-level source: preserve the authored chain, re-encode in place.
            textureData.mipLevels = static_cast<uint32_t>(textureData.mipData.size());
        }

        compressTextureMips(textureData, mode, quality);
        saveToFileTextureWithMips(fileName, location, textureData);
    }

    void Texture::writeFloatHdrMips(std::string_view fileName, std::string_view location,
                                    std::vector<resource::MipLevelData> floatLevels, uint32_t width, uint32_t height,
                                    importConfig::TextureCompressionMode mode,
                                    importConfig::TextureCompressionQuality quality) const
    {
        if (floatLevels.empty())
        {
            vfLogError("KTX/DDS: no float mip levels to write for '{}'", fileName);
            return;
        }

        resource::HDRData hdrData;
        hdrData.headerFileType = resource::FileType::HDR;
        hdrData.width = width;
        hdrData.height = height;
        hdrData.numbersOfChannels = 4;

        if (floatLevels.size() == 1)
        {
            // Regenerate the mip chain from the base level's float pixels.
            const auto& base = floatLevels[0];
            std::vector<float> basePixels(static_cast<size_t>(width) * height * 4, 0.0f);
            const size_t copyBytes = std::min(base.data.size(), basePixels.size() * sizeof(float));
            std::memcpy(basePixels.data(), base.data.data(), copyBytes);
            generateHDRMipmaps(basePixels, width, height, hdrData); // pushes base + mips, sets mipLevels
        }
        else
        {
            hdrData.mipData = std::move(floatLevels);
            hdrData.mipLevels = static_cast<uint32_t>(hdrData.mipData.size());
        }

        compressHDRMips(hdrData, mode, quality);
        saveToFileHDRWithMips(fileName, location, hdrData);
    }

    TextureImportResult Texture::writeDecodedTexture(DecodedTexture&& decoded, std::string_view fileName,
                                                     std::string_view location,
                                                     const importConfig::ImportConfig& config) const
    {
        if (!decoded.ok || decoded.levels.empty() || decoded.width == 0 || decoded.height == 0)
            return TextureImportResult::Failed;

        switch (decoded.payload)
        {
        case DecodedTexture::Payload::BcBlocks:
            writeBlockMips(fileName, location, std::move(decoded.levels), decoded.width, decoded.height,
                           decoded.blockFormat);
            break;
        case DecodedTexture::Payload::Rgba8:
            writeRgba8Mips(fileName, location, std::move(decoded.levels), decoded.width, decoded.height,
                           config.compressionMode, config.compressionQuality);
            break;
        case DecodedTexture::Payload::Float:
            writeFloatHdrMips(fileName, location, std::move(decoded.levels), decoded.width, decoded.height,
                              config.compressionMode, config.compressionQuality);
            break;
        }

        return decoded.isHdr ? TextureImportResult::WroteVfHdr : TextureImportResult::WroteVfImage;
    }
}
