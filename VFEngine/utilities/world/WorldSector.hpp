#pragma once

#include "WorldTypes.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace world
{
    struct WorldSector
    {
        SectorCoord coord;
        SectorState state = SectorState::Unloaded;
        std::vector<uint64_t> entityUUIDs;
        bool dirty = false;
        std::string filePath;
    };

} // namespace world
