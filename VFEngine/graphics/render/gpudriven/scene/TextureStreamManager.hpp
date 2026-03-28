#pragma once

#include "StreamableTexture.hpp"
#include "../GPUDrivenTypes.hpp"
#include "resource/Types.hpp"
#include "resource/TextureStreamHandle.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <future>
#include <memory>
#include <mutex>
#include <set>

namespace core
{
    class Device;
    class DeferredDeletionQueue;
}

namespace render::gpudriven
{
    class BindlessTextureManager;

    struct TextureStreamConfig
    {
        size_t vramBudgetBytes = 1024 * 1024 * 1024;       // 1 GB
        size_t maxBytesPerFrame = 8 * 1024 * 1024;          // 8 MB
        uint32_t maxUploadsPerFrame = 16;
        uint32_t tailMipCount = 2;                           // Mips loaded immediately
        float evictionThreshold = 0.9f;                      // Start evicting at 90%
        float evictionTarget = 0.8f;                         // Evict down to 80%
    };

    struct TextureStreamStats
    {
        uint32_t totalRegistered = 0;
        uint32_t fullyLoaded = 0;
        uint32_t partiallyLoaded = 0;
        uint32_t pendingReads = 0;
        uint32_t pendingUploads = 0;
        uint32_t uploadsThisFrame = 0;
        size_t bytesUploadedThisFrame = 0;
        size_t vramUsedBytes = 0;
        size_t vramBudgetBytes = 0;
        uint32_t evictionsThisFrame = 0;
    };

    struct TextureMipReadResult
    {
        std::string path;
        uint32_t mipLevel = 0;
        resource::MipLevelData mipData;
        bool success = false;
    };

    struct TextureStreamPriorityEntry
    {
        std::string path;
        uint32_t targetMip;
        float priority;

        bool operator<(const TextureStreamPriorityEntry& other) const
        {
            return priority < other.priority;
        }
    };

    class TextureStreamManager
    {
    private:
        core::Device& device;
        BindlessTextureManager& bindlessTextures;
        core::DeferredDeletionQueue* deletionQueue = nullptr;

        TextureStreamConfig config;
        TextureStreamStats stats;

        // Streamable textures indexed by path
        std::unordered_map<std::string, StreamableTexture> textures;

        // Per-texture stream handles for file I/O
        std::unordered_map<std::string, std::unique_ptr<resource::TextureStreamHandle>> streamHandles;

        // Async read futures
        std::vector<std::future<TextureMipReadResult>> pendingReads;

        // Completed reads waiting for GPU upload
        std::vector<TextureMipReadResult> uploadQueue;
        std::mutex uploadQueueMutex;

        // Staging buffer for GPU uploads
        vk::Buffer stagingBuffer;
        core::VulkanAllocation stagingAllocation;
        void* stagingMapped = nullptr;
        size_t stagingBufferSize = 0;

        vk::CommandPool commandPool;

        // Track in-flight async reads to prevent duplicate submissions
        std::set<std::pair<std::string, uint32_t>> inFlightReads;

        uint64_t currentFrame = 0;
        size_t currentVRAMUsage = 0;

    public:
        TextureStreamManager(core::Device& device, BindlessTextureManager& bindlessMgr);
        ~TextureStreamManager();

        TextureStreamManager(const TextureStreamManager&) = delete;
        TextureStreamManager& operator=(const TextureStreamManager&) = delete;

        void init();
        void cleanup();

        void setConfig(const TextureStreamConfig& cfg) { config = cfg; }
        void setDeletionQueue(core::DeferredDeletionQueue* queue) { deletionQueue = queue; }

        // Register a texture for streaming. Returns the bindless index immediately.
        // Only tail mips are loaded; higher mips stream in on demand.
        uint32_t registerTexture(const std::string& path, vk::Format format);

        // Per-frame update: process reads, upload, update priorities, submit new reads, evict
        void update(const glm::vec3& cameraPos, uint64_t frameIndex);

        // Get the bindless index for a path, or INVALID_TEXTURE_INDEX if not registered
        uint32_t getTextureIndex(const std::string& path) const;

        // Check if a texture is registered for streaming
        bool isRegistered(const std::string& path) const;

        // Update the minimum distance to camera for a texture (call per-object per-frame)
        void updateTextureDistance(const std::string& path, float distance);

        // Reset all texture distances to max before per-frame distance updates
        void resetDistances();

        const TextureStreamStats& getStats() const { return stats; }

        void clear();

    private:
        void createStagingBuffer(size_t size);

        // Create the Vulkan image with full mip chain, upload tail mips
        bool createStreamableImage(StreamableTexture& tex, const resource::TextureStreamHeader& header,
                                    const std::vector<resource::MipLevelData>& tailMips);

        // Create a sampler with the specified minLod
        vk::Sampler createMipClampedSampler(uint32_t minLod, uint32_t maxLod);

        // Update the sampler and bindless descriptor after mip upload
        void updateSamplerAndDescriptor(StreamableTexture& tex);

        // Priority/streaming logic
        void processCompletedReads();
        void processUploads(const glm::vec3& cameraPos);
        void updatePriorities(const glm::vec3& cameraPos);
        void submitReadRequests();
        void processEvictions();

        uint32_t calculateDesiredMip(float distance, uint32_t totalMips) const;
        float calculatePriority(uint32_t currentMip, uint32_t desiredMip, float distance) const;

        size_t estimateMipVRAM(uint32_t width, uint32_t height, uint32_t mipLevel, vk::Format format) const;
        size_t estimateFullImageVRAM(uint32_t width, uint32_t height, uint32_t mipLevels, vk::Format format) const;
    };
}
