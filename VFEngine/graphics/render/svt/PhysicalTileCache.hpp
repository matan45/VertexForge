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
        core::Device& device;
        SVTConfig config;

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

        ChannelCache albedoCache;
        ChannelCache normalCache;
        ChannelCache ormCache;
        ChannelCache emissionCache;
        ChannelCache heightCache;

        // CPU-side tile tracking
        std::vector<PhysicalTileInfo> tileSlots;
        std::vector<uint32_t> freeList;

        // Staging buffer for tile uploads
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;
        void* stagingMapped = nullptr;
        size_t stagingBufferSize = 0;

        vk::CommandPool commandPool;

        bool initialized = false;

    public:
        explicit PhysicalTileCache(core::Device& device);
        ~PhysicalTileCache();

        PhysicalTileCache(const PhysicalTileCache&) = delete;
        PhysicalTileCache& operator=(const PhysicalTileCache&) = delete;

        void init(const SVTConfig& cfg);
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

        // Batched upload: all channels for one tile in a single command buffer submit.
        // channelData[i] = {pointer, size} for channel i. Null pointer = skip channel.
        struct ChannelUploadData { const void* data; uint32_t size; };
        void uploadTileBatched(uint32_t tileIndex,
                               const ChannelUploadData channels[SVT_CHANNEL_COUNT]);

        // Flush all pending uploads (must call before rendering)
        void flushUploads();

        // Getters for bindless registration
        vk::ImageView getAlbedoView() const { return albedoCache.view; }
        vk::ImageView getNormalView() const { return normalCache.view; }
        vk::ImageView getORMView() const { return ormCache.view; }
        vk::ImageView getEmissionView() const { return emissionCache.view; }
        vk::ImageView getHeightView() const { return heightCache.view; }

        vk::Sampler getAlbedoSampler() const { return albedoCache.sampler; }
        vk::Sampler getNormalSampler() const { return normalCache.sampler; }
        vk::Sampler getORMSampler() const { return ormCache.sampler; }
        vk::Sampler getEmissionSampler() const { return emissionCache.sampler; }
        vk::Sampler getHeightSampler() const { return heightCache.sampler; }

        void setAlbedoBindlessIndex(uint32_t idx) { albedoCache.bindlessIndex = idx; }
        void setNormalBindlessIndex(uint32_t idx) { normalCache.bindlessIndex = idx; }
        void setORMBindlessIndex(uint32_t idx) { ormCache.bindlessIndex = idx; }
        void setEmissionBindlessIndex(uint32_t idx) { emissionCache.bindlessIndex = idx; }
        void setHeightBindlessIndex(uint32_t idx) { heightCache.bindlessIndex = idx; }

        uint32_t getAlbedoBindlessIndex() const { return albedoCache.bindlessIndex; }
        uint32_t getNormalBindlessIndex() const { return normalCache.bindlessIndex; }
        uint32_t getORMBindlessIndex() const { return ormCache.bindlessIndex; }
        uint32_t getEmissionBindlessIndex() const { return emissionCache.bindlessIndex; }
        uint32_t getHeightBindlessIndex() const { return heightCache.bindlessIndex; }

        const PhysicalTileInfo& getTileInfo(uint32_t tileIndex) const { return tileSlots[tileIndex]; }
        uint32_t getTileCount() const { return config.physicalTileCount; }
        uint32_t getFreeTileCount() const { return static_cast<uint32_t>(freeList.size()); }

        bool isInitialized() const { return initialized; }

    private:
        void createChannelCache(ChannelCache& cache, vk::Format format, const char* debugName);
        void createCacheImage(ChannelCache& cache, vk::Format format);
        void createCacheSampler(ChannelCache& cache);
        void transitionCacheLayout(ChannelCache& cache);

        void destroyChannelCache(ChannelCache& cache);
        void createStagingBuffer();

        void uploadToLayer(ChannelCache& cache, uint32_t layer,
                           const void* data, uint32_t dataSize);
        void prepareUploadBarrier(vk::CommandBuffer cmd, ChannelCache& cache, uint32_t layer);
        void copyBufferToImage(vk::CommandBuffer cmd, ChannelCache& cache, uint32_t layer);
        void finalizeUploadBarrier(vk::CommandBuffer cmd, ChannelCache& cache, uint32_t layer);
    };
}
