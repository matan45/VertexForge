#include "GPUDrivenRenderer.hpp"
#include "../vegetation/GrassComputePipeline.hpp"
#include "../vegetation/GrassMeshShaderPipeline.hpp"
#include "../vegetation/WindSystem.hpp"
#include "../vegetation/VegetationGPUTypes.hpp"
#include "../vegetation/VegetationBufferManager.hpp"
#include "../vegetation/GrassStreamManager.hpp"
#include "../vegetation/VegetationStreamManager.hpp"
#include "../vegetation/VegetationCullLODPipeline.hpp"
#include "../vegetation/VegetationMeshShaderPipeline.hpp"
#include "GPUDrivenCameraBuffer.hpp"
#include "MeshShaderPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "terrain/TerrainTile.hpp"
#include "vegetation/WindConfig.hpp"
#include "vegetation/VegetationPlacementData.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "resource/ResourceManager.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "material/MaterialInstanceTypes.hpp"
#include "BindlessTextureManager.hpp"
#include "resource/AssetLifecycleManager.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

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

        // --- Tree LOD pipeline (cull/LOD compute + mesh) ---
        initTreeLODPipeline(iblDescriptorSetLayout, renderPass);

        vfLogInfo("GPUDrivenRenderer: Vegetation subsystems initialized");
    }

    void GPUDrivenRenderer::updateWind(float deltaTime, const ::vegetation::WindConfig& config)
    {
        if (vegetation.windSystem)
        {
            vegetation.windSystem->update(deltaTime, config);
        }
    }

    void GPUDrivenRenderer::updateVegetationSpecies(uint32_t speciesId,
                                                      const ::vegetation::VegetationSpeciesConfig& config)
    {
        auto& cached = vegetation.cachedSpecies[speciesId];

        // Estimate billboard size from species scale
        float avgScale = (config.minScale + config.maxScale) * 0.5f;
        cached.billboardSize = glm::vec2(avgScale * 2.0f, avgScale * 4.0f);

        // LOD distances from species config
        cached.lod1Distance = config.lod1Distance;
        cached.lod2Distance = config.lod2Distance;
        cached.maxRenderDistance = config.maxRenderDistance;

        // Load/reload species mesh if path changed
        if (!config.meshPath.empty() && config.meshPath != cached.meshPath)
        {
            loadSpeciesMesh(speciesId, config.meshPath);
        }
        else if (config.meshPath.empty() && cached.hasMesh)
        {
            unloadSpeciesMesh(speciesId);
        }

        // Load/reload species material if path changed
        if (!config.materialPath.empty() && config.materialPath != cached.materialPath)
        {
            // Release old material from lifecycle manager
            if (!cached.materialPath.empty())
            {
                resource::AssetLifecycleManager::instance().release(cached.materialPath);
            }

            // Acquire BEFORE registerMaterialTextures so isTracked() returns true
            // when setting up texture dependencies
            resource::AssetLifecycleManager::instance().acquire(
                config.materialPath, resource::AssetType::Material);

            if (registerMaterialTextures(config.materialPath))
            {
                // Extract albedo texture path from material
                mesh::ExtractedPBRValues pbrValues;
                if (material::isInstanceFile(config.materialPath))
                {
                    auto instData = resource::ResourceManager::loadMaterialInstance(config.materialPath);
                    if (instData && !instData->parentMaterialPath.empty())
                    {
                        auto parentMat = resource::ResourceManager::loadMaterial(instData->parentMaterialPath);
                        if (parentMat)
                            pbrValues = mesh::MaterialPBRExtractor::extractPBRFromInstance(*instData, *parentMat);
                    }
                }
                else
                {
                    auto matData = resource::ResourceManager::loadMaterial(config.materialPath);
                    if (matData)
                        pbrValues = mesh::MaterialPBRExtractor::extractPBRFromMaterial(*matData);
                }

                if (!pbrValues.albedoTexturePath.empty() && bindlessTextures)
                {
                    cached.materialTextureIndex = bindlessTextures->getTextureIndex(pbrValues.albedoTexturePath);
                    cached.hasMaterial = (cached.materialTextureIndex != 0 && cached.materialTextureIndex != 0xFFFFFFFF);
                }
                cached.materialPath = config.materialPath;
                vegetation.speciesRenderInfoDirty = true;
            }
            else
            {
                // Registration failed, release the acquire
                resource::AssetLifecycleManager::instance().release(config.materialPath);
            }
        }
        else if (config.materialPath.empty() && cached.hasMaterial)
        {
            // Release material from lifecycle manager
            if (!cached.materialPath.empty())
            {
                resource::AssetLifecycleManager::instance().release(cached.materialPath);
            }
            cached.materialTextureIndex = 0;
            cached.hasMaterial = false;
            cached.materialPath.clear();
            vegetation.speciesRenderInfoDirty = true;
        }

    }

    void GPUDrivenRenderer::removeVegetationSpecies(uint32_t speciesId)
    {
        auto it = vegetation.cachedSpecies.find(speciesId);
        if (it != vegetation.cachedSpecies.end())
        {
            // Release material from lifecycle manager
            if (!it->second.materialPath.empty())
            {
                resource::AssetLifecycleManager::instance().release(it->second.materialPath);
            }
            if (it->second.hasMesh)
            {
                unloadSpeciesMesh(speciesId);
            }
            vegetation.cachedSpecies.erase(it);
            vegetation.speciesRenderInfoDirty = true;
        }
    }

    void GPUDrivenRenderer::clearAllVegetationSpecies()
    {
        // Clear cached tile pointers to prevent dangling pointer access
        // if terrain is deleted while vegetation species are being cleared
        vegetation.cachedVisibleTiles.clear();
        vegetation.currentTreeInstanceCount = 0;

        for (auto& [id, cached] : vegetation.cachedSpecies)
        {
            // Release material from lifecycle manager
            if (!cached.materialPath.empty())
            {
                resource::AssetLifecycleManager::instance().release(cached.materialPath);
            }
            if (cached.hasMesh && meshletBuffer)
            {
                meshletBuffer->freeAllMeshlets(cached.meshPath);
            }
        }
        vegetation.cachedSpecies.clear();
        vegetation.speciesRenderInfoDirty = true;
    }

    void GPUDrivenRenderer::loadSpeciesMesh(uint32_t speciesId, const std::string& meshPath)
    {
        auto it = vegetation.cachedSpecies.find(speciesId);
        if (it == vegetation.cachedSpecies.end()) return;
        auto& cached = it->second;

        if (cached.meshPath == meshPath && cached.hasMesh) return; // Already loaded

        // Unload previous mesh if any
        if (cached.hasMesh && !cached.meshPath.empty())
        {
            unloadSpeciesMesh(speciesId);
        }

        if (meshPath.empty()) return;

        // Open the .vfMesh stream
        auto streamHandle = resource::MeshStreamResource::openStream(meshPath);
        if (!streamHandle)
        {
            vfLogError("GPUDrivenRenderer: Failed to open vegetation mesh: {}", meshPath);
            return;
        }

        const auto& header = streamHandle->getHeader();
        if (header.numSubmeshes == 0)
        {
            vfLogError("GPUDrivenRenderer: Vegetation mesh has no submeshes: {}", meshPath);
            return;
        }

        // Use the first submesh (primary tree geometry)
        const auto& submesh = header.submeshes[0];

        // Reserve meshlets in the shared meshlet buffer
        if (meshletBuffer && streamHandle->hasMeshletData())
        {
            auto* alloc = meshletBuffer->reserveMeshlets(meshPath, header);
            if (!alloc)
            {
                vfLogError("GPUDrivenRenderer: Failed to reserve meshlets for vegetation mesh: {}", meshPath);
                return;
            }

            // Read and upload meshlet data
            resource::SubmeshMeshletData meshletData;
            if (streamHandle->readMeshletData(0, meshletData))
            {
                // Upload vertex data to merged buffer first, to get base vertex offset
                uint32_t baseVertexOffset = 0;

                // Reserve space in merged mesh buffer
                if (mergedBuffer)
                {
                    mergedBuffer->reserveMesh(meshPath, header);
                }

                // Upload each LOD's vertex/index data and meshlet data
                for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
                {
                    const auto& lodInfo = submesh.lods[lod];
                    if (lodInfo.vertexCount == 0) continue;

                    // Read vertex/index data for this LOD
                    std::vector<resource::Vertex> vertices;
                    std::vector<uint32_t> indices;
                    if (!streamHandle->readLODLevel(0, lod, vertices, indices))
                    {
                        continue;
                    }

                    // Upload vertex/index to merged buffer
                    if (mergedBuffer)
                    {
                        gpudriven::LODUploadData uploadData;
                        uploadData.vertexData = vertices.data();
                        uploadData.vertexCount = static_cast<uint32_t>(vertices.size());
                        uploadData.indexData = indices.data();
                        uploadData.indexCount = static_cast<uint32_t>(indices.size());
                        mergedBuffer->uploadLOD(meshPath, submesh.name, 0, lod, uploadData);
                        mergedBuffer->markLODReady(meshPath, submesh.name, 0, lod);

                        // Get the base vertex offset from the first LOD
                        if (lod == 0)
                        {
                            auto* submeshLoc = mergedBuffer->getSubmeshLocation(meshPath, submesh.name, 0);
                            if (submeshLoc && submeshLoc->lods[0].vertexCount > 0)
                            {
                                baseVertexOffset = submeshLoc->lods[0].vertexOffset;
                            }
                        }
                    }

                    // Upload meshlet data for this LOD
                    const auto& lodMeshletInfo = meshletData.lodLevels[lod];
                    if (lodMeshletInfo.meshletCount > 0)
                    {
                        meshletBuffer->uploadMeshletData(
                            meshPath, submesh.name, 0, lod, meshletData, baseVertexOffset);
                    }
                }

                // Flush transfers
                if (meshletBuffer) meshletBuffer->flushPendingTransfers();
                if (mergedBuffer) mergedBuffer->flushPendingTransfers();

                // Record per-LOD meshlet offsets in cached species data
                const auto* meshletAlloc = meshletBuffer->getAllocation(meshPath, submesh.name, 0);
                if (meshletAlloc)
                {
                    cached.hasMesh = true;
                    cached.meshPath = meshPath;
                    cached.baseVertexOffset = baseVertexOffset;
                    cached.availableLODMask = 0;

                    for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
                    {
                        const auto& lodAlloc = meshletAlloc->lods[lod];
                        if (lodAlloc.isAllocated && lodAlloc.meshletCount > 0)
                        {
                            cached.meshletOffset[lod] = lodAlloc.meshletOffset;
                            cached.meshletCount[lod] = lodAlloc.meshletCount;
                            cached.availableLODMask |= (1u << lod);
                        }
                        else
                        {
                            cached.meshletOffset[lod] = 0;
                            cached.meshletCount[lod] = 0;
                        }
                    }

                    vegetation.speciesRenderInfoDirty = true;
                    vfLogInfo("GPUDrivenRenderer: Loaded vegetation mesh '{}' for species {} "
                              "(LOD mask: 0x{:X}, meshlets: {}/{}/{}/{})",
                              meshPath, speciesId, cached.availableLODMask,
                              cached.meshletCount[0], cached.meshletCount[1],
                              cached.meshletCount[2], cached.meshletCount[3]);
                }
            }
            else
            {
                vfLogError("GPUDrivenRenderer: Failed to read meshlet data for: {}", meshPath);
            }
        }
        else
        {
            vfLogWarning("GPUDrivenRenderer: No meshlet buffer or mesh has no meshlet data: {}", meshPath);
        }
    }

    void GPUDrivenRenderer::unloadSpeciesMesh(uint32_t speciesId)
    {
        auto it = vegetation.cachedSpecies.find(speciesId);
        if (it == vegetation.cachedSpecies.end()) return;
        auto& cached = it->second;

        if (!cached.hasMesh || cached.meshPath.empty()) return;

        // Free meshlet allocations
        if (meshletBuffer)
        {
            meshletBuffer->freeAllMeshlets(cached.meshPath);
        }

        // Release mesh from merged buffer
        if (mergedBuffer)
        {
            // The releaseMeshAsset method handles this
            releaseMeshAsset(cached.meshPath);
        }

        cached.hasMesh = false;
        cached.meshPath.clear();
        cached.baseVertexOffset = 0;
        cached.availableLODMask = 0;
        for (uint32_t i = 0; i < 4; ++i)
        {
            cached.meshletOffset[i] = 0;
            cached.meshletCount[i] = 0;
        }

        vegetation.speciesRenderInfoDirty = true;
    }

    void GPUDrivenRenderer::uploadSpeciesRenderInfo()
    {
        if (!vegetation.treeLODInitialized) return;

        std::vector<vegetation::SpeciesRenderInfoGPU> infos(vegetation::MAX_SPECIES);

        for (const auto& [speciesId, cached] : vegetation.cachedSpecies)
        {
            if (speciesId >= vegetation::MAX_SPECIES) continue;

            auto& info = infos[speciesId];
            for (uint32_t lod = 0; lod < 4; ++lod)
            {
                info.meshletOffset[lod] = cached.meshletOffset[lod];
                info.meshletCount[lod] = cached.meshletCount[lod];
            }
            info.baseVertexOffset = cached.baseVertexOffset;
            info.materialTextureIndex = cached.materialTextureIndex;
        }

        // Direct write to host-visible buffer
        vk::Device vkDevice = device.getLogicalDevice();
        vk::DeviceSize dataSize = infos.size() * sizeof(vegetation::SpeciesRenderInfoGPU);

        void* mapped = vkDevice.mapMemory(vegetation.speciesRenderInfoBufferMemory, 0, dataSize);
        std::memcpy(mapped, infos.data(), dataSize);
        vkDevice.unmapMemory(vegetation.speciesRenderInfoBufferMemory);

        vegetation.speciesRenderInfoDirty = false;
    }

    void GPUDrivenRenderer::createTreeLODBuffers(uint32_t maxInstances)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        auto createStorageBuffer = [&](vk::DeviceSize size, vk::Buffer& buffer, vk::DeviceMemory& memory,
                                        vk::BufferUsageFlags extraUsage = {})
        {
            core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
            request.size = size;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | extraUsage;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, buffer, memory);
        };

        // Tree instance buffer (device-local)
        vk::DeviceSize instanceSize = maxInstances * sizeof(vegetation::TreeInstanceGPU);
        createStorageBuffer(instanceSize, vegetation.treeInstanceBuffer, vegetation.treeInstanceBufferMemory);

        // Instance count buffer (single uint32_t)
        createStorageBuffer(sizeof(uint32_t), vegetation.treeInstanceCountBuffer, vegetation.treeInstanceCountBufferMemory);

        // Visible mesh output buffer (VisibleInstance = 8 bytes each)
        vk::DeviceSize visibleSize = maxInstances * 8; // sizeof(VisibleInstance) = 2 * uint32_t
        createStorageBuffer(visibleSize, vegetation.visibleLOD0Buffer, vegetation.visibleLOD0BufferMemory);

        // LOD counters buffer (single uint32_t meshCount, 16 bytes for alignment)
        createStorageBuffer(16, vegetation.lodCountersBuffer, vegetation.lodCountersBufferMemory);

        // Zero-initialize the LOD counters buffer to prevent reading stale GPU memory
        {
            auto cmd = core::Utilities::beginSingleTimeCommands(vkDevice, device.getStagingCommandPool());
            cmd->fillBuffer(vegetation.lodCountersBuffer, 0, 16, 0);
            core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmd);
        }

        // Species render info buffer (per-species meshlet lookup) - host-visible for easy updates
        auto createHostVisibleStorageBuffer = [&](vk::DeviceSize size, vk::Buffer& buffer, vk::DeviceMemory& memory)
        {
            core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
            request.size = size;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, buffer, memory);
        };

        vk::DeviceSize speciesInfoSize = vegetation::MAX_SPECIES * sizeof(vegetation::SpeciesRenderInfoGPU);
        createHostVisibleStorageBuffer(speciesInfoSize, vegetation.speciesRenderInfoBuffer, vegetation.speciesRenderInfoBufferMemory);

        // Staging buffer for tree instances (host-visible, persistently mapped)
        {
            core::BufferInfoRequest request(vkDevice, device.getPhysicalDevice());
            request.size = instanceSize;
            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, vegetation.treeInstanceStagingBuffer, vegetation.treeInstanceStagingMemory);
            vegetation.treeInstanceStagingMapped = vkDevice.mapMemory(vegetation.treeInstanceStagingMemory, 0, instanceSize);
        }

        vegetation.treeInstanceCapacity = maxInstances;
    }

    void GPUDrivenRenderer::initTreeLODPipeline(vk::DescriptorSetLayout iblDescriptorSetLayout,
                                                   vk::RenderPass renderPass)
    {
        constexpr uint32_t initialTreeCapacity = vegetation::MAX_TREE_INSTANCES;
        createTreeLODBuffers(initialTreeCapacity);

        // Cull/LOD compute pipeline
        vegetation.cullLODPipeline = std::make_unique<vegetation::VegetationCullLODPipeline>();
        vegetation.cullLODPipeline->init(device);

        // Update cull/LOD descriptors
        vegetation.cullLODPipeline->updateDescriptors(
            vegetation.treeInstanceBuffer,
            vegetation.treeInstanceCountBuffer,
            vegetation.visibleLOD0Buffer,
            vegetation.lodCountersBuffer
        );

        // Bind camera buffer
        if (cameraBuffer)
        {
            vegetation.cullLODPipeline->updateCameraDescriptor(cameraBuffer->getBuffer());
        }

        // Vegetation mesh shader pipeline (LOD0/LOD1) - needs meshlet data from shared buffers
        if (meshShaderPipeline)
        {
            vegetation.vegMeshPipeline = std::make_unique<vegetation::VegetationMeshShaderPipeline>();
            vegetation.vegMeshPipeline->init(
                device,
                iblDescriptorSetLayout,
                vegetation.windSystem ? vegetation.windSystem->getDescriptorSetLayout() : vk::DescriptorSetLayout{},
                meshShaderPipeline->getMeshletDataLayout(),
                meshShaderPipeline->getVertexDataLayout(),
                bindlessTextures->getDescriptorSetLayout(),
                renderPass
            );

            // Update mesh instance descriptors (meshCount at offset 0) + species render info
            vegetation.vegMeshPipeline->updateInstanceDescriptors(
                vegetation.visibleLOD0Buffer,   // reused as combined mesh visible buffer
                vegetation.lodCountersBuffer,
                vegetation.treeInstanceBuffer,
                0,  // meshCount offset
                vegetation.speciesRenderInfoBuffer
            );
        }

        vegetation.treeLODInitialized = true;
        vegetation.cachedIBLLayout = iblDescriptorSetLayout;
        vegetation.cachedRenderPass = renderPass;

        vfLogInfo("GPUDrivenRenderer: Tree LOD pipeline initialized (cull/LOD + mesh)");
    }

    void GPUDrivenRenderer::dispatchVegetationCullLOD(vk::CommandBuffer cmd)
    {
        if (!vegetation.treeLODInitialized || vegetation.currentTreeInstanceCount == 0) return;
        if (!vegetation.cullLODPipeline || !vegetation.cullLODPipeline->isInitialized()) return;

        // Reset mesh counter to 0
        cmd.fillBuffer(vegetation.lodCountersBuffer, 0, 16, 0);

        vk::MemoryBarrier fillBarrier(
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::DependencyFlags{},
            1, &fillBarrier, 0, nullptr, 0, nullptr);

        // Dispatch cull/LOD compute
        vegetation.cullLODPipeline->dispatch(cmd, vegetation.currentTreeInstanceCount);

        // Barrier: compute writes → task/mesh shader reads
        vk::MemoryBarrier computeBarrier(
            vk::AccessFlagBits::eShaderWrite,
            vk::AccessFlagBits::eShaderRead);
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eTaskShaderEXT | vk::PipelineStageFlagBits::eMeshShaderEXT,
            vk::DependencyFlags{},
            1, &computeBarrier, 0, nullptr, 0, nullptr);
    }

    void GPUDrivenRenderer::renderVegetationDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                                                    uint32_t screenWidth, uint32_t screenHeight)
    {
        if (!vegetation.treeLODInitialized || !vegetation.vegetationRenderingEnabled) return;
        if (vegetation.currentTreeInstanceCount == 0) return;

        // Render LOD0/LOD1 via VegetationMeshShaderPipeline
        // (Currently placeholder - will render actual meshlets when species mesh loading is implemented)
        if (vegetation.vegMeshPipeline && vegetation.vegMeshPipeline->isInitialized() && meshShaderPipeline)
        {
            vegetation.vegMeshPipeline->updateSharedDescriptors(
                iblDescriptorSet,
                vegetation.windSystem ? vegetation.windSystem->getDescriptorSet() : vk::DescriptorSet{},
                meshShaderPipeline->getMeshletDataDescriptorSet(),
                meshShaderPipeline->getVertexDataDescriptorSet(),
                bindlessTextures->getDescriptorSet()
            );

            vegetation.vegMeshPipeline->dispatch(cmd, vegetation.currentTreeInstanceCount);
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
            if (vegetation.vegetationStreamManager)
            {
                auto cfg = vegetation.vegetationStreamManager->getConfig();
                cfg.worldTileSize = actualTileSize;
                vegetation.vegetationStreamManager->setConfig(cfg);
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

        // Build TreeInstanceGPU array from ALL loaded tiles (not just visible ones).
        // The GPU cull shader handles per-instance frustum culling, so we don't need
        // tile-level pre-culling here — it would cause vegetation to pop in/out.
        if (vegetation.treeLODInitialized)
        {
            std::vector<vegetation::TreeInstanceGPU> treeInstances;
            treeInstances.reserve(4096);

            uint32_t totalPlacementInstances = 0;
            uint32_t speciesMissCount = 0;

            for (const auto* tile : allLoadedTiles)
            {
                if (!tile) continue;
                if (tile->vegetationPlacement.getInstanceCount() == 0) continue;
                totalPlacementInstances += static_cast<uint32_t>(tile->vegetationPlacement.getInstanceCount());

                for (const auto& instance : tile->vegetationPlacement.getInstances())
                {
                    auto speciesIt = vegetation.cachedSpecies.find(instance.speciesId);
                    // Fallback: if speciesId 0 (unset default) and only one species cached, use it
                    if (speciesIt == vegetation.cachedSpecies.end() && instance.speciesId == 0
                        && vegetation.cachedSpecies.size() == 1)
                    {
                        speciesIt = vegetation.cachedSpecies.begin();
                    }
                    if (speciesIt == vegetation.cachedSpecies.end())
                    {
                        ++speciesMissCount;
                        continue;
                    }

                    const auto& species = speciesIt->second;

                    vegetation::TreeInstanceGPU gpu{};

                    // Build model matrix from position, rotation, scale
                    float finalScale = instance.scale;
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), instance.position);
                    model = glm::rotate(model, instance.rotation, glm::vec3(0.0f, 1.0f, 0.0f));
                    model = glm::scale(model, glm::vec3(finalScale));
                    gpu.modelMatrix = model;

                    // Bounding sphere centered at instance position, radius based on tree size
                    float maxDim = std::max(species.billboardSize.x, species.billboardSize.y) * instance.scale;
                    gpu.boundingSphere = glm::vec4(instance.position + glm::vec3(0, maxDim * 0.5f, 0), maxDim * 0.5f);

                    gpu.speciesId = speciesIt->first;

                    // LOD mask: bits 0-2=mesh LODs
                    gpu.lodMask = 0;

                    // Set mesh LOD bits from loaded mesh data
                    if (species.hasMesh)
                    {
                        if (species.availableLODMask & 0x1u) gpu.lodMask |= (1u << 0);
                        if (species.availableLODMask & 0x2u) gpu.lodMask |= (1u << 1);
                        if (species.availableLODMask & 0x4u) gpu.lodMask |= (1u << 2);
                    }
                    // Set LOD transition distances from species config
                    gpu.lodDistances[0] = species.lod1Distance;        // LOD0→LOD1
                    gpu.lodDistances[1] = species.lod2Distance;        // LOD1→LOD2
                    gpu.lodDistances[2] = species.maxRenderDistance;    // LOD2→max
                    gpu.lodDistances[3] = species.maxRenderDistance;    // max render

                    treeInstances.push_back(gpu);

                    if (treeInstances.size() >= vegetation.treeInstanceCapacity) break;
                }
                if (treeInstances.size() >= vegetation.treeInstanceCapacity) break;
            }

            vegetation.currentTreeInstanceCount = static_cast<uint32_t>(treeInstances.size());

            // Upload tree instances to staging buffer
            if (vegetation.currentTreeInstanceCount > 0 && vegetation.treeInstanceStagingMapped)
            {
                vk::DeviceSize dataSize = vegetation.currentTreeInstanceCount * sizeof(vegetation::TreeInstanceGPU);
                std::memcpy(vegetation.treeInstanceStagingMapped, treeInstances.data(), dataSize);

                // Also upload instance count
                // (count will be uploaded via fillBuffer in the compute dispatch)
            }

            // Upload species render info if dirty
            if (vegetation.speciesRenderInfoDirty)
            {
                uploadSpeciesRenderInfo();
            }
        }
    }

    void GPUDrivenRenderer::uploadTreeInstances(vk::CommandBuffer cmd)
    {
        if (!vegetation.treeLODInitialized || vegetation.currentTreeInstanceCount == 0) return;

        vk::DeviceSize dataSize = vegetation.currentTreeInstanceCount * sizeof(vegetation::TreeInstanceGPU);

        // Copy staging → device-local tree instance buffer
        vk::BufferCopy copyRegion(0, 0, dataSize);
        cmd.copyBuffer(vegetation.treeInstanceStagingBuffer, vegetation.treeInstanceBuffer, copyRegion);

        // Write instance count to the count buffer
        cmd.fillBuffer(vegetation.treeInstanceCountBuffer, 0, sizeof(uint32_t), vegetation.currentTreeInstanceCount);

        // Barrier: transfer writes → compute reads
        vk::MemoryBarrier barrier(
            vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead);
        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::DependencyFlags{},
            1, &barrier, 0, nullptr, 0, nullptr);
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

        // Cleanup tree LOD pipelines
        if (vegetation.cullLODPipeline)
        {
            vegetation.cullLODPipeline->cleanup();
            vegetation.cullLODPipeline.reset();
        }

        if (vegetation.vegMeshPipeline)
        {
            vegetation.vegMeshPipeline->cleanup();
            vegetation.vegMeshPipeline.reset();
        }

        // Cleanup tree LOD buffers
        if (vegetation.treeInstanceStagingMapped)
        {
            vkDevice.unmapMemory(vegetation.treeInstanceStagingMemory);
            vegetation.treeInstanceStagingMapped = nullptr;
        }
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.treeInstanceBuffer, vegetation.treeInstanceBufferMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.treeInstanceCountBuffer, vegetation.treeInstanceCountBufferMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.visibleLOD0Buffer, vegetation.visibleLOD0BufferMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.lodCountersBuffer, vegetation.lodCountersBufferMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.speciesRenderInfoBuffer, vegetation.speciesRenderInfoBufferMemory);
        core::BufferUtilities::destroyBuffer(vkDevice, vegetation.treeInstanceStagingBuffer, vegetation.treeInstanceStagingMemory);
        vegetation.treeLODInitialized = false;

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
