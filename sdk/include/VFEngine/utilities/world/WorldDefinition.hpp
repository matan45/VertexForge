#pragma once

#include "WorldTypes.hpp"
#include "HLODTypes.hpp"
#include <string>
#include <unordered_map>

namespace world
{
    struct WorldDefinition
    {
        std::string name;
        std::string terrainPath;
        std::string waterDefinitionPath;

        SectorConfig sectorConfig;
        SectorStreamingConfig streamingConfig;
        HLODConfig hlodConfig;

        std::unordered_map<SectorCoord, std::string, SectorCoordHash> sectorFilePaths;

        // VK-1594: baked HLOD proxy path per cell, across every tier. Tier 0 cells are also
        // recorded here, but a world saved before VK-1594 has no "hlodCells" key at all - the
        // streamer then falls back to the tier-0 filename convention on WorldSector::hlodFilePath,
        // so old worlds keep resolving without a format bump.
        std::unordered_map<HLODCellCoord, std::string, HLODCellCoordHash> hlodCells;
    };

} // namespace world
