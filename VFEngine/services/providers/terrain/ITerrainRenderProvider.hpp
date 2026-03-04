#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>

namespace math
{
    class Frustum;
}

namespace terrain
{
    class TerrainTile;
}

namespace services
{
    struct TerrainTileLightmapInfo
    {
        int32_t coordX = 0;
        int32_t coordZ = 0;
        glm::vec4 scaleOffset{1.0f, 1.0f, 0.0f, 0.0f};
        std::string lightmapPath;
    };

    class ITerrainRenderProvider
    {
    public:
        virtual ~ITerrainRenderProvider() = default;

        virtual std::vector<terrain::TerrainTile*> getVisibleTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition) = 0;

        /// Lightweight frustum+distance query that does NOT modify tile state
        /// (no LOD update, no regeneration, no isVisible flag changes).
        /// Used for RTT frustum merging.
        virtual std::vector<terrain::TerrainTile*> queryVisibleTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition) = 0;

        virtual bool hasActiveTerrain() const = 0;

        virtual std::string getTerrainMaterialPath() const = 0;

        virtual void setDistanceCullingEnabled(bool enabled) = 0;
        virtual void setMaxDrawDistance(float distance) = 0;

        virtual bool ensureTileLODData(terrain::TerrainTile& tile, uint8_t lodLevel) = 0;
        virtual void releaseTileRAMData(terrain::TerrainTile& tile) = 0;

        virtual std::vector<TerrainTileLightmapInfo> getTerrainLightmapData() const = 0;
        virtual bool consumeTerrainLightmapDirty() = 0;

        virtual void markTerrainMaterialDirty() = 0;
        virtual bool consumeTerrainMaterialDirty() = 0;
    };
}
