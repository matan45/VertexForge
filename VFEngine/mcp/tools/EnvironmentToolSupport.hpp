#pragma once

#include "../protocol/ArgReader.hpp"

#include "data/DTOs.hpp"
#include "data/EntityHandle.hpp"
#include "types/RenderSettings.hpp"
#include "weather/WeatherTypes.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// VK-1654 helpers shared by the environment tools (EnvironmentTools.cpp) and their undo commands
// (mcp/undo/EnvironmentUndo). Every environment setting exists twice: the scene's AUTHORED copy in
// types::RenderSettings (saved to the .vfSettings by scene_save) and the LIVE copy the renderer uses.
// writeSection keeps both in step; the tables below are the single source of every key, kind and
// valid range the tools accept and report.
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

    // Canonical enum spellings, indexed by the enumerator's value. Matched case-insensitively.
    inline constexpr std::array<std::string_view, 3> fogQualityNames{"Low", "Medium", "High"};
    inline constexpr std::array<std::string_view, 4> giQualityNames{"Off", "Medium", "High", "Ultra"};

    // ------------------------------------------------------------------
    // Field tables
    // ------------------------------------------------------------------

    enum class FieldKind
    {
        Bool,
        Float,
        Int,
        UInt,
        Vec3,  // glm::vec3 or float[3]; min / max bound every component
        Enum
    };

    template <typename T>
    struct FieldDesc
    {
        std::string_view key;                         // the scene serializer's JSON key (= the member name)
        FieldKind kind = FieldKind::Float;
        double min = 0.0;                             // inclusive (Float / Int / UInt / Vec3)
        double max = 0.0;
        std::span<const std::string_view> enumNames;  // Enum: canonical names, index = enumerator value
        bool weatherDriven = false;                   // weather overrides the live value while it is enabled
        std::string_view setHint;                     // non-empty: read-only in a patch; says what to use instead
        bool persisted = true;                        // false: live-only state (GI debug views), never reported
        std::string_view help;                        // units / meaning, for the input schemas
        // The value in the JSON form the tools report (floats as floatJson, enums by name) ...
        nlohmann::json (*get)(const T&) = nullptr;
        // ... and the writer for a value in that same form (strictPatch validates and canonicalises first).
        void (*set)(T&, const nlohmann::json&) = nullptr;
    };

    template <typename T>
    struct SectionTable
    {
        std::string_view name;                          // "atmosphere" | "clouds" | "volumetric fog" | "GI"
        std::vector<FieldDesc<T>> fields;               // struct order
        std::string (*crossCheck)(const T&) = nullptr;  // a staged value's cross-field error, or ""
    };

    // Any thread (function-local statics).
    const SectionTable<AtmosphereSettings>& atmosphereTable();
    const SectionTable<CloudSettings>& cloudTable();
    const SectionTable<FogSettings>& fogTable();
    const SectionTable<GISettings>& giTable();

    // ------------------------------------------------------------------
    // Generic ops over a table (any thread). Explicitly instantiated for the four section types.
    // ------------------------------------------------------------------

    // Every persisted key -> its reported value.
    template <typename T>
    nlohmann::json toJson(const SectionTable<T>& table, const T& value);

    // `base` with the keys of `patch` applied, all-or-nothing. `patch` must be an object; an unknown
    // key, a wrong type, an out-of-range value or a read-only key (setHint) set to anything but its
    // current value throws ArgError naming '<prefix>.<key>' (just '<key>' when `prefix` is empty, i.e.
    // the patch is a tool's own arguments). A read-only key echoing its current value is skipped, so a
    // whole *_get object can be passed back.
    // Numbers are checked as JSON numbers (booleans are not numbers), must be finite and fit a float,
    // and are narrowed to float BEFORE the inclusive [min, max] test against the float bounds, so a
    // value a *_get reported is always accepted back. Ends with the table's crossCheck.
    template <typename T>
    T strictPatch(const SectionTable<T>& table, const T& base, const nlohmann::json& patch, std::string_view prefix);

    // {key: {type, min, max | values, weatherDriven?, readOnly?}} for every persisted key.
    template <typename T>
    nlohmann::json rangesJson(const SectionTable<T>& table);

    // A JSON-Schema object of the settable keys (additionalProperties: false).
    template <typename T>
    nlohmann::json schemaJson(const SectionTable<T>& table, std::string description);

    // The persisted keys whose reported value differs between `from` and `to`, with `to`'s value.
    template <typename T>
    nlohmann::json diffJson(const SectionTable<T>& table, const T& from, const T& to);

    // The keys weather drives live, in table order.
    template <typename T>
    std::vector<std::string> weatherDrivenKeys(const SectionTable<T>& table);

    // ------------------------------------------------------------------
    // Values and arguments (any thread)
    // ------------------------------------------------------------------

    // The shortest decimal that round-trips `value` (std::to_chars), as a JSON number: 0.05f reports
    // as 0.05, and 0.05 narrows back to exactly 0.05f. Non-finite -> null.
    nlohmann::json floatJson(float value);
    nlohmann::json vec3Json(const glm::vec3& value);

    // The checks strictPatch applies, for arguments outside a table. `path` names the value in errors.
    float requireFloat(const nlohmann::json& value, double min, double max, std::string_view path);
    glm::vec3 requireVec3(const nlohmann::json& value, double min, double max, std::string_view path);
    bool requireBool(const nlohmann::json& value, std::string_view path);

    // ArgError for any top-level argument not in `allowed` ("unknown argument 'x' for <tool>; valid
    // arguments: ..."). Null arguments count as none.
    void rejectUnknownArgs(const nlohmann::json& args, std::initializer_list<std::string_view> allowed,
                           std::string_view toolName);

    // ArgError for any key of the object `value` not in `allowed`; `path` names the object.
    void rejectUnknownKeys(const nlohmann::json& value, std::initializer_list<std::string_view> allowed,
                           std::string_view path);

    // ------------------------------------------------------------------
    // Sections
    // ------------------------------------------------------------------

    using SectionValue = std::variant<AtmosphereSettings, CloudSettings, FogSettings, GISettings>;

    // The section's table name ("atmosphere", "clouds", "volumetric fog", "GI"). Any thread.
    std::string_view sectionName(const SectionValue& value);

    // Writes one section: a FRESH GetRenderSettingsQuery with only this section replaced (GI: the
    // debug-view flags cleared - they are not scene data) -> SetRenderSettingsCommand (false throws
    // "no scene is open") -> the live copy (atmosphere: ApplyAtmosphereSettingsNoSunCommand; clouds:
    // ApplyCloudSettingsCommand; fog: the live GetPostProcessSettingsQuery with only volumetricFog
    // replaced -> ApplyPostProcessSettingsCommand; GI: the live debug flags kept ->
    // ApplyGISettingsCommand) -> RenderSettingsChangedNotification{section, sceneLoad = false}.
    void writeSection(const SectionValue& value);

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
