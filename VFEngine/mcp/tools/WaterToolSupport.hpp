#pragma once

#include "SettingsTable.hpp"

#include "data/DTOs.hpp"
#include "data/EntityHandle.hpp"
#include "data/OceanData.hpp"
#include "data/TerrainAuthoringData.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// VK-1658 helpers shared by the water tools (WaterTools.cpp) and their undo commands (mcp/undo/WaterUndo).
// The ocean is ONE value here, services::OceanStateData (GetOceanStateQuery / SetOceanStateCommand): every
// tool reads it fresh, changes what the call asks for and writes it back, so the fields the tools do not
// expose (density, deep alpha, maxVisibleDepth, gravity, the SSR debug view) keep their values. The tables
// below are the single source of every key and range the tools accept and report; keys are the scene
// serializer's (= the member names), and the ranges are the Ocean Editor's widgets except where the engine
// itself writes values outside them (band amplitude and wind direction), so a reported value always echoes
// back.
//
// Every function that dispatches runs on the main thread (inside a Main-affinity handler or an undo
// command); the others say "Any thread". Deliberately free of ToolHelpers.hpp, like SettingsTable.hpp.
namespace mcp::tools::watertool
{
    using Visual = services::OceanVisualSettings;
    using Band = services::OceanBandConfigData;
    using Physics = services::OceanPhysicsSettings;

    // ------------------------------------------------------------------
    // Tables (any thread; function-local statics)
    // ------------------------------------------------------------------

    // The nine look sections of water_set_params / water_get.look, in report order. Each table's name is the
    // section's argument key: color, refraction, caustics, shoreFoam, shoreline, reflections, absorption,
    // antiTiling, interactiveRipples.
    const std::vector<env::SectionTable<Visual>>& lookSections();
    const env::SectionTable<Visual>* findLookSection(std::string_view key);

    // One FFT band (ocean_set_params.bands.<name>): all eleven fields.
    const env::SectionTable<Band>& bandTable();
    // ocean_set_params.physics: physicsEnabled, drag, buoyancyStrength (density is never read by buoyancy).
    const env::SectionTable<Physics>& physicsTable();

    // ------------------------------------------------------------------
    // Names (any thread)
    // ------------------------------------------------------------------

    // FFT band names, indexed like OceanStateData::bands; bit i of a band mask is band i.
    inline constexpr std::array<std::string_view, 3> bandNames{"swell", "agitation", "ripples"};
    // WaterBodyComponentData::type: 0 = lake, 1 = pool.
    inline constexpr std::array<std::string_view, 2> bodyTypeNames{"lake", "pool"};

    enum class TerrainReference
    {
        Lowest,
        Center,
        Rim
    };
    inline constexpr std::array<std::string_view, 3> terrainReferenceNames{"lowest", "center", "rim"};

    // Valid FFT band resolutions (a texture size the FFT can run at).
    inline constexpr std::array<uint32_t, 4> bandResolutions{64, 128, 256, 512};

    // Index of `name` in bandNames (case-insensitive), or -1.
    int bandIndex(std::string_view name);

    // A JSON array of band names -> mask (ArgError naming `path` for anything else).
    uint32_t bandMaskFromNames(const nlohmann::json& names, std::string_view path);
    // The names of the bits set in `mask` (bits above 2 ignored).
    nlohmann::json bandMaskNames(uint32_t mask);

    // ------------------------------------------------------------------
    // Ranges the tables do not hold (any thread)
    // ------------------------------------------------------------------

    inline constexpr double maxLevel = 1.0e6;              // |level| of the ocean and of a body, metres
    inline constexpr double maxLevelAboveTerrain = 1000.0; // |levelAboveTerrain|, metres
    inline constexpr double maxHalfExtent = 5000.0;        // metres (WaterBodyDrawer)
    inline constexpr double maxBodyDepth = 1000.0;         // metres (WaterBodyDrawer)
    inline constexpr double maxWeatherResponse = 2.0;      // OceanEditorWindow
    inline constexpr double maxBeaufort = 12.0;
    inline constexpr double maxWindDirection = 720.0;      // |degrees| of ocean_set_params' windDirection argument
    // |degrees| a band's own windDirection accepts. Direction is periodic and the engine never wraps it: a sea
    // state writes the swell's direction +/- 15 into the other bands, so any finite value must echo back.
    inline constexpr double maxBandWindDirection = 1.0e6;
    inline constexpr double maxChoppiness = 5.0;
    // windSpeedFromBeaufort(12), the strongest wind a sea state maps to (m/s).
    double maxSeaStateWindSpeed();

    // Every range water_get {ranges:true} reports: look sections, band, physics, ocean and body arguments.
    nlohmann::json rangesJson();

    // ------------------------------------------------------------------
    // Ocean state edits (any thread)
    // ------------------------------------------------------------------

    // Exactly what OceanService::setSeaState(beaufort, 0) does: seaStateFromBeaufort(clamped, swell wind
    // direction) writes each band's windSpeed, windDirection, amplitude, choppiness, displacementScale and
    // foamThreshold, and currentBeaufort = the clamped Beaufort (0-12).
    void applySeaState(services::OceanStateData& state, float beaufort);

    // The swell band takes `directionDeg`; the other two keep their offset from it. Results wrap to [0, 360).
    void rotateWind(services::OceanStateData& state, float directionDeg);

    // Every band's choppiness.
    void setChoppiness(services::OceanStateData& state, float choppiness);

    // Does any field the live weather drives (each band's windSpeed, windDirection, amplitude, choppiness,
    // displacementScale, foamThreshold, and currentBeaufort) differ?
    bool weatherOwnedChanged(const services::OceanStateData& a, const services::OceanStateData& b);

    // ------------------------------------------------------------------
    // Parts: what an OceanStateUndo restores (any thread)
    // ------------------------------------------------------------------

    using OceanParts = uint32_t;
    inline constexpr OceanParts partLevel = 1u << 0;     // waterHeight
    inline constexpr OceanParts partVisible = 1u << 1;   // isActive
    inline constexpr OceanParts partPhysics = 1u << 2;   // the physicsTable fields
    inline constexpr OceanParts partWeather = 1u << 3;   // weatherDriven, weatherResponse
    inline constexpr OceanParts partSeaState = 1u << 4;  // currentBeaufort
    inline constexpr OceanParts partBand0 = 1u << 5;     // bands[i] = partBand0 << i
    inline constexpr OceanParts partLook0 = 1u << 8;     // lookSections()[i] = partLook0 << i

    // The parts whose values differ between `a` and `b`.
    OceanParts changedParts(const services::OceanStateData& a, const services::OceanStateData& b);
    // Copies the `parts` of `source` into `target`; everything else of `target` stays.
    void mergeParts(services::OceanStateData& target, const services::OceanStateData& source, OceanParts parts);
    // "level, swell band, color" - for undo descriptions.
    std::string partsText(OceanParts parts);

    // ------------------------------------------------------------------
    // Terrain footprints (any thread)
    // ------------------------------------------------------------------

    // Up to 32 x 32 sample points over the rectangle center +/- halfExtents, about `spacing` apart, row-major
    // (z outer, x inner), corners included; a zero extent gives one row / column. The exact centre is
    // appended last.
    struct Footprint
    {
        std::vector<glm::vec2> points;
        uint32_t samplesX = 0;
        uint32_t samplesZ = 0;
    };
    Footprint footprintGrid(const glm::vec2& center, const glm::vec2& halfExtents, float spacing);

    struct FootprintStats
    {
        std::size_t total = 0;        // grid samples (the appended centre not counted)
        std::size_t valid = 0;        // ... with a terrain height
        float min = 0.0f;             // over every valid sample, the exact centre included (0 when none)
        float max = 0.0f;
        std::optional<float> center;  // the exact centre's height
        std::optional<float> rimMin;  // the lowest valid sample on the grid's outer ring

        // Any terrain height at all: a grid sample or the centre. With an even sample count the centre is
        // not a grid point, so it is folded into min / max on its own.
        bool anyHeight() const { return valid > 0 || center.has_value(); }
    };
    // `heights` answers footprint.points in order (GetTerrainHeightsQuery).
    FootprintStats footprintStats(const Footprint& footprint, std::span<const services::TerrainHeightSample> heights);

    // The height a terrain reference names; nullopt when the footprint has no such sample.
    std::optional<float> referenceHeight(const FootprintStats& stats, TerrainReference reference);

    // {samples, valid, min, max, center, rimMin}; heights null when there are none.
    nlohmann::json footprintJson(const FootprintStats& stats);

    // ------------------------------------------------------------------
    // Creation (any thread)
    // ------------------------------------------------------------------

    // What the Ocean Editor's Create Ocean sends (OceanEditorWindow::createOcean) at `level`: the default
    // creation data and band config, FFT enabled.
    services::OceanCreationData creationDefaults(float level);
    // The creation data for re-creating an ocean that is then given `state` with SetOceanStateCommand.
    services::OceanCreationData creationFromState(const services::OceanStateData& state);

    // ------------------------------------------------------------------
    // Water bodies (any thread)
    // ------------------------------------------------------------------

    // Field-wise equality (WaterBodyComponentData has no operator==).
    bool sameBodyData(const services::WaterBodyComponentData& a, const services::WaterBodyComponentData& b);
    // The surface level the renderer uses: the LOCAL transform Y plus the component's offset.
    float bodyLevel(const services::TransformData& transform, const services::WaterBodyComponentData& data);

    // ------------------------------------------------------------------
    // Reports (any thread)
    // ------------------------------------------------------------------

    // {<section>: {key: value}} for the nine look sections.
    nlohmann::json lookJson(const Visual& visual);
    // The echo-able part of an ocean: {level, weatherDriven, weatherResponse, bands{swell, agitation,
    // ripples}, physics}; ocean_set_params accepts it back unchanged.
    nlohmann::json oceanParamsJson(const services::OceanStateData& state);
    // {entity, name, visible, seaState (read-only: the current Beaufort number), params}.
    nlohmann::json oceanJson(const services::EntityHandle& ocean, const std::string& name,
                             const services::OceanStateData& state);
    // {entity, name, type, center [x, z], halfExtents [hx, hz], level, depth, waves, physicsEnabled, active};
    // water_set_body accepts it back unchanged.
    nlohmann::json bodyJson(const services::EntityHandle& entity, const std::string& name,
                            const services::TransformData& transform, const services::WaterBodyComponentData& data);

    // ------------------------------------------------------------------
    // Engine state (main thread)
    // ------------------------------------------------------------------

    // GetOceanEntityQuery: the ocean OceanService tracks; invalid when there is none (or no ocean service).
    services::EntityHandle trackedOcean();
    // GetOceanStateQuery{ocean} (invalid = the tracked ocean); nullopt when `ocean` is not the scene's ocean.
    std::optional<services::OceanStateData> readOceanState(const services::EntityHandle& ocean);
    // SetOceanStateCommand; throws std::runtime_error when the engine refuses it.
    void writeOceanState(const services::EntityHandle& ocean, const services::OceanStateData& state);

    // What deleting the ocean entity would lose for good: re-creating it (CreateOcean + SetOceanState) brings
    // back the ocean only, not its children (deleted with it) nor the other components its prefab snapshot
    // carries. ocean_enable {remove:true} refuses on them, and so does undoing an ocean's creation.
    struct OceanExtras
    {
        std::size_t children = 0;
        std::vector<std::string> components;  // CopyEntityToJsonQuery "components" keys

        bool empty() const { return children == 0 && components.empty(); }
    };
    OceanExtras oceanExtras(const services::EntityHandle& ocean);

    // GetTerrainHeightsQuery{terrain, points, pageIn}; throws std::runtime_error when the answer does not
    // match the points.
    std::vector<services::TerrainHeightSample> sampleTerrainHeights(const services::EntityHandle& terrain,
                                                                    const std::vector<glm::vec2>& points);
}
