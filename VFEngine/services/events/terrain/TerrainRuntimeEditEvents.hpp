#pragma once
#include "../EventTypes.hpp"
#include "terrain/BrushTypes.hpp"      // terrain::HeightEditMode, BrushFalloff, BrushShape
#include "terrain/PaintBrushTypes.hpp" // terrain::PaintBrushType
#include <glm/glm.hpp>
#include <cstdint>

// VK-1624 runtime (script-driven) terrain editing.
//
// These are NOT the editor brush commands. events::brush::ApplyBrushCommand and its paint / hole
// siblings look self-contained but their handlers immediately query editor tool-mode state --
// GetSculptTargetEntityQuery, GetBrushParamsQuery, GetPaintTargetEntityQuery,
// GetHoleTargetEntityQuery -- and those handlers live in services that only EditorServiceBootstrap
// constructs. EventDispatcher::query throws when a handler is missing, so in the Runtime the
// command dispatch succeeds and the handler body throws. Sculpting is additionally GPU-only
// (TerrainService::brushComputeProvider is null outside the editor), so it would silently no-op
// even if the queries resolved.
//
// Hence a parallel, fully self-describing surface: every parameter travels in the command, nothing
// is read from ambient mode state, and the height path runs on the CPU.
//
// The terrain is resolved from the edit's world XZ rather than carried as an entity handle, so
// these match the read side (GetTerrainHeightAtQuery, GetTerrainLayerWeightsAtQuery), which is
// world-addressed for the same reason: a script has no way to obtain a terrain entity id.

namespace events::terrainEdit
{
    // Every mutating command returns the number of tiles it actually changed. 0 is the universal
    // "nothing happened" signal -- off the loaded terrain, tile not resident, arguments rejected,
    // or a save in progress -- so a script can branch on it without a separate status channel.

    // Radial height edit. `amount` is world-Y metres: a delta for Add, a target height for Set.
    struct DeformTerrainCommand : ICommand<uint32_t>
    {
        glm::vec2 worldPosition{0.0f}; // XZ
        float radius = 0.0f;
        float amount = 0.0f;
        ::terrain::HeightEditMode mode = ::terrain::HeightEditMode::Add;
        ::terrain::BrushFalloff falloff = ::terrain::BrushFalloff::Smooth;
        ::terrain::BrushShape shape = ::terrain::BrushShape::Circle;

        std::string_view getName() const override { return "DeformTerrain"; }
    };

    // Paint one palette layer's weight. `strength` is absolute influence at the brush centre in
    // [0,1] terms, NOT the editor's per-second rate: the service passes deltaTime = 1.0 to
    // WeightBrushApplicator so a one-shot call paints what it says it paints.
    struct PaintTerrainLayerCommand : ICommand<uint32_t>
    {
        glm::vec2 worldPosition{0.0f}; // XZ
        float radius = 0.0f;
        uint32_t layerIndex = 0; // Palette layer, not channel
        float strength = 1.0f;
        float opacity = 1.0f;
        ::terrain::BrushFalloff falloff = ::terrain::BrushFalloff::Smooth;
        ::terrain::BrushShape shape = ::terrain::BrushShape::Circle;
        // SetBaseLayer is rejected by the handler: it calls initializeDefault(), wiping every
        // weight channel of every touched tile. That is not something a stray script argument
        // should be able to do to authored terrain.
        ::terrain::PaintBrushType paintMode = ::terrain::PaintBrushType::PaintLayer;

        std::string_view getName() const override { return "PaintTerrainLayer"; }
    };

    // Punch or fill holes. No falloff parameter: HoleBrushApplicator thresholds the falloff curve
    // at 0.5, so anything but Constant silently shrinks the effective radius by an undocumented
    // factor. The handler hardcodes Constant, and the radius a script asks for is the radius it gets.
    struct SetTerrainHolesCommand : ICommand<uint32_t>
    {
        glm::vec2 worldPosition{0.0f}; // XZ
        float radius = 0.0f;
        bool makeHole = true;
        ::terrain::BrushShape shape = ::terrain::BrushShape::Circle;

        std::string_view getName() const override { return "SetTerrainHoles"; }
    };

    // Open an edit batch. Edits between this and FlushTerrainEdits share one seam-sync pass, one
    // collider submission and one notification. Purely an optimisation: an edit issued with no
    // batch open opens and closes its own, and an unclosed batch is drained on the next terrain
    // tick either way -- so forgetting to flush costs at most one frame of stale collision, never
    // correctness.
    struct BeginTerrainEditBatchCommand : ICommand<>
    {
        std::string_view getName() const override { return "BeginTerrainEditBatch"; }
    };

    // Close the batch and mark it ready. The expensive work (seam weld, collider rebuild) happens
    // on the next terrain tick, where the camera position needed by the async collider path lives
    // and where the terrain grid is otherwise exclusively mutated. Returns the number of tiles the
    // pending batch covers.
    struct FlushTerrainEditsCommand : ICommand<uint32_t>
    {
        std::string_view getName() const override { return "FlushTerrainEdits"; }
    };

    // Published once per drain, after seams are welded and colliders submitted.
    struct RuntimeTerrainEditedNotification : INotification
    {
        uint32_t tileCount = 0;
        glm::vec3 worldMin{0.0f};
        glm::vec3 worldMax{0.0f};

        std::string_view getName() const override { return "RuntimeTerrainEdited"; }
    };
}
