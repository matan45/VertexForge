#pragma once
#include <cstdint>
#include <string>

namespace postprocess
{
    enum class EffectType : uint8_t
    {
        ToneMapping = 0,
        Bloom,
        Vignette,
        ChromaticAberration,
        FilmGrain,
        DepthOfField,
        SSAO,
        EdgeDetection,
        AutoExposure,
        ColorGrading,
        Underwater,
        RainDroplets
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

    struct ToneMappingSettings
    {
        bool enabled = false;
        ToneMappingMode mode = ToneMappingMode::ACES;
        float exposure = 1.0f;
        float gamma = 2.2f;
        float contrast = 1.0f;
        float toe = 0.0f;
        float shoulder = 0.0f;
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

        // GI injection
        float giInjectionIntensity = 1.0f;

        // Ray march early termination threshold (0.01 = skip when 99% opaque)
        float earlyTerminationThreshold = 0.01f;

        // Noise/turbulence modulation
        bool noiseEnabled = false;
        float noiseScale = 0.01f;
        float noiseIntensity = 0.5f;
        float noiseSpeed = 0.05f;
        int noiseOctaves = 3;
    };

    enum class SSAOQuality : uint8_t
    {
        Low = 0,    // 16 samples
        Medium,     // 32 samples
        High,       // 48 samples
        Ultra       // 64 samples
    };

    inline int ssaoSamplesFromQuality(SSAOQuality quality)
    {
        switch (quality)
        {
        case SSAOQuality::Low:    return 16;
        case SSAOQuality::Medium: return 32;
        case SSAOQuality::High:   return 48;
        case SSAOQuality::Ultra:  return 64;
        default:                  return 32;
        }
    }

    struct SSAOSettings
    {
        bool enabled = false;
        SSAOQuality quality = SSAOQuality::Medium;
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

    struct AutoExposureSettings
    {
        bool enabled = false;
        float minExposure = 0.1f;
        float maxExposure = 10.0f;
        float adaptSpeedUp = 3.0f;
        float adaptSpeedDown = 1.0f;
        float exposureCompensation = 0.0f;
        float lowPercentile = 0.1f;
        float highPercentile = 0.9f;
    };

    struct ColorGradingSettings
    {
        bool enabled = false;
        std::string primaryLutPath;
        std::string secondaryLutPath;
        float lutIntensity = 1.0f;
        float lutBlendFactor = 0.0f;
        float liftR = 0.0f, liftG = 0.0f, liftB = 0.0f;
        float gammaR = 1.0f, gammaG = 1.0f, gammaB = 1.0f;
        float gainR = 1.0f, gainG = 1.0f, gainB = 1.0f;
        float saturation = 1.0f;
        float colorTemperature = 6500.0f;
        float colorTint = 0.0f;
    };

    struct UnderwaterSettings
    {
        bool enabled = false;
        float fogDensity = 0.15f;
        float fogColor[3] = {0.0f, 0.15f, 0.3f};
        float absorptionR = 0.45f;
        float absorptionG = 0.08f;
        float absorptionB = 0.02f;
        float causticStrength = 0.5f;
        float causticScale = 50.0f;
        float causticSpeed = 0.3f;
        float meniscusWidth = 0.02f;
        float meniscusDistortion = 0.03f;
        float chromaticStrength = 0.003f;
        float maxFogDistance = 100.0f;
    };

    struct RainDropletsSettings
    {
        bool enabled = false;
        float intensity = 0.0f;      // driven by precipIntensity [0,1]
        float dropletScale = 1.0f;
        float trailSpeed = 2.0f;
    };

    enum class UpscaleMode : uint8_t
    {
        Off = 0,
        DLSS,
        FSR2,
        Auto  // DLSS if available, else FSR2
    };

    enum class UpscaleQuality : uint8_t
    {
        Native = 0,       // 1.0x (no upscaling, only AA)
        Quality,           // 1.5x
        Balanced,          // 1.7x
        Performance,       // 2.0x
        UltraPerformance   // 3.0x
    };

    struct UpscaleSettings
    {
        bool enabled = false;
        UpscaleMode mode = UpscaleMode::DLSS;
        UpscaleQuality quality = UpscaleQuality::Quality;
        bool debugMotionVectors = false;
        bool debugJitter = false;

        static float getScaleFactor(UpscaleQuality q)
        {
            switch (q)
            {
            case UpscaleQuality::Native:           return 1.0f;
            case UpscaleQuality::Quality:          return 1.5f;
            case UpscaleQuality::Balanced:         return 1.7f;
            case UpscaleQuality::Performance:      return 2.0f;
            case UpscaleQuality::UltraPerformance: return 3.0f;
            default: return 1.0f;
            }
        }
    };

    struct PostProcessSettings
    {
        bool enabled = false;

        ToneMappingSettings toneMapping;
        UpscaleSettings upscale;
        BloomSettings bloom;
        VignetteSettings vignette;
        ChromaticAberrationSettings chromaticAberration;
        FilmGrainSettings filmGrain;
        DepthOfFieldSettings depthOfField;
        VolumetricFogSettings volumetricFog;
        SSAOSettings ssao;
        EdgeDetectionSettings edgeDetection;
        AutoExposureSettings autoExposure;
        ColorGradingSettings colorGrading;
        UnderwaterSettings underwater;
        RainDropletsSettings rainDroplets;

        static PostProcessSettings createDefault()
        {
            return PostProcessSettings{};
        }
    };
}
