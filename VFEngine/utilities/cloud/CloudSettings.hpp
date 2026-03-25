#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace render::cloud
{
    struct CloudSettings
    {
        bool enabled = false;

        // Cloud layer geometry (meters above ground)
        float cloudMinAltitude = 1500.0f;
        float cloudMaxAltitude = 4000.0f;

        // Density & coverage
        float globalDensity = 0.8f;          // [0, 1]
        float globalCoverage = 0.6f;         // [0, 1]
        float cloudType = 0.5f;              // [0, 1] stratus(0) to cumulus(1)

        // Noise shaping
        float shapeScale = 0.0003f;
        float detailScale = 0.003f;
        float erosionStrength = 0.3f;
        float curlStrength = 0.2f;

        // Wind
        float windSpeed = 25.0f;             // m/s
        float windDirectionDeg = 45.0f;

        // Color tint (multiplied with sun lighting)
        glm::vec3 cloudColorTint{1.0f, 1.0f, 1.0f};

        // Lighting
        float lightAbsorption = 0.75f;
        float phaseForward = 0.8f;           // HG forward lobe
        float phaseBackward = -0.3f;         // HG backward lobe
        float phaseBlend = 0.5f;
        float ambientIntensity = 0.8f;

        // Silver lining (bright rim when looking toward sun through thin cloud edges)
        float silverLiningIntensity = 0.5f;  // [0, 2]
        float silverLiningSpread = 5.0f;     // [1, 20] exponent for angular falloff

        // Multi-scattering interior boost
        float multiScatterBoost = 0.5f;      // [0, 2]

        // Performance
        float temporalBlendFactor = 0.8f;
        uint32_t maxMarchSteps = 96;
        uint32_t lightMarchSteps = 6;
    };
}
