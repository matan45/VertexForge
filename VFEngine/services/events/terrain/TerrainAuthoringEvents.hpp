#pragma once
#include "../EventTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/TerrainAuthoringData.hpp"
#include "terrain/BrushTypes.hpp" // terrain::BrushFalloff, BrushShape
#include <glm/glm.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// VK-1653 terrain authoring (MCP P4).
//
// These are NOT the editor brush commands (BrushEvents.hpp / PaintBrushEvents.hpp): those read the
// sculpt/paint tool-mode state, a rate-based strength and a held mouse button. They are NOT the
// VK-1624 runtime edits (TerrainRuntimeEditEvents.hpp) either: those are transient by design --
// derived plane only, no markDirty, no undo, seam weld deferred to the render-thread tick.
//
// An authoring command is self-describing, framerate-free and complete when it returns: heights,
// seams, colliders, save-dirty state and exactly ZERO OR ONE VK-1615 stroke undo entry. It drives
// the same per-dab core as the editor brush (TerrainService::applySculptDab / applyLayerPaintDab),
// so an MCP stroke and a human stroke produce the same geometry.
//
// MAIN THREAD ONLY, with the render thread idle (the MCP drain runs at the top of the frame).

namespace events::terrainAuthoring
{
    // Work budget, checked before any mutation (TerrainStrokeStatus::TooMuchWork).
    inline constexpr uint32_t MAX_STROKE_POINTS = 256;
    inline constexpr uint32_t MAX_STROKE_DABS = 1024;      // after resampling, times smooth passes
    inline constexpr uint32_t MAX_GPU_TILE_DABS = 512;     // sum over dabs of footprint tiles (GPU ops)
    inline constexpr uint32_t MAX_CPU_TILE_DABS = 4096;    // same, for the CPU paint path
    inline constexpr uint32_t MAX_SMOOTH_PASSES = 64;
    inline constexpr uint32_t MAX_REPORTED_SAMPLES = 16;
    // The fractional minima are double on purpose: as floats they widen to 0.100000001... in the MCP
    // argument check (which validates in double) and would reject an agent's exact 0.1. A float radius
    // compared against them still passes at the boundary (0.1f >= 0.1).
    inline constexpr double MIN_BRUSH_RADIUS = 0.1;
    inline constexpr float MAX_BRUSH_RADIUS = 512.0f;
    inline constexpr double MIN_DAB_SPACING = 0.05;        // dab spacing as a fraction of the radius
    inline constexpr float MAX_DAB_SPACING = 1.0f;
    inline constexpr uint64_t MAX_TERRAIN_VERTICES = 4'500'000; // tiles * verts^2, create and apply

    enum class SculptOp : uint8_t
    {
        Raise = 0,
        Lower,
        Smooth,
        Flatten
    };

    enum class PaintOp : uint8_t
    {
        Paint = 0,
        Erase
    };

    // A brush swept along a polyline of world XZ points (one point = one dab). Dabs are resampled at
    // a uniform arc length of at most `spacing * radius`.
    //
    // Raise/Lower: `amount` is METRES at the stroke centreline. The per-dab strength is
    //   amount / S, S = sum over |k|s < r of falloff(|k|s / r)   (terrain::centrelineOverlap)
    // with deltaTime = 1, so a polyline raises its centreline by `amount`, not by amount * S.
    // Smooth: `strength` in [0,1] is the per-dab blend toward the 3x3 average, repeated `passes`
    // times over the whole dab list. Flatten: `strength` in [0,1] is the per-dab blend toward the
    // target (1 = reach it at every dab centre).
    struct SculptTerrainStrokeCommand : ICommand<services::TerrainStrokeResult>
    {
        services::EntityHandle terrainEntity; // INVALID = the scene's only live terrain
        SculptOp op = SculptOp::Raise;
        std::vector<glm::vec2> points;        // world XZ, 1..MAX_STROKE_POINTS
        float radius = 5.0f;
        float amount = 1.0f;                  // Raise/Lower: metres, > 0
        float strength = 1.0f;                // Smooth/Flatten: [0,1]
        std::optional<float> targetHeight;    // Flatten: nullopt = the height under points[0]
        uint32_t passes = 1;                  // Smooth: 1..MAX_SMOOTH_PASSES
        float spacing = 0.25f;                // [MIN_DAB_SPACING, MAX_DAB_SPACING]
        ::terrain::BrushFalloff falloff = ::terrain::BrushFalloff::Smooth;
        ::terrain::BrushShape shape = ::terrain::BrushShape::Circle;
        std::string undoLabel;                // empty = the editor's "Sculpt Terrain"

        std::string_view getName() const override { return "SculptTerrainStroke"; }
    };

    // Paint or erase one palette layer. `strength` in [0,1] is the weight ADDED (Paint) or REMOVED
    // (Erase) along the stroke centreline -- normalised by the same overlap sum S as Raise -- with the
    // other channels renormalised. Never SetBaseLayer (it re-initialises whole tiles).
    struct PaintTerrainLayerStrokeCommand : ICommand<services::TerrainStrokeResult>
    {
        services::EntityHandle terrainEntity;
        PaintOp op = PaintOp::Paint;
        std::vector<glm::vec2> points;
        uint32_t layerIndex = 0;              // palette layer, not a weight channel
        float radius = 5.0f;
        float strength = 1.0f;                // [0,1]
        float spacing = 0.25f;
        ::terrain::BrushFalloff falloff = ::terrain::BrushFalloff::Smooth;
        ::terrain::BrushShape shape = ::terrain::BrushShape::Circle;
        // A tile blends at most 8 palette layers. When a 9th is painted the editor brush evicts the
        // least-used channel and renormalises the WHOLE tile; by default this command skips that tile
        // and reports a warning instead.
        bool allowChannelEviction = false;
        std::string undoLabel;

        std::string_view getName() const override { return "PaintTerrainLayerStroke"; }
    };

    // Replace the heights of the WHOLE terrain from a .vfImage heightmap, mapped exactly like
    // terrain creation maps one (stretched over the full tile grid, bilinear), with the normalised
    // value h in [0,1] becoming baseHeight + h * amplitude, clamped to the terrain's height limits.
    // Writes the authoritative plane (VK-1645), so it is one undoable stroke like any sculpt.
    struct ApplyHeightmapCommand : ICommand<services::TerrainStrokeResult>
    {
        services::EntityHandle terrainEntity;
        std::string heightmapPath; // absolute .vfImage
        float baseHeight = 0.0f;
        float amplitude = 30.0f;   // > 0
        std::string undoLabel;

        std::string_view getName() const override { return "ApplyTerrainHeightmap"; }
    };

    // Every entity with a TerrainComponent, live or not, in a stable order (entity id ascending).
    struct ListTerrainsQuery : IQuery<std::vector<services::TerrainSummary>>
    {
        std::string_view getName() const override { return "ListTerrains"; }
    };

    // Entity-addressed, O(1) per point, results in input order. With `pageIn`, tiles that are
    // file-cached but not resident are streamed in and their heights loaded first -- so this works
    // after a reload, unlike GetTerrainHeightAtQuery (first grid only, resident heights only).
    struct GetTerrainHeightsQuery : IQuery<std::vector<services::TerrainHeightSample>>
    {
        services::EntityHandle terrainEntity; // INVALID = the scene's only live terrain
        std::vector<glm::vec2> positions;     // world XZ
        bool pageIn = true;

        std::string_view getName() const override { return "GetTerrainHeights"; }
    };

    // True while a terrain save or load holds the brush lock (TerrainService::saveInProgress).
    struct IsTerrainSaveLockedQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "IsTerrainSaveLocked"; }
    };

    // True while an async terrain creation (BeginCreateTerrainCommand) has not been polled to the end.
    struct IsTerrainCreationPendingQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "IsTerrainCreationPending"; }
    };

    // Loads a heightmap the way terrain creation would, so a caller can refuse a bad file up front
    // instead of getting a silently flat terrain.
    struct ProbeHeightmapQuery : IQuery<services::HeightmapProbeResult>
    {
        std::string path; // absolute .vfImage

        std::string_view getName() const override { return "ProbeHeightmap"; }
    };

    // Saves terrains synchronously on the calling (main) thread: lock -> prepare -> save -> flush the
    // parked component updates -> unlock, with flush-then-unlock guaranteed on every path. Incremental
    // only when the target is the terrain's OWN cached file and it exists. Refuses everything while
    // another save holds the lock. One outcome per request, in request order.
    struct SaveTerrainsCommand : ICommand<std::vector<services::TerrainSaveOutcome>>
    {
        std::vector<services::TerrainSaveRequest> requests;

        std::string_view getName() const override { return "SaveTerrains"; }
    };
}
