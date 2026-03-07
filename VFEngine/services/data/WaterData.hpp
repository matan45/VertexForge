#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <string>

namespace services
{
    struct WaterCreationData
    {
        int32_t tilesX = 4;
        int32_t tilesZ = 4;
        float worldTileSize = 32.0f;
        float waterHeight = 0.0f;
        float waveIntensity = 1.0f;
        bool physicsEnabled = true;

        glm::vec4 shallowColor{0.0f, 0.5f, 0.7f, 0.6f};
        glm::vec4 deepColor{0.0f, 0.1f, 0.3f, 0.9f};
    };

    struct WaterData
    {
        float worldTileSize = 0.0f;
        float defaultWaterHeight = 0.0f;
        float defaultWaveIntensity = 1.0f;

        int32_t gridMinX = 0;
        int32_t gridMinZ = 0;
        int32_t gridMaxX = 0;
        int32_t gridMaxZ = 0;

        glm::vec4 shallowColor{0.0f, 0.5f, 0.7f, 0.6f};
        glm::vec4 deepColor{0.0f, 0.1f, 0.3f, 0.9f};

        bool physicsEnabled = true;
        bool isActive = true;
        uint32_t tileCount = 0;
        uint32_t activeTileCount = 0;
        uint32_t visibleTileCount = 0;

        std::string savePath;
    };

    struct WaterTileData
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;
        float waterHeight = 0.0f;
        float waveIntensity = 1.0f;
        bool physicsEnabled = true;
        bool isVisible = false;
    };

    struct WaterGlobalSettingsData
    {
        float density = 1000.0f;
        float drag = 0.5f;
        float buoyancyStrength = 1.0f;

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

    struct OceanFFTConfigData
    {
        uint32_t resolution = 256;
        float patchSize = 100.0f;
        float windSpeed = 8.0f;
        float windDirection = 45.0f;
        float amplitude = 0.00003f;
        float choppiness = 0.8f;
        float gravity = 9.81f;
        float foamThreshold = -0.1f;
        float displacementScale = 1.0f;
        bool enabled = false;
    };
}
