#include "TerrainTile.hpp"
#include <algorithm>
#include <cmath>

namespace terrain
{
    // ==================== TileWeightMap ====================

    void TileWeightMap::resize(uint32_t w, uint32_t h, uint32_t layers)
    {
        width = w;
        height = h;
        layerCount = std::min(layers, MAX_TERRAIN_LAYERS);

        weights.resize(static_cast<size_t>(w) * h);

        for (auto& pixelWeights : weights)
        {
            pixelWeights.fill(0);
            if (layerCount > 0)
            {
                pixelWeights[0] = 255;
            }
        }
    }

    void TileWeightMap::setWeight(uint32_t x, uint32_t y, uint32_t layer, uint8_t value)
    {
        if (x >= width || y >= height || layer >= layerCount)
            return;

        weights[static_cast<size_t>(y) * width + x][layer] = value;
    }

    uint8_t TileWeightMap::getWeight(uint32_t x, uint32_t y, uint32_t layer) const
    {
        if (x >= width || y >= height || layer >= layerCount)
            return 0;

        return weights[static_cast<size_t>(y) * width + x][layer];
    }

    void TileWeightMap::normalize(uint32_t x, uint32_t y)
    {
        if (x >= width || y >= height || layerCount == 0)
            return;

        auto& pixelWeights = weights[static_cast<size_t>(y) * width + x];

        uint32_t sum = 0;
        for (uint32_t i = 0; i < layerCount; ++i)
        {
            sum += pixelWeights[i];
        }

        if (sum == 0)
        {
            pixelWeights[0] = 255;
            return;
        }

        if (sum == 255)
            return;

        uint32_t newSum = 0;
        for (uint32_t i = 0; i < layerCount - 1; ++i)
        {
            pixelWeights[i] = static_cast<uint8_t>((pixelWeights[i] * 255) / sum);
            newSum += pixelWeights[i];
        }
        // Last layer gets remainder to ensure exact sum of 255
        pixelWeights[layerCount - 1] = static_cast<uint8_t>(255 - newSum);
    }

    void TileWeightMap::clear()
    {
        width = 0;
        height = 0;
        layerCount = 0;
        weights.clear();
        layerMaterialIndices.fill(0);
    }

    // ==================== TerrainTile ====================

    TerrainTile::TerrainTile(const TileCoord& coord, const TerrainTileConfig& config)
        : coord(coord), config(config)
    {
        worldOrigin = computeWorldOrigin();
        initializeFlat(0.0f);
    }

    void TerrainTile::initializeFlat(float height)
    {
        uint32_t vertexCount = config.getVertexCount();
        size_t totalVertices = static_cast<size_t>(vertexCount) * vertexCount;

        heightData.resize(totalVertices);
        std::fill(heightData.begin(), heightData.end(), height);

        isDirty = true;
        updateWorldBounds();
    }

    void TerrainTile::initializeFromHeights(const std::vector<float>& heights)
    {
        uint32_t vertexCount = config.getVertexCount();
        size_t expectedSize = static_cast<size_t>(vertexCount) * vertexCount;

        if (heights.size() != expectedSize)
        {
            initializeFlat(0.0f);
            return;
        }

        heightData = heights;
        isDirty = true;
        updateWorldBounds();
    }

    glm::vec3 TerrainTile::computeWorldOrigin() const
    {
        return glm::vec3(
            static_cast<float>(coord.x) * config.worldTileSize,
            config.minHeight,
            static_cast<float>(coord.z) * config.worldTileSize
        );
    }

    bool TerrainTile::containsWorldPosition(float worldX, float worldZ) const
    {
        float minX = worldOrigin.x;
        float maxX = worldOrigin.x + config.worldTileSize;
        float minZ = worldOrigin.z;
        float maxZ = worldOrigin.z + config.worldTileSize;

        return worldX >= minX && worldX <= maxX && worldZ >= minZ && worldZ <= maxZ;
    }

    float TerrainTile::sampleHeight(float u, float v) const
    {
        if (heightData.empty())
            return 0.0f;

        u = std::clamp(u, 0.0f, 1.0f);
        v = std::clamp(v, 0.0f, 1.0f);

        uint32_t vertexCount = config.getVertexCount();
        float maxIndex = static_cast<float>(vertexCount - 1);

        float fx = u * maxIndex;
        float fz = v * maxIndex;

        uint32_t x0 = static_cast<uint32_t>(fx);
        uint32_t z0 = static_cast<uint32_t>(fz);
        uint32_t x1 = std::min(x0 + 1, vertexCount - 1);
        uint32_t z1 = std::min(z0 + 1, vertexCount - 1);

        float fracX = fx - static_cast<float>(x0);
        float fracZ = fz - static_cast<float>(z0);

        // Bilinear interpolation
        float h00 = getHeight(x0, z0);
        float h10 = getHeight(x1, z0);
        float h01 = getHeight(x0, z1);
        float h11 = getHeight(x1, z1);

        float h0 = h00 * (1.0f - fracX) + h10 * fracX;
        float h1 = h01 * (1.0f - fracX) + h11 * fracX;

        return h0 * (1.0f - fracZ) + h1 * fracZ;
    }

    float TerrainTile::sampleHeightWorld(float worldX, float worldZ) const
    {
        if (!containsWorldPosition(worldX, worldZ))
            return 0.0f;

        float u = (worldX - worldOrigin.x) / config.worldTileSize;
        float v = (worldZ - worldOrigin.z) / config.worldTileSize;

        return sampleHeight(u, v);
    }

    float TerrainTile::getHeight(uint32_t x, uint32_t z) const
    {
        if (!isValidHeightIndex(x, z))
            return 0.0f;

        return heightData[getHeightIndex(x, z)];
    }

    void TerrainTile::setNeighbor(TileEdge edge, const TileCoord& neighborCoord, uint8_t lod)
    {
        uint8_t edgeIndex = static_cast<uint8_t>(edge);
        if (edgeIndex < 4)
        {
            neighbors[edgeIndex] = NeighborInfo(neighborCoord, lod);
        }
    }

    void TerrainTile::clearNeighbor(TileEdge edge)
    {
        uint8_t edgeIndex = static_cast<uint8_t>(edge);
        if (edgeIndex < 4)
        {
            neighbors[edgeIndex] = NeighborInfo();
        }
    }

    bool TerrainTile::stitchingChanged() const
    {
        for (uint8_t i = 0; i < 4; ++i)
        {
            const auto& current = edgeStitchInfo[i];
            const auto& previous = previousStitchState[i];

            if (current.needsSnapping != previous.needsSnapping)
            {
                return true;
            }

            if (current.needsSnapping && current.neighborLOD != previous.neighborLOD)
            {
                return true;
            }
        }
        return false;
    }

    void TerrainTile::saveStitchState()
    {
        for (uint8_t i = 0; i < 4; ++i)
        {
            previousStitchState[i].needsSnapping = edgeStitchInfo[i].needsSnapping;
            previousStitchState[i].neighborLOD = edgeStitchInfo[i].neighborLOD;
        }
    }

    void TerrainTile::updateWorldBounds()
    {
        // Minimum AABB height to prevent frustum culling issues with flat terrain
        constexpr float MIN_AABB_HEIGHT = 1.0f;

        if (heightData.empty())
        {
            worldBounds = math::AABB(
                worldOrigin - glm::vec3(0.0f, MIN_AABB_HEIGHT, 0.0f),
                worldOrigin + glm::vec3(config.worldTileSize, MIN_AABB_HEIGHT, config.worldTileSize)
            );
            return;
        }

        float minH = heightData[0];
        float maxH = heightData[0];
        for (float h : heightData)
        {
            minH = std::min(minH, h);
            maxH = std::max(maxH, h);
        }

        if (maxH - minH < MIN_AABB_HEIGHT)
        {
            float center = (minH + maxH) * 0.5f;
            minH = center - MIN_AABB_HEIGHT * 0.5f;
            maxH = center + MIN_AABB_HEIGHT * 0.5f;
        }

        // Height values are already absolute world heights;
        // only X and Z need worldOrigin offset
        worldBounds = math::AABB(
            glm::vec3(worldOrigin.x, minH, worldOrigin.z),
            glm::vec3(worldOrigin.x + config.worldTileSize, maxH, worldOrigin.z + config.worldTileSize)
        );
    }

    TileLODData& TerrainTile::getCurrentLODData()
    {
        return lodLevels[currentLOD];
    }

    const TileLODData& TerrainTile::getCurrentLODData() const
    {
        return lodLevels[currentLOD];
    }

    TileLODData& TerrainTile::getLODData(uint32_t level)
    {
        return lodLevels[std::min(level, TERRAIN_LOD_COUNT - 1)];
    }

    const TileLODData& TerrainTile::getLODData(uint32_t level) const
    {
        return lodLevels[std::min(level, TERRAIN_LOD_COUNT - 1)];
    }

    bool TerrainTile::isValidHeightIndex(uint32_t x, uint32_t z) const
    {
        uint32_t vertexCount = config.getVertexCount();
        return x < vertexCount && z < vertexCount && !heightData.empty();
    }

    size_t TerrainTile::getHeightIndex(uint32_t x, uint32_t z) const
    {
        return static_cast<size_t>(z) * config.getVertexCount() + x;
    }
}
