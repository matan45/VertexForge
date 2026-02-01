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

        // Initialize with first layer at full weight
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

        // Calculate sum of all weights
        uint32_t sum = 0;
        for (uint32_t i = 0; i < layerCount; ++i)
        {
            sum += pixelWeights[i];
        }

        if (sum == 0)
        {
            // If all weights are zero, set first layer to full
            pixelWeights[0] = 255;
            return;
        }

        if (sum == 255)
            return;  // Already normalized

        // Scale weights to sum to 255
        uint32_t newSum = 0;
        for (uint32_t i = 0; i < layerCount - 1; ++i)
        {
            pixelWeights[i] = static_cast<uint8_t>((pixelWeights[i] * 255) / sum);
            newSum += pixelWeights[i];
        }
        // Last layer gets the remainder to ensure exact sum of 255
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
            // Invalid size, initialize flat
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

    glm::vec3 TerrainTile::getWorldVertexPosition(uint32_t x, uint32_t z) const
    {
        float height = getHeight(x, z);
        float spacing = config.getVertexSpacing();

        return glm::vec3(
            worldOrigin.x + static_cast<float>(x) * spacing,
            height,
            worldOrigin.z + static_cast<float>(z) * spacing
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

        // Clamp UV to [0, 1]
        u = std::clamp(u, 0.0f, 1.0f);
        v = std::clamp(v, 0.0f, 1.0f);

        uint32_t vertexCount = config.getVertexCount();
        float maxIndex = static_cast<float>(vertexCount - 1);

        // Convert UV to grid coordinates
        float fx = u * maxIndex;
        float fz = v * maxIndex;

        // Get integer grid coordinates
        uint32_t x0 = static_cast<uint32_t>(fx);
        uint32_t z0 = static_cast<uint32_t>(fz);
        uint32_t x1 = std::min(x0 + 1, vertexCount - 1);
        uint32_t z1 = std::min(z0 + 1, vertexCount - 1);

        // Get fractional parts for interpolation
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

        // Convert world position to UV
        float u = (worldX - worldOrigin.x) / config.worldTileSize;
        float v = (worldZ - worldOrigin.z) / config.worldTileSize;

        return sampleHeight(u, v);
    }

    void TerrainTile::setHeight(uint32_t x, uint32_t z, float height)
    {
        if (!isValidHeightIndex(x, z))
            return;

        heightData[getHeightIndex(x, z)] = std::clamp(height, config.minHeight, config.maxHeight);
        isDirty = true;
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

    bool TerrainTile::hasNeighbor(TileEdge edge) const
    {
        uint8_t edgeIndex = static_cast<uint8_t>(edge);
        return edgeIndex < 4 && neighbors[edgeIndex].exists;
    }

    bool TerrainTile::needsStitching() const
    {
        // Check if any neighbor has a different LOD level
        for (const auto& neighbor : neighbors)
        {
            if (neighbor.exists && neighbor.lodLevel != currentLOD)
            {
                return true;
            }
        }
        return false;
    }

    bool TerrainTile::hasActiveStitching() const
    {
        for (const auto& stitch : edgeStitchInfo)
        {
            if (stitch.needsSnapping)
            {
                return true;
            }
        }
        return false;
    }

    float TerrainTile::getStitchedEdgeHeight(TileEdge edge, uint32_t vertexIndex) const
    {
        uint8_t edgeIndex = static_cast<uint8_t>(edge);
        if (edgeIndex >= 4)
            return 0.0f;

        const EdgeStitchInfo& stitch = edgeStitchInfo[edgeIndex];

        // If no stitching needed or index out of range, return original height
        if (!stitch.needsSnapping || vertexIndex >= stitch.snappedHeights.size())
        {
            // Return original edge vertex height from current LOD
            const EdgeVertices& edgeVerts = edgeVertices[currentLOD][edgeIndex];
            if (vertexIndex < edgeVerts.positions.size())
            {
                return edgeVerts.positions[vertexIndex].y;
            }
            return 0.0f;
        }

        return stitch.snappedHeights[vertexIndex];
    }

    bool TerrainTile::stitchingChanged() const
    {
        for (uint8_t i = 0; i < 4; ++i)
        {
            const auto& current = edgeStitchInfo[i];
            const auto& previous = previousStitchState[i];

            // Check if needsSnapping status changed
            if (current.needsSnapping != previous.needsSnapping)
            {
                return true;
            }

            // If both need snapping, check if neighbor LOD changed
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

        // Find min/max heights
        float minH = heightData[0];
        float maxH = heightData[0];
        for (float h : heightData)
        {
            minH = std::min(minH, h);
            maxH = std::max(maxH, h);
        }

        // Ensure minimum height for frustum culling stability
        if (maxH - minH < MIN_AABB_HEIGHT)
        {
            float center = (minH + maxH) * 0.5f;
            minH = center - MIN_AABB_HEIGHT * 0.5f;
            maxH = center + MIN_AABB_HEIGHT * 0.5f;
        }

        // Height values (minH, maxH) are already absolute world heights
        // Only X and Z need worldOrigin offset
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

    void TerrainTile::clearGeometry()
    {
        for (auto& lod : lodLevels)
        {
            lod.clear();
        }

        for (auto& lodEdges : edgeVertices)
        {
            for (auto& edge : lodEdges)
            {
                edge.clear();
            }
        }

        isDirty = true;
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

} // namespace terrain
