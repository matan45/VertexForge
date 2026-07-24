#pragma once

// Per-tile spatial hash for vegetation billboards. Thin wrapper over the shared
// spatial::SpatialHashGrid (also backs foliage::FoliageSpatialGrid) that adds the vegetation-only
// helpers: rebuild-from-BillboardInstance and layer-aware neighbour avoidance.
#include "../spatial/SpatialHashGrid.hpp"
#include "VegetationTypes.hpp"
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace vegetation
{
    struct VegSpatialEntry
    {
        uint32_t instanceIndex = 0;
        glm::vec3 position;
        uint32_t paletteEntryIndex = 0; // Layer this instance belongs to (for layer-aware avoidance)
    };

    class VegetationSpatialGrid : public spatial::SpatialHashGrid<VegSpatialEntry, uint32_t>
    {
    public:
        void rebuild(const std::vector<BillboardInstance>& instances)
        {
            clear();
            for (uint32_t i = 0; i < static_cast<uint32_t>(instances.size()); ++i)
                insert(i, instances[i].position, instances[i].paletteEntryIndex);
        }

        // True if any neighbor within minDist belongs to a layer other than 'layer'.
        bool hasNeighborOfOtherLayer(const glm::vec3& position, float minDist, uint32_t layer) const
        {
            return hasNeighborMatching(position, minDist,
                [layer](const VegSpatialEntry& e) { return e.paletteEntryIndex != layer; });
        }
    };
}
