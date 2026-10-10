#pragma once
#include "EntityHandle.hpp"
#include "TerrainData.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// VK-1653 terrain authoring DTOs (MCP P4).
//
// Plain data on purpose: the MCP library and the Editor reach these through EventDispatcher and
// neither links Terrain.dll, so nothing here may name a Terrain.dll class (same reason
// HeightLayerInfo in TerrainData.hpp is a POD rather than terrain::HeightLayerRecord).

namespace services
{
    // ---- Sculpt / paint / apply-heightmap strokes ----

    // Every non-Ok status means NOTHING was mutated and NO undo entry was pushed.
    enum class TerrainStrokeStatus : uint8_t
    {
        Ok = 0,
        InvalidArguments,
        NoTerrain,       // no terrain, the named entity is not a loaded terrain, or the choice is ambiguous
        SaveInProgress,  // a terrain save or load holds the brush lock
        GpuUnavailable,  // a GPU sculpt op with no brush compute provider (editor-only)
        TooMuchWork,     // the work budget was exceeded; checked before any mutation
        OffTerrain,      // no dab footprint reaches a resident or file-cached tile
        NoHeightAtPoint  // flatten target sampling hit a tile with no heights
    };

    struct TerrainStrokeSample
    {
        glm::vec2 xz{0.0f};
        float before = 0.0f; // height (sculpt / apply) or the painted layer's weight (paint)
        float after = 0.0f;
        bool valid = false;
    };

    struct TerrainStrokeResult
    {
        TerrainStrokeStatus status = TerrainStrokeStatus::Ok;
        std::string message;               // agent-readable; set for every non-Ok status
        std::vector<std::string> warnings; // Ok but partial (tiles skipped, eviction refused, ...)

        EntityHandle terrainEntity;
        uint32_t dabsApplied = 0;
        uint32_t tilesChanged = 0;  // tiles in the pushed undo entry (0 = nothing changed)
        uint32_t tilesSkipped = 0;

        bool closedOpenStroke = false; // a human stroke was still open and was pushed first
        bool undoPushed = false;
        std::string undoLabel;

        glm::vec2 footprintMin{0.0f}; // world XZ union of the dab footprints
        glm::vec2 footprintMax{0.0f};
        float flattenTarget = 0.0f;   // Flatten only: the target height actually used
        float perDabStrength = 0.0f;  // the normalised per-dab strength handed to the brush

        std::vector<TerrainStrokeSample> samples; // first MAX_REPORTED_SAMPLES input points
    };

    // ---- Terrain listing, heights, persistence ----

    struct TerrainSummary
    {
        EntityHandle entity;
        std::string name;
        bool live = false;              // a grid backs this entity (false = an empty shell entity)
        bool streamingEnabled = false;
        bool hasCollider = false;
        uint32_t residentHeightTiles = 0; // tiles whose heights are in RAM
        uint32_t pendingMeshTiles = 0;    // tiles still waiting for their mesh rebuild
        TerrainData data;
    };

    struct TerrainHeightSample
    {
        float height = 0.0f;
        bool valid = false;     // a height was produced
        bool onTerrain = false; // the position lies inside the terrain's tile grid
    };

    struct HeightmapProbeResult
    {
        bool valid = false;
        uint32_t width = 0;
        uint32_t height = 0;
        std::string error;
    };

    struct TerrainSaveRequest
    {
        EntityHandle terrainEntity;
        std::string path; // absolute .vfTerrain path
    };

    struct TerrainSaveOutcome
    {
        EntityHandle terrainEntity;
        std::string path;
        bool success = false;
        bool incremental = false;
        std::string error;
    };

    // ---- Terrain material (.vfTerrainMat) layers ----

    struct TerrainMaterialLayerInfo
    {
        std::string name;
        std::string materialPath; // resolved .vfMat / .vfMatInstance path, empty when unset
        float tilingScale = 1.0f;
        bool heightBlend = false;
        float heightContrast = 4.0f;
        bool enabled = true;
    };

    struct TerrainMaterialInfo
    {
        std::string path;
        std::string name;
        uint32_t activeLayerCount = 0;
        uint32_t maxLayers = 0; // terrain::MAX_TERRAIN_LAYERS, filled by the handler
        std::vector<TerrainMaterialLayerInfo> layers; // activeLayerCount entries
    };

    // Only the engaged fields are written.
    struct TerrainMaterialLayerPatch
    {
        std::optional<std::string> name;
        std::optional<std::string> materialPath; // absolute .vfMat / .vfMatInstance
        std::optional<float> tilingScale;
        std::optional<float> heightContrast;
        std::optional<bool> heightBlend;
        std::optional<bool> enabled;
    };

    struct TerrainMaterialEditResult
    {
        bool success = false;
        std::string error;
        uint32_t index = 0; // the layer that was written
        TerrainMaterialInfo material;
    };

    struct CreateTerrainMaterialAssetResult
    {
        bool success = false;
        std::string path; // absolute path of the file written
        std::string error;
    };
}
