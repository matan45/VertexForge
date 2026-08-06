#pragma once

#include "../EventTypes.hpp"
#include "../../data/TerrainData.hpp"
#include "../../utilities/terrain/SplineTypes.hpp"
#include "../../utilities/terrain/RoadMeshTypes.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace events::splineTerrain
{
    struct SetSplineModeActiveCommand : ICommand<>
    {
        bool active = false;

        std::string_view getName() const override { return "SetSplineModeActive"; }
    };

    struct AddSplinePointCommand : ICommand<>
    {
        glm::vec3 worldPosition{0.0f};

        std::string_view getName() const override { return "AddSplinePoint"; }
    };

    struct RemoveLastSplinePointCommand : ICommand<>
    {
        std::string_view getName() const override { return "RemoveLastSplinePoint"; }
    };

    struct ClearActiveSplineCommand : ICommand<>
    {
        std::string_view getName() const override { return "ClearActiveSpline"; }
    };

    struct FinalizeSplineCommand : ICommand<>
    {
        std::string_view getName() const override { return "FinalizeSpline"; }
    };

    struct DeleteSplineCommand : ICommand<>
    {
        uint64_t splineId = 0;

        std::string_view getName() const override { return "DeleteSpline"; }
    };

    struct SetSplineParamsCommand : ICommand<>
    {
        ::terrain::SplineParams params;

        std::string_view getName() const override { return "SetSplineParams"; }
    };

    struct GetSplineParamsQuery : IQuery<::terrain::SplineParams>
    {
        std::string_view getName() const override { return "GetSplineParams"; }
    };

    struct IsSplineModeActiveQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "IsSplineModeActive"; }
    };

    struct GetActiveSplinePointCountQuery : IQuery<uint32_t>
    {
        std::string_view getName() const override { return "GetActiveSplinePointCount"; }
    };

    struct GetActiveSplinePreviewQuery : IQuery<std::vector<glm::vec3>>
    {
        float sampleStep = 0.5f;

        std::string_view getName() const override { return "GetActiveSplinePreview"; }
    };

    // VK-1621 point editing. Control points used to be append-only (add, or pop the last one),
    // which is not enough to satisfy "editing the spline and re-applying regenerates the mesh".
    struct GetActiveSplinePointsQuery : IQuery<std::vector<::terrain::SplineControlPoint>>
    {
        std::string_view getName() const override { return "GetActiveSplinePoints"; }
    };

    struct SetSplinePointCommand : ICommand<bool>
    {
        uint32_t index = 0;
        glm::vec3 position{0.0f};

        std::string_view getName() const override { return "SetSplinePoint"; }
    };

    struct RemoveSplinePointCommand : ICommand<bool>
    {
        uint32_t index = 0;

        std::string_view getName() const override { return "RemoveSplinePoint"; }
    };

    // Re-opens an already-generated road for editing: seeds the active spline from the road's
    // persisted RoadSplineComponent. `replacesEntityId` rides through to SplineAppliedNotification
    // so the next apply replaces that road instead of spawning a second one beside it.
    struct LoadSplineForEditCommand : ICommand<bool>
    {
        std::vector<::terrain::SplineControlPoint> controlPoints;
        ::terrain::SplineParams params;
        uint64_t replacesEntityId = 0;

        // VK-1647. The height layer this road already owns (RoadSplineComponent::splineId, which is
        // persisted in the scene). Carrying it makes the next apply UPDATE that layer in place
        // instead of registering a second one: before this, re-authoring a road left its old
        // corridor composing forever and the terrain was carved twice.
        //
        // 0 means "no existing layer" — a sculpt-less road, or a spline started from scratch.
        uint64_t editsSplineId = 0;

        std::string_view getName() const override { return "LoadSplineForEdit"; }
    };

    struct SplineModeChangedNotification : INotification
    {
        bool isActive = false;

        std::string_view getName() const override { return "SplineModeChanged"; }
    };

    struct SplinePointCountChangedNotification : INotification
    {
        uint32_t pointCount = 0;

        std::string_view getName() const override { return "SplinePointAdded"; }
    };

    // Carries the whole applied spline, not just its id. The road mesh (VK-1621) has to be built
    // Editor-side — Import.dll is Editor-only (premake5.lua:263), and Services does not link it —
    // so the Editor subscribes here and generates when params.ops has SplineOps::Mesh. Publishing
    // AFTER the sculpt/paint ops is what guarantees the mesh conforms to the deformed terrain.
    struct SplineAppliedNotification : INotification
    {
        uint64_t splineId = 0;
        std::vector<::terrain::SplineControlPoint> controlPoints;
        ::terrain::SplineParams params;
        // Non-zero when this apply is a REGENERATION of an existing road entity, which the road
        // builder replaces rather than spawning a duplicate alongside.
        uint64_t replacesEntityId = 0;

        std::string_view getName() const override { return "SplineApplied"; }
    };

    struct SplineParamsChangedNotification : INotification
    {
        ::terrain::SplineParams params;

        std::string_view getName() const override { return "SplineParamsChanged"; }
    };

    // Internal: sent by SplineService to TerrainService for terrain modification
    struct ApplySplinePaintCommand : ICommand<bool>
    {
        std::vector<glm::vec3> splineSamples;
        ::terrain::SplineParams params;

        std::string_view getName() const override { return "ApplySplinePaint"; }
    };

    struct ApplySplineDeformCommand : ICommand<bool>
    {
        std::vector<glm::vec3> splineSamples; // sampled curve points
        ::terrain::SplineParams params;
        uint64_t splineId = 0;

        std::string_view getName() const override { return "ApplySplineDeform"; }
    };

    // VK-1645. A sculpt spline's height effect is a reserved LAYER over the authoritative base
    // now, so undoing an apply is a visibility flip plus a recompose -- not a snapshot restore.
    // Hiding rather than removing is deliberate: coverage survives, so the tile's base stays
    // authoritative and an ordinary sculpt underneath still routes there. Redo is the same
    // command with visible = true, and because compose always restarts from the base, repeated
    // cycles are bit-identical.
    //
    // Returns false when no layer carries that id.
    struct SetSplineHeightLayerVisibleCommand : ICommand<bool>
    {
        uint64_t splineId = 0;
        bool visible = true;

        std::string_view getName() const override { return "SetSplineHeightLayerVisible"; }
    };

    // Drops the layer for good (spline deletion, as opposed to undo). The base blocks it seeded
    // are deliberately kept: they are authoritative artist data, and a later sculpt or another
    // spline over the same tile still needs them.
    struct RemoveSplineHeightLayerCommand : ICommand<>
    {
        uint64_t splineId = 0;

        std::string_view getName() const override { return "RemoveSplineHeightLayer"; }
    };

    // VK-1647. Allocates the next layer id from the terrain that owns the stack, so the counter
    // survives a reload: the stack comes back off the sidecar with ids 1..N while a
    // freshly-constructed service would otherwise start again at 1 and collide with every one of
    // them. Ids are monotonic and never reused — a RoadSplineComponent keeps its splineId in the
    // SCENE and can outlive the layer it names.
    //
    // Returns 0 when there is no terrain to allocate from; the caller falls back to its own
    // counter, which is enough for a paint- or mesh-only spline that registers no layer.
    struct ReserveHeightLayerIdCommand : ICommand<uint64_t>
    {
        std::string_view getName() const override { return "ReserveHeightLayerId"; }
    };

    // The reserved height-layer stack, in composition order (index 0 composes first). POD only —
    // the Editor does not link Terrain.dll. VK-1648's list panel is the intended consumer.
    struct GetHeightLayerStackQuery : IQuery<std::vector<services::HeightLayerInfo>>
    {
        std::string_view getName() const override { return "GetHeightLayerStack"; }
    };

    // Moves one layer to `newIndex`, sliding the layers between its old and new position by one.
    // Only the tiles the moved layer shares with a layer it crossed can change, so that is exactly
    // what gets invalidated.
    //
    // Returns false when no layer carries that id, or when newIndex is past the end of the stack.
    struct MoveHeightLayerCommand : ICommand<bool>
    {
        uint64_t splineId = 0;
        uint32_t newIndex = 0;

        std::string_view getName() const override { return "MoveHeightLayer"; }
    };

    // Published after any change to the stack's membership, order, parameters or visibility, so a
    // list UI can refresh without polling. Carries no payload beyond the terrain: the reader is
    // expected to re-run GetHeightLayerStackQuery, which is the single source of truth.
    struct HeightLayerStackChangedNotification : INotification
    {
        uint64_t terrainEntityId = 0;

        std::string_view getName() const override { return "HeightLayerStackChanged"; }
    };

    // VK-1621. Builds the road ribbon from an already-applied spline. Handled by TerrainService
    // because it owns the tile grid: the mesh must be conformed with terrain::terrainQuadHeight
    // against live tile height data, not with GetTerrainHeightAtQuery, which interpolates
    // bilinearly (TerrainService.cpp:602-610) while the terrain is triangulated on the
    // anti-diagonal — a road built on bilinear heights clips through every ridge it crosses.
    struct BuildSplineRoadMeshQuery : IQuery<::terrain::RoadMeshData>
    {
        std::vector<glm::vec3> splineSamples;
        ::terrain::RoadProfile profile;

        std::string_view getName() const override { return "BuildSplineRoadMesh"; }
    };

}
