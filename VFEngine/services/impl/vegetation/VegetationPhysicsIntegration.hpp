#pragma once

#include "vegetation/VegetationPlacementData.hpp"
#include "vegetation/VegetationSpeciesRegistry.hpp"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace services
{
    class VegetationPhysicsIntegration
    {
    public:
        VegetationPhysicsIntegration() = default;

        // Register collision bodies for tree instances in a tile
        void onTileLoaded(int32_t coordX, int32_t coordZ,
                          const vegetation::VegetationPlacementData& placement,
                          const vegetation::VegetationSpeciesRegistry& registry);

        // Remove collision bodies when tile unloads
        void onTileUnloaded(int32_t coordX, int32_t coordZ);

        void clear();

    private:
        struct TileKey
        {
            int32_t x, z;
            bool operator==(const TileKey& other) const = default;
        };

        struct TileKeyHash
        {
            size_t operator()(const TileKey& k) const
            {
                return std::hash<int32_t>{}(k.x) ^ (std::hash<int32_t>{}(k.z) << 16);
            }
        };

        // Track which bodies were created per tile for cleanup
        std::unordered_map<TileKey, std::vector<uint32_t>, TileKeyHash> tileBodyIds;
    };
}
