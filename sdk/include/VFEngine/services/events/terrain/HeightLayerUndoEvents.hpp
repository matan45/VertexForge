#pragma once

#include "../EventTypes.hpp"
#include "../../utilities/terrain/SplineCorridorDeform.hpp"
#include "../../utilities/terrain/TerrainHeightLayerStore.hpp"
#include "../../utilities/terrain/TerrainTypes.hpp"

#include <cstdint>
#include <string>
#include <unordered_set>

// VK-1648. The REVERSE path for reserved height-layer stack edits, plus the snapshot the forward
// path captures in order to build one.
//
// Services-only, exactly like SplineTerrainUndoEvents.hpp beside it and for the same reason: these
// types reach into Terrain.dll, and the Editor does not link it (premake5.lua). The Editor drives
// the stack through the *WithUndo commands in SplineTerrainEvents.hpp, which are POD.
//
// NOTHING HERE RECORDS AN UNDO ENTRY, and that is the whole point of the split rather than a tidy
// convention. UndoRedoServiceImpl::pushCommand calls clearStackBytes(redoStack), which ends in
// stack.clear() -- so a reverse-path command that pushed would not merely duplicate history, it
// would DESTROY the redo stack the user is halfway through. The same split already exists for
// brush strokes as RestoreStrokeStateCommand.
namespace events::splineTerrain
{
    // The serializable half of a terrain::HeightLayerRecord, plus its position in the stack.
    //
    // Deliberately not the record itself. `eval` is a std::function whose closure captures the
    // polyline BY VALUE (makeSplineCorridorEval), so copying a record carries the samples twice and
    // hides a heap block that IUndoableCommand::getMemoryFootprint() cannot measure -- and the undo
    // service recomputes footprints under _DEBUG and logs on drift. Rebuilding the callable through
    // makeSplineCorridorEval on the way back in also keeps the one-construction-site rule intact, so
    // a restored layer cannot drift from one reloaded off disk.
    //
    // `present == false` is a first-class value, not an error: it is what makes the undo of an ADD
    // a removal rather than a restore.
    struct HeightLayerSnapshot
    {
        bool present = false;

        uint64_t id = 0;
        uint32_t index = 0; // composition-order position to restore at
        bool visible = true;
        std::string name;
        ::terrain::HeightLayerType type = ::terrain::HeightLayerType::Unknown;
        ::terrain::SplineCorridorLayerParams spline;
        std::unordered_set<::terrain::TileCoord, ::terrain::TileCoordHash> affected;
    };

    // Reads one layer back as a snapshot, or `present == false` when the id names no layer.
    //
    // Run BEFORE a destructive op, by the code that will push the undo entry -- never by the handler
    // that performs the op, which must stay free of history entirely.
    struct GetHeightLayerSnapshotQuery : IQuery<HeightLayerSnapshot>
    {
        uint64_t splineId = 0;

        std::string_view getName() const override { return "GetHeightLayerSnapshot"; }
    };

    // Puts a layer back exactly as it was: parameters, name, visibility, affected set and stack
    // position, with `eval` rebuilt through makeSplineCorridorEval.
    //
    // Restore is addLayer + moveLayer rather than a new store insert API. That is always valid:
    // after the add the stack holds N+1 records and the saved index is at most N, so moveLayer's
    // out-of-range guard cannot fire.
    //
    // The snapshot's affected set is filtered on store.hasBase(coord) first. A coord whose tile was
    // deliberately DELETED while this entry sat on the undo stack lost its base and was scrubbed
    // from every live layer by dropCoordFromLayers -- but a snapshot is invisible to that, and
    // re-introducing the coord recreates the claimed-but-baseless state that makes recomposeTile
    // refuse the tile forever while normalizeDerivedSeams keeps welding its boundary column.
    // Streamed-out tiles are NOT dropped by that filter: bases are keyed by coord and survive
    // eviction, which is exactly why the store keys them that way.
    //
    // Invalidates the UNION of the layer's current affected set (read before the mutation) and the
    // snapshot's, then recomposes unbudgeted -- the same rule the in-place spline edit follows.
    //
    // Returns false when there is no terrain, when the snapshot is absent, or when the add was
    // refused (editing locked, or the id came back into use while the entry sat on the stack).
    struct RestoreHeightLayerCommand : ICommand<bool>
    {
        HeightLayerSnapshot snapshot;

        std::string_view getName() const override { return "RestoreHeightLayer"; }
    };
}
