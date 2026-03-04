#include "HeightmapLoader.hpp"
#include "../print/Log.hpp"
#include "../resource/EndianUtils.hpp"

#include <fstream>
#include <algorithm>
#include <filesystem>

namespace terrain
{
    float HeightmapData::sample(float u, float v) const
    {
        if (!isValid())
            return 0.0f;

        u = std::clamp(u, 0.0f, 1.0f);
        v = std::clamp(v, 0.0f, 1.0f);

        float maxIndexX = static_cast<float>(width - 1);
        float maxIndexZ = static_cast<float>(height - 1);

        float fx = u * maxIndexX;
        float fz = v * maxIndexZ;

        uint32_t x0 = static_cast<uint32_t>(fx);
        uint32_t z0 = static_cast<uint32_t>(fz);
        uint32_t x1 = std::min(x0 + 1, width - 1);
        uint32_t z1 = std::min(z0 + 1, height - 1);

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

    std::shared_ptr<HeightmapData> HeightmapLoader::load(const std::string& filePath)
    {
        std::string ext = getExtension(filePath);

        if (ext == ".vfimage")
        {
            return loadVFImage(filePath);
        }
        else
        {
            vfLogError("HeightmapLoader: Unsupported file format: {}. Use .vfImage", ext);
            return nullptr;
        }
    }

    std::shared_ptr<HeightmapData> HeightmapLoader::loadVFImage(const std::string& filePath)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("HeightmapLoader: Failed to open vfImage file: {}", filePath);
            return nullptr;
        }

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

        uint32_t mipWidth = resource::endian::readLE<uint32_t>(file);
        uint32_t mipHeight = resource::endian::readLE<uint32_t>(file);

        if (mipWidth != width || mipHeight != height)
        {
            vfLogWarning("HeightmapLoader: vfImage mip0 dimensions don't match header");
        }

        // Pixel data is BGRA format
        size_t pixelCount = static_cast<size_t>(mipWidth) * mipHeight;
        std::vector<uint8_t> pixelData(pixelCount * 4);
        file.read(reinterpret_cast<char*>(pixelData.data()), static_cast<std::streamsize>(pixelData.size()));
        file.close();

        auto result = std::make_shared<HeightmapData>();
        result->width = mipWidth;
        result->height = mipHeight;
        result->heights.resize(pixelCount);

        // Convert BGRA to grayscale using luminance formula
        for (size_t i = 0; i < pixelCount; ++i)
        {
            size_t idx = i * 4;
            float b = static_cast<float>(pixelData[idx]) / 255.0f;
            float g = static_cast<float>(pixelData[idx + 1]) / 255.0f;
            float r = static_cast<float>(pixelData[idx + 2]) / 255.0f;
            result->heights[i] = 0.299f * r + 0.587f * g + 0.114f * b;
        }

        vfLogInfo("HeightmapLoader: Loaded {}x{} vfImage heightmap from {}", mipWidth, mipHeight, filePath);
        return result;
    }

    std::string HeightmapLoader::getExtension(const std::string& filePath)
    {
        std::filesystem::path path(filePath);
        std::string ext = path.extension().string();

        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        return ext;
    }

    HeightSampler createHeightSamplerFromMap(
        std::shared_ptr<const HeightmapData> heightmap,
        float terrainMinX,
        float terrainMinZ,
        float terrainWidth,
        float terrainDepth,
        float minHeight,
        float maxHeight)
    {
        return [heightmap, terrainMinX, terrainMinZ, terrainWidth, terrainDepth, minHeight, maxHeight]
        (float worldX, float worldZ) -> float
        {
            float u = (worldX - terrainMinX) / terrainWidth;
            float v = (worldZ - terrainMinZ) / terrainDepth;

            float normalizedHeight = heightmap->sample(u, v);

            return minHeight + normalizedHeight * (maxHeight - minHeight);
        };
    }
}
