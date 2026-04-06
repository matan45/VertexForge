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

    public:
        SplineTerrainServiceImpl() = default;
        ~SplineTerrainServiceImpl() override = default;

        void registerEventHandlers() override;

    private:
        void addPoint(const glm::vec3& pos);
        void removeLastPoint();
        void clearActiveSpline();
        void finalizeSpline();
        void deleteSpline(uint64_t id);

        // Catmull-Rom evaluation
        glm::vec3 evaluateCatmullRom(const glm::vec3& p0, const glm::vec3& p1,
                                      const glm::vec3& p2, const glm::vec3& p3, float t) const;

        std::vector<glm::vec3> sampleSplineCurve(const std::vector<terrain::SplineControlPoint>& points,
                                                  float stepSize) const;

        void applySplineToTerrain(terrain::SplineData& spline);
        void restoreOriginalHeights(const terrain::SplineData& spline);
    };
}
