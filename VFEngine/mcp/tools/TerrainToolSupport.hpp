#pragma once

#include "../protocol/ArgReader.hpp"

#include "events/terrain/TerrainAuthoringEvents.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// VK-1653 helpers shared by the terrain tools (TerrainTools.cpp) and by the existing tools that have
// to know about terrain: scene_save, play_start and undo / redo. Every function that dispatches runs
// on the main thread (inside a Main-affinity handler or a runOnMain task); the others say "Any thread".
//
// Deliberately free of ToolHelpers.hpp: SceneTools.cpp includes this header and keeps its own
// file-local entityId(), which ToolHelpers' mcp::tools::entityId would make ambiguous.
namespace mcp::tools
{
    // The ProceduralGen HeightmapPresets ids in table order (terrain_generate_heightmap 'preset').
    // A copy, because Mcp may not link ProceduralGen (Editor-only); test_heightmap_presets pins it to
    // the ProceduralGen table. Any thread.
    inline const std::array<std::string_view, 7>& heightmapPresetIds()
    {
        static constexpr std::array<std::string_view, 7> ids{
            "hills", "plains", "mountains", "peaks", "valleys", "plateaus", "islands"};
        return ids;
    }

    // ------------------------------------------------------------------
    // Project paths
    // ------------------------------------------------------------------

    // The project's asset root (ProjectConfig::workingDirectory), absolute. Throws
    // std::runtime_error when no project is loaded.
    std::filesystem::path terrainProjectRoot();

    // The asset root, or nullopt when no project is loaded (or no project handler is registered).
    std::optional<std::filesystem::path> tryTerrainProjectRoot();

    // Any thread. An engine path string (narrow, like every engine path) as an absolute path; a
    // relative one is taken relative to `root`. Empty input gives an empty path.
    std::filesystem::path terrainEnginePath(const std::optional<std::filesystem::path>& root,
                                            const std::string& enginePath);

    // Any thread. `path` relative to `root` with '/' separators, or the whole path (UTF-8) when there
    // is no root or the path lies outside it.
    std::string projectRelativeUtf8(const std::optional<std::filesystem::path>& root,
                                    const std::filesystem::path& path);

    // Any thread. True when both name the same file (lexically normalised, case-insensitive on Windows).
    bool sameTerrainFile(const std::filesystem::path& a, const std::filesystem::path& b);

    // Any thread. `<directory>/<stem><extension>`, or `<stem>_<n><extension>` with the smallest n >= 1,
    // naming no existing file and no entry of `claimed`.
    std::filesystem::path uniqueFilePath(const std::filesystem::path& directory, const std::string& stem,
                                         std::string_view extension,
                                         const std::vector<std::filesystem::path>& claimed = {});

    // ------------------------------------------------------------------
    // Engine state
    // ------------------------------------------------------------------

    // ListTerrainsQuery. Throws when no terrain service is registered.
    std::vector<services::TerrainSummary> listTerrains();

    // ListTerrainsQuery, or nullopt when no terrain service is registered (the existing tools treat
    // that as "no terrain", so their test fakes need no terrain handlers).
    std::optional<std::vector<services::TerrainSummary>> tryListTerrains();

    // IsTerrainSaveLockedQuery / IsWorldModeQuery; false when the handler is missing.
    bool isTerrainSaveLocked();
    bool isWorldModeActive();

    // nullopt when a terrain edit may run now, else why not: Play mode, or a terrain save holding the
    // brush lock. `action` completes "Cannot <action> ...".
    std::optional<std::string> terrainEditBlocker(std::string_view action);

    // The terrain a tool addresses: the 'terrain' argument (a terrain id, or -- when `acceptTileId` --
    // a terrain tile id standing for its parent terrain) or, without it, the scene's only live
    // terrain. Throws std::runtime_error naming terrain_create / terrain_delete / the terrain ids.
    // `requireLive` refuses an empty shell (a terrain entity whose data failed to load).
    services::TerrainSummary resolveTerrainArg(const ArgReader& reader,
                                               const std::vector<services::TerrainSummary>& terrains,
                                               bool requireLive = true, bool acceptTileId = true);

    // ------------------------------------------------------------------
    // Geometry (any thread)
    // ------------------------------------------------------------------

    struct TerrainWorldBounds
    {
        glm::vec2 lower{0.0f}; // world XZ minimum
        glm::vec2 upper{0.0f}; // world XZ maximum
    };

    uint32_t terrainTilesX(const services::TerrainData& data);
    uint32_t terrainTilesZ(const services::TerrainData& data);
    uint32_t terrainVerticesPerTile(const services::TerrainData& data); // 33 / 65 / 129
    float terrainVertexSpacing(const services::TerrainData& data);      // metres between height samples
    // Height samples along the longer grid axis: tiles * (verticesPerTile - 1) + 1.
    uint32_t terrainVertexSpan(const services::TerrainData& data);
    // tiles * verticesPerTile^2, the figure MAX_TERRAIN_VERTICES caps.
    uint64_t terrainVertexCount(const services::TerrainData& data);
    // Tile (x, z) covers [x * tileSize, (x + 1) * tileSize] on each axis.
    TerrainWorldBounds terrainWorldBounds(const services::TerrainData& data);
    // "low" | "medium" | "high".
    std::string terrainResolutionName(uint8_t resolution);

    // ------------------------------------------------------------------
    // Reporting
    // ------------------------------------------------------------------

    // "5 ('Terrain')". Any thread.
    std::string describeTerrain(const services::TerrainSummary& terrain);

    // [{id, name, live}] in list order. Any thread.
    nlohmann::json terrainListJson(const std::vector<services::TerrainSummary>& terrains);

    // One palette layer {index, name, material (project-relative), tilingScale, heightBlend,
    // heightContrast, enabled}. Any thread.
    nlohmann::json terrainLayerJson(const services::TerrainMaterialLayerInfo& layer, uint32_t index,
                                    const std::optional<std::filesystem::path>& root);

    // terrain_get_info's description of one terrain; terrain_create returns the same.
    nlohmann::json terrainInfoJson(const services::TerrainSummary& terrain,
                                   const std::optional<std::filesystem::path>& root);

    // The agent-facing error for a non-Ok stroke result: status, the engine's message and what to do
    // about it. Any thread.
    std::string strokeErrorText(const services::TerrainStrokeResult& result,
                                const services::TerrainSummary& terrain);

    // ------------------------------------------------------------------
    // Saving
    // ------------------------------------------------------------------

    // A terrain that a save must write: live, and never saved, edited since its last save, or its
    // file is gone. Any thread (file existence only).
    bool terrainNeedsSave(const services::TerrainSummary& terrain);

    // "never saved" | "unsaved changes" | "file missing" (empty when terrainNeedsSave is false).
    std::string terrainSaveReason(const services::TerrainSummary& terrain);

    struct TerrainSaveTarget
    {
        services::EntityHandle entity;
        std::string name;
        std::filesystem::path path; // absolute .vfTerrain
    };

    struct TerrainSaveReport
    {
        bool ok = true;     // every save succeeded (vacuously true for none)
        std::string error;  // agent-readable; set when !ok
        nlohmann::json saved = nlohmann::json::array(); // [{terrain, name, path, incremental}]
        std::vector<std::string> warnings;
    };

    // `<root>/terrains/<safe name>.vfTerrain`, unique against existing files, every terrain's
    // savePath and `claimed`. Any thread.
    std::filesystem::path defaultTerrainSavePath(const std::string& terrainName,
                                                 const std::vector<services::TerrainSummary>& terrains,
                                                 const std::filesystem::path& root,
                                                 const std::vector<std::filesystem::path>& claimed = {});

    // Creates the parent folders, then saves every target through ONE SaveTerrainsCommand (the
    // engine locks, saves, flushes and unlocks). !ok on a refusal or any failed outcome.
    TerrainSaveReport saveTerrainTargets(const std::vector<TerrainSaveTarget>& targets,
                                         const std::optional<std::filesystem::path>& root);

    // The terrain step of scene_save, play_start {unsavedTerrain: "save"} and an argument-less
    // terrain_save: saves every terrain terrainNeedsSave reports, a never-saved one to
    // defaultTerrainSavePath. No terrain service means nothing to save; World mode is skipped with a
    // warning (the world's sectors own its terrain); empty shells are skipped with a warning.
    TerrainSaveReport saveUnsavedTerrains();
}
