#pragma once

#include "../../interfaces/terrain/ISplineTerrainService.hpp"
#include "../../events/EventTypes.hpp"
#include "terrain/SplineTypes.hpp"

#include <vector>
#include <memory>

namespace terrain
{
    class TerrainGrid;
}

namespace services
{
    class SplineTerrainServiceImpl : public ISplineTerrainService
    {
    private:
        bool splineModeActive = false;
        terrain::SplineParams currentParams;
        std::vector<terrain::SplineControlPoint> activePoints;
        std::vector<terrain::SplineData> appliedSplines;
        uint64_t nextSplineId = 1;

        // VK-1621: the road entity the active spline was loaded from, so applying replaces it
        // rather than spawning a second road on top of the first. 0 = authoring a new spline.
        uint64_t editingRoadEntityId = 0;

    public:
        SplineTerrainServiceImpl() = default;
        ~SplineTerrainServiceImpl() override = default;

        void registerEventHandlers() override;

    private:
        void addPoint(const glm::vec3& pos);
        void removeLastPoint();
        bool setPoint(uint32_t index, const glm::vec3& pos);
        bool removePoint(uint32_t index);
        void publishPointCount();
        void clearActiveSpline();
        void finalizeSpline();
        void deleteSpline(uint64_t id);
    };
}
