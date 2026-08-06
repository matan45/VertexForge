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

        // VK-1647: a FALLBACK only. Ids are normally reserved from the terrain's layer store
        // (ReserveHeightLayerIdCommand), whose watermark is persisted in the sidecar and therefore
        // survives a reload; this counter covers the one case that store cannot serve — a paint- or
        // mesh-only spline applied with no terrain loaded, which registers no height layer at all.
        // It is kept at or above every id actually issued, so the two sources cannot collide.
        uint64_t nextSplineId = 1;

        // VK-1621: the road entity the active spline was loaded from, so applying replaces it
        // rather than spawning a second road on top of the first. 0 = authoring a new spline.
        uint64_t editingRoadEntityId = 0;

        // VK-1647: the height layer that road already owns. Re-applying reuses this id so the
        // corridor is UPDATED in place instead of a second layer being stacked on top of the first
        // — which used to carve the terrain twice and left the old corridor composing forever.
        // 0 = allocate a fresh id.
        uint64_t editingSplineId = 0;

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
