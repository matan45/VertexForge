#pragma once
// VK-1575: per-tile spatial hash for foliage instances. Thin wrapper over the shared
// spatial::SpatialHashGrid (also backs vegetation::VegetationSpatialGrid), keyed by the foliage
// typeIndex instead of the billboard paletteEntryIndex. Used for spacing rejection during a stroke
// and for radius queries during erase.
#include "../spatial/SpatialHashGrid.hpp"
#include <cstdint>
#include <glm/glm.hpp>

namespace foliage
{
    struct FoliageSpatialEntry
    {
        uint32_t  instanceIndex = 0;   // index into the tile's foliageInstances vector
        glm::vec3 position{0.0f};
        uint16_t  typeIndex = 0;       // foliage type (for select/erase-by-type)
    };

    // insert(index, position, typeIndex); queryRadius returns FoliageSpatialEntry (reads .typeIndex).
    class FoliageSpatialGrid : public spatial::SpatialHashGrid<FoliageSpatialEntry, uint16_t>
    {
    };
}
