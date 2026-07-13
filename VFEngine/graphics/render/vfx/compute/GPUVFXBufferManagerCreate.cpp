#include "GPUVFXBufferManager.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/BufferUtilities.hpp"

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

        core::BufferUtilities::createBuffer(request, particleBuffer, particleAllocation, device.getMemoryManager());
        return particleBuffer && particleAllocation;
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

        core::BufferUtilities::createBuffer(request, configBuffer, configAllocation, device.getMemoryManager());

        if (configBuffer && configAllocation)
        {
            configMapped = configAllocation.mappedPtr;

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

        core::BufferUtilities::createBuffer(stateRequest, stateBuffer, stateAllocation, device.getMemoryManager());

        if (!stateBuffer || !stateAllocation)
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

            core::BufferUtilities::createBuffer(stagingRequest, stateStagingBuffers[i], stateStagingAllocations[i], device.getMemoryManager());

            if (!stateStagingBuffers[i] || !stateStagingAllocations[i])
            {
                return false;
            }

            stateStagingMapped[i] = stateStagingAllocations[i].mappedPtr;

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

        core::BufferUtilities::createBuffer(request, drawCommandBuffer, drawCommandAllocation, device.getMemoryManager());
        return drawCommandBuffer && drawCommandAllocation;
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

        core::BufferUtilities::createBuffer(request, lutBuffer, lutAllocation, device.getMemoryManager());

        if (lutBuffer && lutAllocation)
        {
            lutMapped = lutAllocation.mappedPtr;
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

        core::BufferUtilities::createBuffer(ringRequest, ribbonRingBuffer, ribbonRingAllocation, device.getMemoryManager());
        if (!ribbonRingBuffer || !ribbonRingAllocation)
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

        core::BufferUtilities::createBuffer(headRequest, ribbonHeadBuffer, ribbonHeadAllocation, device.getMemoryManager());
        return ribbonHeadBuffer && ribbonHeadAllocation;
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

        core::BufferUtilities::createBuffer(eventRequest, eventBuffer, eventAllocation, device.getMemoryManager());
        if (!eventBuffer || !eventAllocation)
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
                eventReadbackBuffers[i], eventReadbackAllocations[i], device.getMemoryManager());
            if (!eventReadbackBuffers[i] || !eventReadbackAllocations[i])
            {
                return false;
            }

            eventReadbackMapped[i] = eventReadbackAllocations[i].mappedPtr;
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

        core::BufferUtilities::createBuffer(request, colliderBuffer, colliderAllocation, device.getMemoryManager());

        if (colliderBuffer && colliderAllocation)
        {
            colliderMapped = colliderAllocation.mappedPtr;
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

        core::BufferUtilities::createBuffer(request, terrainBuffer, terrainAllocation, device.getMemoryManager());

        if (terrainBuffer && terrainAllocation)
        {
            terrainMapped = terrainAllocation.mappedPtr;
            std::memset(terrainMapped, 0, getTerrainBufferSize());
            return true;
        }
        return false;
    }

    bool GPUVFXBufferManager::createSpawnRequestBuffer()
    {
        core::BufferInfoRequest deviceRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            getSpawnRequestBufferSize(),
            vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );

        core::BufferUtilities::createBuffer(deviceRequest, spawnRequestBuffer,
            spawnRequestAllocation, device.getMemoryManager());
        if (!spawnRequestBuffer || !spawnRequestAllocation)
        {
            return false;
        }

        for (uint32_t i = 0; i < core::MAX_FRAMES_IN_FLIGHT; ++i)
        {
            core::BufferInfoRequest stagingRequest(
                device.getLogicalDevice(),
                device.getPhysicalDevice(),
                getSpawnRequestBufferSize(),
                vk::BufferUsageFlagBits::eTransferSrc,
                vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent
            );

            core::BufferUtilities::createBuffer(stagingRequest, spawnRequestStagingBuffers[i],
                spawnRequestStagingAllocations[i], device.getMemoryManager());
            if (!spawnRequestStagingBuffers[i] || !spawnRequestStagingAllocations[i])
            {
                return false;
            }

            spawnRequestStagingMapped[i] = spawnRequestStagingAllocations[i].mappedPtr;
            std::memset(spawnRequestStagingMapped[i], 0, getSpawnRequestBufferSize());
        }

        return true;
    }
}
