#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <string>

namespace components
{
    struct WaterComponent
    {
        float globalDensity = 1000.0f;
        float globalDrag = 0.5f;
        float globalBuoyancyStrength = 2.0f;

        float defaultWaterHeight = 0.0f;
        float defaultWaveIntensity = 1.0f;

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

        float worldTileSize = 32.0f;

        int32_t gridMinX = 0;
        int32_t gridMinZ = 0;
        int32_t gridMaxX = 0;
        int32_t gridMaxZ = 0;

        bool physicsEnabled = true;
        bool isActive = true;

        uint32_t activeTileCount = 0;
        uint32_t visibleTileCount = 0;

        std::string savePath;

        // Ocean FFT settings
        bool oceanFFTEnabled = false;
        uint32_t oceanResolution = 256;
        float oceanPatchSize = 100.0f;
        float oceanWindSpeed = 20.0f;
        float oceanWindDirection = 45.0f;
        float oceanAmplitude = 0.0003f;
        float oceanChoppiness = 1.5f;
        float oceanGravity = 9.81f;
        float oceanFoamThreshold = 0.3f;
        float oceanDisplacementScale = 1.0f;
    };

    struct WaterTileComponent
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;

        float waterHeight = 0.0f;
        float waveIntensity = 1.0f;

        bool physicsEnabled = true;
        bool isVisible = true;
    };
}
