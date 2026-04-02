#include "HeightmapLoader.hpp"
#include "../print/Log.hpp"
#include "../resource/EndianUtils.hpp"
#include "../resource/BC7Decoder.hpp"
#include "../resource/VFSHelpers.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>

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
        auto rawData = resource::readFileBytes(filePath);
        if (rawData.empty())
        {
            vfLogError("HeightmapLoader: Failed to read vfImage file: {}", filePath);
            return nullptr;
        }

        std::string dataStr(rawData.begin(), rawData.end());
        std::istringstream file(dataStr, std::ios::binary);

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

        // Read compression format byte (added by VK-914 texture compression)
        uint8_t compressionFormat = resource::endian::readLE<uint8_t>(file);

        uint32_t mipWidth = resource::endian::readLE<uint32_t>(file);
        uint32_t mipHeight = resource::endian::readLE<uint32_t>(file);
        uint32_t dataSize = resource::endian::readLE<uint32_t>(file);

        if (mipWidth != width || mipHeight != height)
        {
            vfLogWarning("HeightmapLoader: vfImage mip0 dimensions don't match header");
        }

        std::vector<uint8_t> pixelData;
        size_t pixelCount = static_cast<size_t>(mipWidth) * mipHeight;

        if (compressionFormat == 0)
        {
            // Uncompressed BGRA pixel data
            pixelData.resize(pixelCount * 4);
            file.read(reinterpret_cast<char*>(pixelData.data()), static_cast<std::streamsize>(pixelData.size()));
        }
        else if (compressionFormat == 1)
        {
            // BC7 compressed — read compressed data and decompress
            std::vector<uint8_t> compressedData(dataSize);
            file.read(reinterpret_cast<char*>(compressedData.data()), dataSize);
            pixelData = resource::BC7Decoder::decompress(compressedData.data(), mipWidth, mipHeight);
            if (pixelData.empty())
            {
                vfLogError("HeightmapLoader: Failed to decompress BC7 heightmap: {}", filePath);
                return nullptr;
            }
        }
        else
        {
            vfLogError("HeightmapLoader: Unsupported compression format {} in heightmap: {}",
                       compressionFormat, filePath);
            return nullptr;
        }

        auto result = std::make_shared<HeightmapData>();
        result->width = mipWidth;
        result->height = mipHeight;
        result->heights.resize(pixelCount);

        // Convert to grayscale using luminance formula
        // Uncompressed vfImage is BGRA, BC7-decoded is RGBA
        bool isBGRA = (compressionFormat == 0);
        for (size_t i = 0; i < pixelCount; ++i)
        {
            size_t idx = i * 4;
            float r, g, b;
            if (isBGRA)
            {
                b = static_cast<float>(pixelData[idx]) / 255.0f;
                g = static_cast<float>(pixelData[idx + 1]) / 255.0f;
                r = static_cast<float>(pixelData[idx + 2]) / 255.0f;
            }
            else
            {
                r = static_cast<float>(pixelData[idx]) / 255.0f;
                g = static_cast<float>(pixelData[idx + 1]) / 255.0f;
                b = static_cast<float>(pixelData[idx + 2]) / 255.0f;
            }
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
        const TerrainBounds& bounds)
    {
        return [heightmap, bounds]
        (float worldX, float worldZ) -> float
        {
            float u = (worldX - bounds.minX) / bounds.width;
            float v = (worldZ - bounds.minZ) / bounds.depth;

            float normalizedHeight = heightmap->sample(u, v);

            return bounds.minHeight + normalizedHeight * (bounds.maxHeight - bounds.minHeight);
        };
    }

    HeightSampler createCompositeHeightSampler(
        const std::vector<HeightmapRegion>& regions,
        float worldTileSize,
        float minHeight,
        float maxHeight)
    {
        // Build per-region samplers
        struct RegionSampler
        {
            int32_t tileMinX, tileMinZ, tileMaxX, tileMaxZ;
            HeightSampler sampler;
        };

        auto regionSamplers = std::make_shared<std::vector<RegionSampler>>();

        for (const auto& region : regions)
        {
            if (region.filePath.empty())
                continue;

            TerrainBounds regionBounds;
            regionBounds.minX = static_cast<float>(region.tileMinX) * worldTileSize;
            regionBounds.minZ = static_cast<float>(region.tileMinZ) * worldTileSize;
            regionBounds.width = static_cast<float>(region.tileMaxX - region.tileMinX + 1) * worldTileSize;
            regionBounds.depth = static_cast<float>(region.tileMaxZ - region.tileMinZ + 1) * worldTileSize;
            regionBounds.minHeight = minHeight;
            regionBounds.maxHeight = maxHeight;

            HeightSampler sampler;

            auto heightmapData = HeightmapLoader::load(region.filePath);
            if (heightmapData && heightmapData->isValid())
            {
                sampler = createHeightSamplerFromMap(heightmapData, regionBounds);
            }
            else
            {
                vfLogWarning("CompositeHeightSampler: Failed to load region heightmap: {}", region.filePath);
                continue;
            }

            regionSamplers->push_back({region.tileMinX, region.tileMinZ,
                                       region.tileMaxX, region.tileMaxZ,
                                       std::move(sampler)});

            vfLogInfo("CompositeHeightSampler: Loaded region [{},{} -> {},{}] from {}",
                      region.tileMinX, region.tileMinZ, region.tileMaxX, region.tileMaxZ,
                      region.filePath);
        }

        // Build O(1) tile→region lookup grid
        int32_t gridMinX = std::numeric_limits<int32_t>::max();
        int32_t gridMinZ = std::numeric_limits<int32_t>::max();
        int32_t gridMaxX = std::numeric_limits<int32_t>::min();
        int32_t gridMaxZ = std::numeric_limits<int32_t>::min();
        for (const auto& rs : *regionSamplers)
        {
            gridMinX = std::min(gridMinX, rs.tileMinX);
            gridMinZ = std::min(gridMinZ, rs.tileMinZ);
            gridMaxX = std::max(gridMaxX, rs.tileMaxX);
            gridMaxZ = std::max(gridMaxZ, rs.tileMaxZ);
        }

        int32_t gridW = (regionSamplers->empty()) ? 0 : (gridMaxX - gridMinX + 1);
        int32_t gridH = (regionSamplers->empty()) ? 0 : (gridMaxZ - gridMinZ + 1);

        // -1 = no region, otherwise index into regionSamplers
        auto tileGrid = std::make_shared<std::vector<int32_t>>(
            static_cast<size_t>(gridW) * gridH, -1);

        // Last region wins for overlaps
        for (size_t i = 0; i < regionSamplers->size(); ++i)
        {
            const auto& rs = (*regionSamplers)[i];
            for (int32_t tz = rs.tileMinZ; tz <= rs.tileMaxZ; ++tz)
            {
                for (int32_t tx = rs.tileMinX; tx <= rs.tileMaxX; ++tx)
                {
                    int32_t gx = tx - gridMinX;
                    int32_t gz = tz - gridMinZ;
                    (*tileGrid)[static_cast<size_t>(gz) * gridW + gx] = static_cast<int32_t>(i);
                }
            }
        }

        float flatHeight = minHeight;
        float blendDistance = worldTileSize;
        float probeOffset = worldTileSize * 0.01f;

        // O(1) lookup lambda
        auto lookupRegion = [regionSamplers, tileGrid, gridMinX, gridMinZ, gridW, gridH, worldTileSize]
        (int32_t tileX, int32_t tileZ) -> const RegionSampler*
        {
            int32_t gx = tileX - gridMinX;
            int32_t gz = tileZ - gridMinZ;
            if (gx < 0 || gz < 0 || gx >= gridW || gz >= gridH)
                return nullptr;
            int32_t idx = (*tileGrid)[static_cast<size_t>(gz) * gridW + gx];
            return (idx >= 0) ? &(*regionSamplers)[idx] : nullptr;
        };

        return [regionSamplers, tileGrid, lookupRegion, worldTileSize, flatHeight, blendDistance, probeOffset,
                gridMinX, gridMinZ, gridW, gridH]
        (float worldX, float worldZ) -> float
        {
            int32_t tileX = static_cast<int32_t>(std::floor(worldX / worldTileSize));
            int32_t tileZ = static_cast<int32_t>(std::floor(worldZ / worldTileSize));

            const RegionSampler* primaryRegion = lookupRegion(tileX, tileZ);
            if (!primaryRegion)
                return flatHeight;

            float primaryHeight = primaryRegion->sampler(worldX, worldZ);

            // Calculate distance to nearest edge of primary region (in world units)
            float regionMinX = static_cast<float>(primaryRegion->tileMinX) * worldTileSize;
            float regionMinZ = static_cast<float>(primaryRegion->tileMinZ) * worldTileSize;
            float regionMaxX = static_cast<float>(primaryRegion->tileMaxX + 1) * worldTileSize;
            float regionMaxZ = static_cast<float>(primaryRegion->tileMaxZ + 1) * worldTileSize;

            float distToEdge = std::min({
                worldX - regionMinX,
                regionMaxX - worldX,
                worldZ - regionMinZ,
                regionMaxZ - worldZ
            });

            if (distToEdge >= blendDistance)
                return primaryHeight;

            // Probe into the neighboring tile across the nearest edge
            float probeX = worldX;
            float probeZ = worldZ;
            float edgeDistX = std::min(worldX - regionMinX, regionMaxX - worldX);
            float edgeDistZ = std::min(worldZ - regionMinZ, regionMaxZ - worldZ);

            if (edgeDistX < edgeDistZ)
            {
                if (worldX - regionMinX < regionMaxX - worldX)
                    probeX = regionMinX - probeOffset;
                else
                    probeX = regionMaxX + probeOffset;
            }
            else
            {
                if (worldZ - regionMinZ < regionMaxZ - worldZ)
                    probeZ = regionMinZ - probeOffset;
                else
                    probeZ = regionMaxZ + probeOffset;
            }

            int32_t probeTileX = static_cast<int32_t>(std::floor(probeX / worldTileSize));
            int32_t probeTileZ = static_cast<int32_t>(std::floor(probeZ / worldTileSize));
            const RegionSampler* neighborRegion = lookupRegion(probeTileX, probeTileZ);

            float t = distToEdge / blendDistance;
            t = t * t * (3.0f - 2.0f * t); // smoothstep

            if (!neighborRegion)
            {
                return flatHeight + t * (primaryHeight - flatHeight);
            }

            float neighborHeight = neighborRegion->sampler(worldX, worldZ);
            return neighborHeight + t * (primaryHeight - neighborHeight);
        };
    }
}
