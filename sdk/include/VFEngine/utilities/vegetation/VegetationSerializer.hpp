#pragma once

#include "VegetationTypes.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <array>

namespace vegetation
{
    static constexpr std::array<char, 4> VEGETATION_INSTANCE_MAGIC = {'V', 'F', 'V', 'I'};
    // v2: added per-instance heightScale, tint, and surface normal.
    static constexpr uint32_t VEGETATION_INSTANCE_FORMAT_VERSION = 2;

    class VegetationSerializer
    {
    public:
        static bool saveBillboardInstances(const std::string& filePath,
                                            const std::vector<BillboardInstance>& instances);
        static bool loadBillboardInstances(const std::string& filePath,
                                            std::vector<BillboardInstance>& instances);

        static bool saveBillboardPalette(const std::string& filePath,
                                          const std::vector<BillboardPaletteEntry>& palette);
        static bool loadBillboardPalette(const std::string& filePath,
                                          std::vector<BillboardPaletteEntry>& palette);
    };
}
