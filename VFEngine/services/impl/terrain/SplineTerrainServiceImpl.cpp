#include "SplineTerrainServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/SplineTerrainEvents.hpp"
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
    }

    void SplineTerrainServiceImpl::addPoint(const glm::vec3& pos)
    {
        activePoints.push_back({pos});

        events::splineTerrain::SplinePointCountChangedNotification n;
        n.pointCount = static_cast<uint32_t>(activePoints.size());
        events::EventDispatcher::instance().publish(n);
    }

    void SplineTerrainServiceImpl::removeLastPoint()
    {
        if (!activePoints.empty())
        {
            activePoints.pop_back();

            events::splineTerrain::SplinePointCountChangedNotification n;
            n.pointCount = static_cast<uint32_t>(activePoints.size());
            events::EventDispatcher::instance().publish(n);
        }
    }

    void SplineTerrainServiceImpl::clearActiveSpline()
    {
        activePoints.clear();
    }

    void SplineTerrainServiceImpl::finalizeSpline()
    {
        if (activePoints.size() < 2)
            return;

        auto& dispatcher = events::EventDispatcher::instance();

        auto samples = terrain::sampleSplineCurve(activePoints, 0.5f);
        if (samples.size() < 2)
            return;

        bool success = false;

        if (currentParams.mode == terrain::SplineMode::Paint)
        {
            // Paint mode: paint material layer along spline
            events::splineTerrain::ApplySplinePaintCommand paintCmd;
            paintCmd.splineSamples = samples;
            paintCmd.params = currentParams;
            success = dispatcher.query(paintCmd);
        }
        else
        {
            // Sculpt mode: deform terrain height
            float totalHalfWidth = currentParams.corridorWidth + currentParams.falloffWidth;
            events::splineTerrain::GetSplineOriginalHeightsQuery heightQuery;
            heightQuery.splineSamples = samples;
            heightQuery.totalHalfWidth = totalHalfWidth;
            auto originalHeights = dispatcher.query(heightQuery);

            events::splineTerrain::ApplySplineDeformCommand deformCmd;
            deformCmd.splineSamples = samples;
            deformCmd.params = currentParams;
            deformCmd.splineId = nextSplineId;
            success = dispatcher.query(deformCmd);

            if (success)
            {
                terrain::SplineData spline;
                spline.id = nextSplineId++;
                spline.controlPoints = activePoints;
                spline.params = currentParams;
                spline.originalHeights = std::move(originalHeights);
                appliedSplines.push_back(std::move(spline));
            }
        }

        if (success)
        {
            events::splineTerrain::SplineAppliedNotification n;
            n.splineId = nextSplineId - 1;
            dispatcher.publish(n);
        }

        activePoints.clear();
    }

    void SplineTerrainServiceImpl::deleteSpline(uint64_t id)
    {
        auto it = std::find_if(appliedSplines.begin(), appliedSplines.end(),
            [id](const terrain::SplineData& s) { return s.id == id; });

        if (it == appliedSplines.end())
            return;

        // Restore original heights
        events::splineTerrain::RestoreSplineHeightsCommand restoreCmd;
        restoreCmd.splineId = id;
        restoreCmd.originalHeights = it->originalHeights;
        events::EventDispatcher::instance().execute(restoreCmd);

        appliedSplines.erase(it);
    }

}
