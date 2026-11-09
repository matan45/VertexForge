#pragma once

#include "data/VFXAssetSummary.hpp"
#include "vfx/VFXParameterRegistry.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// VK-1657 helpers shared by the "VFX" component binding (VFXBinding.cpp) and the vfx_* tools (VFXTools.cpp).
// A VFX component's per-entity parameters are vfx::VFXParamOverride entries (VFXComponent::overrides, saved
// with the scene / prefab / sector; the .vfVFX is never written). The agent sets six of them by the keys of
// paramSpecs, with the tools' ranges, and every other overridable emitter parameter through "otherParams"
// {registry name: value}, typed by vfx::kExposedParameters. In the agent JSON a key that is null (or absent
// from otherParams) has no override: the effect plays it as authored.
//
// Every function here is pure: any thread, nothing is dispatched (the tests drive them directly).
namespace mcp::tools::vfxtool
{
    enum class ParamKind
    {
        Float,     // a number in [min, max]
        Tint,      // [r, g, b] or [r, g, b, a]: RGB in [min, max], alpha in [0, maxTintAlpha] (default 1)
        BlendMode, // one of blendModeNames, stored as its vfx::VFXBlendMode value
        Int        // an integer in [min, max]
    };

    struct ParamSpec
    {
        std::string_view key;          // the tools' name
        std::string_view overrideName; // the vfx::kExposedParameters entry it is stored as
        ParamKind kind = ParamKind::Float;
        double min = 0.0;              // inclusive (Float / Int; Tint: each RGB component)
        double max = 0.0;
        std::string_view help;         // meaning and units, for the schemas and the binding's fieldHelp
    };

    inline constexpr double maxTintAlpha = 1.0;

    // Indexed by vfx::VFXBlendMode (the GPU blendMode value), spelled like vfx::blendModeToString.
    inline constexpr std::array<std::string_view, 4> blendModeNames{"alpha", "additive", "premultiplied", "multiply"};
    static_assert(blendModeNames.size() == static_cast<std::size_t>(vfx::kBlendModeCount),
                  "one name per VFXBlendMode value");

    // The object that holds every override outside paramSpecs.
    inline constexpr std::string_view otherParamsKey = "otherParams";

    // Ranges: the .vfVFX editor's (VFXAssetDefaults.cpp), with lifetime and size kept above 0; the tint lets a
    // channel brighten up to 4x; sortOrder is the asset loader's clamp (vfx::kMinSortOrder..kMaxSortOrder).
    inline constexpr std::array<ParamSpec, 6> paramSpecs{{
        {"spawnRate", "spawnRate", ParamKind::Float, 0.0, 1000.0,
         "particles emitted per second; the effect's bursts are not affected"},
        {"lifetime", "lifetime", ParamKind::Float, 0.01, 60.0, "seconds each particle lives"},
        {"size", "startSize", ParamKind::Float, 0.01, 100.0, "particle start size in world units"},
        {"color", "colorTint", ParamKind::Tint, 0.0, 4.0,
         "a tint multiplied over every authored colour, the start colour and the colour-over-lifetime gradient "
         "alike; [1, 1, 1, 1] = as authored, above 1 brightens"},
        {"blendMode", "blendMode", ParamKind::BlendMode, 0.0, 3.0,
         "alpha = alpha-over, additive = a glow that brightens what is behind, premultiplied = the fire-into-smoke "
         "gradient trick, multiply = darkens what is behind"},
        {"sortOrder", "sortOrder", ParamKind::Int, static_cast<double>(vfx::kMinSortOrder),
         static_cast<double>(vfx::kMaxSortOrder),
         "draw order among the effects of the same pass; higher draws later, on top"}
    }};

    // ------------------------------------------------------------------
    // Keys and values
    // ------------------------------------------------------------------

    // By tool key / by registry name; nullptr when not in paramSpecs.
    const ParamSpec* findParam(std::string_view key);
    const ParamSpec* findParamByOverride(std::string_view overrideName);

    // The registry names "otherParams" accepts (every exposed parameter outside paramSpecs), registry order.
    std::vector<std::string_view> otherParamNames();

    // "spawnRate, lifetime, size, color, blendMode, sortOrder" (+ ", otherParams" with `withOtherParams`).
    std::string paramKeyList(bool withOtherParams);

    // Each table key with its range and meaning ("spawnRate (0 to 1000: particles emitted per second; ...), ..."),
    // then, with `withOtherParams`, otherParams with its names grouped by type: the binding's fieldHelp and the
    // tool descriptions.
    std::string paramsHelp(bool withOtherParams);

    // True when `path` (UTF-8) names a .vfVFXSequence combo (case-insensitive extension).
    bool isComboPath(std::string_view path);

    // A table parameter's value from the agent JSON; ArgError naming `path`.
    vfx::VFXPropertyValue parseParamValue(const ParamSpec& spec, const nlohmann::json& value, std::string_view path);

    // A stored value in that same JSON form: floats as their shortest round trip (env::floatJson), the tint as
    // [r, g, b, a], blendMode by name. Null when the value is not of the spec's type.
    nlohmann::json paramValueJson(const ParamSpec& spec, const vfx::VFXPropertyValue& value);

    // An "otherParams" value typed by `parameter`'s registry type (a finite number, an integer, a boolean,
    // [x, y, z], [r, g, b] or [r, g, b, a] with alpha 1 by default) and vfx::isValidOverrideValue; ArgError
    // naming `path`.
    vfx::VFXPropertyValue parseOtherValue(const vfx::VFXExposedParameter& parameter, const nlohmann::json& value,
                                          std::string_view path);
    nlohmann::json otherValueJson(const vfx::VFXPropertyValue& value);

    // Which table values patchOverrides accepts: the tools' ranges (the agent's input), or every value the
    // engine stores (vfx::isValidOverrideValue). An undo replays a snapshot of stored values, which a scene
    // file may hold outside the tools' ranges. "otherParams" values follow the engine's rules either way.
    enum class ValueRules
    {
        Tool,
        Stored
    };

    // `current` with the object `patch` applied, all-or-nothing: every key of `patch` is a table key (value =
    // set, null = remove the override) or "otherParams" (an object that becomes the whole set of overrides
    // outside the table, keyed by registry name; null = none). Throws ArgError naming '<prefix>.<key>' ('<key>'
    // when `prefix` is empty) for an unknown key, a wrong type or a value `rules` refuse, before anything is
    // returned. A value equal to what the key reports now is kept as stored. Overrides keep their order; new
    // ones are appended.
    std::vector<vfx::VFXParamOverride> patchOverrides(const std::vector<vfx::VFXParamOverride>& current,
                                                      const nlohmann::json& patch, std::string_view prefix,
                                                      ValueRules rules = ValueRules::Tool);

    // Writes the six table keys (null = as authored) and "otherParams" {name: value} into `out`.
    void writeParams(nlohmann::json& out, const std::vector<vfx::VFXParamOverride>& overrides);

    // ------------------------------------------------------------------
    // Authored vs effective
    // ------------------------------------------------------------------

    // A table parameter as the effect authors it: the tint's [1, 1, 1, 1] always; the others from `effect`,
    // null without it.
    nlohmann::json authoredParamJson(const ParamSpec& spec, const services::VFXEffectSummary* effect);

    // {key: {override, authored, effective}} for the six table keys; effective = override ?? authored.
    nlohmann::json paramsReportJson(const services::VFXEffectSummary* effect,
                                    const std::vector<vfx::VFXParamOverride>& overrides);

    // The colour a particle is born with. A colour-over-lifetime gradient replaces the start colour every
    // frame, so its age-0 colour wins; the tint multiplies either:
    //   authored  = colorOverLifetime ? birthColor : startColor
    //   effective = (colorOverLifetime ? birthColor : (startColor override ?? startColor)) * (colorTint ?? 1)
    glm::vec4 authoredBirthColor(const services::VFXEffectSummary& effect);
    glm::vec4 effectiveBirthColor(const services::VFXEffectSummary& effect,
                                  const std::vector<vfx::VFXParamOverride>& overrides);

    // {authored, effective, source: "colorOverLifetime" | "startColor"}; null without `effect`.
    nlohmann::json birthColorJson(const services::VFXEffectSummary* effect,
                                  const std::vector<vfx::VFXParamOverride>& overrides);

    // The particle lifetime an instance plays with: the lifetime override, else the authored one.
    float effectiveLifetime(const services::VFXEffectSummary& effect, const std::vector<vfx::VFXParamOverride>& overrides);

    // ------------------------------------------------------------------
    // Reporting (filesystem only)
    // ------------------------------------------------------------------

    // An engine path (absolute, or relative to `root`) relative to `root` with '/' separators; the whole
    // path when it lies outside. "" stays "".
    std::string projectPath(const std::optional<std::filesystem::path>& root, const std::string& enginePath);

    // The project-relative path when `enginePath` lies inside `root`, else nullopt.
    std::optional<std::string> insideProjectPath(const std::filesystem::path& root, const std::string& enginePath);

    // An effect: {path, name, valid, error?} and, when valid, its authored spawnRate, lifetime, size (start
    // size), startSpeed, startColor, looping, burstCount, colorOverLifetime, birthColor, deathColor, blendMode,
    // sortOrder, renderMode, emitsLight, texture, events [{type, notify, subEffect}].
    nlohmann::json effectJson(const services::VFXAssetSummary& summary, const std::optional<std::filesystem::path>& root);

    // A combo: {path, name, valid, error?} and, when valid, steps [{label, kind, effect, startTime, cue, loop}]
    // (cue null = time-driven), cues and playbackRate.
    nlohmann::json comboJson(const services::VFXAssetSummary& summary, const std::optional<std::filesystem::path>& root);
}
