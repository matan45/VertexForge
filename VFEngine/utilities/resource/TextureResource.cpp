#include "../print/Log.hpp"
#include "TextureResource.hpp"
#include "EndianUtils.hpp"

#include <fstream>
#include <bit>  // For std::bit_cast

namespace resource
{
    TextureData TextureResource::loadTexture(std::string_view path)
    {
        resource::TextureData textureData;

        // Validate input
        if (path.empty())
        {
            vfLogError("Empty path provided for texture loading");
            return {};
        }

        // Open the file in binary mode
        std::ifstream inFile(path.data(), std::ios::binary);
        if (!inFile)
        {
            vfLogError("Failed to open texture file for reading: {}", path);
            return {};
        }

        // Read header file type (single byte, endian-safe)
        uint8_t headerFileType = endian::readLE<uint8_t>(inFile);
        textureData.headerFileType = static_cast<resource::FileType>(headerFileType);

        // Read version information (endian-safe)
        uint32_t majorVersion = endian::readLE<uint32_t>(inFile);
        uint32_t minorVersion = endian::readLE<uint32_t>(inFile);
        uint32_t patchVersion = endian::readLE<uint32_t>(inFile);

        if (majorVersion != Version::major || minorVersion != Version::minor || patchVersion != Version::patch)
        {
            vfLogError("Incompatible texture file version: {}.{}.{}, expected {}.{}.{}. Re-import required. path: {}",
                       majorVersion, minorVersion, patchVersion,
                       Version::major, Version::minor, Version::patch, path);
            return {};
        }

        // Read texture dimensions (endian-safe)
        textureData.width = endian::readLE<uint32_t>(inFile);
        textureData.height = endian::readLE<uint32_t>(inFile);
        textureData.numbersOfChannels = endian::readLE<uint32_t>(inFile);

        // Validate texture dimensions
        if (textureData.width == 0 || textureData.height == 0)
        {
            vfLogError("Invalid texture dimensions: {}x{}", textureData.width, textureData.height);
            return {};
        }

        if (textureData.width > 16384 || textureData.height > 16384)
        {
            vfLogError("Texture dimensions {}x{} exceed maximum limit (16384x16384)", textureData.width,
                       textureData.height);
            return {};
        }

        if (textureData.numbersOfChannels == 0 || textureData.numbersOfChannels > 4)
        {
            vfLogError("Invalid number of channels: {}", textureData.numbersOfChannels);
            return {};
        }

        textureData.mipLevels = endian::readLE<uint32_t>(inFile);

        if (textureData.mipLevels == 0 || textureData.mipLevels > 16)
        {
            vfLogError("Invalid mip level count: {}", textureData.mipLevels);
            return {};
        }

        // Read compression format
        uint8_t compressionByte = endian::readLE<uint8_t>(inFile);
        textureData.compressionFormat = static_cast<TextureCompressionFormat>(compressionByte);

        bool isCompressed = (textureData.compressionFormat != TextureCompressionFormat::Uncompressed);

        textureData.mipData.reserve(textureData.mipLevels);

        for (uint32_t level = 0; level < textureData.mipLevels; ++level)
        {
            MipLevelData mipLevel;
            mipLevel.width = endian::readLE<uint32_t>(inFile);
            mipLevel.height = endian::readLE<uint32_t>(inFile);
            mipLevel.dataSize = endian::readLE<uint32_t>(inFile);

            if (isCompressed)
            {
                // Read compressed data directly (no BGRA swap needed)
                mipLevel.data.resize(mipLevel.dataSize);
                inFile.read(reinterpret_cast<char*>(mipLevel.data.data()), mipLevel.dataSize);
            }
            else
            {
                // Read pixel data with BGRA->RGBA swap
                TGAReader::readTGA(inFile, mipLevel.width, mipLevel.height, mipLevel.data);
                mipLevel.dataSize = static_cast<uint32_t>(mipLevel.data.size());
            }

            textureData.mipData.push_back(std::move(mipLevel));
        }

        inFile.close();

        return textureData;
    }

    HDRData TextureResource::loadHDR(std::string_view path)
    {
        resource::HDRData hdrData;

        std::ifstream inFile(path.data(), std::ios::binary);
        if (!inFile)
        {
            vfLogError("Failed to open file for reading: ", path);
            return {};
        }

        uint8_t headerFileType = endian::readLE<uint8_t>(inFile);
        hdrData.headerFileType = static_cast<resource::FileType>(headerFileType);

        uint32_t majorVersion = endian::readLE<uint32_t>(inFile);
        uint32_t minorVersion = endian::readLE<uint32_t>(inFile);
        uint32_t patchVersion = endian::readLE<uint32_t>(inFile);

        if (majorVersion != Version::major || minorVersion != Version::minor || patchVersion != Version::patch)
        {
            vfLogError("Incompatible HDR file version: {}.{}.{}, expected {}.{}.{}. Re-import required.",
                       majorVersion, minorVersion, patchVersion,
                       Version::major, Version::minor, Version::patch);
            return {};
        }

        hdrData.width = endian::readLE<uint32_t>(inFile);
        hdrData.height = endian::readLE<uint32_t>(inFile);
        hdrData.numbersOfChannels = endian::readLE<uint32_t>(inFile);
        hdrData.mipLevels = endian::readLE<uint32_t>(inFile);

        // Read compression format
        uint8_t compressionByte = endian::readLE<uint8_t>(inFile);
        hdrData.compressionFormat = static_cast<TextureCompressionFormat>(compressionByte);

        hdrData.mipData.reserve(hdrData.mipLevels);

        for (uint32_t level = 0; level < hdrData.mipLevels; ++level)
        {
            MipLevelData mipLevel;
            mipLevel.width = endian::readLE<uint32_t>(inFile);
            mipLevel.height = endian::readLE<uint32_t>(inFile);
            mipLevel.dataSize = endian::readLE<uint32_t>(inFile);

            mipLevel.data.resize(mipLevel.dataSize);
            inFile.read(reinterpret_cast<char*>(mipLevel.data.data()), mipLevel.dataSize);

            hdrData.mipData.push_back(std::move(mipLevel));
        }

        inFile.close();

        return hdrData;
    }

    void HDRReader::readHDR(std::ifstream& file, int width, int height, int channels, std::vector<float>& pixels)
    {
        size_t pixelCount = static_cast<size_t>(width) * height * channels;
        pixels.resize(pixelCount);

        for (size_t i = 0; i < pixelCount; ++i)
        {
            pixels[i] = endian::readLE<float>(file);
        }
    }

    void TGAReader::readTGA(std::ifstream& file, int width, int height,
                            std::vector<unsigned char>& pixelData)
    {
        size_t pixelDataSize = width * height * 4;
        pixelData.resize(pixelDataSize);

        file.read(reinterpret_cast<char*>(pixelData.data()), pixelDataSize);

        for (size_t i = 0; i < pixelDataSize; i += 4)
        {
            std::swap(pixelData[i], pixelData[i + 2]); // Swap B and R
        }
    }
}
