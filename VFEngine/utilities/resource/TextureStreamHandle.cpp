#include "TextureStreamHandle.hpp"
#include "EndianUtils.hpp"
#include "../print/Log.hpp"
#include "../config/Config.hpp"

namespace resource
{
    TextureStreamHandle::~TextureStreamHandle() = default;

    TextureStreamHandle::TextureStreamHandle(TextureStreamHandle&& other) noexcept
        : header(std::move(other.header)),
          filePath(std::move(other.filePath))
    {
    }

    TextureStreamHandle& TextureStreamHandle::operator=(TextureStreamHandle&& other) noexcept
    {
        if (this != &other)
        {
            header = std::move(other.header);
            filePath = std::move(other.filePath);
        }
        return *this;
    }

    bool TextureStreamHandle::openStream(std::string_view path)
    {
        filePath = std::string(path);

        // Open file temporarily to parse header and record mip offsets, then close.
        // The file is re-opened on demand for each readMipLevel call to avoid
        // holding OS file descriptors for potentially thousands of textures.
        std::ifstream file(filePath, std::ios::binary);
        if (!file)
        {
            vfLogError("TextureStreamHandle: Failed to open file: {}", filePath);
            return false;
        }

        if (!parseHeader(file))
        {
            filePath.clear();
            return false;
        }

        return true;
    }

    void TextureStreamHandle::close()
    {
        header = {};
        filePath.clear();
    }

    bool TextureStreamHandle::parseHeader(std::ifstream& file)
    {
        // Read file type byte
        uint8_t headerFileType = endian::readLE<uint8_t>(file);
        header.headerFileType = static_cast<FileType>(headerFileType);

        // Read and validate version
        uint32_t majorVersion = endian::readLE<uint32_t>(file);
        uint32_t minorVersion = endian::readLE<uint32_t>(file);
        uint32_t patchVersion = endian::readLE<uint32_t>(file);

        if (majorVersion != Version::major || minorVersion != Version::minor || patchVersion != Version::patch)
        {
            vfLogError("TextureStreamHandle: Incompatible file version: {}.{}.{}, expected {}.{}.{}. path: {}",
                       majorVersion, minorVersion, patchVersion,
                       Version::major, Version::minor, Version::patch, filePath);
            return false;
        }

        // Read texture dimensions
        header.width = endian::readLE<uint32_t>(file);
        header.height = endian::readLE<uint32_t>(file);
        header.channels = endian::readLE<uint32_t>(file);
        header.mipLevels = endian::readLE<uint32_t>(file);

        if (header.width == 0 || header.height == 0 || header.mipLevels == 0 || header.mipLevels > 16)
        {
            vfLogError("TextureStreamHandle: Invalid texture dimensions {}x{} or mip count {} in {}",
                       header.width, header.height, header.mipLevels, filePath);
            return false;
        }

        // Read compression format
        uint8_t compressionByte = endian::readLE<uint8_t>(file);
        header.compression = static_cast<TextureCompressionFormat>(compressionByte);

        // Now scan through mip levels to record file offsets
        // Each mip in the file is: [width(4) height(4) dataSize(4) data(dataSize)]
        header.mipInfos.resize(header.mipLevels);

        for (uint32_t level = 0; level < header.mipLevels; ++level)
        {
            // Record position before reading mip header
            header.mipInfos[level].fileOffset = file.tellg();

            uint32_t mipWidth = endian::readLE<uint32_t>(file);
            uint32_t mipHeight = endian::readLE<uint32_t>(file);
            uint32_t mipDataSize = endian::readLE<uint32_t>(file);

            header.mipInfos[level].width = mipWidth;
            header.mipInfos[level].height = mipHeight;

            if (header.compression == TextureCompressionFormat::Uncompressed)
            {
                // Uncompressed: TGA stores width*height*4, but dataSize from file
                // may differ; for streaming we need to know what to read
                header.mipInfos[level].dataSize = mipWidth * mipHeight * 4;
            }
            else
            {
                header.mipInfos[level].dataSize = mipDataSize;
            }

            // Skip past this mip's pixel data
            if (header.compression == TextureCompressionFormat::Uncompressed)
            {
                // Uncompressed TGA data: width*height*4 bytes
                file.seekg(static_cast<std::streamoff>(mipWidth * mipHeight * 4), std::ios::cur);
            }
            else
            {
                file.seekg(static_cast<std::streamoff>(mipDataSize), std::ios::cur);
            }

            if (!file)
            {
                vfLogError("TextureStreamHandle: Failed to scan mip level {} in {}", level, filePath);
                return false;
            }
        }

        return true;
    }

    bool TextureStreamHandle::readMipLevel(uint32_t level, MipLevelData& out)
    {
        if (level >= header.mipLevels)
        {
            vfLogError("TextureStreamHandle: Mip level {} out of range (max {})", level, header.mipLevels);
            return false;
        }

        std::lock_guard lock(fileMutex);

        // Open file on demand — avoids holding OS file descriptors indefinitely
        std::ifstream file(filePath, std::ios::binary);
        if (!file)
        {
            vfLogError("TextureStreamHandle: Failed to re-open file for mip read: {}", filePath);
            return false;
        }

        const auto& mipInfo = header.mipInfos[level];

        // Seek to the mip's file position
        file.seekg(mipInfo.fileOffset);
        if (!file)
        {
            vfLogError("TextureStreamHandle: Failed to seek to mip level {} in {}", level, filePath);
            return false;
        }

        // Read past the per-mip header (width, height, dataSize)
        uint32_t mipWidth = endian::readLE<uint32_t>(file);
        uint32_t mipHeight = endian::readLE<uint32_t>(file);
        uint32_t mipDataSize = endian::readLE<uint32_t>(file);

        out.width = mipWidth;
        out.height = mipHeight;

        if (header.compression == TextureCompressionFormat::Uncompressed)
        {
            // Read and do BGRA->RGBA swap (TGA format)
            size_t pixelDataSize = mipWidth * mipHeight * 4;
            out.data.resize(pixelDataSize);
            file.read(reinterpret_cast<char*>(out.data.data()), pixelDataSize);

            for (size_t i = 0; i < pixelDataSize; i += 4)
            {
                std::swap(out.data[i], out.data[i + 2]); // Swap B and R
            }

            out.dataSize = static_cast<uint32_t>(pixelDataSize);
        }
        else
        {
            // Compressed data — read directly
            out.data.resize(mipDataSize);
            file.read(reinterpret_cast<char*>(out.data.data()), mipDataSize);
            out.dataSize = mipDataSize;
        }

        if (!file)
        {
            vfLogError("TextureStreamHandle: Failed to read mip level {} data in {}", level, filePath);
            return false;
        }

        return true;
    }

    bool TextureStreamHandle::readMipRange(uint32_t fromLevel, uint32_t toLevel, std::vector<MipLevelData>& out)
    {
        if (fromLevel > toLevel || toLevel >= header.mipLevels)
        {
            vfLogError("TextureStreamHandle: Invalid mip range [{}, {}] (max {})",
                       fromLevel, toLevel, header.mipLevels - 1);
            return false;
        }

        out.resize(toLevel - fromLevel + 1);
        for (uint32_t level = fromLevel; level <= toLevel; ++level)
        {
            if (!readMipLevel(level, out[level - fromLevel]))
            {
                return false;
            }
        }

        return true;
    }

    std::unique_ptr<TextureStreamHandle> TextureStreamResource::openStream(std::string_view path)
    {
        auto handle = std::make_unique<TextureStreamHandle>();
        if (!handle->openStream(path))
        {
            return nullptr;
        }
        return handle;
    }
}
