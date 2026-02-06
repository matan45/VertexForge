#include "TerrainTile.hpp"
#include <algorithm>
#include <cmath>

namespace terrain
{
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
