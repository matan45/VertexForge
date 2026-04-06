#pragma once

#include "TerrainTypes.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>

namespace terrain
{
    struct SplineControlPoint
    {
        glm::vec3 position{0.0f}; // world XYZ (Y = height)
    };

    struct SplineParams
    {
        float corridorWidth = 5.0f;    // flat area half-width
        float falloffWidth = 3.0f;     // smooth blend distance outside corridor
        float embankmentHeight = 0.0f; // raise above spline height
    };

    struct SplineData
    {
        uint64_t id = 0;
        std::vector<SplineControlPoint> controlPoints;
        SplineParams params;
        // Non-destructive: original heights per affected tile
        std::unordered_map<TileCoord, std::vector<float>, TileCoordHash> originalHeights;
    };
}
