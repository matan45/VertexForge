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

// Windows defines MemoryBarrier as a macro - undefine it to use vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

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

        // Cache visible tiles for grass compute dispatch later in the frame
        vegetation.cachedVisibleTiles.assign(visibleTiles.begin(), visibleTiles.end());

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

            // Build billboard instances from vegetation impostor requests and merge with billboard system
            vegetation.vegetationStreamManager->buildBillboardInstances();
            const auto& vegBillboards = vegetation.vegetationStreamManager->getBillboardInstances();
            if (!vegBillboards.empty() && billboard.initialized && billboard.renderingEnabled)
            {
                // Merge vegetation impostor billboards with existing billboard instances
                auto mergedInstances = billboard.instanceList;
                mergedInstances.insert(mergedInstances.end(), vegBillboards.begin(), vegBillboards.end());
                updateBillboards(mergedInstances);
            }
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

    void GPUDrivenRenderer::clearVegetationData()
    {
        // Remove all registered vegetation tiles and free their GPU buffers
        for (uint64_t key : vegetation.registeredTileKeys)
        {
            int32_t coordX = static_cast<int32_t>(key >> 32);
            int32_t coordZ = static_cast<int32_t>(key & 0xFFFFFFFF);

            if (vegetation.grassStreamManager)
            {
                vegetation.grassStreamManager->removeTile(coordX, coordZ);
            }
            if (vegetation.vegetationStreamManager)
            {
                vegetation.vegetationStreamManager->removeTile(coordX, coordZ);
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
        if (vegetation.vegetationStreamManager)
        {
            vegetation.vegetationStreamManager->markTileDirty(coordX, coordZ);
        }
    }

    void GPUDrivenRenderer::ensureTileStagingBuffers(uint32_t texelsPerTile, uint32_t tileCount)
    {
        bool needsResize = (vegetation.tileComputeCapacity < texelsPerTile) ||
                           (vegetation.tileStagingTileSlots < tileCount);
        if (!needsResize) return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        // Unmap and destroy old staging buffer
        if (vegetation.tileStagingMapped)
        {
            vkDevice.unmapMemory(vegetation.tileStagingBufferMemory);
            vegetation.tileStagingMapped = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.tileStagingBuffer, vegetation.tileStagingBufferMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.tileComputeDensity, vegetation.tileComputeDensityMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.tileComputeHeight, vegetation.tileComputeHeightMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.tileComputeHole, vegetation.tileComputeHoleMemory);

        // Per-tile data sizes
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
        if (!vegetation.grassComputePipeline || !vegetation.grassComputePipeline->isInitialized()) return;

        // Collect tiles with density data
        struct TileDispatchInfo
        {
            const terrain::TerrainTile* tile;
            uint32_t texelCount;
        };
        std::vector<TileDispatchInfo> dispatchTiles;

        uint32_t maxTexelCount = 0;
        for (const auto* tile : visibleTiles)
        {
            if (!tile || !tile->vegetationDensity.isInitialized()) continue;
            uint32_t tc = static_cast<uint32_t>(tile->vegetationDensity.getTexelCount());
            if (tc == 0) continue;
            dispatchTiles.push_back({tile, tc});
            if (tc > maxTexelCount) maxTexelCount = tc;
        }

        if (dispatchTiles.empty())
        {
            vegetation.currentGrassInstanceCount = 0;
            return;
        }

        uint32_t tileCount = static_cast<uint32_t>(dispatchTiles.size());

        // Ensure buffers are large enough for all tiles
        ensureTileStagingBuffers(maxTexelCount, tileCount);

        // Per-tile slot layout in staging buffer:
        //   [density floats | height floats | hole uint32s] per tile
        vk::DeviceSize densityPerTile = maxTexelCount * sizeof(float);
        vk::DeviceSize heightPerTile = maxTexelCount * sizeof(float);
        vk::DeviceSize holePerTile = maxTexelCount * sizeof(uint32_t);
        vk::DeviceSize slotSize = densityPerTile + heightPerTile + holePerTile;

        // Phase 1: Upload ALL tiles' data to staging buffer at different offsets (CPU side)
        auto* basePtr = static_cast<uint8_t*>(vegetation.tileStagingMapped);

        for (uint32_t i = 0; i < tileCount; ++i)
        {
            const auto& info = dispatchTiles[i];
            uint8_t* slotPtr = basePtr + static_cast<size_t>(i) * slotSize;
            vk::DeviceSize actualDensitySize = info.texelCount * sizeof(float);
            vk::DeviceSize actualHeightSize = info.texelCount * sizeof(float);
            vk::DeviceSize actualHoleSize = info.texelCount * sizeof(uint32_t);

            // Density
            std::memcpy(slotPtr, info.tile->vegetationDensity.densityData.data(), actualDensitySize);

            // Height
            uint8_t* heightPtr = slotPtr + densityPerTile;
            if (info.tile->heightData.size() >= info.texelCount)
            {
                std::memcpy(heightPtr, info.tile->heightData.data(), actualHeightSize);
            }
            else
            {
                std::memset(heightPtr, 0, actualHeightSize);
            }

            // Hole mask
            uint8_t* holePtr = slotPtr + densityPerTile + heightPerTile;
            if (info.tile->hasHoleMask())
            {
                auto* dst = reinterpret_cast<uint32_t*>(holePtr);
                for (uint32_t j = 0; j < info.texelCount && j < info.tile->holeMask.size(); ++j)
                    dst[j] = info.tile->holeMask[j];
                for (uint32_t j = static_cast<uint32_t>(info.tile->holeMask.size()); j < info.texelCount; ++j)
                    dst[j] = 0;
            }
            else
            {
                std::memset(holePtr, 0, actualHoleSize);
            }
        }

        // Phase 2: GPU commands — reset counter, then per-tile copy+dispatch

        // Reset counter to 0 on GPU timeline
        cmd.fillBuffer(vegetation.grassCounterBuffer, 0, sizeof(uint32_t), 0);

        vk::MemoryBarrier fillBarrier(
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::DependencyFlags{},
            1, &fillBarrier,
            0, nullptr,
            0, nullptr);

        // Update descriptors ONCE — point to device-local compute input buffers
        vegetation.grassComputePipeline->updateDescriptors(
            vegetation.tileComputeDensity,
            vegetation.tileComputeHeight,
            vegetation.tileComputeHole,
            vegetation.grassInstanceBuffer,
            vegetation.grassCounterBuffer);

        // Phase 3: Per-tile GPU copy from staging[offset] → compute buffer, then dispatch
        for (uint32_t i = 0; i < tileCount; ++i)
        {
            const auto& info = dispatchTiles[i];
            vk::DeviceSize stagingOffset = static_cast<vk::DeviceSize>(i) * slotSize;
            vk::DeviceSize actualDensitySize = info.texelCount * sizeof(float);
            vk::DeviceSize actualHeightSize = info.texelCount * sizeof(float);
            vk::DeviceSize actualHoleSize = info.texelCount * sizeof(uint32_t);

            // Copy this tile's data from staging to compute input buffers
            vk::BufferCopy densityCopy(stagingOffset, 0, actualDensitySize);
            cmd.copyBuffer(vegetation.tileStagingBuffer, vegetation.tileComputeDensity, densityCopy);

            vk::BufferCopy heightCopy(stagingOffset + densityPerTile, 0, actualHeightSize);
            cmd.copyBuffer(vegetation.tileStagingBuffer, vegetation.tileComputeHeight, heightCopy);

            vk::BufferCopy holeCopy(stagingOffset + densityPerTile + heightPerTile, 0, actualHoleSize);
            cmd.copyBuffer(vegetation.tileStagingBuffer, vegetation.tileComputeHole, holeCopy);

            // Barrier: transfer → compute
            vk::MemoryBarrier copyBarrier(
                vk::AccessFlagBits::eTransferWrite,
                vk::AccessFlagBits::eShaderRead);
            cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eComputeShader,
                vk::DependencyFlags{},
                1, &copyBarrier,
                0, nullptr,
                0, nullptr);

            vegetation::GrassComputePushConstants pushConstants{};
            pushConstants.tileWorldOrigin = glm::vec2(
                static_cast<float>(info.tile->coord.x) * info.tile->config.worldTileSize,
                static_cast<float>(info.tile->coord.z) * info.tile->config.worldTileSize);
            pushConstants.tileWorldSize = info.tile->config.worldTileSize;
            pushConstants.vertexSpacing = info.tile->config.getVertexSpacing();
            pushConstants.verticesPerSide = info.tile->config.getVertexCount();
            pushConstants.maxInstances = vegetation.grassInstanceCapacity;
            pushConstants.slopeLimit = vegetation.grassConfig.slopeLimit;
            pushConstants.densityMultiplier = vegetation.grassConfig.densityMultiplier;
            pushConstants.heightMin = vegetation.grassConfig.heightMin;
            pushConstants.heightMax = vegetation.grassConfig.heightMax;
            pushConstants.widthMin = vegetation.grassConfig.widthMin;
            pushConstants.widthMax = vegetation.grassConfig.widthMax;
            pushConstants.time = cachedCamera.time;

            vegetation.grassComputePipeline->dispatch(cmd, info.texelCount, pushConstants);

            // Barrier: compute reads/writes must finish before next tile overwrites compute buffers
            if (i + 1 < tileCount)
            {
                vk::MemoryBarrier interTileBarrier(
                    vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
                    vk::AccessFlagBits::eTransferWrite);
                cmd.pipelineBarrier(
                    vk::PipelineStageFlagBits::eComputeShader,
                    vk::PipelineStageFlagBits::eTransfer,
                    vk::DependencyFlags{},
                    1, &interTileBarrier,
                    0, nullptr,
                    0, nullptr);
            }
        }

        // Final barrier: compute writes → task/mesh shader reads
        vk::MemoryBarrier barrier(
            vk::AccessFlagBits::eShaderWrite,
            vk::AccessFlagBits::eShaderRead);
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eTaskShaderEXT | vk::PipelineStageFlagBits::eMeshShaderEXT,
            vk::DependencyFlags{},
            1, &barrier,
            0, nullptr,
            0, nullptr);

        // Task shader reads actual count from GPU counter — estimate upper bound for dispatch
        uint32_t maxPossibleInstances = tileCount * maxTexelCount * 4;
        vegetation.currentGrassInstanceCount = std::min(maxPossibleInstances, vegetation.grassInstanceCapacity);

        // Update mesh pipeline descriptors with the instance buffer
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
            vegetation.grassConfig.fadeStartDistance,
            vegetation.grassConfig.fadeEndDistance,
            vegetation.grassConfig.baseColor,
            vegetation.grassConfig.tipColor
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

        core::BufferUtilities::destroyBuffer(vkDevice,
                                             vegetation.grassInstanceBuffer,
                                             vegetation.grassInstanceBufferMemory);
        core::BufferUtilities::destroyBuffer(vkDevice,
                                             vegetation.grassCounterBuffer,
                                             vegetation.grassCounterBufferMemory);

        // Unmap and destroy staging buffer
        if (vegetation.tileStagingMapped)
        {
            vkDevice.unmapMemory(vegetation.tileStagingBufferMemory);
            vegetation.tileStagingMapped = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.tileStagingBuffer, vegetation.tileStagingBufferMemory);

        // Destroy compute input buffers
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.tileComputeDensity, vegetation.tileComputeDensityMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.tileComputeHeight, vegetation.tileComputeHeightMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.tileComputeHole, vegetation.tileComputeHoleMemory);

        vegetation.tileComputeCapacity = 0;
        vegetation.tileStagingTileSlots = 0;
        vegetation.tileStagingTexelsPerSlot = 0;

        vegetation.registeredTileKeys.clear();
        vegetation.grassInitialized = false;
    }
}
