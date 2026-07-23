#pragma once
// VK-1575: per-tile foliage sidecar IO. Mirrors VegetationSerializer:
//  - instances: binary VFFI blob (magic + version + recordSize + count + bulk 56B records).
//    FoliageInstance is a frozen, trivially-copyable 56-byte POD, so the record blob is a
//    single bulk read/write (no field-by-field loop like the billboard v2 format needs).
//  - palette: JSON (FoliageType carries std::string members + ~25 fields).
#include "FoliageTypes.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <array>

namespace foliage
{
    static constexpr std::array<char, 4> FOLIAGE_INSTANCE_MAGIC = {'V', 'F', 'F', 'I'};
    static constexpr uint32_t FOLIAGE_INSTANCE_FORMAT_VERSION = 1;

    class FoliageSerializer
    {
    public:
        static bool saveFoliageInstances(const std::string& filePath,
                                         const std::vector<FoliageInstance>& instances);
        static bool loadFoliageInstances(const std::string& filePath,
                                         std::vector<FoliageInstance>& instances);

        static bool saveFoliagePalette(const std::string& filePath,
                                       const std::vector<FoliageType>& palette);
        static bool loadFoliagePalette(const std::string& filePath,
                                       std::vector<FoliageType>& palette);
    };
}
