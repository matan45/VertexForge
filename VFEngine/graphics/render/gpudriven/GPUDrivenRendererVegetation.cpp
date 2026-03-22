#include "GPUDrivenRenderer.hpp"
#include "print/Log.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>
#include "../vegetation/GrassMeshShaderPipeline.hpp"
#include "../vegetation/WindSystem.hpp"
#include "../vegetation/VegetationGPUTypes.hpp"
#include "../vegetation/VegetationBufferManager.hpp"
#include "../vegetation/GrassStreamManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "terrain/TerrainTile.hpp"
#include "vegetation/VegetationTypes.hpp"
#include "vegetation/WindConfig.hpp"
#include "scene/BindlessTextureManager.hpp"

// Windows defines MemoryBarrier as a macro - undefine it to use vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::gpudriven
{
    uint64_t GPUDrivenRenderer::makeTileKey(int32_t x, int32_t z)
    {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
               static_cast<uint64_t>(static_cast<uint32_t>(z));
    }

    void GPUDrivenRenderer::createGrassBuffers(uint32_t maxInstances)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DeviceSize instanceSize = maxInstances * sizeof(vegetation::GrassInstanceGPU);

        core::BufferInfoRequest instanceRequest(vkDevice, device.getPhysicalDevice());
        instanceRequest.size = instanceSize;
        instanceRequest.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
        instanceRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::BufferUtilities::createBuffer(instanceRequest,
                                            vegetation.grassInstanceBuffer,
                                            vegetation.grassInstanceBufferMemory);

        // Counter buffer (device-local, reset via vkCmdFillBuffer on GPU timeline)
        core::BufferInfoRequest counterRequest(vkDevice, device.getPhysicalDevice());
        counterRequest.size = sizeof(uint32_t);
        counterRequest.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
        counterRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::BufferUtilities::createBuffer(counterRequest,
                                            vegetation.grassCounterBuffer,
                                            vegetation.grassCounterBufferMemory);

        vegetation.grassInstanceCapacity = maxInstances;
    }

    void GPUDrivenRenderer::initVegetationSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout,
                                                      vk::RenderPass renderPass)
    {
        vegetation.windSystem = std::make_unique<vegetation::WindSystem>();
        vegetation.windSystem->init(device);

        vegetation.bufferManager = std::make_unique<vegetation::VegetationBufferManager>();
        vegetation.bufferManager->init(device);

        vegetation.grassStreamManager = std::make_unique<vegetation::GrassStreamManager>();
        vegetation.grassStreamManager->init(device, *vegetation.bufferManager);

        // Grass instance buffers - start with reasonable capacity
        // Can be resized later when more tiles are streamed
        constexpr uint32_t initialGrassCapacity = 4 * 1024 * 1024; // ~4M instances
        createGrassBuffers(initialGrassCapacity);

        // No compute pipeline needed - instances are uploaded directly

        vegetation.grassMeshPipeline = std::make_unique<vegetation::GrassMeshShaderPipeline>();
        vk::DescriptorSetLayout lightLayout = lightBufferManager
            ? lightBufferManager->getDescriptorSetLayout()
            : vk::DescriptorSetLayout{};
        vk::DescriptorSetLayout bindlessLayout = bindlessTextures
            ? bindlessTextures->getDescriptorSetLayout()
            : vk::DescriptorSetLayout{};
        vegetation.grassMeshPipeline->init(
            device,
            iblDescriptorSetLayout,
            vegetation.windSystem->getDescriptorSetLayout(),
            lightLayout,
            bindlessLayout,
            renderPass
        );

        vegetation.grassInitialized = true;

        vegetation.cachedIBLLayout = iblDescriptorSetLayout;
        vegetation.cachedRenderPass = renderPass;

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
                                                       const std::vector<terrain::TerrainTile*>& allLoadedTiles,
                                                       const glm::vec3& cameraPosition)
    {
        if (!initialized || !vegetation.grassInitialized) return;

        // Cache ALL loaded tiles for grass compute dispatch later in the frame
        // Using allLoadedTiles instead of visibleTiles prevents grass from disappearing
        // when tiles leave the camera frustum or stream out and back in
        vegetation.cachedVisibleTiles.assign(allLoadedTiles.begin(), allLoadedTiles.end());

        // Sync tile size from actual terrain config (may differ from default 32)
        if (!allLoadedTiles.empty() && allLoadedTiles[0])
        {
            float actualTileSize = allLoadedTiles[0]->config.worldTileSize;
            if (vegetation.grassStreamManager)
            {
                auto cfg = vegetation.grassStreamManager->getConfig();
                cfg.worldTileSize = actualTileSize;
                vegetation.grassStreamManager->setConfig(cfg);
            }
        }

        // Sync vegetation tiles with all loaded terrain tiles.
        // Auto-register tiles that appear and auto-remove tiles that stream out.
        std::unordered_set<uint64_t> currentlyLoaded;

        for (const auto* tile : allLoadedTiles)
        {
            if (!tile) continue;

            int32_t cx = tile->coord.x;
            int32_t cz = tile->coord.z;
            uint64_t key = makeTileKey(cx, cz);
            currentlyLoaded.insert(key);

            if (!vegetation.registeredTileKeys.contains(key))
            {
                vegetation.registeredTileKeys.insert(key);

                if (vegetation.grassStreamManager)
                {
                    vegetation.grassStreamManager->addTile(cx, cz);
                }
            }
        }

        // Remove tiles that are no longer loaded (streamed out)
        auto it = vegetation.registeredTileKeys.begin();
        while (it != vegetation.registeredTileKeys.end())
        {
            if (!currentlyLoaded.contains(*it))
            {
                uint64_t key = *it;
                int32_t cx = static_cast<int32_t>(key >> 32);
                int32_t cz = static_cast<int32_t>(key & 0xFFFFFFFF);

                if (vegetation.grassStreamManager)
                {
                    vegetation.grassStreamManager->removeTile(cx, cz);
                }

                it = vegetation.registeredTileKeys.erase(it);
            }
            else
            {
                ++it;
            }
        }

        if (vegetation.grassStreamManager)
        {
            vegetation.grassStreamManager->update(cameraPosition);
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
    }

    void GPUDrivenRenderer::removeVegetationTile(int32_t coordX, int32_t coordZ)
    {
        uint64_t key = makeTileKey(coordX, coordZ);
        vegetation.registeredTileKeys.erase(key);

        if (vegetation.grassStreamManager)
        {
            vegetation.grassStreamManager->removeTile(coordX, coordZ);
        }
    }

    void GPUDrivenRenderer::clearVegetationData()
    {
        for (uint64_t key : vegetation.registeredTileKeys)
        {
            int32_t coordX = static_cast<int32_t>(key >> 32);
            int32_t coordZ = static_cast<int32_t>(key & 0xFFFFFFFF);

            if (vegetation.grassStreamManager)
            {
                vegetation.grassStreamManager->removeTile(coordX, coordZ);
            }
        }
        vegetation.registeredTileKeys.clear();

        // Clear cached tile pointers to prevent dangling pointer access
        // in dispatchGrassCompute on the next frame
        vegetation.cachedVisibleTiles.clear();
        vegetation.currentGrassInstanceCount = 0;
    }

    void GPUDrivenRenderer::markVegetationTileDirty(int32_t coordX, int32_t coordZ)
    {
        if (vegetation.grassStreamManager)
        {
            vegetation.grassStreamManager->markTileDirty(coordX, coordZ);
        }
    }

    void GPUDrivenRenderer::ensureTileStagingBuffers(uint32_t texelsPerTile, uint32_t tileCount)
    {
        bool needsResize = (vegetation.tileComputeCapacity < texelsPerTile) ||
                           (vegetation.tileStagingTileSlots < tileCount);
        if (!needsResize) return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (vegetation.tileStagingMapped)
        {
            vkDevice.unmapMemory(vegetation.tileStagingBufferMemory);
            vegetation.tileStagingMapped = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.tileStagingBuffer, vegetation.tileStagingBufferMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.tileComputeDensity, vegetation.tileComputeDensityMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.tileComputeHeight, vegetation.tileComputeHeightMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.tileComputeHole, vegetation.tileComputeHoleMemory);

        vk::DeviceSize densityPerTile = texelsPerTile * sizeof(float);
        vk::DeviceSize heightPerTile = texelsPerTile * sizeof(float);
        vk::DeviceSize holePerTile = texelsPerTile * sizeof(uint32_t);
        vk::DeviceSize perTileTotal = densityPerTile + heightPerTile + holePerTile;

        // Single large staging buffer with room for all tiles (persistently mapped)
        {
            core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
            request.size = perTileTotal * tileCount;
            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, vegetation.tileStagingBuffer, vegetation.tileStagingBufferMemory);
            vegetation.tileStagingMapped = vkDevice.mapMemory(vegetation.tileStagingBufferMemory, 0, request.size);
        }

        // Device-local compute input buffers (single tile — copied per dispatch)
        auto createComputeBuffer = [&](vk::DeviceSize size, vk::Buffer& buffer, vk::DeviceMemory& memory)
        {
            core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
            request.size = size;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, buffer, memory);
        };

        createComputeBuffer(densityPerTile, vegetation.tileComputeDensity, vegetation.tileComputeDensityMemory);
        createComputeBuffer(heightPerTile, vegetation.tileComputeHeight, vegetation.tileComputeHeightMemory);
        createComputeBuffer(holePerTile, vegetation.tileComputeHole, vegetation.tileComputeHoleMemory);

        vegetation.tileComputeCapacity = texelsPerTile;
        vegetation.tileStagingTileSlots = tileCount;
        vegetation.tileStagingTexelsPerSlot = texelsPerTile;
    }

    void GPUDrivenRenderer::dispatchGrassCompute(vk::CommandBuffer cmd,
                                                 const std::vector<terrain::TerrainTile*>& visibleTiles)
    {
        if (!initialized || !vegetation.grassInitialized) return;
        if (!vegetation.grassRenderingEnabled) return;

        // Check if any tile needs re-upload
        bool anyDirty = false;
        for (const auto* tile : visibleTiles)
        {
            if (tile && tile->billboardInstancesGPUDirty)
            {
                anyDirty = true;
                break;
            }
        }

        // Skip upload if nothing changed and we already have data
        if (!anyDirty && vegetation.currentGrassInstanceCount > 0)
            return;

        // Only rebuild when dirty
        if (!anyDirty && vegetation.currentGrassInstanceCount == 0)
        {
            // First frame or no instances - check if any tiles have instances
            for (const auto* tile : visibleTiles)
            {
                if (tile && !tile->billboardInstances.empty())
                {
                    anyDirty = true;
                    break;
                }
            }
            if (!anyDirty) return;
        }

        // Collect all billboard instances from visible tiles
        std::vector<vegetation::GrassInstanceGPU> allInstances;

        float maxVegDist = vegetation.grassConfig.fadeEndDistance;

        for (auto* tile : visibleTiles)
        {
            if (!tile || tile->billboardInstances.empty()) continue;

            // Distance check per tile
            float tileCenterX = static_cast<float>(tile->coord.x) * tile->config.worldTileSize
                              + tile->config.worldTileSize * 0.5f;
            float tileCenterZ = static_cast<float>(tile->coord.z) * tile->config.worldTileSize
                              + tile->config.worldTileSize * 0.5f;
            float tdx = cachedCamera.position.x - tileCenterX;
            float tdz = cachedCamera.position.z - tileCenterZ;
            float tileDistSq = tdx * tdx + tdz * tdz;
            float cullDist = maxVegDist + tile->config.worldTileSize;
            if (tileDistSq > cullDist * cullDist) continue;

            // Convert BillboardInstance → GrassInstanceGPU
            for (const auto& inst : tile->billboardInstances)
            {
                if (allInstances.size() >= vegetation.grassInstanceCapacity) break;

                uint32_t texIdx = 0xFFFFFFFF;
                uint32_t bbMode = 0;
                if (inst.paletteEntryIndex < static_cast<uint32_t>(vegetation.billboardPalette.size()))
                {
                    const auto& entry = vegetation.billboardPalette[inst.paletteEntryIndex];
                    if (!entry.visible) continue;
                    texIdx = entry.bindlessIndex;
                    bbMode = entry.mode;
                }

                vegetation::GrassInstanceGPU gpu;
                gpu.positionAndRotation = glm::vec4(inst.position, inst.rotation);
                gpu.scaleAndDensity = glm::vec4(inst.scale, inst.scale * 0.5f, 1.0f, inst.windPhase);
                gpu.color = glm::vec4(static_cast<float>(texIdx), static_cast<float>(bbMode), 1.0f, 0.0f);
                allInstances.push_back(gpu);
            }

            // Clear dirty flag
            tile->billboardInstancesGPUDirty = false;
        }

        uint32_t instanceCount = static_cast<uint32_t>(allInstances.size());
        vegetation.currentGrassInstanceCount = instanceCount;

        if (instanceCount == 0) return;

        vk::DeviceSize dataSize = instanceCount * sizeof(vegetation::GrassInstanceGPU);

        // Ensure staging buffer is large enough
        if (dataSize > vegetation.instanceStagingCapacity)
        {
            vk::Device vkDevice = device.getLogicalDevice();
            vkDevice.waitIdle();

            if (vegetation.instanceStagingMapped)
            {
                vkDevice.unmapMemory(vegetation.instanceStagingMemory);
                vegetation.instanceStagingMapped = nullptr;
            }
            core::BufferUtilities::destroyBuffer(vkDevice,
                vegetation.instanceStagingBuffer, vegetation.instanceStagingMemory);

            vk::DeviceSize allocSize = dataSize * 2;
            core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
            request.size = allocSize;
            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request,
                vegetation.instanceStagingBuffer, vegetation.instanceStagingMemory);
            vegetation.instanceStagingMapped = vkDevice.mapMemory(vegetation.instanceStagingMemory, 0, allocSize);
            vegetation.instanceStagingCapacity = static_cast<uint32_t>(allocSize);
        }

        std::memcpy(vegetation.instanceStagingMapped, allInstances.data(), dataSize);

        vk::BufferCopy copyRegion(0, 0, dataSize);
        cmd.copyBuffer(vegetation.instanceStagingBuffer, vegetation.grassInstanceBuffer, copyRegion);

        cmd.updateBuffer(vegetation.grassCounterBuffer, 0, sizeof(uint32_t), &instanceCount);

        vk::MemoryBarrier barrier(
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead);
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eTaskShaderEXT | vk::PipelineStageFlagBits::eMeshShaderEXT,
            vk::DependencyFlags{},
            1, &barrier,
            0, nullptr,
            0, nullptr);

        if (vegetation.grassMeshPipeline)
        {
            vegetation.grassMeshPipeline->updateGrassDataDescriptors(
                vegetation.grassInstanceBuffer,
                vegetation.grassCounterBuffer);
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

        vegetation.grassMeshPipeline->updateSharedDescriptors(
            iblDescriptorSet,
            vegetation.windSystem ? vegetation.windSystem->getDescriptorSet() : vk::DescriptorSet{},
            lightBufferManager ? lightBufferManager->getDescriptorSet() : vk::DescriptorSet{},
            bindlessTextures ? bindlessTextures->getDescriptorSet() : vk::DescriptorSet{}
        );

        vegetation.grassMeshPipeline->dispatch(
            cmd,
            vegetation.currentGrassInstanceCount,
            vegetation.grassConfig.fadeStartDistance,
            vegetation.grassConfig.fadeEndDistance,
            vegetation.grassConfig.baseColor,
            vegetation.grassConfig.tipColor,
            vegetation.grassConfig.sssDistortion,
            vegetation.grassConfig.sssPower,
            vegetation.grassConfig.sssScale
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

        if (vegetation.bufferManager)
        {
            vegetation.bufferManager->cleanup();
            vegetation.bufferManager.reset();
        }

        core::BufferUtilities::destroyBuffer(vkDevice,
                                             vegetation.grassInstanceBuffer,
                                             vegetation.grassInstanceBufferMemory);
        core::BufferUtilities::destroyBuffer(vkDevice,
                                             vegetation.grassCounterBuffer,
                                             vegetation.grassCounterBufferMemory);

        // Clean up instance staging buffer
        if (vegetation.instanceStagingMapped)
        {
            vkDevice.unmapMemory(vegetation.instanceStagingMemory);
            vegetation.instanceStagingMapped = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice,
            vegetation.instanceStagingBuffer, vegetation.instanceStagingMemory);
        vegetation.instanceStagingCapacity = 0;

        // Clean up old density staging buffers (if still present)
        if (vegetation.tileStagingMapped)
        {
            vkDevice.unmapMemory(vegetation.tileStagingBufferMemory);
            vegetation.tileStagingMapped = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.tileStagingBuffer, vegetation.tileStagingBufferMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.tileComputeDensity, vegetation.tileComputeDensityMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.tileComputeHeight, vegetation.tileComputeHeightMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.tileComputeHole, vegetation.tileComputeHoleMemory);

        vegetation.registeredTileKeys.clear();
        vegetation.grassInitialized = false;
    }
}
