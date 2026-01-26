#pragma once
#include <cstdint>

namespace types
{
    enum class ShadowQuality : uint8_t
    {
        Off = 0,
        Low,      // 512px
        Medium,   // 1024px
        High,     // 2048px
        Ultra     // 4096px
    };

    enum class CascadeSplitMode : uint8_t
    {
        Linear = 0,
        Logarithmic,
        Practical
    };

    /**
     * PCF kernel size for shadow filtering.
     * Controls shadow softness vs performance tradeoff.
     */
    enum class PCFKernelSize : uint8_t
    {
        None = 0,    // 1x1 - Hard shadows (no filtering)
        Small = 1,   // 3x3 - 9 samples
        Medium = 2,  // 5x5 - 25 samples
        Large = 3    // 7x7 - 49 samples
    };

    /**
     * Shadow atlas configuration.
     * Controls atlas size and per-light-type resolutions.
     */
    struct ShadowAtlasConfig
    {
        uint32_t atlasSize = 4096;              // Total atlas size (width and height)
        uint32_t directionalResolution = 2048;  // Per cascade resolution
        uint32_t spotResolution = 1024;         // Per spot light resolution
        uint32_t pointResolution = 512;         // Per cube face resolution
        bool dynamicReallocation = true;        // Allow runtime resize

        /**
         * Get atlas configuration based on shadow quality level.
         */
        static ShadowAtlasConfig fromQuality(ShadowQuality quality)
        {
            ShadowAtlasConfig config;
            switch (quality)
            {
                case ShadowQuality::Off:
                    config.atlasSize = 0;
                    config.directionalResolution = 0;
                    config.spotResolution = 0;
                    config.pointResolution = 0;
                    break;
                case ShadowQuality::Low:
                    config.atlasSize = 2048;
                    config.directionalResolution = 512;
                    config.spotResolution = 256;
                    config.pointResolution = 256;
                    break;
                case ShadowQuality::Medium:
                    config.atlasSize = 4096;
                    config.directionalResolution = 1024;
                    config.spotResolution = 512;
                    config.pointResolution = 512;
                    break;
                case ShadowQuality::High:
                    config.atlasSize = 4096;
                    config.directionalResolution = 2048;
                    config.spotResolution = 1024;
                    config.pointResolution = 512;
                    break;
                case ShadowQuality::Ultra:
                    config.atlasSize = 8192;
                    config.directionalResolution = 4096;
                    config.spotResolution = 2048;
                    config.pointResolution = 1024;
                    break;
            }
            return config;
        }
    };

    struct ShadowSettings
    {
        bool enabled = true;
        ShadowQuality quality = ShadowQuality::High;

        // Directional Light (CSM) settings
        uint8_t cascadeCount = 4;
        CascadeSplitMode cascadeSplitMode = CascadeSplitMode::Practical;

        // Global bias settings
        float shadowBias = 0.005f;
        float normalBias = 0.02f;

        // PCF filtering settings
        PCFKernelSize pcfKernelSize = PCFKernelSize::Small;  // Default 3x3
        bool softShadowsEnabled = true;                       // Global soft shadow toggle

        // Shadow intensity: controls how much ambient light is reduced in shadowed areas
        // 0.0 = no ambient occlusion in shadows (lighter shadows)
        // 1.0 = full ambient occlusion in shadows (darker shadows)
        float shadowIntensity = 0.5f;

        // Atlas configuration
        ShadowAtlasConfig atlas;
    };

    struct RenderSettings
    {
        ShadowSettings shadows;

        static RenderSettings createDefault()
        {
            RenderSettings settings;
            settings.shadows = ShadowSettings{};
            return settings;
        }
    };
}
