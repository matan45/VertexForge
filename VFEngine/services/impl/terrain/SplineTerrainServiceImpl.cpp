#include "SplineTerrainServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/SplineTerrainEvents.hpp"
#include "../../events/terrain/SplineTerrainUndoEvents.hpp"
#include "../../events/editor/UndoRedoEvents.hpp"
#include "../../data/SplineApplyUndoCommands.hpp"
#include "terrain/SplineSampling.hpp"

#include <glm/glm.hpp>
#include <algorithm>

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
        // not silently replace the road that was last opened for editing.
        editingRoadEntityId = 0;
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
        const uint64_t splineId = nextSplineId++;

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
        bool heightLayerRegistered = false;
        events::splineTerrain::SplineWeightSnapshot originalWeights;
        events::splineTerrain::SplineWeightSnapshot appliedWeights;

        // Sculpt strictly first: the road mesh conforms to the terrain as it stands after the
        // corridor is flattened, so running it later would drape the road over the old ground.
        if (wantsSculpt)
        {
            // VK-1645: no before/after height snapshots any more. The deform registers a reserved
            // layer over each covered tile's authoritative base, so undo/redo is a visibility
            // flip on that layer and the result is recomputed rather than replayed from bytes.
            events::splineTerrain::ApplySplineDeformCommand deformCmd;
            deformCmd.splineSamples = samples;
            deformCmd.params = currentParams;
            deformCmd.splineId = splineId;
            heightLayerRegistered = dispatcher.query(deformCmd);
            anyApplied |= heightLayerRegistered;
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
        terrain::SplineData spline;
        spline.id = splineId;
        spline.controlPoints = activePoints;
        spline.params = currentParams;
        appliedSplines.push_back(std::move(spline));

        auto undoCommand = std::make_shared<SplineApplyUndoCommand>(
            beginBatch.description, splineId, heightLayerRegistered,
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
    }

    void SplineTerrainServiceImpl::deleteSpline(uint64_t id)
    {
        auto it = std::find_if(appliedSplines.begin(), appliedSplines.end(),
            [id](const terrain::SplineData& s) { return s.id == id; });

        if (it == appliedSplines.end())
            return;

        // VK-1645: drop the reserved height layer and recompose the tiles it covered from their
        // authoritative bases. A no-op when this spline had no sculpt op. Overlapping splines and
        // ordinary sculpt edits made underneath both survive, because neither was ever folded
        // into the base.
        events::splineTerrain::RemoveSplineHeightLayerCommand removeCmd;
        removeCmd.splineId = id;
        events::EventDispatcher::instance().execute(removeCmd);

        appliedSplines.erase(it);
    }

}
