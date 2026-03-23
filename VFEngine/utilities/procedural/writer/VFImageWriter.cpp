#include "VFImageWriter.hpp"
#include "resource/EndianUtils.hpp"
#include "config/Config.hpp"
#include <fstream>

namespace procedural
{
    bool VFImageWriter::write(const std::string& outputPath,
                              uint32_t width, uint32_t height,
                              const std::vector<uint8_t>& rgbaData)
    {
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile)
            return false;

        constexpr uint8_t fileType = 0;          // TEXTURE
        constexpr uint8_t compressionFormat = 0;  // Uncompressed
        constexpr uint32_t channels = 4;
        constexpr uint32_t mipLevels = 1;

        // Write header (matches Texture::saveToFileTextureWithMips format)
        resource::endian::writeLE<uint8_t>(outFile, fileType);
        resource::endian::writeLE<uint32_t>(outFile, Version::major);
        resource::endian::writeLE<uint32_t>(outFile, Version::minor);
        resource::endian::writeLE<uint32_t>(outFile, Version::patch);
        resource::endian::writeLE<uint32_t>(outFile, width);
        resource::endian::writeLE<uint32_t>(outFile, height);
        resource::endian::writeLE<uint32_t>(outFile, channels);
        resource::endian::writeLE<uint32_t>(outFile, mipLevels);
        resource::endian::writeLE<uint8_t>(outFile, compressionFormat);

        // Write mip 0 header
        uint32_t dataSize = width * height * channels;
        resource::endian::writeLE<uint32_t>(outFile, width);
        resource::endian::writeLE<uint32_t>(outFile, height);
        resource::endian::writeLE<uint32_t>(outFile, dataSize);

        // Write pixel data in BGRA format (matching TGAWriter::writeTGA)
        for (size_t i = 0; i < rgbaData.size(); i += 4)
        {
            uint8_t bgra[4] = {
                rgbaData[i + 2],  // B
                rgbaData[i + 1],  // G
                rgbaData[i + 0],  // R
                rgbaData[i + 3]   // A
            };
            outFile.write(reinterpret_cast<const char*>(bgra), 4);
        }

        outFile.close();
        return outFile.good();
    }
}
