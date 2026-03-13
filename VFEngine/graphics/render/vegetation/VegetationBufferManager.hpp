#pragma once

#include "VegetationGPUTypes.hpp"
#include <vector>
#include <unordered_map>

namespace core
{
    class Device;
}

namespace render::vegetation
{
    struct VegetationTileKey
    {
        int32_t coordX = 0;
        int32_t coordZ = 0;

        bool operator==(const VegetationTileKey& other) const = default;
    };

    struct VegetationTileKeyHash
    {
        size_t operator()(const VegetationTileKey& key) const
        {
            // Bit masking handles negative coordinates correctly
            uint64_t x = static_cast<uint32_t>(key.coordX);
            uint64_t z = static_cast<uint32_t>(key.coordZ);
            return std::hash<uint64_t>()((x << 32) | z);
        }
    };

    struct VegetationTileAllocation
    {
        VegetationTileKey key;
        uint32_t grassInstanceOffset = 0;
        uint32_t grassInstanceCount = 0;
        bool isUploaded = false;
    };

    class VegetationBufferManager
    {
    public:
        void init(core::Device& device);
        void cleanup();

        // Grass instance management
        VegetationTileAllocation allocateGrassInstances(const VegetationTileKey& key, uint32_t count);
        void freeGrassInstances(const VegetationTileKey& key);

        // Free all allocations for a tile
        void freeTile(const VegetationTileKey& key);

        // Query
        [[nodiscard]] bool hasTile(const VegetationTileKey& key) const;
        [[nodiscard]] const VegetationTileAllocation* getTileAllocation(const VegetationTileKey& key) const;

        // Build GPU tile data array for shader consumption
        [[nodiscard]] std::vector<VegetationTileGPUData> buildGPUTileData() const;

        // Statistics
        [[nodiscard]] uint32_t getTotalGrassInstances() const { return totalGrassInstances; }
        [[nodiscard]] size_t getActiveTileCount() const { return tileAllocations.size(); }

    private:
        std::unordered_map<VegetationTileKey, VegetationTileAllocation, VegetationTileKeyHash> tileAllocations;

        uint32_t nextGrassOffset = 0;
        uint32_t totalGrassInstances = 0;

        core::Device* devicePtr = nullptr;
    };
}
