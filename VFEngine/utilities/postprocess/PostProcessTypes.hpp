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
        DepthOfField,
        SSAO,
        EdgeDetection
    };

    enum class VolumetricQuality : uint8_t
    {
        Low = 0,
        Medium,
        High
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

    enum class DoFFocusMode : uint8_t
    {
        Manual = 0,
        TargetPoint
    };

    struct DepthOfFieldSettings
    {
        bool enabled = false;
        DoFFocusMode focusMode = DoFFocusMode::Manual;
        float focalDistance = 10.0f;
        float focusTargetX = 0.0f;
        float focusTargetY = 0.0f;
        float focusTargetZ = 0.0f;
        float focusSmoothing = 5.0f;
        float focalRange = 5.0f;
        float maxBlurRadius = 5.0f;
        int sampleCount = 32;
    };

    struct VolumetricFogSettings
    {
        bool enabled = false;
        VolumetricQuality quality = VolumetricQuality::Medium;
        float uniformDensity = 0.02f;
        float fogColor[3] = {0.8f, 0.85f, 0.9f};
        float heightFogDensity = 0.05f;
        float heightFogFalloff = 0.1f;
        float heightFogOffset = 0.0f;
        float scatteringCoefficient = 0.5f;
        float absorptionCoefficient = 0.1f;
        float anisotropy = 0.7f;
        float temporalBlendFactor = 0.9f;
        float intensity = 1.0f;
        float ambientIntensity = 0.15f;
        float maxDistance = 500.0f;
    };

    struct SSAOSettings
    {
        bool enabled = false;
        float radius = 0.5f;
        float bias = 0.025f;
        float intensity = 1.0f;
        int kernelSize = 32;
        float power = 2.0f;
    };

    struct EdgeDetectionSettings
    {
        bool enabled = false;
        float threshold = 0.1f;
        float edgeWidth = 1.0f;
        float edgeColor[3] = {0.0f, 0.0f, 0.0f};
        float opacity = 1.0f;
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
        DepthOfFieldSettings depthOfField;
        VolumetricFogSettings volumetricFog;
        SSAOSettings ssao;
        EdgeDetectionSettings edgeDetection;

        static PostProcessSettings createDefault()
        {
            return PostProcessSettings{};
        }
    };
}
