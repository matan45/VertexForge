#pragma once

#include "GPUVFXTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
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
    public:
        static constexpr uint32_t FRAMES_IN_FLIGHT = 2;

        explicit GPUVFXBufferManager(core::Device& device);
        ~GPUVFXBufferManager();

        GPUVFXBufferManager(const GPUVFXBufferManager&) = delete;
        GPUVFXBufferManager& operator=(const GPUVFXBufferManager&) = delete;

        // Initialization
        bool init(uint32_t maxParticles = GPUVFXConstants::MAX_GPU_PARTICLES,
                  uint32_t maxEmitters = GPUVFXConstants::MAX_EMITTERS);
        void cleanup();
        bool isInitialized() const { return initialized; }

        // Buffer accessors (for descriptor binding)
        vk::Buffer getParticleBuffer() const { return particleBuffer; }
        vk::Buffer getConfigBuffer() const { return configBuffer; }
        vk::Buffer getStateBuffer() const { return stateBuffer; }
        vk::Buffer getDrawCommandBuffer() const { return drawCommandBuffer; }

        // Buffer sizes
        vk::DeviceSize getParticleBufferSize() const;
        vk::DeviceSize getConfigBufferSize() const;
        vk::DeviceSize getStateBufferSize() const;
        vk::DeviceSize getDrawCommandBufferSize() const;

        // Per-frame updates (CPU -> GPU)
        void updateEmitterConfig(uint32_t emitterIndex, const GPUEmitterConfig& config);
        void updateEmitterState(uint32_t emitterIndex, const GPUEmitterState& state);

        // Command buffer operations
        void resetActiveCount(vk::CommandBuffer cmd, uint32_t emitterIndex);
        void resetAllActiveCounts(vk::CommandBuffer cmd);
        void uploadStateBuffer(vk::CommandBuffer cmd);
        void clearDrawCommands(vk::CommandBuffer cmd);
        void clearParticleBufferIfNeeded(vk::CommandBuffer cmd);

        // Emitter allocation
        struct EmitterAllocation
        {
            uint32_t emitterIndex;
            uint32_t particleOffset;
            uint32_t particleCount;
        };

        EmitterAllocation allocateEmitter(uint32_t particleCount);
        void freeEmitter(uint32_t emitterIndex);

        // Capacity info
        uint32_t getMaxParticles() const { return maxParticles; }
        uint32_t getMaxEmitters() const { return maxEmitters; }
        uint32_t getAllocatedParticleCount() const { return allocatedParticleCount; }
        uint32_t getActiveEmitterCount() const { return activeEmitterCount; }

        // Reset particle buffer cleared flag (call when all instances are destroyed)
        void resetParticleBufferClearedFlag() { particleBufferCleared = false; }

        // Frame management for ring-buffered staging
        void advanceFrame() { currentFrameIndex = (currentFrameIndex + 1) % FRAMES_IN_FLIGHT; }
        uint32_t getCurrentFrameIndex() const { return currentFrameIndex; }

    private:
        core::Device& device;

        // GPU Buffers
        vk::Buffer particleBuffer;          // Main particle SSBO (device-local)
        vk::DeviceMemory particleMemory;

        vk::Buffer configBuffer;            // Emitter configs (host-visible for updates)
        vk::DeviceMemory configMemory;
        void* configMapped = nullptr;

        vk::Buffer stateBuffer;             // Emitter states (device-local, compute RW)
        vk::DeviceMemory stateMemory;

        // Ring-buffered staging for state updates (avoids CPU/GPU race)
        std::array<vk::Buffer, FRAMES_IN_FLIGHT> stateStagingBuffers{};
        std::array<vk::DeviceMemory, FRAMES_IN_FLIGHT> stateStagingMemories{};
        std::array<void*, FRAMES_IN_FLIGHT> stateStagingMapped{};

        vk::Buffer drawCommandBuffer;       // Indirect draw commands (device-local)
        vk::DeviceMemory drawCommandMemory;

        // Configuration
        uint32_t maxParticles = 0;
        uint32_t maxEmitters = 0;
        uint32_t currentFrameIndex = 0;
        bool initialized = false;
        bool particleBufferCleared = false;

        // Allocation tracking
        uint32_t allocatedParticleCount = 0;
        uint32_t activeEmitterCount = 0;
        std::vector<bool> emitterSlots;     // true = slot in use
        std::vector<uint32_t> emitterParticleOffsets;
        std::vector<uint32_t> emitterParticleCounts;

        // Internal helpers
        bool createParticleBuffer();
        bool createConfigBuffer();
        bool createStateBuffer();
        bool createDrawCommandBuffer();
        void destroyBuffers();
    };
}
