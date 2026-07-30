#pragma once

#include "terrain/TerrainSerializer.hpp"
#include "terrain/TerrainSurfaceMaskAsset.hpp"
#include "foliage/FoliageTypes.hpp"
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
    struct TileLoadContextResult
    {
        std::string filePath;
        terrain::TileIndexEntry indexEntry{};
        bool hasMeshletCache = false;
        bool valid = false;
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

        /// Returns all tiles currently loaded in memory (not just frustum-visible).
        /// Used for vegetation tree instance building where GPU cull handles visibility.
        virtual std::vector<terrain::TerrainTile*> getAllLoadedTiles() = 0;

        virtual bool hasActiveTerrain() const = 0;

        virtual std::string getTerrainMaterialPath() const = 0;
        virtual void getTerrainGridWorldBounds(glm::vec2& outMin, glm::vec2& outMax) const = 0;

        /// VK-1573: per-scene foliage type palette (typeIndex -> mesh/material/cull distance),
        /// consumed by the graphics-side foliage instanced collector.
        virtual const std::vector<foliage::FoliageType>& getFoliagePalette() const = 0;

        virtual void setDistanceCullingEnabled(bool enabled) = 0;
        virtual void setMaxDrawDistance(float distance) = 0;

        virtual bool ensureTileLODData(terrain::TerrainTile& tile, uint8_t lodLevel) = 0;
        virtual void releaseTileRAMData(terrain::TerrainTile& tile) = 0;

        virtual TileLoadContextResult prepareTileLoadContext(int32_t coordX, int32_t coordZ) = 0;

        virtual void markTerrainMaterialDirty() = 0;
        virtual bool consumeTerrainMaterialDirty() = 0;

        /// VK-1614 world-anchored wetness/snow mask. Two separate dirty signals because the two
        /// transitions cost wildly different amounts:
        ///   ASSIGN  - a mask was created, loaded, resized or cleared. Needs image (re)creation, a
        ///             descriptor rewrite and a pipeline recreate for the TERRAIN_WEATHER_MASK macro.
        ///             Rare (asset assign, scene load).
        ///   PIXELS  - the same image's contents changed. One buffer-to-image copy, nothing else.
        ///             This is the paint-stroke path and runs at interactive rates, so it must not
        ///             drag the assign work behind it.
        virtual bool consumeSurfaceMaskAssignDirty() = 0;
        virtual bool consumeSurfaceMaskPixelsDirty() = 0;
        /// Null when no mask is assigned. Borrowed for the duration of the call — the service owns
        /// the paintable master copy and keeps mutating it.
        virtual const terrain::TerrainSurfaceMaskData* getSurfaceMask() const = 0;
        /// (minX, minZ, maxX, maxZ) in world space. AUTHORED and snapshotted at mask creation, never
        /// derived from live grid bounds — see components::TerrainComponent for why that matters.
        virtual glm::vec4 getSurfaceMaskWorldRect() const = 0;
    };
}
