#pragma once

#include <array>
#include <cstdint>

namespace terrain
{
    // Kept independent of TerrainWeightMap.hpp so Services/SDK consumers do not
    // acquire a Terrain DLL import dependency merely by including the result type.
    inline constexpr uint8_t TERRAIN_LAYER_WEIGHT_SLOTS = 8;
    inline constexpr float TERRAIN_LAYER_WEIGHT_EPSILON = 0.001f;

    struct TerrainLayerWeight
    {
        uint8_t layer = 0;
        float weight = 0.0f;
    };

    struct TerrainLayerWeightsAtResult
    {
        std::array<TerrainLayerWeight, TERRAIN_LAYER_WEIGHT_SLOTS> layers{};
        uint8_t count = 0;
        bool valid = false;
    };
}
