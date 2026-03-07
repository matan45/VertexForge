#pragma once

#include "WorldTypes.hpp"
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

        std::unordered_map<SectorCoord, std::string, SectorCoordHash> sectorFilePaths;
    };

} // namespace world
