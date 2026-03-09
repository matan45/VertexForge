#pragma once

#include "VegetationDensityMap.hpp"
#include "VegetationPlacementData.hpp"
#include <string>
#include <cstdint>
#include <array>

namespace vegetation
{
    static constexpr std::array<char, 4> VEGETATION_DENSITY_MAGIC = {'V', 'F', 'V', 'D'};
    static constexpr std::array<char, 4> VEGETATION_PLACEMENT_MAGIC = {'V', 'F', 'V', 'P'};
    static constexpr uint32_t VEGETATION_FORMAT_VERSION = 1;

    // Sanity caps for file-read sizes to guard against corrupt data
    static constexpr uint32_t MAX_DENSITY_RESOLUTION = 512;
    static constexpr uint32_t MAX_PLACEMENT_INSTANCES = 1 << 20; // ~1M instances

    class VegetationSerializer
    {
    public:
        // Save vegetation density map for a tile to binary file
        static bool saveDensityMap(const std::string& filePath, const VegetationDensityMap& densityMap);
        static bool loadDensityMap(const std::string& filePath, VegetationDensityMap& densityMap);

        // Save vegetation placement data for a tile to binary file
        static bool savePlacementData(const std::string& filePath, const VegetationPlacementData& placement);
        static bool loadPlacementData(const std::string& filePath, VegetationPlacementData& placement);
    };
}
