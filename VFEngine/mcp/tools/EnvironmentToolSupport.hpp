#pragma once

#include "RenderSettingsSections.hpp"
#include "SettingsTable.hpp"

#include "data/DTOs.hpp"
#include "data/EntityHandle.hpp"
#include "types/RenderSettings.hpp"
#include "weather/WeatherTypes.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

// VK-1654 helpers shared by the environment tools (EnvironmentTools.cpp) and their undo commands
// (mcp/undo/EnvironmentUndo). Every environment setting exists twice: the scene's AUTHORED copy in
// types::RenderSettings (saved to the .vfSettings by scene_save) and the LIVE copy the renderer uses.
// writeSections keeps both in step; the tables below (and the VK-1655 render-settings tables in
// RenderSettingsToolSupport) are the single source of every key, kind and valid range the tools accept
// and report. The table machinery itself lives in SettingsTable.hpp.
//
// Every function that dispatches runs on the main thread (inside a Main-affinity handler or an undo
// command); the others say "Any thread". Deliberately free of ToolHelpers.hpp, like
// TerrainToolSupport.hpp: SceneTools.cpp keeps its own file-local entityId().
namespace mcp::tools::env
{
    using AtmosphereSettings = ::render::atmosphere::AtmosphereSettings;
    using CloudSettings = ::render::cloud::CloudSettings;
    using FogSettings = ::postprocess::VolumetricFogSettings;
    using GISettings = ::render::gi::GISettings;

    // VK-1655 post-process sections (RenderSettings::postProcess.<member>).
    using ToneMappingSettings = ::postprocess::ToneMappingSettings;
    using AutoExposureSettings = ::postprocess::AutoExposureSettings;
    using BloomSettings = ::postprocess::BloomSettings;
    using ColorGradingSettings = ::postprocess::ColorGradingSettings;
    using SSAOSettings = ::postprocess::SSAOSettings;
    using VignetteSettings = ::postprocess::VignetteSettings;
    using ChromaticAberrationSettings = ::postprocess::ChromaticAberrationSettings;
    using FilmGrainSettings = ::postprocess::FilmGrainSettings;
    using DepthOfFieldSettings = ::postprocess::DepthOfFieldSettings;
    using EdgeDetectionSettings = ::postprocess::EdgeDetectionSettings;
    using FXAASettings = ::postprocess::FXAASettings;
    using UpscaleSettings = ::postprocess::UpscaleSettings;

    // Canonical enum spellings, indexed by the enumerator's value. Matched case-insensitively.
    inline constexpr std::array<std::string_view, 3> fogQualityNames{"Low", "Medium", "High"};
    inline constexpr std::array<std::string_view, 4> giQualityNames{"Off", "Medium", "High", "Ultra"};

    template <>
    struct EnumNames<::postprocess::VolumetricQuality>
    {
        static constexpr std::span<const std::string_view> names{fogQualityNames};
    };

    template <>
    struct EnumNames<::render::gi::GIQuality>
    {
        static constexpr std::span<const std::string_view> names{giQualityNames};
    };

    // Any thread (function-local statics).
    const SectionTable<AtmosphereSettings>& atmosphereTable();
    const SectionTable<CloudSettings>& cloudTable();
    const SectionTable<FogSettings>& fogTable();
    const SectionTable<GISettings>& giTable();

    // ------------------------------------------------------------------
    // Sections
    // ------------------------------------------------------------------

    using SectionValue = std::variant<AtmosphereSettings, CloudSettings, FogSettings, GISettings,
                                      PostProcessMasterSettings, ToneMappingSettings, AutoExposureSettings,
                                      BloomSettings, ColorGradingSettings, SSAOSettings, VignetteSettings,
                                      ChromaticAberrationSettings, FilmGrainSettings, DepthOfFieldSettings,
                                      EdgeDetectionSettings, FXAASettings, UpscaleSettings, ShadowQualitySettings,
                                      DisplayAntiAliasingSettings>;

    // The section's table name ("atmosphere", "clouds", "volumetric fog", "GI", "bloom", ...). Any thread.
    std::string_view sectionName(const SectionValue& value);

    // Writes the sections (each at most once, else std::logic_error): ONE fresh GetRenderSettingsQuery
    // with only these sections replaced (GI: the debug-view flags cleared - they are not scene data;
    // the VK-1655 partial sections copy only their own fields) -> ONE SetRenderSettingsCommand (false
    // throws "no scene is open") -> each live copy at most once, in this order: atmosphere
    // (ApplyAtmosphereSettingsNoSunCommand); clouds (ApplyCloudSettingsCommand); post-process (the live
    // GetPostProcessSettingsQuery with only the staged fog / post-process sections replaced ->
    // ApplyPostProcessSettingsCommand, so weather's live fog and the runtime-driven effects survive); GI
    // (the live debug flags kept -> ApplyGISettingsCommand); shadows (ApplyShadowSettingsCommand with the
    // stored settings); MSAA (ApplyDisplaySettingsNotification with the stored present mode) -> one
    // RenderSettingsChangedNotification{section, sceneLoad = false} per distinct section. One section
    // dispatches exactly the VK-1654 single-section sequence.
    void writeSections(std::span<const SectionValue> values);

    // ------------------------------------------------------------------
    // Engine state
    // ------------------------------------------------------------------

    // nullopt when an environment edit may run now, else why not: Play mode (IsPlayModeQuery) or a
    // pending scene load (IsSceneLoadPendingQuery; a missing handler counts as "not pending").
    // `action` completes "Cannot <action> ...".
    std::optional<std::string> editBlocker(std::string_view action);

    // IsWeatherEnabledQuery; false when no weather service is registered.
    bool isWeatherEnabled();

    // GetSunEntityQuery: activeOnly = the light the renderer uses as the sun, otherwise any
    // directional light. nullopt when there is none.
    std::optional<services::EntityHandle> sunEntity(bool activeOnly);

    // The scene root's IBLComponent (GetRootEntityQuery + GetIBLDataQuery), nullopt without one.
    std::optional<services::IBLData> rootIbl();

    // Applies the scene's image-based lighting. With data: SetIBLDataCommand on the scene root (false
    // throws) -> when the data has an HDR: SetIBLCommand{hdrRef.resolve()} only if `rebake` (false
    // throws) -> SetIBLParamsCommand; without an HDR: RemoveIBLCommand (no environment, neutral knobs,
    // like a scene load). nullopt: RemoveIBLCommand -> RemoveIBLComponentCommand (a missing component
    // is not an error). Then RenderSettingsChangedNotification{IBL}.
    void applyIbl(const std::optional<services::IBLData>& data, bool rebake);

    // Field-wise equality of two IBL states (HDR reference, intensity, rotation, tint). Any thread.
    bool sameIbl(const std::optional<services::IBLData>& a, const std::optional<services::IBLData>& b);

    // The session's weather (it is never saved with the scene).
    struct WeatherSnapshot
    {
        bool enabled = false;
        ::weather::WeatherState state;
    };

    // IsWeatherEnabledQuery + GetWeatherStateQuery.
    WeatherSnapshot readWeather();

    // SetWeatherEnabledCommand{enabled} -> SetWeatherImmediateCommand{state} -> UpdateWeatherCommand{0}
    // when enabled. In this order the state survives the reset a true -> false switch performs.
    void applyWeather(const WeatherSnapshot& snapshot);

    // Every WeatherState field within `tolerance` (0 = exact). Any thread.
    bool sameWeatherState(const ::weather::WeatherState& a, const ::weather::WeatherState& b, float tolerance);
}
