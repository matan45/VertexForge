#pragma once

#include "types/RenderSettings.hpp"

#include <type_traits>

// VK-1655: the render-settings sections the MCP tools own that cover only PART of a stored struct.
// writeSections copies exactly these fields into the scene's types::RenderSettings, so undoing a
// render_settings_set never touches the fields the tools do not own (the shadow clipmap and VSM
// resolutions, the present mode, the other post-process effects, ...). Defaults are the stored
// structs' own defaults.
namespace mcp::tools::env
{
    using PostProcessSettings = ::postprocess::PostProcessSettings;

    // The sections stored as one whole RenderSettings::postProcess member (VK-1654's fog included).
    template <typename S>
    inline constexpr bool isPostProcessMember =
        std::is_same_v<S, ::postprocess::VolumetricFogSettings> || std::is_same_v<S, ::postprocess::ToneMappingSettings> ||
        std::is_same_v<S, ::postprocess::AutoExposureSettings> || std::is_same_v<S, ::postprocess::BloomSettings> ||
        std::is_same_v<S, ::postprocess::ColorGradingSettings> || std::is_same_v<S, ::postprocess::SSAOSettings> ||
        std::is_same_v<S, ::postprocess::VignetteSettings> ||
        std::is_same_v<S, ::postprocess::ChromaticAberrationSettings> ||
        std::is_same_v<S, ::postprocess::FilmGrainSettings> || std::is_same_v<S, ::postprocess::DepthOfFieldSettings> ||
        std::is_same_v<S, ::postprocess::EdgeDetectionSettings> || std::is_same_v<S, ::postprocess::UpscaleSettings>;

    // The RenderSettings::postProcess member such a section is stored in.
    template <typename S>
    constexpr S PostProcessSettings::*postProcessMember()
    {
        static_assert(isPostProcessMember<S>, "not a post-process member section");
        if constexpr (std::is_same_v<S, ::postprocess::VolumetricFogSettings>)
        {
            return &PostProcessSettings::volumetricFog;
        }
        else if constexpr (std::is_same_v<S, ::postprocess::ToneMappingSettings>)
        {
            return &PostProcessSettings::toneMapping;
        }
        else if constexpr (std::is_same_v<S, ::postprocess::AutoExposureSettings>)
        {
            return &PostProcessSettings::autoExposure;
        }
        else if constexpr (std::is_same_v<S, ::postprocess::BloomSettings>)
        {
            return &PostProcessSettings::bloom;
        }
        else if constexpr (std::is_same_v<S, ::postprocess::ColorGradingSettings>)
        {
            return &PostProcessSettings::colorGrading;
        }
        else if constexpr (std::is_same_v<S, ::postprocess::SSAOSettings>)
        {
            return &PostProcessSettings::ssao;
        }
        else if constexpr (std::is_same_v<S, ::postprocess::VignetteSettings>)
        {
            return &PostProcessSettings::vignette;
        }
        else if constexpr (std::is_same_v<S, ::postprocess::ChromaticAberrationSettings>)
        {
            return &PostProcessSettings::chromaticAberration;
        }
        else if constexpr (std::is_same_v<S, ::postprocess::FilmGrainSettings>)
        {
            return &PostProcessSettings::filmGrain;
        }
        else if constexpr (std::is_same_v<S, ::postprocess::DepthOfFieldSettings>)
        {
            return &PostProcessSettings::depthOfField;
        }
        else if constexpr (std::is_same_v<S, ::postprocess::EdgeDetectionSettings>)
        {
            return &PostProcessSettings::edgeDetection;
        }
        else
        {
            return &PostProcessSettings::upscale;
        }
    }

    // RenderSettings::postProcess.enabled: the master switch of every built-in post-process effect.
    struct PostProcessMasterSettings
    {
        bool enabled = ::postprocess::PostProcessSettings{}.enabled;
    };

    // The look-related fields of RenderSettings::shadows.
    struct ShadowQualitySettings
    {
        bool enabled = types::ShadowSettings{}.enabled;
        types::ShadowQuality quality = types::ShadowSettings{}.quality;
        float shadowBias = types::ShadowSettings{}.shadowBias;
        float slopeBias = types::ShadowSettings{}.slopeBias;
        float normalBias = types::ShadowSettings{}.normalBias;
        bool softShadows = types::ShadowSettings{}.softShadows;
        float shadowIntensity = types::ShadowSettings{}.shadowIntensity;
    };

    // RenderSettings::display.msaa (the present mode is not a look setting and stays untouched).
    struct DisplayAntiAliasingSettings
    {
        types::MsaaSamples msaa = types::DisplaySettings{}.msaa;
    };

    inline ShadowQualitySettings shadowQualityOf(const types::ShadowSettings& shadows)
    {
        ShadowQualitySettings value;
        value.enabled = shadows.enabled;
        value.quality = shadows.quality;
        value.shadowBias = shadows.shadowBias;
        value.slopeBias = shadows.slopeBias;
        value.normalBias = shadows.normalBias;
        value.softShadows = shadows.softShadows;
        value.shadowIntensity = shadows.shadowIntensity;
        return value;
    }

    inline void storeShadowQuality(types::ShadowSettings& shadows, const ShadowQualitySettings& value)
    {
        shadows.enabled = value.enabled;
        shadows.quality = value.quality;
        shadows.shadowBias = value.shadowBias;
        shadows.slopeBias = value.slopeBias;
        shadows.normalBias = value.normalBias;
        shadows.softShadows = value.softShadows;
        shadows.shadowIntensity = value.shadowIntensity;
    }
}
