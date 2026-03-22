#include "GPUDrivenRenderer.hpp"
#include "print/Log.hpp"
#include <cmath>
#include "../vegetation/GrassComputePipeline.hpp"
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

        vegetation.grassComputePipeline = std::make_unique<vegetation::GrassComputePipeline>();
        vegetation.grassComputePipeline->init(device);

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
        if (!vegetation.grassComputePipeline || !vegetation.grassComputePipeline->isInitialized()) return;

        struct TileDispatchInfo
        {
            const terrain::TerrainTile* tile;
            uint32_t texelCount;
            uint32_t activeTypeMask; // bitmask of vegetation types with initialized density
        };
        std::vector<TileDispatchInfo> dispatchTiles;

        float maxVegDist = vegetation.grassConfig.fadeEndDistance;

        uint32_t maxTexelCount = 0;
        for (const auto* tile : visibleTiles)
        {
            if (!tile) continue;

            // Check which vegetation types have density data
            uint32_t typeMask = 0;
            uint32_t tc = 0;
            for (uint32_t t = 0; t < ::vegetation::VEGETATION_TYPE_COUNT; ++t)
            {
                if (tile->vegetationDensityMaps[t].isInitialized())
                {
                    typeMask |= (1u << t);
                    tc = static_cast<uint32_t>(tile->vegetationDensityMaps[t].getTexelCount());
                }
            }
            if (typeMask == 0 || tc == 0) continue;

            // CPU-side skip: tiles beyond vegetation draw distance
            float tileCenterX = static_cast<float>(tile->coord.x) * tile->config.worldTileSize
                              + tile->config.worldTileSize * 0.5f;
            float tileCenterZ = static_cast<float>(tile->coord.z) * tile->config.worldTileSize
                              + tile->config.worldTileSize * 0.5f;
            float tdx = cachedCamera.position.x - tileCenterX;
            float tdz = cachedCamera.position.z - tileCenterZ;
            float tileDistSq = tdx * tdx + tdz * tdz;
            float cullDist = maxVegDist + tile->config.worldTileSize;
            if (tileDistSq > cullDist * cullDist) continue;

            dispatchTiles.push_back({tile, tc, typeMask});
            if (tc > maxTexelCount) maxTexelCount = tc;
        }

        if (dispatchTiles.empty())
        {
            vegetation.currentGrassInstanceCount = 0;
            return;
        }

        uint32_t tileCount = static_cast<uint32_t>(dispatchTiles.size());

        ensureTileStagingBuffers(maxTexelCount, tileCount);

        // Per-tile slot layout in staging buffer:
        //   [density floats | height floats | hole uint32s] per tile
        vk::DeviceSize densityPerTile = maxTexelCount * sizeof(float);
        vk::DeviceSize heightPerTile = maxTexelCount * sizeof(float);
        vk::DeviceSize holePerTile = maxTexelCount * sizeof(uint32_t);
        vk::DeviceSize slotSize = densityPerTile + heightPerTile + holePerTile;

        // Phase 1: Upload ALL tiles' height+hole data to staging buffer (CPU side)
        // Density is uploaded per-type in the dispatch loop below
        auto* basePtr = static_cast<uint8_t*>(vegetation.tileStagingMapped);

        for (uint32_t i = 0; i < tileCount; ++i)
        {
            const auto& info = dispatchTiles[i];
            uint8_t* slotPtr = basePtr + static_cast<size_t>(i) * slotSize;
            vk::DeviceSize actualDensitySize = info.texelCount * sizeof(float);
            vk::DeviceSize actualHeightSize = info.texelCount * sizeof(float);
            vk::DeviceSize actualHoleSize = info.texelCount * sizeof(uint32_t);

            // Upload type 0 (grass) density as default; will be overwritten per-type in dispatch loop
            if (info.tile->vegetationDensityMaps[0].isInitialized())
                std::memcpy(slotPtr, info.tile->vegetationDensityMaps[0].densityData.data(), actualDensitySize);
            else
                std::memset(slotPtr, 0, actualDensitySize);

            uint8_t* heightPtr = slotPtr + densityPerTile;
            if (info.tile->heightData.size() >= info.texelCount)
            {
                std::memcpy(heightPtr, info.tile->heightData.data(), actualHeightSize);
            }
            else
            {
                std::memset(heightPtr, 0, actualHeightSize);
            }

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

        // Phase 3: Per-tile, per-type GPU copy from staging[offset] → compute buffer, then dispatch
        bool isFirstDispatch = true;
        for (uint32_t i = 0; i < tileCount; ++i)
        {
            const auto& info = dispatchTiles[i];
            vk::DeviceSize stagingOffset = static_cast<vk::DeviceSize>(i) * slotSize;
            vk::DeviceSize actualHeightSize = info.texelCount * sizeof(float);
            vk::DeviceSize actualHoleSize = info.texelCount * sizeof(uint32_t);
            vk::DeviceSize actualDensitySize = info.texelCount * sizeof(float);

            // Compute shared push constant fields once per tile
            vegetation::GrassComputePushConstants basePushConstants{};
            basePushConstants.tileWorldOrigin = glm::vec2(
                static_cast<float>(info.tile->coord.x) * info.tile->config.worldTileSize,
                static_cast<float>(info.tile->coord.z) * info.tile->config.worldTileSize);
            basePushConstants.tileWorldSize = info.tile->config.worldTileSize;
            basePushConstants.vertexSpacing = info.tile->config.getVertexSpacing();
            basePushConstants.verticesPerSide = info.tile->config.getVertexCount();
            basePushConstants.maxInstances = vegetation.grassInstanceCapacity;
            basePushConstants.slopeLimit = vegetation.grassConfig.slopeLimit;
            basePushConstants.heightMin = vegetation.grassConfig.heightMin;
            basePushConstants.heightMax = vegetation.grassConfig.heightMax;
            basePushConstants.widthMin = vegetation.grassConfig.widthMin;
            basePushConstants.widthMax = vegetation.grassConfig.widthMax;
            basePushConstants.time = cachedCamera.time;
            basePushConstants.cameraX = cachedCamera.position.x;
            basePushConstants.cameraZ = cachedCamera.position.z;
            basePushConstants.densityFadeStart = vegetation.grassConfig.fadeStartDistance
                                               * vegetation.grassConfig.densityFadeStartFactor;
            basePushConstants.densityFadeEnd = vegetation.grassConfig.fadeEndDistance;
            basePushConstants.minDensityScale = vegetation.grassConfig.minDensityScale;

            // Terrain LOD density scaling
            float baseDensityMult = vegetation.grassConfig.densityMultiplier;
            if (vegetation.grassConfig.terrainLODIntegration)
            {
                float tileCenterX = basePushConstants.tileWorldOrigin.x + basePushConstants.tileWorldSize * 0.5f;
                float tileCenterZ = basePushConstants.tileWorldOrigin.y + basePushConstants.tileWorldSize * 0.5f;
                float ldx = cachedCamera.position.x - tileCenterX;
                float ldz = cachedCamera.position.z - tileCenterZ;
                float tileDist = std::sqrt(ldx * ldx + ldz * ldz);

                float lodScale = 1.0f;
                if (tileDist > 320.0f) lodScale = 0.1f;
                else if (tileDist > 160.0f) lodScale = 0.25f;
                else if (tileDist > 80.0f) lodScale = 0.5f;
                else if (tileDist > 40.0f) lodScale = 0.75f;

                baseDensityMult *= lodScale;
            }

            // Dispatch for each active vegetation type on this tile
            for (uint32_t vegType = 0; vegType < ::vegetation::VEGETATION_TYPE_COUNT; ++vegType)
            {
                if (!(info.activeTypeMask & (1u << vegType))) continue;

                const auto& densityMap = info.tile->vegetationDensityMaps[vegType];
                if (!densityMap.isInitialized()) continue;

                // Inter-dispatch barrier (compute→transfer) before overwriting compute buffers
                if (!isFirstDispatch)
                {
                    vk::MemoryBarrier interBarrier(
                        vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
                        vk::AccessFlagBits::eTransferWrite);
                    cmd.pipelineBarrier(
                        vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eTransfer,
                        vk::DependencyFlags{},
                        1, &interBarrier,
                        0, nullptr,
                        0, nullptr);
                }

                // Update density data in staging buffer for this vegetation type
                {
                    uint8_t* slotPtr = basePtr + static_cast<size_t>(i) * slotSize;
                    std::memcpy(slotPtr, densityMap.densityData.data(), actualDensitySize);
                }

                // Copy tile data from staging → device-local compute buffers
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

                // vegType = density map slot = billboard entry index
                // Each entry has its own density map, dispatched independently
                auto pushConstants = basePushConstants;
                pushConstants.densityMultiplier = baseDensityMult;
                pushConstants.vegetationType = 0; // Always billboard
                pushConstants.paletteEntryIndex = 0;
                pushConstants.paletteEntryCount = 1; // Each entry dispatches independently, no hash partitioning

                // Look up billboard palette entry for this slot
                if (vegType < static_cast<uint32_t>(vegetation.billboardPalette.size()))
                {
                    const auto& entry = vegetation.billboardPalette[vegType];
                    if (!entry.visible) { isFirstDispatch = false; continue; }
                    pushConstants.billboardTextureIndex = entry.bindlessIndex;
                    pushConstants.billboardMode = entry.mode;
                    pushConstants.entryScaleMin = entry.scaleMin;
                    pushConstants.entryScaleMax = entry.scaleMax;
                    pushConstants.densityMultiplier = baseDensityMult * entry.densityMultiplier;
                }
                else
                {
                    // Density map exists but no palette entry for it - skip
                    isFirstDispatch = false;
                    continue;
                }

                vegetation.grassComputePipeline->dispatch(cmd, info.texelCount, pushConstants);
                isFirstDispatch = false;
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

        if (maxPossibleInstances > vegetation.grassInstanceCapacity)
        {
            vfLogError("Vegetation instance buffer overflow! Estimated {} instances, capacity {}. Reduce density or entry count.",
                       maxPossibleInstances, vegetation.grassInstanceCapacity);
        }

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

        if (vegetation.tileStagingMapped)
        {
            vkDevice.unmapMemory(vegetation.tileStagingBufferMemory);
            vegetation.tileStagingMapped = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.tileStagingBuffer, vegetation.tileStagingBufferMemory);

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
