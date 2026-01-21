#pragma once

#include "GPUVFXTypes.hpp"
#include "../../core/RenderManager.hpp"
#include <vulkan/vulkan.hpp>
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

        vk::DeviceSize getParticleBufferSize() const;
        vk::DeviceSize getConfigBufferSize() const;
        vk::DeviceSize getStateBufferSize() const;
        vk::DeviceSize getDrawCommandBufferSize() const;

        void updateEmitterConfig(uint32_t emitterIndex, const GPUEmitterConfig& config);
        void updateEmitterState(uint32_t emitterIndex, const GPUEmitterState& state);

        void resetActiveCount(vk::CommandBuffer cmd, uint32_t emitterIndex);
        void resetAllActiveCounts(vk::CommandBuffer cmd);
        void uploadStateBuffer(vk::CommandBuffer cmd);
        void clearDrawCommands(vk::CommandBuffer cmd);
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

        void advanceFrame() { currentFrameIndex = (currentFrameIndex + 1) % core::MAX_FRAMES_IN_FLIGHT; }
        uint32_t getCurrentFrameIndex() const { return currentFrameIndex; }

    private:
        bool createParticleBuffer();
        bool createConfigBuffer();
        bool createStateBuffer();
        bool createDrawCommandBuffer();
        void destroyBuffers();
    };
}
