#include "GPUVFXBufferManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"

namespace render::vfx
{
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

    bool GPUVFXBufferManager::createTerrainBuffer()
    {
        core::BufferInfoRequest request(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            getTerrainBufferSize(),
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent
        );

        core::BufferUtilities::createBuffer(request, terrainBuffer, terrainMemory);

        if (terrainBuffer && terrainMemory)
        {
            terrainMapped = device.getLogicalDevice().mapMemory(
                terrainMemory, 0, getTerrainBufferSize(), vk::MemoryMapFlags{}
            );
            std::memset(terrainMapped, 0, getTerrainBufferSize());
            return true;
        }
        return false;
    }
}
