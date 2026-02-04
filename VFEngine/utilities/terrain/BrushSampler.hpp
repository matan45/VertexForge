#pragma once

#include "BrushTypes.hpp"
#include "TerrainTypes.hpp"
#include <vector>
#include <glm/glm.hpp>

namespace terrain
{
    class BrushSampler
    {
    public:
        static std::vector<TileCoord> getAffectedTiles(
            const glm::vec2& brushCenter,
            float radius,
            float worldTileSize);

        static float applyFalloff(float t, BrushFalloff falloff);
    };
}
