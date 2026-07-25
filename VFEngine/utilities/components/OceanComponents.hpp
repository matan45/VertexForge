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

        // VK-1605: shoaling. Each band's amplitude follows Green's law once it feels the bottom,
        // capped by the depth-limited breaking height so it collapses at the waterline instead of
        // growing through the beach. Both this and the breakers below need the camera-following
        // shore-depth field (waterHeight - terrainHeight); with no terrain the field reads "no
        // bottom" everywhere and every factor is exactly 1, leaving deep-ocean scenes untouched.
        bool shoalingEnabled = false;
        float shoalingStrength = 1.0f;      // 0..1 blend toward the full effect
        float shoalingMinDepth = 0.0f;      // depth at which a band is already fully flattened
        float shoalingWavelengthScale = 1.0f; // scales each band's Pierson-Moskowitz peak wavelength
        float shoalingGamma = 0.78f;        // McCowan depth-limited breaking ratio H/d
        float shoreEdgeFadeStart = 0.88f;   // 0..1, where the shore-field window starts fading out

        // VK-1605: traveling breakers. Phase is measured in water DEPTH, so crests follow iso-depth
        // contours - they arrive parallel to the shore and wrap headlands with no bathymetry solve.
        bool shoreWavesEnabled = false;
        float shoreWaveAmplitude = 0.4f;    // metres; 0 disables
        float shoreWaveLength = 12.0f;      // metres of depth between successive crests
        float shoreWaveSpeed = 0.35f;       // crests per second, travelling shoreward
        float shoreWaveBreakDepth = 1.5f;   // offshore edge of the surf zone
        float shoreWaveBreakRange = 1.0f;   // envelope fade width at both ends
        float shoreWaveCrestFoam = 0.6f;
        float shoreWaveCrestFoamThreshold = 0.55f;
        float shoreWaveLean = 0.5f;         // forward lean along the shore-depth gradient

        // VK-1606: interactive ripple patch (wakes, splashes, scripted impulses). Off by default so
        // an existing scene renders exactly as it did before.
        bool rippleSimEnabled = false;
        float ripplePatchSize = 100.0f;     // metres covered by the 512^2 camera-following patch
        float rippleWaveSpeed = 3.0f;       // m/s, clamped to the CFL bound before reaching the GPU
        float rippleDamping = 0.8f;         // per-second velocity decay
        float rippleHeightScale = 1.0f;
        float rippleNormalScale = 1.0f;
        float rippleFoamGain = 0.1f;        // foam per unit of surface curvature
        float rippleFoamScale = 1.0f;
        float rippleFoamDecay = 1.5f;       // per-second foam decay
        float rippleEdgeFadeStart = 0.85f;  // patch-border fade start, 0..1

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

    // VK-1607: bounded water at its own level - a lake, a harbour pool, a flooded basement. Additive
    // to the ocean rather than a replacement for it: the ocean stays the scene singleton and any
    // number of bodies coexist with it, each suppressing the ocean inside its own footprint.
    enum class WaterBodyType : uint8_t
    {
        Lake = 0,
        Pool = 1
    };

    struct WaterBodyComponent
    {
        WaterBodyType type = WaterBodyType::Lake;

        // Surface Y = TransformComponent.position.y + waterHeight. An offset rather than an absolute
        // level so the move gizmo's Y axis still does something, while a precise numeric nudge stays
        // available.
        float waterHeight = 0.0f;

        // Half size of the axis-aligned XZ box, in WORLD METRES around the entity's transform
        // position. Entity scale is deliberately not applied - these numbers mean what they say.
        glm::vec2 halfExtents{10.0f, 10.0f};

        // Metres of water below the surface. The body claims points from surfaceHeight - depth up
        // to (and above) the surface, and nothing below that: without it a body is an infinite
        // column and a rooftop pool submerges the room underneath it.
        float depth = 10.0f;

        // Bits 0..2 = swell / agitation / ripples, ANDed with the ocean's own band mask. 0 (the
        // default) is a mirror-flat surface, which is what a pool should be; the VK-1606 ripple
        // patch still applies on top because it is a world-space overlay, not a band.
        uint32_t bandMask = 0u;

        bool physicsEnabled = true;
        bool isActive = true;
    };
}
