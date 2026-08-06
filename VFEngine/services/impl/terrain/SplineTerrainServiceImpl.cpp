#include "SplineTerrainServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/SplineTerrainEvents.hpp"
#include "../../events/terrain/SplineTerrainUndoEvents.hpp"
#include "../../events/terrain/HeightLayerUndoEvents.hpp"
#include "../../events/editor/UndoRedoEvents.hpp"
#include "../../data/SplineApplyUndoCommands.hpp"
#include "../../data/TerrainLayerUndoCommands.hpp"
#include "terrain/SplineSampling.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <memory>

namespace services
{
    void SplineTerrainServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::splineTerrain::SetSplineModeActiveCommand>(
            [this](const events::splineTerrain::SetSplineModeActiveCommand& cmd)
            {
                splineModeActive = cmd.active;
                if (!cmd.active)
                {
                    clearActiveSpline();
                }

                events::splineTerrain::SplineModeChangedNotification n;
                n.isActive = cmd.active;
                events::EventDispatcher::instance().publish(n);
            });

        dispatcher.registerCommandHandler<events::splineTerrain::AddSplinePointCommand>(
            [this](const events::splineTerrain::AddSplinePointCommand& cmd)
            {
                addPoint(cmd.worldPosition);
            });

        dispatcher.registerCommandHandler<events::splineTerrain::RemoveLastSplinePointCommand>(
            [this](const events::splineTerrain::RemoveLastSplinePointCommand&)
            {
                removeLastPoint();
            });

        dispatcher.registerCommandHandler<events::splineTerrain::ClearActiveSplineCommand>(
            [this](const events::splineTerrain::ClearActiveSplineCommand&)
            {
                clearActiveSpline();
            });

        dispatcher.registerCommandHandler<events::splineTerrain::FinalizeSplineCommand>(
            [this](const events::splineTerrain::FinalizeSplineCommand&)
            {
                finalizeSpline();
            });

        dispatcher.registerCommandHandler<events::splineTerrain::DeleteSplineCommand>(
            [this](const events::splineTerrain::DeleteSplineCommand& cmd)
            {
                deleteSpline(cmd.splineId);
            });

        // VK-1648. The Editor-facing stack edits: record one undo entry, then delegate to the
        // non-recording primitive TerrainService owns.
        //
        // They live HERE rather than beside those primitives on purpose. TerrainServiceHandlers.cpp
        // must stay free of undo entirely — every one of its handlers doubles as a reverse path,
        // and a reverse path that recorded would clear the redo stack the user is halfway through
        // (UndoRedoServiceImpl::pushCommand calls clearStackBytes(redoStack)). This file is already
        // the one that pushes spline undo entries and already owns deleteSpline.
        //
        // Every one captures the prior value BEFORE the mutation and records only on success, so a
        // refusal (editing locked, unknown id, index out of range) leaves the history untouched.
        dispatcher.registerQueryHandler<events::splineTerrain::SetHeightLayerVisibleWithUndoCommand>(
            [](const events::splineTerrain::SetHeightLayerVisibleWithUndoCommand& cmd) -> bool
            {
                auto& d = events::EventDispatcher::instance();

                events::splineTerrain::GetHeightLayerSnapshotQuery snapshotQuery;
                snapshotQuery.splineId = cmd.splineId;
                const auto before = d.query(snapshotQuery);
                if (!before.present)
                    return false;

                events::splineTerrain::SetSplineHeightLayerVisibleCommand primitive;
                primitive.splineId = cmd.splineId;
                primitive.visible = cmd.visible;
                if (!d.query(primitive))
                    return false;

                // A no-op toggle still succeeded, but recording it would cost the user a Ctrl+Z
                // that changes nothing on screen.
                if (before.visible == cmd.visible)
                    return true;

                events::undoredo::PushUndoableCommand pushCmd;
                pushCmd.command = std::make_shared<HeightLayerVisibilityUndoCommand>(
                    cmd.visible ? "Show Height Layer" : "Hide Height Layer", cmd.splineId,
                    before.visible, cmd.visible);
                d.execute(pushCmd);
                return true;
            });

        dispatcher.registerQueryHandler<events::splineTerrain::MoveHeightLayerWithUndoCommand>(
            [](const events::splineTerrain::MoveHeightLayerWithUndoCommand& cmd) -> bool
            {
                auto& d = events::EventDispatcher::instance();

                // The prior INDEX is the whole undo payload, and MoveHeightLayerCommand returns
                // only a bool — so it has to be read before the move or it is unrecoverable.
                events::splineTerrain::GetHeightLayerSnapshotQuery snapshotQuery;
                snapshotQuery.splineId = cmd.splineId;
                const auto before = d.query(snapshotQuery);
                if (!before.present)
                    return false;

                events::splineTerrain::MoveHeightLayerCommand primitive;
                primitive.splineId = cmd.splineId;
                primitive.newIndex = cmd.newIndex;
                if (!d.query(primitive))
                    return false;

                if (before.index == cmd.newIndex)
                    return true; // the handler treats this as success; it is not history

                events::undoredo::PushUndoableCommand pushCmd;
                pushCmd.command = std::make_shared<HeightLayerOrderUndoCommand>(
                    "Reorder Height Layer", cmd.splineId, before.index, cmd.newIndex);
                d.execute(pushCmd);
                return true;
            });

        dispatcher.registerQueryHandler<events::splineTerrain::RenameHeightLayerWithUndoCommand>(
            [](const events::splineTerrain::RenameHeightLayerWithUndoCommand& cmd) -> bool
            {
                auto& d = events::EventDispatcher::instance();

                events::splineTerrain::GetHeightLayerSnapshotQuery snapshotQuery;
                snapshotQuery.splineId = cmd.splineId;
                const auto before = d.query(snapshotQuery);
                if (!before.present)
                    return false;

                if (before.name == cmd.name)
                    return true; // committing an unedited text field must not fill the history

                events::splineTerrain::SetHeightLayerNameCommand primitive;
                primitive.splineId = cmd.splineId;
                primitive.name = cmd.name;
                if (!d.query(primitive))
                    return false;

                events::undoredo::PushUndoableCommand pushCmd;
                pushCmd.command = std::make_shared<HeightLayerNameUndoCommand>(
                    "Rename Height Layer", cmd.splineId, before.name, cmd.name);
                d.execute(pushCmd);
                return true;
            });

        dispatcher.registerCommandHandler<events::splineTerrain::SetSplineParamsCommand>(
            [this](const events::splineTerrain::SetSplineParamsCommand& cmd)
            {
                currentParams = cmd.params;

                events::splineTerrain::SplineParamsChangedNotification n;
                n.params = currentParams;
                events::EventDispatcher::instance().publish(n);
            });

        dispatcher.registerQueryHandler<events::splineTerrain::GetSplineParamsQuery>(
            [this](const events::splineTerrain::GetSplineParamsQuery&)
            {
                return currentParams;
            });

        dispatcher.registerQueryHandler<events::splineTerrain::IsSplineModeActiveQuery>(
            [this](const events::splineTerrain::IsSplineModeActiveQuery&)
            {
                return splineModeActive;
            });

        dispatcher.registerQueryHandler<events::splineTerrain::GetActiveSplinePointCountQuery>(
            [this](const events::splineTerrain::GetActiveSplinePointCountQuery&)
            {
                return static_cast<uint32_t>(activePoints.size());
            });

        dispatcher.registerQueryHandler<events::splineTerrain::GetActiveSplinePreviewQuery>(
            [this](const events::splineTerrain::GetActiveSplinePreviewQuery& query)
            {
                return terrain::sampleSplineCurve(activePoints, query.sampleStep);
            });

        dispatcher.registerQueryHandler<events::splineTerrain::GetActiveSplinePointsQuery>(
            [this](const events::splineTerrain::GetActiveSplinePointsQuery&)
            {
                return activePoints;
            });

        dispatcher.registerQueryHandler<events::splineTerrain::SetSplinePointCommand>(
            [this](const events::splineTerrain::SetSplinePointCommand& cmd)
            {
                return setPoint(cmd.index, cmd.position);
            });

        dispatcher.registerQueryHandler<events::splineTerrain::RemoveSplinePointCommand>(
            [this](const events::splineTerrain::RemoveSplinePointCommand& cmd)
            {
                return removePoint(cmd.index);
            });

        dispatcher.registerQueryHandler<events::splineTerrain::LoadSplineForEditCommand>(
            [this](const events::splineTerrain::LoadSplineForEditCommand& cmd)
            {
                if (cmd.controlPoints.size() < 2)
                    return false;

                activePoints = cmd.controlPoints;
                currentParams = cmd.params;
                editingRoadEntityId = cmd.replacesEntityId;
                editingSplineId = cmd.editsSplineId;

                if (!splineModeActive)
                {
                    splineModeActive = true;
                    events::splineTerrain::SplineModeChangedNotification modeChanged;
                    modeChanged.isActive = true;
                    events::EventDispatcher::instance().publish(modeChanged);
                }

                events::splineTerrain::SplineParamsChangedNotification paramsChanged;
                paramsChanged.params = currentParams;
                events::EventDispatcher::instance().publish(paramsChanged);
                publishPointCount();
                return true;
            });
    }

    void SplineTerrainServiceImpl::publishPointCount()
    {
        events::splineTerrain::SplinePointCountChangedNotification n;
        n.pointCount = static_cast<uint32_t>(activePoints.size());
        events::EventDispatcher::instance().publish(n);
    }

    bool SplineTerrainServiceImpl::setPoint(uint32_t index, const glm::vec3& pos)
    {
        if (index >= activePoints.size())
            return false;

        activePoints[index].position = pos;
        // The count is unchanged, but subscribers use this to invalidate their cached preview —
        // keying a preview cache on the point COUNT alone is what made dragging a point look like
        // nothing had happened.
        publishPointCount();
        return true;
    }

    bool SplineTerrainServiceImpl::removePoint(uint32_t index)
    {
        if (index >= activePoints.size())
            return false;

        activePoints.erase(activePoints.begin() + static_cast<std::ptrdiff_t>(index));
        publishPointCount();
        return true;
    }

    void SplineTerrainServiceImpl::addPoint(const glm::vec3& pos)
    {
        activePoints.push_back({pos});
        publishPointCount();
    }

    void SplineTerrainServiceImpl::removeLastPoint()
    {
        if (!activePoints.empty())
        {
            activePoints.pop_back();
            publishPointCount();
        }
    }

    void SplineTerrainServiceImpl::clearActiveSpline()
    {
        activePoints.clear();
        // Dropping the points also drops the link to the road they came from; a fresh spline must
        // not silently replace the road that was last opened for editing -- nor reuse its height
        // layer, which would carve the new corridor into the old road's tiles.
        editingRoadEntityId = 0;
        editingSplineId = 0;
    }

    void SplineTerrainServiceImpl::finalizeSpline()
    {
        if (activePoints.size() < 2)
            return;

        auto& dispatcher = events::EventDispatcher::instance();

        // Sample once and reuse for every op, so the corridor the terrain is flattened to, the
        // corridor the weights are painted along, and the curve the road mesh follows are the
        // same curve by construction rather than by coincidence.
        const float sampleStep = (currentParams.road.ringSpacing > 0.0f)
                                     ? std::min(0.5f, currentParams.road.ringSpacing * 0.5f)
                                     : 0.5f;
        auto samples = terrain::sampleSplineCurve(activePoints, sampleStep);
        if (samples.size() < 2)
            return;

        // Allocate the id UP FRONT. It used to be owned by the sculpt branch, so a paint-only
        // apply published `nextSplineId - 1` without ever incrementing it — id 0 for the first
        // paint, and the previous sculpt's id afterwards.
        //
        // VK-1647: re-authoring a road keeps its id, so the apply below updates that layer in place
        // rather than stacking a second corridor over the first. Otherwise the id comes from the
        // terrain's layer store, whose watermark is persisted in the sidecar — a service-local
        // counter restarts at 1 on every load and collides with the ids the sidecar just restored,
        // and because removeLayer/setLayerVisible resolve by first match, the new spline's undo
        // would then hide someone else's road.
        uint64_t splineId = editingSplineId;
        if (splineId == 0)
        {
            splineId = dispatcher.query(events::splineTerrain::ReserveHeightLayerIdCommand{});
            if (splineId == 0)
            {
                // No terrain to allocate from — a paint- or mesh-only spline. The local counter is
                // enough, because nothing will register a height layer for it.
                splineId = nextSplineId;
            }
        }
        nextSplineId = std::max(nextSplineId, splineId + 1);

        const float totalHalfWidth = currentParams.corridorWidth + currentParams.falloffWidth;
        const bool wantsSculpt = terrain::hasOp(currentParams.ops, terrain::SplineOps::Sculpt);
        const bool wantsPaint = terrain::hasOp(currentParams.ops, terrain::SplineOps::Paint);
        const bool wantsMesh = terrain::hasOp(currentParams.ops, terrain::SplineOps::Mesh);

        // One batch around the whole apply. The terrain snapshots are pushed from here and the
        // road entities are pushed by the Editor from inside the SplineAppliedNotification below;
        // publish() is synchronous, so both land in this batch and the user gets a single Ctrl+Z.
        events::undoredo::BeginBatchCommand beginBatch;
        beginBatch.description = "Apply Spline";
        if (!currentParams.roadName.empty() && wantsMesh)
            beginBatch.description = "Apply Road \"" + currentParams.roadName + "\"";
        dispatcher.execute(beginBatch);

        bool anyApplied = false;
        events::splineTerrain::HeightLayerSnapshot layerBefore;
        events::splineTerrain::HeightLayerSnapshot layerAfter;
        events::splineTerrain::SplineWeightSnapshot originalWeights;
        events::splineTerrain::SplineWeightSnapshot appliedWeights;

        // Sculpt strictly first: the road mesh conforms to the terrain as it stands after the
        // corridor is flattened, so running it later would drape the road over the old ground.
        if (wantsSculpt)
        {
            // VK-1645: no per-tile height snapshots. The deform registers a reserved layer over
            // each covered tile's authoritative base, so undo replays a DEFINITION rather than
            // writing bytes back.
            //
            // VK-1648 captures that definition on both sides of the apply. Deliberately through a
            // separate query rather than by widening ApplySplineDeformCommand's result: the forward
            // mutation sequence stays byte-for-byte what it was, which is what keeps the
            // hand-mirrored replay tests a faithful model of this path. It is the same shape the
            // paint branch below already uses for weights.
            //
            // `before` is absent on a first apply and present on a re-author, and that single bit
            // is what makes undo remove the layer in one case and restore the older corridor in
            // the other.
            events::splineTerrain::GetHeightLayerSnapshotQuery snapshotQuery;
            snapshotQuery.splineId = splineId;
            layerBefore = dispatcher.query(snapshotQuery);

            events::splineTerrain::ApplySplineDeformCommand deformCmd;
            deformCmd.splineSamples = samples;
            deformCmd.params = currentParams;
            deformCmd.splineId = splineId;
            const bool heightLayerRegistered = dispatcher.query(deformCmd);
            anyApplied |= heightLayerRegistered;

            if (heightLayerRegistered)
                layerAfter = dispatcher.query(snapshotQuery);
            else
                layerBefore = {}; // the apply was refused; there is nothing to undo
        }

        if (wantsPaint)
        {
            events::splineTerrain::GetSplineOriginalWeightsQuery weightQuery;
            weightQuery.splineSamples = samples;
            weightQuery.totalHalfWidth = totalHalfWidth;
            originalWeights = dispatcher.query(weightQuery);

            events::splineTerrain::ApplySplinePaintCommand paintCmd;
            paintCmd.splineSamples = samples;
            paintCmd.params = currentParams;
            anyApplied |= dispatcher.query(paintCmd);

            appliedWeights = dispatcher.query(weightQuery);
        }

        if (!anyApplied && !wantsMesh)
        {
            events::undoredo::EndBatchCommand endBatch;
            dispatcher.execute(endBatch);
            activePoints.clear();
            return;
        }

        // Record the spline whenever anything landed, not just for sculpt. A paint-only or
        // mesh-only apply registers no height layer, so deleteSpline is a no-op for heights —
        // right, since nothing deformed them.
        // VK-1647: re-authoring updates the existing record rather than appending a second one with
        // the same id. deleteSpline() and every lookup here resolve by first match, so a duplicate
        // would shadow the newer definition for the rest of the session.
        auto existing = std::find_if(appliedSplines.begin(), appliedSplines.end(),
            [splineId](const terrain::SplineData& s) { return s.id == splineId; });

        if (existing != appliedSplines.end())
        {
            existing->controlPoints = activePoints;
            existing->params = currentParams;
        }
        else
        {
            terrain::SplineData spline;
            spline.id = splineId;
            spline.controlPoints = activePoints;
            spline.params = currentParams;
            appliedSplines.push_back(std::move(spline));
        }

        auto undoCommand = std::make_shared<SplineApplyUndoCommand>(
            beginBatch.description, splineId, std::move(layerBefore), std::move(layerAfter),
            std::move(originalWeights), std::move(appliedWeights));
        if (undoCommand->hasSnapshots())
        {
            events::undoredo::PushUndoableCommand pushCmd;
            pushCmd.command = undoCommand;
            dispatcher.execute(pushCmd);
        }
        // The Editor builds the road mesh off this (Import.dll is Editor-only), which is why the
        // notification carries the points and params rather than just the id. It is dispatched
        // synchronously, so the road-entity undo command the Editor pushes joins THIS batch.
        events::splineTerrain::SplineAppliedNotification n;
        n.splineId = splineId;
        n.controlPoints = activePoints;
        n.params = currentParams;
        n.replacesEntityId = editingRoadEntityId;
        dispatcher.publish(n);

        events::undoredo::EndBatchCommand endBatch;
        dispatcher.execute(endBatch);

        activePoints.clear();
        editingRoadEntityId = 0;
        editingSplineId = 0;
    }

    void SplineTerrainServiceImpl::deleteSpline(uint64_t id)
    {
        // VK-1647: dispatched UNCONDITIONALLY, and deliberately not gated on `appliedSplines`.
        // That vector is session-only — it is appended to by finalizeSpline and nothing repopulates
        // it on load, while the layer stack itself comes back off the sidecar. Returning early when
        // the id was not applied in THIS session therefore made every layer from a previous session
        // undeletable, which breaks "delete produces identical results for identical ordered
        // definitions". The command is already a no-op when no layer carries the id.
        //
        // VK-1645: dropping the layer recomposes the tiles it covered from their authoritative
        // bases. Overlapping splines and ordinary sculpt edits made underneath both survive,
        // because neither was ever folded into the base.
        auto& dispatcher = events::EventDispatcher::instance();

        // VK-1648. Captured BEFORE the removal — afterwards the record is gone and with it the
        // only copy of the layer's parameters, affected set and stack position, so there would be
        // nothing left to restore from. This is the reason the undo entry is built here in Services
        // rather than by the Editor: the Editor does not link Terrain.dll and cannot hold a layer.
        events::splineTerrain::GetHeightLayerSnapshotQuery snapshotQuery;
        snapshotQuery.splineId = id;
        events::splineTerrain::HeightLayerSnapshot before = dispatcher.query(snapshotQuery);

        events::splineTerrain::RemoveSplineHeightLayerCommand removeCmd;
        removeCmd.splineId = id;
        dispatcher.execute(removeCmd);

        // Only once the layer is actually gone. If the removal was refused (editing locked) the
        // record is still in the stack, and an undo entry claiming otherwise would re-add a
        // duplicate id on redo.
        const bool removed = before.present && !dispatcher.query(snapshotQuery).present;
        if (removed)
        {
            auto undoCommand = std::make_shared<HeightLayerRecordUndoCommand>(
                "Delete Height Layer", std::move(before),
                events::splineTerrain::HeightLayerSnapshot{});

            events::undoredo::PushUndoableCommand pushCmd;
            pushCmd.command = undoCommand;
            dispatcher.execute(pushCmd);
        }

        auto it = std::find_if(appliedSplines.begin(), appliedSplines.end(),
            [id](const terrain::SplineData& s) { return s.id == id; });
        if (it != appliedSplines.end())
            appliedSplines.erase(it);
    }

}
