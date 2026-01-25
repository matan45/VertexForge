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
