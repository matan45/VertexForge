#include "HeightmapLoader.hpp"
#include "../print/EditorLogger.hpp"
#include "../resource/EndianUtils.hpp"

#include <fstream>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace terrain
{
    float HeightmapData::sample(float u, float v) const
    {
        if (!isValid())
            return 0.0f;

        // Clamp UV to [0, 1]
        u = std::clamp(u, 0.0f, 1.0f);
        v = std::clamp(v, 0.0f, 1.0f);

        float maxIndexX = static_cast<float>(width - 1);
        float maxIndexZ = static_cast<float>(height - 1);

        // Convert UV to grid coordinates
        float fx = u * maxIndexX;
        float fz = v * maxIndexZ;

        // Get integer grid coordinates
        uint32_t x0 = static_cast<uint32_t>(fx);
        uint32_t z0 = static_cast<uint32_t>(fz);
        uint32_t x1 = std::min(x0 + 1, width - 1);
        uint32_t z1 = std::min(z0 + 1, height - 1);

        // Get fractional parts for interpolation
        float fracX = fx - static_cast<float>(x0);
        float fracZ = fz - static_cast<float>(z0);

        // Bilinear interpolation
        float h00 = heights[z0 * width + x0];
        float h10 = heights[z0 * width + x1];
        float h01 = heights[z1 * width + x0];
        float h11 = heights[z1 * width + x1];

        float h0 = h00 * (1.0f - fracX) + h10 * fracX;
        float h1 = h01 * (1.0f - fracX) + h11 * fracX;

        return h0 * (1.0f - fracZ) + h1 * fracZ;
    }

    std::optional<HeightmapData> HeightmapLoader::load(const std::string& filePath)
    {
        std::string ext = getExtension(filePath);

        if (ext == ".raw")
        {
            return loadRawHeightmap(filePath);
        }
        else if (ext == ".vfimage")
        {
            return loadVFImage(filePath);
        }
        else
        {
            vfLogError("HeightmapLoader: Unsupported file format: {}. Use .vfImage or .raw", ext);
            return std::nullopt;
        }
    }

    std::optional<HeightmapData> HeightmapLoader::loadRawHeightmap(const std::string& filePath)
    {
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            vfLogError("HeightmapLoader: Failed to open RAW file: {}", filePath);
            return std::nullopt;
        }

        // Get file size
        std::streamsize fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        // Assume 16-bit heightmap, calculate dimensions (assume square)
        size_t numPixels = static_cast<size_t>(fileSize) / 2;  // 2 bytes per pixel
        uint32_t dimension = static_cast<uint32_t>(std::sqrt(static_cast<double>(numPixels)));

        if (dimension * dimension * 2 != static_cast<size_t>(fileSize))
        {
            vfLogError("HeightmapLoader: RAW file size doesn't match square dimensions: {}", filePath);
            return std::nullopt;
        }

        // Read 16-bit data
        std::vector<uint16_t> rawData(numPixels);
        file.read(reinterpret_cast<char*>(rawData.data()), fileSize);
        file.close();

        HeightmapData result;
        result.width = dimension;
        result.height = dimension;
        result.heights.resize(numPixels);

        // Convert 16-bit to normalized [0, 1]
        for (size_t i = 0; i < numPixels; ++i)
        {
            result.heights[i] = static_cast<float>(rawData[i]) / 65535.0f;
        }

        vfLogInfo("HeightmapLoader: Loaded {}x{} RAW heightmap from {}", dimension, dimension, filePath);
        return result;
    }

    std::optional<HeightmapData> HeightmapLoader::loadVFImage(const std::string& filePath)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("HeightmapLoader: Failed to open vfImage file: {}", filePath);
            return std::nullopt;
        }

        // Read header
        uint8_t fileType = resource::endian::readLE<uint8_t>(file);
        uint32_t vMajor = resource::endian::readLE<uint32_t>(file);
        uint32_t vMinor = resource::endian::readLE<uint32_t>(file);
        uint32_t vPatch = resource::endian::readLE<uint32_t>(file);
        uint32_t width = resource::endian::readLE<uint32_t>(file);
        uint32_t height = resource::endian::readLE<uint32_t>(file);
        uint32_t channels = resource::endian::readLE<uint32_t>(file);
        uint32_t mipLevels = resource::endian::readLE<uint32_t>(file);

        (void)fileType;
        (void)vMajor;
        (void)vMinor;
        (void)vPatch;
        (void)channels;
        (void)mipLevels;

        // Read first mip level dimensions
        uint32_t mipWidth = resource::endian::readLE<uint32_t>(file);
        uint32_t mipHeight = resource::endian::readLE<uint32_t>(file);

        if (mipWidth != width || mipHeight != height)
        {
            vfLogWarning("HeightmapLoader: vfImage mip0 dimensions don't match header");
        }

        // Read pixel data (BGRA format in TGA-style)
        size_t pixelCount = static_cast<size_t>(mipWidth) * mipHeight;
        std::vector<uint8_t> pixelData(pixelCount * 4);
        file.read(reinterpret_cast<char*>(pixelData.data()), static_cast<std::streamsize>(pixelData.size()));
        file.close();

        HeightmapData result;
        result.width = mipWidth;
        result.height = mipHeight;
        result.heights.resize(pixelCount);

        // Convert BGRA to grayscale heights normalized to [0, 1]
        for (size_t i = 0; i < pixelCount; ++i)
        {
            size_t idx = i * 4;
            // BGRA order
            float b = static_cast<float>(pixelData[idx]) / 255.0f;
            float g = static_cast<float>(pixelData[idx + 1]) / 255.0f;
            float r = static_cast<float>(pixelData[idx + 2]) / 255.0f;
            // Use luminance formula
            result.heights[i] = 0.299f * r + 0.587f * g + 0.114f * b;
        }

        vfLogInfo("HeightmapLoader: Loaded {}x{} vfImage heightmap from {}", mipWidth, mipHeight, filePath);
        return result;
    }

    std::string HeightmapLoader::getExtension(const std::string& filePath)
    {
        std::filesystem::path path(filePath);
        std::string ext = path.extension().string();

        // Convert to lowercase
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return std::tolower(c); });

        return ext;
    }

    HeightSampler createHeightSamplerFromMap(
        const HeightmapData& heightmap,
        float terrainMinX,
        float terrainMinZ,
        float terrainWidth,
        float terrainDepth,
        float minHeight,
        float maxHeight)
    {
        // Capture heightmap by value to ensure it persists
        return [heightmap, terrainMinX, terrainMinZ, terrainWidth, terrainDepth, minHeight, maxHeight]
            (float worldX, float worldZ) -> float
        {
            // Convert world position to UV coordinates
            float u = (worldX - terrainMinX) / terrainWidth;
            float v = (worldZ - terrainMinZ) / terrainDepth;

            // Sample normalized height [0, 1]
            float normalizedHeight = heightmap.sample(u, v);

            // Map to actual height range
            return minHeight + normalizedHeight * (maxHeight - minHeight);
        };
    }

} // namespace terrain
