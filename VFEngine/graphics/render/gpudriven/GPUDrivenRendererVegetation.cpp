#include "GPUDrivenRenderer.hpp"
#include "../vegetation/GrassComputePipeline.hpp"
#include "../vegetation/GrassMeshShaderPipeline.hpp"
#include "../vegetation/WindSystem.hpp"
#include "../vegetation/VegetationGPUTypes.hpp"
#include "../vegetation/VegetationBufferManager.hpp"
#include "../vegetation/GrassStreamManager.hpp"
#include "../vegetation/VegetationStreamManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/BufferUtilities.hpp"
#include "terrain/TerrainTile.hpp"
#include "vegetation/WindConfig.hpp"
#include "print/Log.hpp"

namespace render::gpudriven
{
    static uint64_t makeTileKey(int32_t x, int32_t z)
    {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
               static_cast<uint64_t>(static_cast<uint32_t>(z));
    }

    void GPUDrivenRenderer::createGrassBuffers(uint32_t maxInstances)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Grass instance buffer (device-local for GPU compute output)
        vk::DeviceSize instanceSize = maxInstances * sizeof(vegetation::GrassInstanceGPU);

        core::BufferInfoRequest instanceRequest(vkDevice, device.getPhysicalDevice());
        instanceRequest.size = instanceSize;
        instanceRequest.usage = vk::BufferUsageFlagBits::eStorageBuffer;
        instanceRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::BufferUtilities::createBuffer(instanceRequest,
                                            vegetation.grassInstanceBuffer,
                                            vegetation.grassInstanceBufferMemory);

        // Counter buffer (host-visible for readback of instance count)
        core::BufferInfoRequest counterRequest(vkDevice, device.getPhysicalDevice());
        counterRequest.size = sizeof(uint32_t);
        counterRequest.usage = vk::BufferUsageFlagBits::eStorageBuffer;
        counterRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(counterRequest,
                                            vegetation.grassCounterBuffer,
                                            vegetation.grassCounterBufferMemory);

        vegetation.grassCounterMapped = vkDevice.mapMemory(
            vegetation.grassCounterBufferMemory, 0, sizeof(uint32_t));
        std::memset(vegetation.grassCounterMapped, 0, sizeof(uint32_t));

        vegetation.grassInstanceCapacity = maxInstances;
    }

    void GPUDrivenRenderer::initVegetationSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout,
                                                      vk::RenderPass renderPass)
    {
        // Wind system
        vegetation.windSystem = std::make_unique<vegetation::WindSystem>();
        vegetation.windSystem->init(device);

        // Buffer manager for vegetation tile allocations
        vegetation.bufferManager = std::make_unique<vegetation::VegetationBufferManager>();
        vegetation.bufferManager->init(device);

        // Grass stream manager
        vegetation.grassStreamManager = std::make_unique<vegetation::GrassStreamManager>();
        vegetation.grassStreamManager->init(device, *vegetation.bufferManager);

        // Vegetation (tree) stream manager
        vegetation.vegetationStreamManager = std::make_unique<vegetation::VegetationStreamManager>();
        vegetation.vegetationStreamManager->init(device, *vegetation.bufferManager);

        // Grass instance buffers - start with reasonable capacity
        // Can be resized later when more tiles are streamed
        constexpr uint32_t initialGrassCapacity = 1024 * 1024; // ~1M instances
        createGrassBuffers(initialGrassCapacity);

        // Grass compute pipeline
        vegetation.grassComputePipeline = std::make_unique<vegetation::GrassComputePipeline>();
        vegetation.grassComputePipeline->init(device);

        // Grass mesh shader pipeline
        // Use IBL descriptor set layout for camera data (set 1)
        vegetation.grassMeshPipeline = std::make_unique<vegetation::GrassMeshShaderPipeline>();
        vegetation.grassMeshPipeline->init(
            device,
            iblDescriptorSetLayout,
            vegetation.windSystem->getDescriptorSetLayout(),
            renderPass
        );

        vegetation.grassInitialized = true;
        vfLogInfo("GPUDrivenRenderer: Vegetation subsystems initialized");
    }

    void GPUDrivenRenderer::updateWind(float deltaTime, const ::vegetation::WindConfig& config)
    {
        if (vegetation.windSystem)
        {
            vegetation.windSystem->update(deltaTime, config);
        }
    }

    void GPUDrivenRenderer::updateVegetationStreaming(const std::vector<terrain::TerrainTile*>& visibleTiles,
                                                       const glm::vec3& cameraPosition)
    {
        if (!initialized || !vegetation.grassInitialized) return;

        // Sync vegetation tiles with visible terrain tiles.
        // Auto-register tiles that appear and auto-remove tiles that disappear.
        std::unordered_set<uint64_t> currentlyVisible;

        for (const auto* tile : visibleTiles)
        {
            if (!tile) continue;

            int32_t cx = tile->coord.x;
            int32_t cz = tile->coord.z;
            uint64_t key = makeTileKey(cx, cz);
            currentlyVisible.insert(key);

            // Register new tiles with stream managers
            if (!vegetation.registeredTileKeys.contains(key))
            {
                vegetation.registeredTileKeys.insert(key);

                if (vegetation.grassStreamManager)
                {
                    vegetation.grassStreamManager->addTile(cx, cz);
                }
                if (vegetation.vegetationStreamManager)
                {
                    vegetation.vegetationStreamManager->addTile(cx, cz);
                }
            }
        }

        // Remove tiles that are no longer visible
        auto it = vegetation.registeredTileKeys.begin();
        while (it != vegetation.registeredTileKeys.end())
        {
            if (!currentlyVisible.contains(*it))
            {
                uint64_t key = *it;
                int32_t cx = static_cast<int32_t>(key >> 32);
                int32_t cz = static_cast<int32_t>(key & 0xFFFFFFFF);

                if (vegetation.grassStreamManager)
                {
                    vegetation.grassStreamManager->removeTile(cx, cz);
                }
                if (vegetation.vegetationStreamManager)
                {
                    vegetation.vegetationStreamManager->removeTile(cx, cz);
                }

                it = vegetation.registeredTileKeys.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // Run per-frame streaming updates
        if (vegetation.grassStreamManager)
        {
            vegetation.grassStreamManager->update(cameraPosition);
        }
        if (vegetation.vegetationStreamManager)
        {
            vegetation.vegetationStreamManager->update(cameraPosition);
        }
    }

    void GPUDrivenRenderer::addVegetationTile(int32_t coordX, int32_t coordZ)
    {
        uint64_t key = makeTileKey(coordX, coordZ);
        vegetation.registeredTileKeys.insert(key);

        if (vegetation.grassStreamManager)
        {
            vegetation.grassStreamManager->addTile(coordX, coordZ);
        }
        if (vegetation.vegetationStreamManager)
        {
            vegetation.vegetationStreamManager->addTile(coordX, coordZ);
        }
    }

    void GPUDrivenRenderer::removeVegetationTile(int32_t coordX, int32_t coordZ)
    {
        uint64_t key = makeTileKey(coordX, coordZ);
        vegetation.registeredTileKeys.erase(key);

        if (vegetation.grassStreamManager)
        {
            vegetation.grassStreamManager->removeTile(coordX, coordZ);
        }
        if (vegetation.vegetationStreamManager)
        {
            vegetation.vegetationStreamManager->removeTile(coordX, coordZ);
        }
    }

    void GPUDrivenRenderer::markVegetationTileDirty(int32_t coordX, int32_t coordZ)
    {
        if (vegetation.grassStreamManager)
        {
            vegetation.grassStreamManager->markTileDirty(coordX, coordZ);
        }
        if (vegetation.vegetationStreamManager)
        {
            vegetation.vegetationStreamManager->markTileDirty(coordX, coordZ);
        }
    }

    void GPUDrivenRenderer::renderGrassDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                                             uint32_t screenWidth, uint32_t screenHeight)
    {
        if (!initialized || !vegetation.grassRenderingEnabled || !vegetation.grassInitialized)
        {
            return;
        }

        if (!vegetation.grassMeshPipeline || !vegetation.grassMeshPipeline->isInitialized())
        {
            return;
        }

        if (vegetation.currentGrassInstanceCount == 0)
        {
            return;
        }

        // Update shared descriptors
        // Pass IBL descriptor set as camera data source
        vegetation.grassMeshPipeline->updateSharedDescriptors(
            iblDescriptorSet,
            vegetation.windSystem ? vegetation.windSystem->getDescriptorSet() : vk::DescriptorSet{}
        );

        // Dispatch grass mesh shader
        vegetation.grassMeshPipeline->dispatch(
            cmd,
            vegetation.currentGrassInstanceCount,
            vegetation.grassFadeStart,
            vegetation.grassFadeEnd
        );
    }

    void GPUDrivenRenderer::cleanupVegetation()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        if (vegetation.grassMeshPipeline)
        {
            vegetation.grassMeshPipeline->cleanup();
            vegetation.grassMeshPipeline.reset();
        }

        if (vegetation.grassComputePipeline)
        {
            vegetation.grassComputePipeline->cleanup();
            vegetation.grassComputePipeline.reset();
        }

        if (vegetation.windSystem)
        {
            vegetation.windSystem->cleanup();
            vegetation.windSystem.reset();
        }

        if (vegetation.grassStreamManager)
        {
            vegetation.grassStreamManager->cleanup();
            vegetation.grassStreamManager.reset();
        }

        if (vegetation.vegetationStreamManager)
        {
            vegetation.vegetationStreamManager->cleanup();
            vegetation.vegetationStreamManager.reset();
        }

        if (vegetation.bufferManager)
        {
            vegetation.bufferManager->cleanup();
            vegetation.bufferManager.reset();
        }

        if (vegetation.grassCounterMapped)
        {
            vkDevice.unmapMemory(vegetation.grassCounterBufferMemory);
            vegetation.grassCounterMapped = nullptr;
        }

        core::BufferUtilities::destroyBuffer(vkDevice,
                                             vegetation.grassInstanceBuffer,
                                             vegetation.grassInstanceBufferMemory);
        core::BufferUtilities::destroyBuffer(vkDevice,
                                             vegetation.grassCounterBuffer,
                                             vegetation.grassCounterBufferMemory);

        vegetation.registeredTileKeys.clear();
        vegetation.grassInitialized = false;
    }
}
