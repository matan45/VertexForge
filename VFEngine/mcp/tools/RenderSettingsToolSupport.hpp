#pragma once

#include "EnvironmentToolSupport.hpp"

#include "events/editor/PluginPostProcessEffectEvents.hpp"

#include <nlohmann/json.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// VK-1655 helpers shared by the render-settings tools (RenderSettingsTools.cpp) and their undo commands
// (mcp/undo/PluginEffectUndo). The scene's post-processing, shadows and anti-aliasing live in the same
// types::RenderSettings as the VK-1654 environment, so they are sections of the same SectionValue,
// written by the same writeSections. The tables below are the single source of every key, kind and
// valid range render_settings_get reports and render_settings_set accepts: each range equals or lies
// inside the scene loader's clamp (SceneSerializePostProcess.cpp), or the editor slider where the loader
// has none (shadows: RenderConfigWindowSections.cpp), so every accepted value survives a save + load.
//
// Every function that dispatches runs on the main thread; the others say "Any thread".
namespace mcp::tools::env
{
    // Canonical enum spellings, indexed by the enumerator's value. Matched case-insensitively.
    inline constexpr std::array<std::string_view, 7> toneMappingModeNames{
        "ACES", "Reinhard", "Uncharted2", "Linear", "GranTurismo", "AgX", "KhronosPBRNeutral"};
    inline constexpr std::array<std::string_view, 4> ssaoQualityNames{"Low", "Medium", "High", "Ultra"};
    inline constexpr std::array<std::string_view, 2> focusModeNames{"Manual", "TargetPoint"};
    inline constexpr std::array<std::string_view, 3> upscaleModeNames{"Off", "DLSS", "Auto"};
    inline constexpr std::array<std::string_view, 5> upscaleQualityNames{
        "Native", "Quality", "Balanced", "Performance", "UltraPerformance"};
    inline constexpr std::array<std::string_view, 5> shadowQualityNames{"Off", "Low", "Medium", "High", "Ultra"};
    inline constexpr std::array<std::string_view, 4> msaaNames{"Off", "X2", "X4", "X8"};

    template <>
    struct EnumNames<::postprocess::ToneMappingMode>
    {
        static constexpr std::span<const std::string_view> names{toneMappingModeNames};
    };

    template <>
    struct EnumNames<::postprocess::SSAOQuality>
    {
        static constexpr std::span<const std::string_view> names{ssaoQualityNames};
    };

    template <>
    struct EnumNames<::postprocess::DoFFocusMode>
    {
        static constexpr std::span<const std::string_view> names{focusModeNames};
    };

    template <>
    struct EnumNames<::postprocess::UpscaleMode>
    {
        static constexpr std::span<const std::string_view> names{upscaleModeNames};
    };

    template <>
    struct EnumNames<::postprocess::UpscaleQuality>
    {
        static constexpr std::span<const std::string_view> names{upscaleQualityNames};
    };

    template <>
    struct EnumNames<types::ShadowQuality>
    {
        static constexpr std::span<const std::string_view> names{shadowQualityNames};
    };

    template <>
    struct EnumNames<types::MsaaSamples>
    {
        static constexpr std::span<const std::string_view> names{msaaNames};
    };

    // ------------------------------------------------------------------
    // Tables (any thread; function-local statics)
    // ------------------------------------------------------------------

    const SectionTable<PostProcessMasterSettings>& postProcessMasterTable();
    const SectionTable<ToneMappingSettings>& toneMappingTable();
    const SectionTable<AutoExposureSettings>& autoExposureTable();
    const SectionTable<BloomSettings>& bloomTable();
    const SectionTable<ColorGradingSettings>& colorGradingTable();
    const SectionTable<SSAOSettings>& ssaoTable();
    const SectionTable<VignetteSettings>& vignetteTable();
    const SectionTable<ChromaticAberrationSettings>& chromaticAberrationTable();
    const SectionTable<FilmGrainSettings>& filmGrainTable();
    const SectionTable<DepthOfFieldSettings>& depthOfFieldTable();
    const SectionTable<EdgeDetectionSettings>& edgeDetectionTable();
    const SectionTable<UpscaleSettings>& upscaleTable();
    const SectionTable<ShadowQualitySettings>& shadowTable();
    const SectionTable<DisplayAntiAliasingSettings>& displayTable();

    // The 11 effect sections inside RenderSettings::postProcess, in the order the tools report them:
    // f(key, table, member) once per section; key = the scene serializer's JSON key. Any thread.
    template <typename F>
    void forEachPostProcessSection(F&& f)
    {
        f(std::string_view("toneMapping"), toneMappingTable(), &PostProcessSettings::toneMapping);
        f(std::string_view("autoExposure"), autoExposureTable(), &PostProcessSettings::autoExposure);
        f(std::string_view("bloom"), bloomTable(), &PostProcessSettings::bloom);
        f(std::string_view("colorGrading"), colorGradingTable(), &PostProcessSettings::colorGrading);
        f(std::string_view("ssao"), ssaoTable(), &PostProcessSettings::ssao);
        f(std::string_view("vignette"), vignetteTable(), &PostProcessSettings::vignette);
        f(std::string_view("chromaticAberration"), chromaticAberrationTable(), &PostProcessSettings::chromaticAberration);
        f(std::string_view("filmGrain"), filmGrainTable(), &PostProcessSettings::filmGrain);
        f(std::string_view("depthOfField"), depthOfFieldTable(), &PostProcessSettings::depthOfField);
        f(std::string_view("edgeDetection"), edgeDetectionTable(), &PostProcessSettings::edgeDetection);
        f(std::string_view("upscale"), upscaleTable(), &PostProcessSettings::upscale);
    }

    // The RenderSettings::postProcess keys render_settings_set refuses, with why and what to use instead
    // (volumetricFog -> fog_set, ...); nullopt for every other key. Any thread.
    std::optional<std::string_view> unsettablePostProcessKey(std::string_view key);

    // ------------------------------------------------------------------
    // Colour-grading LUT paths (.cube)
    // ------------------------------------------------------------------

    // The LUT path keys of a colorGrading table.
    inline constexpr std::array<std::string_view, 2> lutPathKeys{"primaryLutPath", "secondaryLutPath"};

    // `patch` (a colorGrading object) with its LUT path keys in the stored form: "" clears the slot; the
    // stored path, or the form reportLutPath gives it, keeps the stored path (the editor's Browse may
    // store a file outside the project); anything else must name an existing .cube file inside the
    // project and becomes its absolute path (ColorGradingEffect opens it with std::ifstream, relative to
    // the working directory). A non-string value is left for strictPatch to reject. ArgError names
    // '<prefix>.<key>'. Main thread: asks for the project root only when a path must be resolved.
    nlohmann::json resolveLutPaths(const nlohmann::json& patch, const ColorGradingSettings& current,
                                   std::string_view prefix);

    // A stored LUT path as the tools report it: relative to the project root (UTF-8, '/') when inside it,
    // else the whole path. "" stays "". Any thread.
    std::string reportLutPath(const std::optional<std::filesystem::path>& root, const std::string& stored);

    // Rewrites the LUT path keys present in `colorGrading` (a toJson / diffJson object) to reportLutPath.
    // Any thread.
    void reportLutPaths(nlohmann::json& colorGrading, const std::optional<std::filesystem::path>& root);

    // The non-empty LUT paths of `value` that name no regular file (as the effect would open them), as UTF-8
    // for reporting. Any thread.
    std::vector<std::string> missingLutPaths(const ColorGradingSettings& value);

    // ------------------------------------------------------------------
    // Plugin post-process effects (VK-1409)
    // ------------------------------------------------------------------

    // ListPluginPostProcessEffectsQuery. Throws std::runtime_error when no plugin runtime is registered.
    std::vector<events::editor::PluginPostProcessEffectInfo> listPluginEffects();

    // SetPluginPostProcessEffectEnabledCommand: false when no effect has this id. Throws
    // std::runtime_error when no plugin runtime is registered.
    bool setPluginEffectEnabled(uint64_t id, bool enabled);

    // {id, plugin, name, priority, stage, enabled, pluginActive, rendering}. Any thread.
    nlohmann::json pluginEffectJson(const events::editor::PluginPostProcessEffectInfo& effect);
}
