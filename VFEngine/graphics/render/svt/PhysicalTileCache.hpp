#pragma once

#include "SVTTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>
#include <cstdint>

namespace core
{
    class Device;
    class DeferredDeletionQueue;
}

namespace render::svt
{
    // Manages three physical tile cache texture arrays (albedo, normal, ORM)
    // and a free-list / LRU allocator for tile slots.
    class PhysicalTileCache
    {
    private:
        core::Device& device_;
        SVTConfig config_;

        // Per-channel cache images (sampler2DArray)
        struct ChannelCache
        {
            vk::Image image;
            vk::DeviceMemory memory;
            vk::ImageView view;
            vk::Sampler sampler;
            vk::Format format = vk::Format::eUndefined;
            uint32_t bindlessIndex = 0;
        };

        ChannelCache albedoCache_;
        ChannelCache normalCache_;
        ChannelCache ormCache_;
        ChannelCache emissionCache_;
        ChannelCache heightCache_;

        // CPU-side tile tracking
        std::vector<PhysicalTileInfo> tileSlots_;
        std::vector<uint32_t> freeList_;

        // Staging buffer for tile uploads
        vk::Buffer stagingBuffer_;
        vk::DeviceMemory stagingMemory_;
        void* stagingMapped_ = nullptr;
        size_t stagingBufferSize_ = 0;

        vk::CommandPool commandPool_;

        bool initialized_ = false;

    public:
        explicit PhysicalTileCache(core::Device& device);
        ~PhysicalTileCache();

        PhysicalTileCache(const PhysicalTileCache&) = delete;
        PhysicalTileCache& operator=(const PhysicalTileCache&) = delete;

        void init(const SVTConfig& config);
        void cleanup();

        // Allocate a physical tile slot. Returns tile index or SVT_INVALID_TILE if full.
        // When full, caller should call evictLRU() first.
        uint32_t allocateTile();

        // Free a tile slot back to the pool
        void freeTile(uint32_t tileIndex);

        // Evict the least recently used tile. Returns the evicted tile index, or SVT_INVALID_TILE.
        uint32_t evictLRU(uint64_t currentFrame);

        // Mark a tile as used this frame (for LRU tracking)
        void touchTile(uint32_t tileIndex, uint64_t frame);

        // Set the virtual coord mapping for a physical tile
        void setTileMapping(uint32_t tileIndex, const VirtualTileCoord& virtualCoord);

        // Upload compressed tile data (BC7) to a specific channel and tile slot
        void uploadTileData(uint32_t tileIndex, uint32_t channelIndex,
                            const void* compressedData, uint32_t dataSize);

        // Flush all pending uploads (must call before rendering)
        void flushUploads();

        // Getters for bindless registration
        vk::ImageView getAlbedoView() const { return albedoCache_.view; }
        vk::ImageView getNormalView() const { return normalCache_.view; }
        vk::ImageView getORMView() const { return ormCache_.view; }
        vk::ImageView getEmissionView() const { return emissionCache_.view; }
        vk::ImageView getHeightView() const { return heightCache_.view; }

        vk::Sampler getAlbedoSampler() const { return albedoCache_.sampler; }
        vk::Sampler getNormalSampler() const { return normalCache_.sampler; }
        vk::Sampler getORMSampler() const { return ormCache_.sampler; }
        vk::Sampler getEmissionSampler() const { return emissionCache_.sampler; }
        vk::Sampler getHeightSampler() const { return heightCache_.sampler; }

        void setAlbedoBindlessIndex(uint32_t idx) { albedoCache_.bindlessIndex = idx; }
        void setNormalBindlessIndex(uint32_t idx) { normalCache_.bindlessIndex = idx; }
        void setORMBindlessIndex(uint32_t idx) { ormCache_.bindlessIndex = idx; }
        void setEmissionBindlessIndex(uint32_t idx) { emissionCache_.bindlessIndex = idx; }
        void setHeightBindlessIndex(uint32_t idx) { heightCache_.bindlessIndex = idx; }

        uint32_t getAlbedoBindlessIndex() const { return albedoCache_.bindlessIndex; }
        uint32_t getNormalBindlessIndex() const { return normalCache_.bindlessIndex; }
        uint32_t getORMBindlessIndex() const { return ormCache_.bindlessIndex; }
        uint32_t getEmissionBindlessIndex() const { return emissionCache_.bindlessIndex; }
        uint32_t getHeightBindlessIndex() const { return heightCache_.bindlessIndex; }

        const PhysicalTileInfo& getTileInfo(uint32_t tileIndex) const { return tileSlots_[tileIndex]; }
        uint32_t getTileCount() const { return config_.physicalTileCount; }
        uint32_t getFreeTileCount() const { return static_cast<uint32_t>(freeList_.size()); }

        bool isInitialized() const { return initialized_; }

    private:
        void createChannelCache(ChannelCache& cache, vk::Format format, const char* debugName);
        void destroyChannelCache(ChannelCache& cache);
        void createStagingBuffer();
        void uploadToLayer(ChannelCache& cache, uint32_t layer,
                           const void* data, uint32_t dataSize);
    };
}
