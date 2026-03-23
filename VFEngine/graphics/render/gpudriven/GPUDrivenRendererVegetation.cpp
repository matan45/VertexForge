#include "GPUDrivenRenderer.hpp"
#include "print/Log.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/VegetationComponents.hpp"
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

        // Grass instance buffers - start with moderate capacity, grows on demand
        constexpr uint32_t initialGrassCapacity = 512 * 1024; // ~512K instances (~24MB)
        createGrassBuffers(initialGrassCapacity);

        vegetation.grassMeshPipeline = std::make_unique<vegetation::GrassMeshShaderPipeline>();
        vk::DescriptorSetLayout lightLayout = lightBufferManager
            ? lightBufferManager->getDescriptorSetLayout()
            : vk::DescriptorSetLayout{};
        vk::DescriptorSetLayout bindlessLayout = bindlessTextures
            ? bindlessTextures->getDescriptorSetLayout()
            : vk::DescriptorSetLayout{};
        vegetation.grassMeshPipeline->init(
            device,
            vegetation.windSystem->getDescriptorSetLayout(),
            lightLayout,
            bindlessLayout,
            renderPass
        );

        // Bind the shared camera buffer so grass reads correct camera data
        if (cameraBuffer)
        {
            vegetation.grassMeshPipeline->updateCameraDescriptor(cameraBuffer->getBuffer());
        }

        vegetation.grassInitialized = true;
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

        autoLoadBillboardPaletteFromECS();

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

    void GPUDrivenRenderer::autoLoadBillboardPaletteFromECS()
    {
        if (!vegetation.billboardPalette.empty()) return;
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::GrassComponent>();
        for (auto entity : view)
        {
            const auto& palette = view.get<components::GrassComponent>(entity).billboardPalette;
            if (!palette.empty())
                setBillboardPaletteFromEntries(palette);
            break;
        }
    }

    bool GPUDrivenRenderer::needsVegetationUpload(const std::vector<terrain::TerrainTile*>& tiles) const
    {
        for (const auto* tile : tiles)
        {
            if (tile && tile->billboardInstancesGPUDirty)
                return true;
        }
        if (vegetation.currentGrassInstanceCount == 0)
        {
            for (const auto* tile : tiles)
            {
                if (tile && !tile->billboardInstances.empty())
                    return true;
            }
        }
        return false;
    }

    std::vector<vegetation::GrassInstanceGPU> GPUDrivenRenderer::collectBillboardInstances(
        const std::vector<terrain::TerrainTile*>& tiles)
    {
        std::vector<vegetation::GrassInstanceGPU> result;
        float maxVegDist = vegetation.grassConfig.fadeEndDistance;

        for (auto* tile : tiles)
        {
            if (!tile || tile->billboardInstances.empty()) continue;

            float tileCenterX = static_cast<float>(tile->coord.x) * tile->config.worldTileSize
                              + tile->config.worldTileSize * 0.5f;
            float tileCenterZ = static_cast<float>(tile->coord.z) * tile->config.worldTileSize
                              + tile->config.worldTileSize * 0.5f;
            float tdx = cachedCamera.position.x - tileCenterX;
            float tdz = cachedCamera.position.z - tileCenterZ;
            float cullDist = maxVegDist + tile->config.worldTileSize;
            if (tdx * tdx + tdz * tdz > cullDist * cullDist) continue;

            for (const auto& inst : tile->billboardInstances)
            {
                if (result.size() >= vegetation.grassInstanceCapacity) break;

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
                gpu.color = glm::vec4(
                    glm::uintBitsToFloat(texIdx),
                    glm::uintBitsToFloat(bbMode),
                    1.0f, 0.0f);
                result.push_back(gpu);
            }

            tile->billboardInstancesGPUDirty = false;
        }
        return result;
    }

    void GPUDrivenRenderer::ensureInstanceStagingCapacity(vk::DeviceSize requiredSize)
    {
        if (requiredSize <= vegetation.instanceStagingCapacity) return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (vegetation.instanceStagingMapped)
        {
            vkDevice.unmapMemory(vegetation.instanceStagingMemory);
            vegetation.instanceStagingMapped = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice,
            vegetation.instanceStagingBuffer, vegetation.instanceStagingMemory);

        vk::DeviceSize allocSize = requiredSize * 2;
        core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
        request.size = allocSize;
        request.usage = vk::BufferUsageFlagBits::eTransferSrc;
        request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(request,
            vegetation.instanceStagingBuffer, vegetation.instanceStagingMemory);
        vegetation.instanceStagingMapped = vkDevice.mapMemory(vegetation.instanceStagingMemory, 0, allocSize);
        vegetation.instanceStagingCapacity = static_cast<uint32_t>(allocSize);
    }

    void GPUDrivenRenderer::dispatchGrassCompute(vk::CommandBuffer cmd,
                                                 const std::vector<terrain::TerrainTile*>& visibleTiles)
    {
        if (!initialized || !vegetation.grassInitialized) return;
        if (!vegetation.grassRenderingEnabled) return;

        autoLoadBillboardPaletteFromECS();

        if (!needsVegetationUpload(visibleTiles) && vegetation.currentGrassInstanceCount > 0)
            return;

        auto allInstances = collectBillboardInstances(visibleTiles);
        uint32_t instanceCount = static_cast<uint32_t>(allInstances.size());
        vegetation.currentGrassInstanceCount = instanceCount;
        if (instanceCount == 0) return;

        vk::DeviceSize dataSize = instanceCount * sizeof(vegetation::GrassInstanceGPU);

        if (instanceCount > vegetation.grassInstanceCapacity)
        {
            instanceCount = vegetation.grassInstanceCapacity;
            vegetation.currentGrassInstanceCount = instanceCount;
            dataSize = instanceCount * sizeof(vegetation::GrassInstanceGPU);
        }

        ensureInstanceStagingCapacity(dataSize);

        std::memcpy(vegetation.instanceStagingMapped, allInstances.data(), dataSize);
        cmd.copyBuffer(vegetation.instanceStagingBuffer, vegetation.grassInstanceBuffer, vk::BufferCopy(0, 0, dataSize));
        cmd.updateBuffer(vegetation.grassCounterBuffer, 0, sizeof(uint32_t), &instanceCount);

        vk::MemoryBarrier barrier(vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead);
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eTaskShaderEXT | vk::PipelineStageFlagBits::eMeshShaderEXT,
            {}, 1, &barrier, 0, nullptr, 0, nullptr);

        if (vegetation.grassMeshPipeline)
        {
            vegetation.grassMeshPipeline->updateGrassDataDescriptors(
                vegetation.grassInstanceBuffer, vegetation.grassCounterBuffer);
        }
    }

    void GPUDrivenRenderer::renderGrassDraw(vk::CommandBuffer cmd,
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

        // Update camera buffer each frame (data may have changed)
        if (cameraBuffer)
        {
            vegetation.grassMeshPipeline->updateCameraDescriptor(cameraBuffer->getBuffer());
        }

        vegetation.grassMeshPipeline->updateSharedDescriptors(
            vegetation.windSystem ? vegetation.windSystem->getDescriptorSet() : vk::DescriptorSet{},
            lightBufferManager ? lightBufferManager->getDescriptorSet() : vk::DescriptorSet{},
            bindlessTextures ? bindlessTextures->getDescriptorSet() : vk::DescriptorSet{}
        );

        vegetation::GrassDispatchParams params;
        params.instanceCount = vegetation.currentGrassInstanceCount;
        params.fadeStartDistance = vegetation.grassConfig.fadeStartDistance;
        params.fadeEndDistance = vegetation.grassConfig.fadeEndDistance;
        params.baseColor = vegetation.grassConfig.baseColor;
        params.tipColor = vegetation.grassConfig.tipColor;
        params.sssDistortion = vegetation.grassConfig.sssDistortion;
        params.sssPower = vegetation.grassConfig.sssPower;
        params.sssScale = vegetation.grassConfig.sssScale;
        vegetation.grassMeshPipeline->dispatch(cmd, params);
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

        if (vegetation.instanceStagingMapped)
        {
            vkDevice.unmapMemory(vegetation.instanceStagingMemory);
            vegetation.instanceStagingMapped = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice,
            vegetation.instanceStagingBuffer, vegetation.instanceStagingMemory);
        vegetation.instanceStagingCapacity = 0;

        vegetation.registeredTileKeys.clear();
        vegetation.grassInitialized = false;
    }
}
