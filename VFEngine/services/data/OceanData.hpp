#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace services
{
    // Must stay in sync with components::MAX_OCEAN_BANDS in OceanComponents.hpp
    static constexpr uint32_t MAX_OCEAN_BANDS = 3;

    struct OceanBandConfigData
    {
        uint32_t resolution = 256;
        float patchSize = 100.0f;
        float windSpeed = 8.0f;
        float windDirection = 45.0f;
        float amplitude = 0.00003f;
        float choppiness = 1.2f;
        float foamThreshold = -0.1f;
        float displacementScale = 4.0f;
        bool enabled = true;
        float foamPersistence = 0.85f;
        float foamDecay = 0.5f;
    };

    struct OceanFFTConfigData
    {
        OceanBandConfigData bands[MAX_OCEAN_BANDS] = {
            {128, 100.0f, 12.0f, 45.0f, 0.00005f, 1.5f, -0.1f, 4.0f, true, 0.85f, 0.5f},   // Swell
            {128, 100.0f,  8.0f, 60.0f, 0.00003f, 1.2f, -0.1f, 4.0f, true, 0.85f, 0.5f},   // Agitation
            {128,  20.0f,  4.0f, 30.0f, 0.00001f, 0.8f, -0.1f, 4.0f, true, 0.0f,  0.5f},   // Ripples
        };
        float gravity = 9.81f;
        bool enabled = false;
    };

    struct OceanVisualSettings
    {
        glm::vec4 shallowColor{0.0f, 0.4f, 0.6f, 0.7f};
        glm::vec4 deepColor{0.0f, 0.05f, 0.2f, 0.95f};
        float maxVisibleDepth = 10.0f;
        float fresnelPower = 5.0f;
        float refractionStrength = 0.5f;
        float refractionChromatic = 0.0f;
        float refractionDepthScale = 0.2f;
        float causticStrength = 1.0f;
        float causticDepthFalloff = 0.5f;
        float shoreFoamRange = 0.0f;
        float shoreFoamIntensity = 0.8f;
        float shoreBreakingStrength = 0.8f;
        float shoreWetRange = 5.0f;
        float shoreWetDarkening = 0.3f;
        float shoreWetRoughness = 0.15f;

        // VK-1604 — mirrors components::OceanComponent; see that header for the rationale.
        bool ssrEnabled = false;
        float ssrIntensity = 1.0f;
        float ssrMaxDistance = 60.0f;
        float ssrThickness = 0.35f;
        uint32_t ssrMaxSteps = 24;

        bool beerLambertEnabled = false;
        glm::vec3 absorptionCoeff{0.45f, 0.08f, 0.02f};
        glm::vec3 scatteringColor{0.0f, 0.35f, 0.30f};
        float scatterCoeff = 0.05f;
        float absorptionMaxDistance = 30.0f;

        bool hexTilingEnabled = false;
        uint32_t hexBandMask = 0x6;
        float hexCellScale = 1.0f;
        float hexBlendContrast = 4.0f;
    };

    struct OceanPhysicsSettings
    {
        float density = 1000.0f;
        float drag = 0.5f;
        float buoyancyStrength = 2.0f;
        bool physicsEnabled = true;
    };

    struct OceanCreationData
    {
        float waterHeight = 0.0f;
        bool physicsEnabled = true;

        glm::vec4 shallowColor{0.0f, 0.4f, 0.6f, 0.7f};
        glm::vec4 deepColor{0.0f, 0.05f, 0.2f, 0.95f};

        OceanFFTConfigData oceanConfig;
    };

    struct OceanData
    {
        float waterHeight = 0.0f;
        bool physicsEnabled = true;
        bool isActive = true;

        glm::vec4 shallowColor{0.0f, 0.4f, 0.6f, 0.7f};
        glm::vec4 deepColor{0.0f, 0.05f, 0.2f, 0.95f};
        float maxVisibleDepth = 10.0f;
        float fresnelPower = 5.0f;
        float refractionStrength = 0.5f;
        float refractionChromatic = 0.0f;
        float refractionDepthScale = 0.2f;
        float causticStrength = 1.0f;
        float causticDepthFalloff = 0.5f;
        float shoreFoamRange = 0.0f;
        float shoreFoamIntensity = 0.8f;
        float shoreBreakingStrength = 0.8f;
        float shoreWetRange = 5.0f;
        float shoreWetDarkening = 0.3f;
        float shoreWetRoughness = 0.15f;

        // VK-1604 — mirrors components::OceanComponent; see that header for the rationale.
        bool ssrEnabled = false;
        float ssrIntensity = 1.0f;
        float ssrMaxDistance = 60.0f;
        float ssrThickness = 0.35f;
        uint32_t ssrMaxSteps = 24;

        bool beerLambertEnabled = false;
        glm::vec3 absorptionCoeff{0.45f, 0.08f, 0.02f};
        glm::vec3 scatteringColor{0.0f, 0.35f, 0.30f};
        float scatterCoeff = 0.05f;
        float absorptionMaxDistance = 30.0f;

        bool hexTilingEnabled = false;
        uint32_t hexBandMask = 0x6;
        float hexCellScale = 1.0f;
        float hexBlendContrast = 4.0f;

        // Weather-driven sea state
        bool weatherDriven = false;
        float weatherResponse = 1.0f;
        float currentBeaufort = 3.0f;

        OceanFFTConfigData oceanConfig;
    };
}
