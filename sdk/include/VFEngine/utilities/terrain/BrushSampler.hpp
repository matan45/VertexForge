#pragma once
#include "TerrainExport.hpp"

#include "TerrainTypes.hpp"
#include <vector>
#include <glm/glm.hpp>

namespace terrain
{
    class VF_TERRAIN_API BrushSampler
    {
    public:
        static std::vector<TileCoord> getAffectedTiles(
            const glm::vec2& brushCenter,
            float radius,
            float worldTileSize);

        static std::vector<TileCoord> getAffectedTilesForSegment(
            const glm::vec2& start,
            const glm::vec2& end,
            float halfWidth,
            float worldTileSize);
    };
}
