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

        virtual bool hasActiveTerrain() const = 0;

        virtual std::string getTerrainMaterialPath() const = 0;

        virtual bool ensureTileLODData(terrain::TerrainTile& tile, uint8_t lodLevel) = 0;
        virtual void releaseTileRAMData(terrain::TerrainTile& tile) = 0;
    };
}
