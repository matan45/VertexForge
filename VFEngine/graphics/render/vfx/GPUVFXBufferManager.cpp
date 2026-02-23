#include "GPUVFXBufferManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Logger.hpp"

namespace render::vfx
{
    GPUVFXBufferManager::GPUVFXBufferManager(core::Device& device)
        : device(device)
    {
    }

    GPUVFXBufferManager::~GPUVFXBufferManager()
    {
        cleanup();
    }

    bool GPUVFXBufferManager::init(uint32_t maxParticles, uint32_t maxEmitters)
    {
        if (initialized)
        {
            loggerWarning("GPUVFXBufferManager already initialized");
            return true;
        }

        this->maxParticles = maxParticles;
        this->maxEmitters = maxEmitters;

        emitterSlots.resize(maxEmitters, false);
        emitterParticleOffsets.resize(maxEmitters, 0);
        emitterParticleCounts.resize(maxEmitters, 0);

        try
        {
            if (!createParticleBuffer())
            {
                loggerError("GPUVFXBufferManager: Failed to create particle buffer");
                return false;
            }

            if (!createConfigBuffer())
            {
                loggerError("GPUVFXBufferManager: Failed to create config buffer");
                destroyBuffers();
                return false;
            }

            if (!createStateBuffer())
            {
                loggerError("GPUVFXBufferManager: Failed to create state buffer");
                destroyBuffers();
                return false;
            }

            if (!createDrawCommandBuffer())
            {
                loggerError("GPUVFXBufferManager: Failed to create draw command buffer");
                destroyBuffers();
                return false;
            }

            if (!createLUTBuffer())
            {
                loggerError("GPUVFXBufferManager: Failed to create LUT buffer");
                destroyBuffers();
                return false;
            }

            if (!createRibbonBuffers())
            {
                loggerError("GPUVFXBufferManager: Failed to create ribbon buffers");
                destroyBuffers();
                return false;
            }

            if (!createEventBuffers())
            {
                loggerError("GPUVFXBufferManager: Failed to create event buffers");
                destroyBuffers();
                return false;
            }

            if (!createColliderBuffer())
            {
                loggerError("GPUVFXBufferManager: Failed to create collider buffer");
                destroyBuffers();
                return false;
            }

            initialized = true;
            loggerInfo("GPUVFXBufferManager initialized: {} particles, {} emitters, {:.2f} MB total",
                       maxParticles, maxEmitters,
                       static_cast<float>(getParticleBufferSize() + getConfigBufferSize() +
                           getStateBufferSize() + getDrawCommandBufferSize() +
                           getLUTBufferSize() + getRibbonRingBufferSize() +
                           getRibbonHeadBufferSize() + getEventBufferSize() +
                           getColliderBufferSize()) / (1024.0f * 1024.0f));
            return true;
        }
        catch (const vk::OutOfDeviceMemoryError& e)
        {
            loggerError("GPUVFXBufferManager: Out of device memory - {}", e.what());
            destroyBuffers();
            return false;
        }
        catch (const std::exception& e)
        {
            loggerError("GPUVFXBufferManager: Exception during init - {}", e.what());
            destroyBuffers();
            return false;
        }
    }

    void GPUVFXBufferManager::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        device.getLogicalDevice().waitIdle();

        destroyBuffers();

        emitterSlots.clear();
        emitterParticleOffsets.clear();
        emitterParticleCounts.clear();

        maxParticles = 0;
        maxEmitters = 0;
        allocatedParticleCount = 0;
        activeEmitterCount = 0;
        initialized = false;
        particleBufferCleared = false;

        loggerInfo("GPUVFXBufferManager cleaned up");
    }

    bool GPUVFXBufferManager::createParticleBuffer()
    {
        core::BufferInfoRequest request(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            getParticleBufferSize(),
            vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );

        core::BufferUtilities::createBuffer(request, particleBuffer, particleMemory);
        return particleBuffer && particleMemory;
    }

    bool GPUVFXBufferManager::createConfigBuffer()
    {
        core::BufferInfoRequest request(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            getConfigBufferSize(),
            vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent
        );

        core::BufferUtilities::createBuffer(request, configBuffer, configMemory);

        if (configBuffer && configMemory)
        {
            configMapped = device.getLogicalDevice().mapMemory(
                configMemory, 0, getConfigBufferSize(), vk::MemoryMapFlags{}
            );

            std::memset(configMapped, 0, getConfigBufferSize());
            return true;
        }
        return false;
    }

    bool GPUVFXBufferManager::createStateBuffer()
    {
        core::BufferInfoRequest stateRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            getStateBufferSize(),
            vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );

        core::BufferUtilities::createBuffer(stateRequest, stateBuffer, stateMemory);

        if (!stateBuffer || !stateMemory)
        {
            return false;
        }

        for (uint32_t i = 0; i < core::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            core::BufferInfoRequest stagingRequest(
                device.getLogicalDevice(),
                device.getPhysicalDevice(),
                getStateBufferSize(),
                vk::BufferUsageFlagBits::eTransferSrc,
                vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent
            );

            core::BufferUtilities::createBuffer(stagingRequest, stateStagingBuffers[i], stateStagingMemories[i]);

            if (!stateStagingBuffers[i] || !stateStagingMemories[i])
            {
                return false;
            }

            stateStagingMapped[i] = device.getLogicalDevice().mapMemory(
                stateStagingMemories[i], 0, getStateBufferSize(), vk::MemoryMapFlags{}
            );

            std::memset(stateStagingMapped[i], 0, getStateBufferSize());
        }

        return true;
    }

    bool GPUVFXBufferManager::createDrawCommandBuffer()
    {
        core::BufferInfoRequest request(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            getDrawCommandBufferSize(),
            vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eIndirectBuffer |
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );

        core::BufferUtilities::createBuffer(request, drawCommandBuffer, drawCommandMemory);
        return drawCommandBuffer && drawCommandMemory;
    }

    void GPUVFXBufferManager::destroyBuffers()
    {
        auto& vkDevice = device.getLogicalDevice();

        if (colliderMapped && colliderMemory)
        {
            vkDevice.unmapMemory(colliderMemory);
            colliderMapped = nullptr;
        }

        if (lutMapped && lutMemory)
        {
            vkDevice.unmapMemory(lutMemory);
            lutMapped = nullptr;
        }

        if (configMapped && configMemory)
        {
            vkDevice.unmapMemory(configMemory);
            configMapped = nullptr;
        }

        for (uint32_t i = 0; i < core::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            if (stateStagingMapped[i] && stateStagingMemories[i])
            {
                vkDevice.unmapMemory(stateStagingMemories[i]);
                stateStagingMapped[i] = nullptr;
            }
            core::BufferUtilities::destroyBuffer(vkDevice, stateStagingBuffers[i], stateStagingMemories[i]);
        }

        core::BufferUtilities::destroyBuffer(vkDevice, particleBuffer, particleMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, configBuffer, configMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, stateBuffer, stateMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, drawCommandBuffer, drawCommandMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, lutBuffer, lutMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, ribbonRingBuffer, ribbonRingMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, ribbonHeadBuffer, ribbonHeadMemory);

        for (uint32_t i = 0; i < core::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            if (eventReadbackMapped[i] && eventReadbackMemories[i])
            {
                vkDevice.unmapMemory(eventReadbackMemories[i]);
                eventReadbackMapped[i] = nullptr;
            }
            core::BufferUtilities::destroyBuffer(vkDevice, eventReadbackBuffers[i], eventReadbackMemories[i]);
        }
        core::BufferUtilities::destroyBuffer(vkDevice, eventBuffer, eventMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, colliderBuffer, colliderMemory);
    }

    vk::DeviceSize GPUVFXBufferManager::getParticleBufferSize() const
    {
        return static_cast<vk::DeviceSize>(maxParticles) * sizeof(GPUParticle);
    }

    vk::DeviceSize GPUVFXBufferManager::getConfigBufferSize() const
    {
        return static_cast<vk::DeviceSize>(maxEmitters) * sizeof(GPUEmitterConfig);
    }

    vk::DeviceSize GPUVFXBufferManager::getStateBufferSize() const
    {
        return static_cast<vk::DeviceSize>(maxEmitters) * sizeof(GPUEmitterState);
    }

    vk::DeviceSize GPUVFXBufferManager::getDrawCommandBufferSize() const
    {
        return static_cast<vk::DeviceSize>(maxEmitters) * sizeof(VFXDrawIndirectCommand);
    }

    vk::DeviceSize GPUVFXBufferManager::getLUTBufferSize() const
    {
        return static_cast<vk::DeviceSize>(maxEmitters) *
               GPUVFXConstants::LUT_CHANNELS * GPUVFXConstants::LUT_RESOLUTION *
               sizeof(glm::vec4);
    }

    vk::DeviceSize GPUVFXBufferManager::getRibbonRingBufferSize() const
    {
        return static_cast<vk::DeviceSize>(maxEmitters) *
               GPUVFXConstants::MAX_TRAIL_POINTS * sizeof(uint32_t);
    }

    vk::DeviceSize GPUVFXBufferManager::getRibbonHeadBufferSize() const
    {
        return static_cast<vk::DeviceSize>(maxEmitters) * sizeof(uint32_t);
    }

    vk::DeviceSize GPUVFXBufferManager::getEventBufferSize() const
    {
        // std430: GPUVFXEvent has vec3 → 16-byte struct alignment
        // events[] starts at offset 16 (not 4) due to padding after eventCount
        constexpr vk::DeviceSize EVENT_DATA_OFFSET = 16;
        return EVENT_DATA_OFFSET +
               static_cast<vk::DeviceSize>(GPUVFXConstants::MAX_VFX_EVENTS_PER_FRAME) * sizeof(GPUVFXEvent);
    }

    vk::DeviceSize GPUVFXBufferManager::getColliderBufferSize() const
    {
        return static_cast<vk::DeviceSize>(GPUVFXConstants::MAX_SCENE_COLLIDERS) * sizeof(GPUCollider);
    }

    bool GPUVFXBufferManager::createColliderBuffer()
    {
        core::BufferInfoRequest request(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            getColliderBufferSize(),
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent
        );

        core::BufferUtilities::createBuffer(request, colliderBuffer, colliderMemory);

        if (colliderBuffer && colliderMemory)
        {
            colliderMapped = device.getLogicalDevice().mapMemory(
                colliderMemory, 0, getColliderBufferSize(), vk::MemoryMapFlags{}
            );
            std::memset(colliderMapped, 0, getColliderBufferSize());
            return true;
        }
        return false;
    }

    void GPUVFXBufferManager::updateSceneColliders(const std::vector<GPUCollider>& colliders, uint32_t count)
    {
        if (!initialized || !colliderMapped)
        {
            return;
        }

        uint32_t copyCount = std::min(count, GPUVFXConstants::MAX_SCENE_COLLIDERS);
        if (copyCount > 0)
        {
            std::memcpy(colliderMapped, colliders.data(), copyCount * sizeof(GPUCollider));
        }

        // Zero remaining slots
        if (copyCount < GPUVFXConstants::MAX_SCENE_COLLIDERS)
        {
            auto* dst = static_cast<uint8_t*>(colliderMapped) + copyCount * sizeof(GPUCollider);
            std::memset(dst, 0, (GPUVFXConstants::MAX_SCENE_COLLIDERS - copyCount) * sizeof(GPUCollider));
        }
    }

    bool GPUVFXBufferManager::createLUTBuffer()
    {
        core::BufferInfoRequest request(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            getLUTBufferSize(),
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent
        );

        core::BufferUtilities::createBuffer(request, lutBuffer, lutMemory);

        if (lutBuffer && lutMemory)
        {
            lutMapped = device.getLogicalDevice().mapMemory(
                lutMemory, 0, getLUTBufferSize(), vk::MemoryMapFlags{}
            );
            std::memset(lutMapped, 0, getLUTBufferSize());
            return true;
        }
        return false;
    }

    bool GPUVFXBufferManager::createRibbonBuffers()
    {
        // Ring buffer: stores particle indices in spawn order per emitter
        core::BufferInfoRequest ringRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            getRibbonRingBufferSize(),
            vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );

        core::BufferUtilities::createBuffer(ringRequest, ribbonRingBuffer, ribbonRingMemory);
        if (!ribbonRingBuffer || !ribbonRingMemory)
        {
            return false;
        }

        // Head buffer: write head position per emitter
        core::BufferInfoRequest headRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            getRibbonHeadBufferSize(),
            vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );

        core::BufferUtilities::createBuffer(headRequest, ribbonHeadBuffer, ribbonHeadMemory);
        return ribbonHeadBuffer && ribbonHeadMemory;
    }

    void GPUVFXBufferManager::clearRibbonHead(vk::CommandBuffer cmd, uint32_t emitterIndex)
    {
        if (!initialized || emitterIndex >= maxEmitters || !ribbonHeadBuffer)
        {
            return;
        }

        vk::DeviceSize offset = static_cast<vk::DeviceSize>(emitterIndex) * sizeof(uint32_t);
        cmd.fillBuffer(ribbonHeadBuffer, offset, sizeof(uint32_t), 0);
    }

    bool GPUVFXBufferManager::createEventBuffers()
    {
        core::BufferInfoRequest eventRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            getEventBufferSize(),
            vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferSrc |
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );

        core::BufferUtilities::createBuffer(eventRequest, eventBuffer, eventMemory);
        if (!eventBuffer || !eventMemory)
        {
            return false;
        }

        for (uint32_t i = 0; i < core::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            core::BufferInfoRequest readbackRequest(
                device.getLogicalDevice(),
                device.getPhysicalDevice(),
                getEventBufferSize(),
                vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent
            );

            core::BufferUtilities::createBuffer(readbackRequest,
                eventReadbackBuffers[i], eventReadbackMemories[i]);
            if (!eventReadbackBuffers[i] || !eventReadbackMemories[i])
            {
                return false;
            }

            eventReadbackMapped[i] = device.getLogicalDevice().mapMemory(
                eventReadbackMemories[i], 0, getEventBufferSize(), vk::MemoryMapFlags{}
            );
            std::memset(eventReadbackMapped[i], 0, getEventBufferSize());
        }
        return true;
    }

    void GPUVFXBufferManager::clearEventBuffer(vk::CommandBuffer cmd)
    {
        if (!initialized || !eventBuffer)
        {
            return;
        }

        cmd.fillBuffer(eventBuffer, 0, sizeof(uint32_t), 0);
    }

    void GPUVFXBufferManager::copyEventBufferToReadback(vk::CommandBuffer cmd)
    {
        if (!initialized || !eventBuffer || !eventReadbackBuffers[currentFrameIndex])
        {
            return;
        }

        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = getEventBufferSize();
        cmd.copyBuffer(eventBuffer, eventReadbackBuffers[currentFrameIndex], copyRegion);
    }

    std::vector<GPUVFXEvent> GPUVFXBufferManager::readbackEvents(uint32_t& outEventCount)
    {
        uint32_t readIndex = (currentFrameIndex + core::MAX_FRAMES_IN_FLIGHT - 1)
                             % core::MAX_FRAMES_IN_FLIGHT;

        if (!eventReadbackMapped[readIndex])
        {
            outEventCount = 0;
            return {};
        }

        auto* data = static_cast<const uint8_t*>(eventReadbackMapped[readIndex]);
        uint32_t eventCount = *reinterpret_cast<const uint32_t*>(data);
        eventCount = std::min(eventCount, GPUVFXConstants::MAX_VFX_EVENTS_PER_FRAME);
        outEventCount = eventCount;

        if (eventCount == 0)
        {
            return {};
        }

        // std430: events[] starts at offset 16 due to struct alignment padding
        constexpr size_t EVENT_DATA_OFFSET = 16;
        std::vector<GPUVFXEvent> events(eventCount);
        std::memcpy(events.data(), data + EVENT_DATA_OFFSET, eventCount * sizeof(GPUVFXEvent));
        return events;
    }

    void GPUVFXBufferManager::updateEmitterConfig(uint32_t emitterIndex, const GPUEmitterConfig& config)
    {
        if (!initialized || emitterIndex >= maxEmitters || !configMapped)
        {
            return;
        }

        auto* configs = static_cast<GPUEmitterConfig*>(configMapped);
        configs[emitterIndex] = config;
    }

    void GPUVFXBufferManager::updateEmitterState(uint32_t emitterIndex, const GPUEmitterState& state)
    {
        if (!initialized || emitterIndex >= maxEmitters || !stateStagingMapped[currentFrameIndex])
        {
            return;
        }

        auto* states = static_cast<GPUEmitterState*>(stateStagingMapped[currentFrameIndex]);
        states[emitterIndex] = state;
    }

    void GPUVFXBufferManager::updateEmitterLUT(uint32_t emitterIndex, const std::vector<glm::vec4>& lutData)
    {
        if (!initialized || emitterIndex >= maxEmitters || !lutMapped)
        {
            return;
        }

        constexpr uint32_t entriesPerEmitter = GPUVFXConstants::LUT_CHANNELS * GPUVFXConstants::LUT_RESOLUTION;
        uint32_t offset = emitterIndex * entriesPerEmitter;
        uint32_t copyCount = std::min(static_cast<uint32_t>(lutData.size()), entriesPerEmitter);

        auto* dst = static_cast<glm::vec4*>(lutMapped) + offset;
        std::memcpy(dst, lutData.data(), copyCount * sizeof(glm::vec4));
    }

    void GPUVFXBufferManager::resetActiveCount(vk::CommandBuffer cmd, uint32_t emitterIndex)
    {
        if (!initialized || emitterIndex >= maxEmitters)
        {
            return;
        }

        vk::DeviceSize stateOffset = static_cast<vk::DeviceSize>(emitterIndex) * sizeof(GPUEmitterState);
        vk::DeviceSize activeCountOffset = stateOffset + offsetof(GPUEmitterState, activeCount);

        cmd.fillBuffer(stateBuffer, activeCountOffset, sizeof(uint32_t), 0);

        vk::DeviceSize spawnCounterOffset = stateOffset + offsetof(GPUEmitterState, spawnCounter);
        cmd.fillBuffer(stateBuffer, spawnCounterOffset, sizeof(uint32_t), 0);
    }

    void GPUVFXBufferManager::resetAllActiveCounts(vk::CommandBuffer cmd)
    {
        if (!initialized)
        {
            return;
        }

        for (uint32_t i = 0; i < maxEmitters; ++i)
        {
            if (emitterSlots[i])
            {
                resetActiveCount(cmd, i);
            }
        }
    }

    void GPUVFXBufferManager::uploadStateBuffer(vk::CommandBuffer cmd)
    {
        if (!initialized || !stateStagingBuffers[currentFrameIndex] || !stateBuffer)
        {
            return;
        }

        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = getStateBufferSize();

        cmd.copyBuffer(stateStagingBuffers[currentFrameIndex], stateBuffer, copyRegion);
    }

    void GPUVFXBufferManager::clearDrawCommands(vk::CommandBuffer cmd)
    {
        if (!initialized || !drawCommandBuffer)
        {
            return;
        }

        cmd.fillBuffer(drawCommandBuffer, 0, getDrawCommandBufferSize(), 0);
    }

    void GPUVFXBufferManager::clearParticleBufferIfNeeded(vk::CommandBuffer cmd)
    {
        if (!initialized || !particleBuffer || particleBufferCleared)
        {
            return;
        }

        cmd.fillBuffer(particleBuffer, 0, getParticleBufferSize(), 0);
        particleBufferCleared = true;
    }

    GPUVFXBufferManager::EmitterAllocation GPUVFXBufferManager::allocateEmitter(uint32_t particleCount)
    {
        EmitterAllocation allocation{};
        allocation.emitterIndex = UINT32_MAX;

        if (!initialized)
        {
            loggerError("GPUVFXBufferManager::allocateEmitter: Not initialized");
            return allocation;
        }

        uint32_t freeSlot = UINT32_MAX;
        for (uint32_t i = 0; i < maxEmitters; ++i)
        {
            if (!emitterSlots[i])
            {
                freeSlot = i;
                break;
            }
        }

        if (freeSlot == UINT32_MAX)
        {
            loggerError("GPUVFXBufferManager::allocateEmitter: No free emitter slots");
            return allocation;
        }

        if (allocatedParticleCount + particleCount > maxParticles)
        {
            loggerError("GPUVFXBufferManager::allocateEmitter: Not enough particle budget ({} + {} > {})",
                        allocatedParticleCount, particleCount, maxParticles);
            return allocation;
        }

        allocation.emitterIndex = freeSlot;
        allocation.particleOffset = allocatedParticleCount;
        allocation.particleCount = particleCount;

        emitterSlots[freeSlot] = true;
        emitterParticleOffsets[freeSlot] = allocatedParticleCount;
        emitterParticleCounts[freeSlot] = particleCount;
        allocatedParticleCount += particleCount;
        activeEmitterCount++;

        loggerInfo("GPUVFXBufferManager: Allocated emitter {} with {} particles at offset {}",
                   freeSlot, particleCount, allocation.particleOffset);

        return allocation;
    }

    void GPUVFXBufferManager::freeEmitter(uint32_t emitterIndex)
    {
        if (!initialized || emitterIndex >= maxEmitters || !emitterSlots[emitterIndex])
        {
            return;
        }

        emitterSlots[emitterIndex] = false;
        activeEmitterCount--;

        loggerInfo("GPUVFXBufferManager: Freed emitter {}", emitterIndex);
    }
}
