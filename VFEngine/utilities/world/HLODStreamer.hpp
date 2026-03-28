#pragma once

#include "HLODTypes.hpp"
#include "WorldTypes.hpp"
#include "WorldSectorManager.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <unordered_set>
#include <unordered_map>

namespace world
{
    struct HLODStreamingAction
    {
        HLODCellCoord cellCoord;
        bool isLoad = true;
    };

    class HLODStreamer
    {
    public:
        void setConfig(const SectorStreamingConfig& streamConfig, const HLODConfig& hlodConfig);

        void update(
            const std::vector<StreamingSource>& sources,
            const WorldSectorManager& manager,
            const SectorConfig& sectorConfig,
            std::vector<HLODStreamingAction>& outActions);

        void clear();

        const std::unordered_set<HLODCellCoord, HLODCellCoordHash>& getLoadedProxies() const
        {
            return loadedProxies;
        }

    private:
        SectorStreamingConfig streamConfig;
        HLODConfig hlodConfig;

        std::unordered_set<HLODCellCoord, HLODCellCoordHash> loadedProxies;

        // Check if any sector covered by this cell is currently loaded
        bool anySectorLoaded(const HLODCellCoord& cell, const HLODTierConfig& tier,
                             const WorldSectorManager& manager) const;

        HLODCellCoord sectorToCell(const SectorCoord& coord, uint8_t cellSize) const;

        float cellDistanceSq(const HLODCellCoord& cell, uint8_t cellSize,
                             const glm::vec3& pos, float sectorWorldSize) const;
    };

} // namespace world
