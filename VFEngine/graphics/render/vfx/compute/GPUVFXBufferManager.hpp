#pragma once

#include "GPUVFXTypes.hpp"
#include "vfx/VFXChildSpawn.hpp" // VK-1501: child-region capacities (single source of truth)
#include "../../../core/RenderManager.hpp"
#include "../../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <array>

namespace core
{
    class Device;
}

namespace render::vfx
{
    class GPUVFXBufferManager
    {
    private:
        core::Device& device;

        vk::Buffer particleBuffer;
        core::VulkanAllocation particleAllocation;

        vk::Buffer configBuffer;
        core::VulkanAllocation configAllocation;
        void* configMapped = nullptr;

        vk::Buffer stateBuffer;
        core::VulkanAllocation stateAllocation;

        std::array<vk::Buffer, core::MAX_FRAMES_IN_FLIGHT> stateStagingBuffers{};
        std::array<core::VulkanAllocation, core::MAX_FRAMES_IN_FLIGHT> stateStagingAllocations{};
        std::array<void*, core::MAX_FRAMES_IN_FLIGHT> stateStagingMapped{};

        vk::Buffer drawCommandBuffer;
        core::VulkanAllocation drawCommandAllocation;

        vk::Buffer lutBuffer;
        core::VulkanAllocation lutAllocation;
        void* lutMapped = nullptr;

        vk::Buffer ribbonRingBuffer;
        core::VulkanAllocation ribbonRingAllocation;
        vk::Buffer ribbonHeadBuffer;
        core::VulkanAllocation ribbonHeadAllocation;

        vk::Buffer eventBuffer;
        core::VulkanAllocation eventAllocation;
        std::array<vk::Buffer, core::MAX_FRAMES_IN_FLIGHT> eventReadbackBuffers{};
        std::array<core::VulkanAllocation, core::MAX_FRAMES_IN_FLIGHT> eventReadbackAllocations{};
        std::array<void*, core::MAX_FRAMES_IN_FLIGHT> eventReadbackMapped{};

        vk::Buffer colliderBuffer;
        core::VulkanAllocation colliderAllocation;
        void* colliderMapped = nullptr;

        vk::Buffer terrainBuffer;
        core::VulkanAllocation terrainAllocation;
        void* terrainMapped = nullptr;

        vk::Buffer spawnRequestBuffer;
        core::VulkanAllocation spawnRequestAllocation;
        std::array<vk::Buffer, core::MAX_FRAMES_IN_FLIGHT> spawnRequestStagingBuffers{};
        std::array<core::VulkanAllocation, core::MAX_FRAMES_IN_FLIGHT> spawnRequestStagingAllocations{};
        std::array<void*, core::MAX_FRAMES_IN_FLIGHT> spawnRequestStagingMapped{};

        // VK-1501: GPU event->child request ring (binding 11). Single device-local, GPU-written
        // buffer (no staging), ping-ponged into two frame-parity halves internally.
        vk::Buffer childSpawnBuffer;
        core::VulkanAllocation childSpawnAllocation;

        uint32_t maxParticles = 0;
        uint32_t maxEmitters = 0;
        uint32_t currentFrameIndex = 0;
        bool initialized = false;
        bool particleBufferCleared = false;
        bool childSpawnCleared = false; // VK-1501: one-time zero of both counter halves on first use
        uint32_t lastRawEventCount = 0;

        uint32_t allocatedParticleCount = 0;
        uint32_t activeEmitterCount = 0;
        std::vector<bool> emitterSlots;
        std::vector<uint32_t> emitterParticleOffsets;
        std::vector<uint32_t> emitterParticleCounts;

        struct FreeBlock
        {
            uint32_t offset;
            uint32_t count;
        };
        std::vector<FreeBlock> freeList;

    public:
        explicit GPUVFXBufferManager(core::Device& device);
        ~GPUVFXBufferManager();

        GPUVFXBufferManager(const GPUVFXBufferManager&) = delete;
        GPUVFXBufferManager& operator=(const GPUVFXBufferManager&) = delete;

        bool init(uint32_t maxParticles = GPUVFXConstants::MAX_GPU_PARTICLES,
                  uint32_t maxEmitters = GPUVFXConstants::MAX_EMITTERS);
        void cleanup();
        bool isInitialized() const { return initialized; }

        vk::Buffer getParticleBuffer() const { return particleBuffer; }
        vk::Buffer getConfigBuffer() const { return configBuffer; }
        vk::Buffer getStateBuffer() const { return stateBuffer; }
        vk::Buffer getDrawCommandBuffer() const { return drawCommandBuffer; }
        vk::Buffer getLUTBuffer() const { return lutBuffer; }
        vk::Buffer getRibbonRingBuffer() const { return ribbonRingBuffer; }
        vk::Buffer getRibbonHeadBuffer() const { return ribbonHeadBuffer; }
        vk::Buffer getEventBuffer() const { return eventBuffer; }
        vk::Buffer getColliderBuffer() const { return colliderBuffer; }
        vk::Buffer getTerrainBuffer() const { return terrainBuffer; }
        vk::Buffer getSpawnRequestBuffer() const { return spawnRequestBuffer; }
        vk::Buffer getChildSpawnBuffer() const { return childSpawnBuffer; }

        GPUVFXBufferSet getBufferSet() const
        {
            return {particleBuffer, configBuffer, stateBuffer, drawCommandBuffer,
                    lutBuffer, ribbonRingBuffer, ribbonHeadBuffer, eventBuffer,
                    colliderBuffer, terrainBuffer, spawnRequestBuffer, childSpawnBuffer};
        }

        vk::DeviceSize getParticleBufferSize() const;
        vk::DeviceSize getConfigBufferSize() const;
        vk::DeviceSize getStateBufferSize() const;
        vk::DeviceSize getDrawCommandBufferSize() const;
        vk::DeviceSize getLUTBufferSize() const;
        vk::DeviceSize getRibbonRingBufferSize() const;
        vk::DeviceSize getRibbonHeadBufferSize() const;
        vk::DeviceSize getEventBufferSize() const;
        vk::DeviceSize getColliderBufferSize() const;
        vk::DeviceSize getTerrainBufferSize() const;
        vk::DeviceSize getSpawnRequestBufferSize() const;
        vk::DeviceSize getChildSpawnBufferSize() const;

        void updateSceneColliders(const std::vector<GPUCollider>& colliders, uint32_t count);
        void updateTerrainHeightfield(const GPUTerrainHeightfield& header,
                                       const float* heights, uint32_t heightCount);
        void clearTerrainHeightfield();

        void clearEventBuffer(vk::CommandBuffer cmd);
        // VK-1501: zero the given parity half's per-region counters before parents append into it.
        // The first call zeroes BOTH halves so frame 0's reader can't observe garbage.
        void clearChildSpawnCounters(vk::CommandBuffer cmd, uint32_t writeHalf);
        void copyEventBufferToReadback(vk::CommandBuffer cmd);
        std::vector<GPUVFXEvent> readbackEvents(uint32_t& outEventCount);
        uint32_t getLastRawEventCount() const { return lastRawEventCount; }

        void updateEmitterConfig(uint32_t emitterIndex, const GPUEmitterConfig& config);
        void updateEmitterState(uint32_t emitterIndex, const GPUEmitterState& state);
        void updateEmitterLUT(uint32_t emitterIndex, const std::vector<glm::vec4>& lutData);

        void resetActiveCount(vk::CommandBuffer cmd, uint32_t emitterIndex);
        void resetAllActiveCounts(vk::CommandBuffer cmd);
        void uploadStateBuffer(vk::CommandBuffer cmd);
        GPUVFXSpawnRequest* mapSpawnRequestStaging();
        void uploadSpawnRequestBuffer(vk::CommandBuffer cmd);
        void clearDrawCommands(vk::CommandBuffer cmd);
        // VK-1460: zero a single emitter's draw command (used by the selective per-frame
        // clear so temporally-throttled emitters keep their command on off-frames).
        void clearDrawCommand(vk::CommandBuffer cmd, uint32_t emitterIndex);
        void clearRibbonHead(vk::CommandBuffer cmd, uint32_t emitterIndex);
        void clearParticleBufferIfNeeded(vk::CommandBuffer cmd);

        struct EmitterAllocation
        {
            uint32_t emitterIndex;
            uint32_t particleOffset;
            uint32_t particleCount;
        };

        EmitterAllocation allocateEmitter(uint32_t particleCount);
        void freeEmitter(uint32_t emitterIndex);

        uint32_t getMaxParticles() const { return maxParticles; }
        uint32_t getMaxEmitters() const { return maxEmitters; }
        uint32_t getAllocatedParticleCount() const { return allocatedParticleCount; }
        uint32_t getActiveEmitterCount() const { return activeEmitterCount; }

        uint32_t getFreeBlockCount() const { return static_cast<uint32_t>(freeList.size()); }
        float getFragmentationPercent() const;

        void resetParticleBufferClearedFlag() { particleBufferCleared = false; }
        void resetAllocator()
        {
            allocatedParticleCount = 0;
            activeEmitterCount = 0;
            std::fill(emitterSlots.begin(), emitterSlots.end(), false);
            freeList.clear();
            freeList.push_back({0, maxParticles});
        }

        void advanceFrame() { currentFrameIndex = (currentFrameIndex + 1) % core::MAX_FRAMES_IN_FLIGHT; }
        uint32_t getCurrentFrameIndex() const { return currentFrameIndex; }

    private:
        bool createParticleBuffer();
        bool createConfigBuffer();
        bool createStateBuffer();
        bool createDrawCommandBuffer();
        bool createLUTBuffer();
        bool createRibbonBuffers();
        bool createEventBuffers();
        bool createColliderBuffer();
        bool createTerrainBuffer();
        bool createSpawnRequestBuffer();
        bool createChildSpawnBuffer();
        void destroyBuffers();
    };
}
