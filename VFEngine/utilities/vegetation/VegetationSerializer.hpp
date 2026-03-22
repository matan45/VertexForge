#pragma once

#include "VegetationTypes.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <array>

namespace vegetation
{
    static constexpr std::array<char, 4> VEGETATION_INSTANCE_MAGIC = {'V', 'F', 'V', 'I'};
    static constexpr uint32_t VEGETATION_INSTANCE_FORMAT_VERSION = 1;

    class VegetationSerializer
    {
    public:
        static bool saveBillboardInstances(const std::string& filePath,
                                            const std::vector<BillboardInstance>& instances);
        static bool loadBillboardInstances(const std::string& filePath,
                                            std::vector<BillboardInstance>& instances);
    };
}
