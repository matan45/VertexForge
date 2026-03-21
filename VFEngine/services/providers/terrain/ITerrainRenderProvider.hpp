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

        /// Returns all tiles currently loaded in memory (not just frustum-visible).
        /// Used for vegetation tree instance building where GPU cull handles visibility.
        virtual std::vector<terrain::TerrainTile*> getAllLoadedTiles() = 0;

        virtual bool hasActiveTerrain() const = 0;

        virtual std::string getTerrainMaterialPath() const = 0;
        virtual void getTerrainGridWorldBounds(glm::vec2& outMin, glm::vec2& outMax) const = 0;

        virtual void setDistanceCullingEnabled(bool enabled) = 0;
        virtual void setMaxDrawDistance(float distance) = 0;

        virtual bool ensureTileLODData(terrain::TerrainTile& tile, uint8_t lodLevel) = 0;
        virtual void releaseTileRAMData(terrain::TerrainTile& tile) = 0;

        virtual void markTerrainMaterialDirty() = 0;
        virtual bool consumeTerrainMaterialDirty() = 0;
    };
}
