#include "HeightmapLoader.hpp"
#include "../print/Log.hpp"
#include "../resource/EndianUtils.hpp"
#include "../resource/BC7Decoder.hpp"
#include "../../graphics/render/svt/SVTFileFormat.hpp"

#include <fstream>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <mutex>

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
        else if (ext == ".vfsvt")
        {
            return loadVFSVT(filePath);
        }
        else
        {
            vfLogError("HeightmapLoader: Unsupported file format: {}. Use .vfImage or .vfSVT", ext);
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
                file.close();
                return nullptr;
            }
        }
        else
        {
            vfLogError("HeightmapLoader: Unsupported compression format {} in heightmap: {}",
                       compressionFormat, filePath);
            file.close();
            return nullptr;
        }
        file.close();

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

    std::shared_ptr<HeightmapData> HeightmapLoader::loadVFSVT(const std::string& filePath)
    {
        render::svt::SVTFileReader reader;
        if (!reader.open(filePath))
        {
            vfLogError("HeightmapLoader: Failed to open SVT file: {}", filePath);
            return nullptr;
        }

        const auto& header = reader.getHeader();
        uint32_t tileSize = 1u << header.tileSizeLog2;
        uint32_t border = header.borderSize;
        uint32_t physTileSize = tileSize + 2 * border;
        uint32_t mipLevels = render::svt::computeMipLevelCount(
            header.virtualSizeLog2, header.tileSizeLog2);

        // Use mip 0 for full resolution, or a coarser mip if it's too large
        uint32_t selectedMip = 0;
        uint32_t virtualSize = 1u << header.virtualSizeLog2;
        while (virtualSize > 8192 && selectedMip < mipLevels - 1)
        {
            virtualSize >>= 1;
            ++selectedMip;
        }

        uint32_t tilesPerSide = render::svt::computeTilesPerMipSide(
            selectedMip, header.virtualSizeLog2, header.tileSizeLog2);
        if (tilesPerSide == 0) tilesPerSide = 1;

        uint32_t outputWidth = tilesPerSide * tileSize;
        uint32_t outputHeight = tilesPerSide * tileSize;

        auto result = std::make_shared<HeightmapData>();
        result->width = outputWidth;
        result->height = outputHeight;
        result->heights.resize(static_cast<size_t>(outputWidth) * outputHeight, 0.0f);

        auto decodeTileToHeights = [&](uint32_t tx, uint32_t ty)
        {
            std::vector<uint8_t> tileData;
            render::svt::VirtualTileCoord coord{tx, ty, selectedMip};
            if (!reader.readTile(coord, tileData) || tileData.empty()) return;

            auto decoded = resource::BC7Decoder::decompress(
                tileData.data(), physTileSize, physTileSize);
            if (decoded.empty()) return;

            for (uint32_t py = 0; py < tileSize; ++py)
            {
                for (uint32_t px = 0; px < tileSize; ++px)
                {
                    uint32_t imgX = tx * tileSize + px;
                    uint32_t imgY = ty * tileSize + py;
                    if (imgX >= outputWidth || imgY >= outputHeight) continue;

                    size_t srcIdx = (static_cast<size_t>(py + border) * physTileSize + px + border) * 4;
                    if (srcIdx + 2 < decoded.size())
                    {
                        float r = decoded[srcIdx + 0] / 255.0f;
                        float g = decoded[srcIdx + 1] / 255.0f;
                        float b = decoded[srcIdx + 2] / 255.0f;
                        result->heights[static_cast<size_t>(imgY) * outputWidth + imgX] =
                            0.299f * r + 0.587f * g + 0.114f * b;
                    }
                }
            }
        };

        for (uint32_t ty = 0; ty < tilesPerSide; ++ty)
            for (uint32_t tx = 0; tx < tilesPerSide; ++tx)
                decodeTileToHeights(tx, ty);

        reader.close();
        vfLogInfo("HeightmapLoader: Loaded {}x{} SVT heightmap from {} (mip {})",
                  outputWidth, outputHeight, filePath, selectedMip);
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

    // Streaming SVT tile cache for on-demand height sampling
    struct SVTHeightTileCache
    {
        render::svt::SVTFileReader reader;
        uint32_t tileSize = 0;
        uint32_t border = 0;
        uint32_t physTileSize = 0;
        uint32_t virtualSizeLog2 = 0;
        uint32_t tileSizeLog2 = 0;
        uint32_t mipLevel = 0;
        uint32_t tilesPerSide = 0;

        // LRU cache: tile coord key → decompressed heights (tileSize x tileSize floats)
        static constexpr size_t MAX_CACHED_TILES = 32;

        struct CachedTile
        {
            uint32_t tx = 0, ty = 0;
            std::vector<float> heights; // tileSize * tileSize
            uint64_t accessOrder = 0;
        };

        std::vector<CachedTile> cache;
        uint64_t accessCounter = 0;
        mutable std::mutex cacheMutex;

        float sampleHeight(float u, float v)
        {
            u = std::clamp(u, 0.0f, 0.9999f);
            v = std::clamp(v, 0.0f, 0.9999f);

            // Determine which tile this falls in
            float fx = u * static_cast<float>(tilesPerSide * tileSize);
            float fz = v * static_cast<float>(tilesPerSide * tileSize);
            uint32_t tx = static_cast<uint32_t>(fx) / tileSize;
            uint32_t ty = static_cast<uint32_t>(fz) / tileSize;
            tx = std::min(tx, tilesPerSide - 1);
            ty = std::min(ty, tilesPerSide - 1);

            // Local pixel within tile
            uint32_t localX = static_cast<uint32_t>(fx) % tileSize;
            uint32_t localY = static_cast<uint32_t>(fz) % tileSize;

            std::lock_guard<std::mutex> lock(cacheMutex);

            // Check cache
            for (auto& entry : cache)
            {
                if (entry.tx == tx && entry.ty == ty)
                {
                    entry.accessOrder = ++accessCounter;
                    size_t idx = static_cast<size_t>(localY) * tileSize + localX;
                    return idx < entry.heights.size() ? entry.heights[idx] : 0.0f;
                }
            }

            // Cache miss — load and decompress tile
            std::vector<uint8_t> tileData;
            render::svt::VirtualTileCoord coord{tx, ty, mipLevel};
            if (!reader.readTile(coord, tileData) || tileData.empty())
                return 0.0f;

            auto decoded = resource::BC7Decoder::decompress(
                tileData.data(), physTileSize, physTileSize);
            if (decoded.empty())
                return 0.0f;

            // Convert to grayscale heights (skip border)
            std::vector<float> heights(static_cast<size_t>(tileSize) * tileSize);
            for (uint32_t py = 0; py < tileSize; ++py)
            {
                for (uint32_t px = 0; px < tileSize; ++px)
                {
                    uint32_t srcX = px + border;
                    uint32_t srcY = py + border;
                    size_t srcIdx = (static_cast<size_t>(srcY) * physTileSize + srcX) * 4;
                    if (srcIdx + 2 < decoded.size())
                    {
                        float r = decoded[srcIdx + 0] / 255.0f;
                        float g = decoded[srcIdx + 1] / 255.0f;
                        float b = decoded[srcIdx + 2] / 255.0f;
                        heights[static_cast<size_t>(py) * tileSize + px] = 0.299f * r + 0.587f * g + 0.114f * b;
                    }
                }
            }

            // Evict oldest if cache full
            if (cache.size() >= MAX_CACHED_TILES)
            {
                auto oldest = std::min_element(cache.begin(), cache.end(),
                    [](const CachedTile& a, const CachedTile& b) { return a.accessOrder < b.accessOrder; });
                *oldest = CachedTile{tx, ty, std::move(heights), ++accessCounter};
            }
            else
            {
                cache.push_back(CachedTile{tx, ty, std::move(heights), ++accessCounter});
            }

            size_t idx = static_cast<size_t>(localY) * tileSize + localX;
            return idx < cache.back().heights.size() ? cache.back().heights[idx] : 0.0f;
        }
    };

    HeightSampler createStreamingHeightSamplerFromSVT(
        const std::string& svtPath,
        const TerrainBounds& bounds)
    {
        auto tileCache = std::make_shared<SVTHeightTileCache>();
        if (!tileCache->reader.open(svtPath))
        {
            vfLogError("HeightmapLoader: Failed to open SVT for streaming: {}", svtPath);
            return {};
        }

        const auto& header = tileCache->reader.getHeader();
        tileCache->tileSize = 1u << header.tileSizeLog2;
        tileCache->border = header.borderSize;
        tileCache->physTileSize = tileCache->tileSize + 2 * tileCache->border;
        tileCache->virtualSizeLog2 = header.virtualSizeLog2;
        tileCache->tileSizeLog2 = header.tileSizeLog2;

        tileCache->mipLevel = 0;
        tileCache->tilesPerSide = render::svt::computeTilesPerMipSide(
            0, header.virtualSizeLog2, header.tileSizeLog2);
        if (tileCache->tilesPerSide == 0) tileCache->tilesPerSide = 1;

        vfLogInfo("HeightmapLoader: Streaming SVT heightmap from {} ({}x{} tiles, mip 0)",
                  svtPath, tileCache->tilesPerSide, tileCache->tilesPerSide);

        return [tileCache, bounds]
        (float worldX, float worldZ) -> float
        {
            float u = (worldX - bounds.minX) / bounds.width;
            float v = (worldZ - bounds.minZ) / bounds.depth;

            float normalizedHeight = tileCache->sampleHeight(u, v);
            return bounds.minHeight + normalizedHeight * (bounds.maxHeight - bounds.minHeight);
        };
    }

    HeightSampler createCompositeHeightSampler(
        const std::vector<HeightmapRegion>& regions,
        float worldTileSize,
        float minHeight,
        float maxHeight)
    {
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

            bool isSVT = region.filePath.size() > 6 &&
                         region.filePath.substr(region.filePath.size() - 6) == ".vfSVT";

            HeightSampler sampler;

            if (isSVT)
            {
                sampler = createStreamingHeightSamplerFromSVT(region.filePath, regionBounds);
            }

            if (!sampler)
            {
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
            }

            regionSamplers->push_back({region.tileMinX, region.tileMinZ,
                                       region.tileMaxX, region.tileMaxZ,
                                       std::move(sampler)});

            vfLogInfo("CompositeHeightSampler: Loaded region [{},{} -> {},{}] from {}",
                      region.tileMinX, region.tileMinZ, region.tileMaxX, region.tileMaxZ,
                      region.filePath);
        }

        float flatHeight = minHeight;

        return [regionSamplers, worldTileSize, flatHeight]
        (float worldX, float worldZ) -> float
        {
            int32_t tileX = static_cast<int32_t>(std::floor(worldX / worldTileSize));
            int32_t tileZ = static_cast<int32_t>(std::floor(worldZ / worldTileSize));

            // Last region wins (iterate in reverse)
            for (auto it = regionSamplers->rbegin(); it != regionSamplers->rend(); ++it)
            {
                if (tileX >= it->tileMinX && tileX <= it->tileMaxX &&
                    tileZ >= it->tileMinZ && tileZ <= it->tileMaxZ)
                {
                    return it->sampler(worldX, worldZ);
                }
            }

            return flatHeight;
        };
    }
}
