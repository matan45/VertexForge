#pragma once

#include "vegetation/VegetationPlacementData.hpp"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace services
{
    class IPhysicsProvider;
    class IVegetationProvider;

    class VegetationPhysicsIntegration
    {
    public:
        VegetationPhysicsIntegration() = default;

        void setPhysicsProvider(IPhysicsProvider* provider) { physicsProvider = provider; }
        void setVegetationProvider(IVegetationProvider* provider) { vegetationProvider = provider; }

        // Register collision bodies for tree instances in a tile
        void onTileLoaded(int32_t coordX, int32_t coordZ,
                          const vegetation::VegetationPlacementData& placement);

        // Remove collision bodies when tile unloads
        void onTileUnloaded(int32_t coordX, int32_t coordZ);

        void clear();

    private:
        IPhysicsProvider* physicsProvider = nullptr;
        IVegetationProvider* vegetationProvider = nullptr;

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

        // Track which tiles have colliders
        std::unordered_map<TileKey, bool, TileKeyHash> activeTiles;
    };
}
