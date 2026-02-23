#pragma once

#include "GPUVFXTypes.hpp"
#include "../../core/RenderManager.hpp"
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
        vk::DeviceMemory particleMemory;

        vk::Buffer configBuffer;
        vk::DeviceMemory configMemory;
        void* configMapped = nullptr;

        vk::Buffer stateBuffer;
        vk::DeviceMemory stateMemory;

        std::array<vk::Buffer, core::MAX_FRAMES_IN_FLIGHT> stateStagingBuffers{};
        std::array<vk::DeviceMemory, core::MAX_FRAMES_IN_FLIGHT> stateStagingMemories{};
        std::array<void*, core::MAX_FRAMES_IN_FLIGHT> stateStagingMapped{};

        vk::Buffer drawCommandBuffer;
        vk::DeviceMemory drawCommandMemory;

        vk::Buffer lutBuffer;
        vk::DeviceMemory lutMemory;
        void* lutMapped = nullptr;

        vk::Buffer ribbonRingBuffer;
        vk::DeviceMemory ribbonRingMemory;
        vk::Buffer ribbonHeadBuffer;
        vk::DeviceMemory ribbonHeadMemory;

        vk::Buffer eventBuffer;
        vk::DeviceMemory eventMemory;
        std::array<vk::Buffer, core::MAX_FRAMES_IN_FLIGHT> eventReadbackBuffers{};
        std::array<vk::DeviceMemory, core::MAX_FRAMES_IN_FLIGHT> eventReadbackMemories{};
        std::array<void*, core::MAX_FRAMES_IN_FLIGHT> eventReadbackMapped{};

        vk::Buffer colliderBuffer;
        vk::DeviceMemory colliderMemory;
        void* colliderMapped = nullptr;

        vk::Buffer terrainBuffer;
        vk::DeviceMemory terrainMemory;
        void* terrainMapped = nullptr;

        uint32_t maxParticles = 0;
        uint32_t maxEmitters = 0;
        uint32_t currentFrameIndex = 0;
        bool initialized = false;
        bool particleBufferCleared = false;

        uint32_t allocatedParticleCount = 0;
        uint32_t activeEmitterCount = 0;
        std::vector<bool> emitterSlots;
        std::vector<uint32_t> emitterParticleOffsets;
        std::vector<uint32_t> emitterParticleCounts;

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

        GPUVFXBufferSet getBufferSet() const
        {
            return {particleBuffer, configBuffer, stateBuffer, drawCommandBuffer,
                    lutBuffer, ribbonRingBuffer, ribbonHeadBuffer, eventBuffer,
                    colliderBuffer, terrainBuffer};
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

        void updateSceneColliders(const std::vector<GPUCollider>& colliders, uint32_t count);
        void updateTerrainHeightfield(const GPUTerrainHeightfield& header,
                                       const float* heights, uint32_t heightCount);
        void clearTerrainHeightfield();

        void clearEventBuffer(vk::CommandBuffer cmd);
        void copyEventBufferToReadback(vk::CommandBuffer cmd);
        std::vector<GPUVFXEvent> readbackEvents(uint32_t& outEventCount);

        void updateEmitterConfig(uint32_t emitterIndex, const GPUEmitterConfig& config);
        void updateEmitterState(uint32_t emitterIndex, const GPUEmitterState& state);
        void updateEmitterLUT(uint32_t emitterIndex, const std::vector<glm::vec4>& lutData);

        void resetActiveCount(vk::CommandBuffer cmd, uint32_t emitterIndex);
        void resetAllActiveCounts(vk::CommandBuffer cmd);
        void uploadStateBuffer(vk::CommandBuffer cmd);
        void clearDrawCommands(vk::CommandBuffer cmd);
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

        void resetParticleBufferClearedFlag() { particleBufferCleared = false; }
        void resetAllocator()
        {
            allocatedParticleCount = 0;
            activeEmitterCount = 0;
            std::fill(emitterSlots.begin(), emitterSlots.end(), false);
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
        void destroyBuffers();
    };
}
