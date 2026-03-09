#pragma once

#include "VegetationDensityMap.hpp"
#include <string>
#include <cstdint>
#include <array>

namespace vegetation
{
    static constexpr std::array<char, 4> VEGETATION_DENSITY_MAGIC = {'V', 'F', 'V', 'D'};
    static constexpr uint32_t VEGETATION_FORMAT_VERSION = 1;

    // Sanity caps for file-read sizes to guard against corrupt data
    static constexpr uint32_t MAX_DENSITY_RESOLUTION = 512;

    class VegetationSerializer
    {
    public:
        // Save vegetation density map for a tile to binary file
        static bool saveDensityMap(const std::string& filePath, const VegetationDensityMap& densityMap);
        static bool loadDensityMap(const std::string& filePath, VegetationDensityMap& densityMap);
    };
}
