#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace components
{
    struct OceanComponent
    {
        // Physics
        float density = 1000.0f;
        float drag = 0.5f;
        float buoyancyStrength = 2.0f;
        bool physicsEnabled = true;

        // Visual
        glm::vec4 shallowColor{0.0f, 0.4f, 0.6f, 0.7f};
        glm::vec4 deepColor{0.0f, 0.05f, 0.2f, 0.95f};
        float maxVisibleDepth = 10.0f;
        float fresnelPower = 5.0f;
        float refractionStrength = 0.5f;
        float refractionChromatic = 0.0f;
        float refractionDepthScale = 0.2f;
        float causticStrength = 1.0f;
        float causticDepthFalloff = 0.5f;

        // Ocean FFT
        uint32_t oceanResolution = 256;
        float oceanPatchSize = 100.0f;
        float oceanWindSpeed = 8.0f;
        float oceanWindDirection = 45.0f;
        float oceanAmplitude = 0.00003f;
        float oceanChoppiness = 1.2f;
        float oceanFoamThreshold = -0.1f;
        float oceanDisplacementScale = 4.0f;

        // Runtime
        float waterHeight = 0.0f;
        bool isActive = true;
    };
}
