#pragma once
#include <cstdint>

namespace postprocess
{
    enum class EffectType : uint8_t
    {
        ToneMapping = 0,
        FXAA,
        Bloom,
        Vignette,
        ChromaticAberration,
        FilmGrain,
        GodRays,
        DepthOfField
    };

    enum class ToneMappingMode : uint8_t
    {
        ACES = 0,
        Reinhard,
        Uncharted2,
        Linear,
        GranTurismo,
        AgX,
        KhronosPBRNeutral
    };

    enum class FXAAQuality : uint8_t
    {
        Low = 0,
        Medium,
        High
    };

    struct ToneMappingSettings
    {
        bool enabled = true;
        ToneMappingMode mode = ToneMappingMode::ACES;
        float exposure = 1.0f;
        float gamma = 2.2f;
        float contrast = 1.0f;
    };

    struct FXAASettings
    {
        bool enabled = false;
        FXAAQuality quality = FXAAQuality::Medium;
        float edgeThresholdMin = 0.0312f;
        float edgeThreshold = 0.125f;
    };

    struct BloomSettings
    {
        bool enabled = false;
        float threshold = 0.7f;
        float intensity = 0.5f;
        float radius = 0.5f;
        uint32_t passes = 5;
    };

    struct VignetteSettings
    {
        bool enabled = false;
        float intensity = 0.3f;
        float radius = 0.8f;
        float softness = 0.5f;
    };

    struct ChromaticAberrationSettings
    {
        bool enabled = false;
        float intensity = 0.005f;
    };

    struct FilmGrainSettings
    {
        bool enabled = false;
        float intensity = 0.1f;
        float size = 1.6f;
    };

    struct GodRaysSettings
    {
        bool enabled = false;
        float intensity = 0.8f;
        float decay = 0.98f;
        float density = 1.0f;
        float weight = 1.0f;
        int sampleCount = 64;
        float threshold = 0.5f;
    };

    struct DepthOfFieldSettings
    {
        bool enabled = false;
        float focalDistance = 10.0f;
        float focalRange = 5.0f;
        float maxBlurRadius = 5.0f;
        int sampleCount = 32;
    };

    struct PostProcessSettings
    {
        bool enabled = true;

        ToneMappingSettings toneMapping;
        FXAASettings fxaa;
        BloomSettings bloom;
        VignetteSettings vignette;
        ChromaticAberrationSettings chromaticAberration;
        FilmGrainSettings filmGrain;
        GodRaysSettings godRays;
        DepthOfFieldSettings depthOfField;

        static PostProcessSettings createDefault()
        {
            return PostProcessSettings{};
        }
    };
}
