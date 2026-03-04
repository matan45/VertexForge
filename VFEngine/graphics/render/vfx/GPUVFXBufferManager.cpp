#include "GPUVFXBufferManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"

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
                vfLogError("GPUVFXBufferManager: Failed to create particle buffer");
                return false;
            }

            if (!createConfigBuffer())
            {
                vfLogError("GPUVFXBufferManager: Failed to create config buffer");
                destroyBuffers();
                return false;
            }

            if (!createStateBuffer())
            {
                vfLogError("GPUVFXBufferManager: Failed to create state buffer");
                destroyBuffers();
                return false;
            }

            if (!createDrawCommandBuffer())
            {
                vfLogError("GPUVFXBufferManager: Failed to create draw command buffer");
                destroyBuffers();
                return false;
            }

            if (!createLUTBuffer())
            {
                vfLogError("GPUVFXBufferManager: Failed to create LUT buffer");
                destroyBuffers();
                return false;
            }

            if (!createRibbonBuffers())
            {
                vfLogError("GPUVFXBufferManager: Failed to create ribbon buffers");
                destroyBuffers();
                return false;
            }

            if (!createEventBuffers())
            {
                vfLogError("GPUVFXBufferManager: Failed to create event buffers");
                destroyBuffers();
                return false;
            }

            if (!createColliderBuffer())
            {
                vfLogError("GPUVFXBufferManager: Failed to create collider buffer");
                destroyBuffers();
                return false;
            }

            if (!createTerrainBuffer())
            {
                vfLogError("GPUVFXBufferManager: Failed to create terrain buffer");
                destroyBuffers();
                return false;
            }

            initialized = true;
            vfLogInfo("GPUVFXBufferManager initialized: {} particles, {} emitters, {:.2f} MB total",
                       maxParticles, maxEmitters,
                       static_cast<float>(getParticleBufferSize() + getConfigBufferSize() +
                           getStateBufferSize() + getDrawCommandBufferSize() +
                           getLUTBufferSize() + getRibbonRingBufferSize() +
                           getRibbonHeadBufferSize() + getEventBufferSize() +
                           getColliderBufferSize() + getTerrainBufferSize()) / (1024.0f * 1024.0f));
            return true;
        }
        catch (const vk::OutOfDeviceMemoryError& e)
        {
            vfLogError("GPUVFXBufferManager: Out of device memory - {}", e.what());
            destroyBuffers();
            return false;
        }
        catch (const std::exception& e)
        {
            vfLogError("GPUVFXBufferManager: Exception during init - {}", e.what());
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

    }

    void GPUVFXBufferManager::destroyBuffers()
    {
        auto& vkDevice = device.getLogicalDevice();

        if (terrainMapped && terrainMemory)
        {
            vkDevice.unmapMemory(terrainMemory);
            terrainMapped = nullptr;
        }

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
        core::BufferUtilities::destroyBuffer(vkDevice, terrainBuffer, terrainMemory);
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

    vk::DeviceSize GPUVFXBufferManager::getTerrainBufferSize() const
    {
        return static_cast<vk::DeviceSize>(GPUVFXConstants::MAX_TERRAIN_HEIGHTFIELD_BYTES);
    }
}
