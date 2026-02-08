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
    // Bridges TerrainService (which owns terrain data) to the graphics layer
    class ITerrainRenderProvider
    {
    public:
        virtual ~ITerrainRenderProvider() = default;

        virtual std::vector<terrain::TerrainTile*> getVisibleTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition) = 0;

        virtual bool hasActiveTerrain() const = 0;

        virtual std::string getTerrainMaterialPath() const = 0;
    };
}
