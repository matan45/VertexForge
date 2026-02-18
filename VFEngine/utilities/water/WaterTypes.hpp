#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include "../terrain/TerrainTypes.hpp"

namespace water
{
    using terrain::TileCoord;
    using terrain::TileCoordHash;

    struct WaterTileConfig
    {
        float worldTileSize = 32.0f;
        uint32_t subdivisions = 32;
    };

    struct WaterGlobalSettings
    {
        float density = 1000.0f;
        float drag = 0.5f;
        float buoyancyStrength = 2.0f;

        float waveSpeed = 1.0f;
        float waveAmplitude = 0.5f;
        float waveFrequency = 1.0f;

        glm::vec4 shallowColor{0.0f, 0.5f, 0.7f, 0.6f};
        glm::vec4 deepColor{0.0f, 0.1f, 0.3f, 0.9f};
        float maxVisibleDepth = 10.0f;
        float fresnelPower = 5.0f;

        float dudvTiling = 4.0f;
        float dudvStrength = 0.02f;
        float waveDirectionDegrees = 0.0f;
    };
}
