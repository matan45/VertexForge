#include "VegetationBufferManager.hpp"

namespace render::vegetation
{
    void VegetationBufferManager::init(core::Device& device)
    {
        devicePtr = &device;
        tileAllocations.clear();
        nextGrassOffset = 0;
        nextTreeOffset = 0;
        totalGrassInstances = 0;
        totalTreeInstances = 0;
    }

    void VegetationBufferManager::cleanup()
    {
        tileAllocations.clear();
        nextGrassOffset = 0;
        nextTreeOffset = 0;
        totalGrassInstances = 0;
        totalTreeInstances = 0;
        devicePtr = nullptr;
    }

    VegetationTileAllocation VegetationBufferManager::allocateGrassInstances(
        const VegetationTileKey& key, uint32_t count)
    {
        auto& alloc = tileAllocations[key];
        alloc.key = key;
        alloc.grassInstanceOffset = nextGrassOffset;
        alloc.grassInstanceCount = count;
        alloc.isUploaded = false;

        nextGrassOffset += count;
        totalGrassInstances += count;

        return alloc;
    }

    void VegetationBufferManager::freeGrassInstances(const VegetationTileKey& key)
    {
        auto it = tileAllocations.find(key);
        if (it == tileAllocations.end())
            return;

        totalGrassInstances -= it->second.grassInstanceCount;
        it->second.grassInstanceOffset = 0;
        it->second.grassInstanceCount = 0;
        it->second.isUploaded = false;

        // Remove tile entry if both grass and tree allocations are empty
        if (it->second.treeInstanceCount == 0)
        {
            tileAllocations.erase(it);
        }
    }

    VegetationTileAllocation VegetationBufferManager::allocateTreeInstances(
        const VegetationTileKey& key, uint32_t count)
    {
        auto& alloc = tileAllocations[key];
        alloc.key = key;
        alloc.treeInstanceOffset = nextTreeOffset;
        alloc.treeInstanceCount = count;
        alloc.isUploaded = false;

        nextTreeOffset += count;
        totalTreeInstances += count;

        return alloc;
    }

    void VegetationBufferManager::freeTreeInstances(const VegetationTileKey& key)
    {
        auto it = tileAllocations.find(key);
        if (it == tileAllocations.end())
            return;

        totalTreeInstances -= it->second.treeInstanceCount;
        it->second.treeInstanceOffset = 0;
        it->second.treeInstanceCount = 0;
        it->second.isUploaded = false;

        // Remove tile entry if both grass and tree allocations are empty
        if (it->second.grassInstanceCount == 0)
        {
            tileAllocations.erase(it);
        }
    }

    void VegetationBufferManager::freeTile(const VegetationTileKey& key)
    {
        auto it = tileAllocations.find(key);
        if (it == tileAllocations.end())
            return;

        totalGrassInstances -= it->second.grassInstanceCount;
        totalTreeInstances -= it->second.treeInstanceCount;
        tileAllocations.erase(it);
    }

    bool VegetationBufferManager::hasTile(const VegetationTileKey& key) const
    {
        return tileAllocations.contains(key);
    }

    const VegetationTileAllocation* VegetationBufferManager::getTileAllocation(
        const VegetationTileKey& key) const
    {
        auto it = tileAllocations.find(key);
        if (it == tileAllocations.end())
            return nullptr;
        return &it->second;
    }

    std::vector<VegetationTileGPUData> VegetationBufferManager::buildGPUTileData() const
    {
        std::vector<VegetationTileGPUData> result;
        result.reserve(tileAllocations.size());

        for (const auto& [key, alloc] : tileAllocations)
        {
            VegetationTileGPUData gpuData{};
            gpuData.tileCoord = glm::ivec2(key.coordX, key.coordZ);
            gpuData.grassInstanceOffset = alloc.grassInstanceOffset;
            gpuData.grassInstanceCount = alloc.grassInstanceCount;
            gpuData.treeInstanceOffset = alloc.treeInstanceOffset;
            gpuData.treeInstanceCount = alloc.treeInstanceCount;
            // boundingSphere, aabbMin, aabbMax left at zero - will be populated
            // when actual geometry data is available during upload
            result.push_back(gpuData);
        }

        return result;
    }
}
