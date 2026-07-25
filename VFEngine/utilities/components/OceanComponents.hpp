#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace components
{
    // Must stay in sync with services::MAX_OCEAN_BANDS in OceanData.hpp
    static constexpr uint32_t MAX_OCEAN_BANDS = 3;

    struct OceanBandData
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
        // Persistent foam: how much advected previous-frame foam survives (0 = instantaneous
        // Jacobian foam only) and its exponential decay rate per second
        float foamPersistence = 0.85f;
        float foamDecay = 0.5f;
    };

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
        float shoreFoamRange = 3.0f;
        float shoreFoamIntensity = 0.8f;
        float shoreBreakingStrength = 0.8f;
        float shoreWetRange = 5.0f;
        float shoreWetDarkening = 0.3f;
        float shoreWetRoughness = 0.15f;

        // VK-1604: screen-space reflections. In-fragment march in water.glsl against the scene
        // depth + pre-water color copy (set 9) — the generic SSR chain cannot see water, which
        // draws after the depth prepass with depth writes off. IBL fills misses and screen edges.
        bool ssrEnabled = false;
        float ssrIntensity = 1.0f;
        float ssrMaxDistance = 60.0f;       // metres
        float ssrThickness = 0.35f;         // metres; range-scaled in the shader
        uint32_t ssrMaxSteps = 24;
        bool ssrDebugView = false;          // renders SSR confidence as greyscale

        // VK-1604: Beer-Lambert absorption / in-scattering. OFF keeps the legacy height-based
        // deep/shallow tint byte-for-byte, so existing content is unaffected until opted in.
        bool beerLambertEnabled = false;
        glm::vec3 absorptionCoeff{0.45f, 0.08f, 0.02f};   // per-channel extinction, 1/m
        glm::vec3 scatteringColor{0.0f, 0.35f, 0.30f};
        float scatterCoeff = 0.05f;         // 1/m
        float absorptionMaxDistance = 30.0f;// metres; clamps the path length

        // VK-1604: hex tile-and-blend anti-tiling. hexBandMask picks which FFT bands pay the
        // 3x sample cost; the CPU buoyancy height path honours the same mask, so the rendered
        // surface and physics agree on every band.
        bool hexTilingEnabled = false;
        uint32_t hexBandMask = 0x6;         // bands 1 and 2 by default
        float hexCellScale = 1.0f;          // hex cells per band patch
        float hexBlendContrast = 4.0f;      // weight sharpening exponent

        // Ocean FFT bands
        OceanBandData oceanBands[MAX_OCEAN_BANDS] = {
            {256, 500.0f, 12.0f, 45.0f, 0.00005f, 1.5f, -0.1f, 4.0f, true, 0.85f, 0.5f},   // Swell
            {128, 100.0f,  8.0f, 60.0f, 0.00003f, 1.2f, -0.1f, 4.0f, true, 0.85f, 0.5f},   // Agitation
            {128,  20.0f,  4.0f, 30.0f, 0.00001f, 0.8f, -0.1f, 4.0f, true, 0.0f,  0.5f},   // Ripples
        };
        float oceanGravity = 9.81f;

        // Weather-driven sea state: when enabled the ocean service maps the live
        // WeatherState (wind/gusts) onto band wind/amplitude/choppiness each frame.
        // Manual band authoring above stays untouched while this is off.
        bool weatherDriven = false;
        float weatherResponse = 1.0f;
        float currentBeaufort = 3.0f;

        // Runtime
        float waterHeight = 0.0f;
        bool isActive = true;
    };
}
