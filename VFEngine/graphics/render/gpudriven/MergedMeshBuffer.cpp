#include "MergedMeshBuffer.hpp"
#include "../mesh/MeshGPUCache.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../../core/Device.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/TransferManager.hpp"
#include "resource/Types.hpp"
#include "print/Logger.hpp"
#include <cstring>

// Verify vertex stride matches actual Vertex struct
static_assert(sizeof(resource::Vertex) == 32, "Vertex size must be 32 bytes for MergedMeshBuffer");

namespace render::gpudriven {

    MergedMeshBuffer::MergedMeshBuffer(core::Device& device)
        : device(device)
    {
        uint32_t transferQueueFamily = device.getQueueFamilyIndices().transferFamily.value();
        transferManager = std::make_unique<core::TransferManager>(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getTransferQueue(),
            transferQueueFamily
        );
    }

    MergedMeshBuffer::~MergedMeshBuffer()
    {
        cleanup();
    }

    void MergedMeshBuffer::init(uint32_t maxVertices, uint32_t maxIndices)
    {
        if (initialized) {
            loggerWarning("MergedMeshBuffer already initialized");
            return;
        }

        maxVertexCount = maxVertices;
        maxIndexCount = maxIndices;
        maxObjectCount = MAX_GPU_OBJECTS;

        cpuObjectData.resize(maxObjectCount);

        createBuffers();
        initialized = true;

        loggerInfo("MergedMeshBuffer initialized: {} max vertices, {} max indices, {} max objects",
                   maxVertexCount, maxIndexCount, maxObjectCount);
    }

    void MergedMeshBuffer::cleanup()
    {
        if (!initialized) return;

        device.getLogicalDevice().waitIdle();
        destroyBuffers();

        registeredMeshes.clear();
        meshPathToIndex.clear();
        allSubmeshLocations.clear();
        submeshKeyToIndex.clear();
        cpuObjectData.clear();

        totalVertexCount = 0;
        totalIndexCount = 0;
        currentObjectCount = 0;
        initialized = false;

        loggerInfo("MergedMeshBuffer cleaned up");
    }

    void MergedMeshBuffer::createBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        // Create merged vertex buffer (device-local)
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxVertexCount * vertexStride;
            request.usage = vk::BufferUsageFlagBits::eVertexBuffer |
                           vk::BufferUsageFlagBits::eTransferDst |
                           vk::BufferUsageFlagBits::eStorageBuffer; // For compute access if needed
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(request, vertexBuffer, vertexBufferMemory);
        }

        // Create merged index buffer (device-local)
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxIndexCount * sizeof(uint32_t);
            request.usage = vk::BufferUsageFlagBits::eIndexBuffer |
                           vk::BufferUsageFlagBits::eTransferDst |
                           vk::BufferUsageFlagBits::eStorageBuffer;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(request, indexBuffer, indexBufferMemory);
        }

        // Create object buffer (device-local storage buffer)
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxObjectCount * sizeof(GPUObjectData);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                           vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(request, objectBuffer, objectBufferMemory);
        }

        // Create staging buffer for object updates (host-visible, persistently mapped)
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxObjectCount * sizeof(GPUObjectData);
            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
            core::Utilities::createBuffer(request, objectStagingBuffer, objectStagingMemory);

            // Persistently map staging buffer
            objectStagingMapped = logicalDevice.mapMemory(
                objectStagingMemory, 0, request.size, vk::MemoryMapFlags{}
            );
        }
    }

    void MergedMeshBuffer::destroyBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (objectStagingMapped) {
            logicalDevice.unmapMemory(objectStagingMemory);
            objectStagingMapped = nullptr;
        }

        if (objectStagingBuffer) {
            logicalDevice.destroyBuffer(objectStagingBuffer);
            logicalDevice.freeMemory(objectStagingMemory);
            objectStagingBuffer = nullptr;
        }

        if (objectBuffer) {
            logicalDevice.destroyBuffer(objectBuffer);
            logicalDevice.freeMemory(objectBufferMemory);
            objectBuffer = nullptr;
        }

        if (indexBuffer) {
            logicalDevice.destroyBuffer(indexBuffer);
            logicalDevice.freeMemory(indexBufferMemory);
            indexBuffer = nullptr;
        }

        if (vertexBuffer) {
            logicalDevice.destroyBuffer(vertexBuffer);
            logicalDevice.freeMemory(vertexBufferMemory);
            vertexBuffer = nullptr;
        }
    }

    void MergedMeshBuffer::rebuildFromCache(const mesh::MeshGPUCache& cache)
    {
        if (!initialized) {
            loggerError("MergedMeshBuffer not initialized");
            return;
        }

        // Clear existing data
        registeredMeshes.clear();
        meshPathToIndex.clear();
        allSubmeshLocations.clear();
        submeshKeyToIndex.clear();
        totalVertexCount = 0;
        totalIndexCount = 0;

        // Get all loaded mesh IDs
        auto meshIds = cache.getLoadedMeshIds();

        for (const auto& meshPath : meshIds) {
            const auto* meshData = cache.getMesh(meshPath);
            if (meshData) {
                registerMesh(meshPath, *meshData);
            }
        }

        // Wait for all transfers to complete
        transferManager->waitAll();
        dirty = false;

        loggerInfo("MergedMeshBuffer rebuilt: {} meshes, {} submeshes, {} vertices, {} indices",
                   registeredMeshes.size(), allSubmeshLocations.size(),
                   totalVertexCount, totalIndexCount);
    }

    MergedMeshInfo* MergedMeshBuffer::registerMesh(const std::string& meshPath,
                                                    const mesh::MeshGPUData& meshData)
    {
        if (!initialized) {
            loggerError("MergedMeshBuffer not initialized");
            return nullptr;
        }

        // Check if already registered
        auto it = meshPathToIndex.find(meshPath);
        if (it != meshPathToIndex.end()) {
            return &registeredMeshes[it->second];
        }

        MergedMeshInfo meshInfo;
        meshInfo.meshPath = meshPath;
        meshInfo.firstSubmeshIndex = static_cast<uint32_t>(allSubmeshLocations.size());
        meshInfo.submeshCount = 0;

        bool boundingBoxInit = false;

        // Process each submesh
        for (size_t subIdx = 0; subIdx < meshData.subMeshes.size(); ++subIdx) {
            const auto& subMesh = meshData.subMeshes[subIdx];

            SubmeshLocation loc;
            loc.meshPath = meshPath;
            loc.submeshName = subMesh.name;
            loc.submeshIndex = static_cast<uint32_t>(subIdx);

            // Copy bounding box
            loc.aabbMin = subMesh.boundingBox.min;
            loc.aabbMax = subMesh.boundingBox.max;
            loc.calculateBoundingSphere();

            // Process each LOD level
            for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod) {
                const auto& lodBuffers = subMesh.lodLevels[lod];

                LODDrawInfo& lodInfo = loc.lods[lod];

                if (lodBuffers.vertexCount == 0) {
                    // Empty LOD, use previous or skip
                    if (lod > 0) {
                        lodInfo = loc.lods[lod - 1];
                    } else {
                        lodInfo = {0, 0, 0, 0};
                    }
                    continue;
                }

                // Check capacity
                if (totalVertexCount + lodBuffers.vertexCount > maxVertexCount) {
                    loggerError("MergedMeshBuffer: vertex capacity exceeded");
                    return nullptr;
                }
                if (totalIndexCount + lodBuffers.indexCount > maxIndexCount) {
                    loggerError("MergedMeshBuffer: index capacity exceeded");
                    return nullptr;
                }

                // Record offsets BEFORE adding data
                lodInfo.vertexOffset = totalVertexCount;
                lodInfo.indexOffset = totalIndexCount;
                lodInfo.vertexCount = lodBuffers.vertexCount;
                lodInfo.indexCount = lodBuffers.indexCount;

                // We need to copy data from the existing GPU buffers
                // Since we don't have direct access to the data, we need to read it back
                // For now, we'll accumulate offsets; actual data copy happens via
                // buffer-to-buffer copies on the GPU

                // Copy vertex buffer contents
                if (lodBuffers.vertexBuffer) {
                    // Use transfer manager to copy from source buffer to merged buffer
                    vk::BufferCopy copyRegion;
                    copyRegion.srcOffset = 0;
                    copyRegion.dstOffset = totalVertexCount * vertexStride;
                    copyRegion.size = lodBuffers.vertexCount * vertexStride;

                    // We need a command buffer for buffer-to-buffer copy
                    auto cmdBuffer = core::Utilities::beginSingleTimeCommands(
                        device.getLogicalDevice(),
                        device.getStagingCommandPool()
                    );
                    cmdBuffer->copyBuffer(lodBuffers.vertexBuffer, vertexBuffer, copyRegion);
                    core::Utilities::endSingleTimeCommands(
                        device.getGraphicsQueue(),
                        cmdBuffer
                    );
                }

                // Copy index buffer contents
                if (lodBuffers.indexBuffer && lodBuffers.indexCount > 0) {
                    vk::BufferCopy copyRegion;
                    copyRegion.srcOffset = 0;
                    copyRegion.dstOffset = totalIndexCount * sizeof(uint32_t);
                    copyRegion.size = lodBuffers.indexCount * sizeof(uint32_t);

                    auto cmdBuffer = core::Utilities::beginSingleTimeCommands(
                        device.getLogicalDevice(),
                        device.getStagingCommandPool()
                    );
                    cmdBuffer->copyBuffer(lodBuffers.indexBuffer, indexBuffer, copyRegion);
                    core::Utilities::endSingleTimeCommands(
                        device.getGraphicsQueue(),
                        cmdBuffer
                    );
                }

                totalVertexCount += lodBuffers.vertexCount;
                totalIndexCount += lodBuffers.indexCount;
            }

            // Update mesh bounding box
            if (!boundingBoxInit) {
                meshInfo.aabbMin = loc.aabbMin;
                meshInfo.aabbMax = loc.aabbMax;
                boundingBoxInit = true;
            } else {
                meshInfo.aabbMin = glm::min(meshInfo.aabbMin, loc.aabbMin);
                meshInfo.aabbMax = glm::max(meshInfo.aabbMax, loc.aabbMax);
            }

            // Store submesh location
            std::string key = makeSubmeshKey(meshPath, subMesh.name);
            submeshKeyToIndex[key] = allSubmeshLocations.size();
            allSubmeshLocations.push_back(std::move(loc));
            meshInfo.submeshes.push_back(allSubmeshLocations.back());
            meshInfo.submeshCount++;
        }

        // Store mesh info
        meshPathToIndex[meshPath] = registeredMeshes.size();
        registeredMeshes.push_back(std::move(meshInfo));

        dirty = true;
        return &registeredMeshes.back();
    }

    void MergedMeshBuffer::unregisterMesh(const std::string& meshPath)
    {
        auto it = meshPathToIndex.find(meshPath);
        if (it == meshPathToIndex.end()) {
            return;
        }

        // Note: This doesn't reclaim space, just marks as unused
        // A full rebuild would be needed to defragment
        const auto& meshInfo = registeredMeshes[it->second];

        for (const auto& submesh : meshInfo.submeshes) {
            std::string key = makeSubmeshKey(meshPath, submesh.submeshName);
            submeshKeyToIndex.erase(key);
        }

        // Mark entry as invalid (empty path)
        registeredMeshes[it->second].meshPath.clear();
        meshPathToIndex.erase(it);

        dirty = true;
        loggerInfo("Unregistered mesh from MergedMeshBuffer: {}", meshPath);
    }

    void MergedMeshBuffer::updateObjects(const std::vector<mesh::MeshRenderData>& renderData,
                                          const mesh::MeshGPUCache& cache,
                                          const TextureIndexResolver& textureResolver)
    {
        currentObjectCount = 0;

        for (const auto& meshRender : renderData) {
            // Get submesh locations for this mesh
            auto meshIt = meshPathToIndex.find(meshRender.meshPath);
            if (meshIt == meshPathToIndex.end()) {
                // Mesh not in merged buffer, try to register it dynamically
                const auto* meshData = cache.getMesh(meshRender.meshPath);
                if (!meshData) continue;

                // Dynamic registration - add mesh to merged buffer on-the-fly
                MergedMeshInfo* registered = registerMesh(meshRender.meshPath, *meshData);
                if (!registered) {
                    loggerWarning("MergedMeshBuffer: Failed to dynamically register mesh: {}", meshRender.meshPath);
                    continue;
                }

                // Re-lookup after registration
                meshIt = meshPathToIndex.find(meshRender.meshPath);
                if (meshIt == meshPathToIndex.end()) continue;
            }

            const auto& meshInfo = registeredMeshes[meshIt->second];

            // Create one GPUObjectData per submesh
            for (const auto& submeshLoc : meshInfo.submeshes) {
                if (currentObjectCount >= maxObjectCount) {
                    loggerWarning("MergedMeshBuffer: max object count reached");
                    break;
                }

                GPUObjectData& obj = cpuObjectData[currentObjectCount];

                // Transform
                obj.modelMatrix = meshRender.modelMatrix;

                // Bounding volumes (in local space)
                obj.boundingSphere = submeshLoc.boundingSphere;

                // LOD data
                obj.lod0Data = glm::uvec4(
                    submeshLoc.lods[0].vertexOffset,
                    submeshLoc.lods[0].indexOffset,
                    submeshLoc.lods[0].indexCount,
                    submeshLoc.lods[0].vertexCount
                );
                obj.lod1Data = glm::uvec4(
                    submeshLoc.lods[1].vertexOffset,
                    submeshLoc.lods[1].indexOffset,
                    submeshLoc.lods[1].indexCount,
                    submeshLoc.lods[1].vertexCount
                );
                obj.lod2Data = glm::uvec4(
                    submeshLoc.lods[2].vertexOffset,
                    submeshLoc.lods[2].indexOffset,
                    submeshLoc.lods[2].indexCount,
                    submeshLoc.lods[2].vertexCount
                );
                obj.lod3Data = glm::uvec4(
                    submeshLoc.lods[3].vertexOffset,
                    submeshLoc.lods[3].indexOffset,
                    submeshLoc.lods[3].indexCount,
                    submeshLoc.lods[3].vertexCount
                );

                // LOD thresholds (with bias)
                float bias = meshRender.lodBias;
                obj.lodThresholds = glm::vec4(
                    LOD_THRESHOLD_0 * std::pow(2.0f, -bias),
                    LOD_THRESHOLD_1 * std::pow(2.0f, -bias),
                    LOD_THRESHOLD_2 * std::pow(2.0f, -bias),
                    bias
                );

                // Material data
                // Check for submesh-specific material
                const auto* subMat = meshRender.getMaterialForSubmesh(submeshLoc.submeshName);
                if (subMat) {
                    obj.albedo = subMat->albedo;
                    obj.materialParams = glm::vec4(
                        subMat->metallic,
                        subMat->roughness,
                        subMat->ao,
                        subMat->emission
                    );
                    obj.iblParams = glm::vec4(subMat->iblDiffuse, subMat->iblSpecular, 0.0f, 0.0f);
                } else {
                    obj.albedo = meshRender.albedo;
                    obj.materialParams = glm::vec4(
                        meshRender.metallic,
                        meshRender.roughness,
                        meshRender.ao,
                        meshRender.emission
                    );
                    obj.iblParams = glm::vec4(1.0f, 0.5f, 0.0f, 0.0f);
                }

                // Texture indices - resolve via callback if provided
                if (textureResolver) {
                    // Get material path for this submesh
                    std::string materialPath;
                    if (subMat && !subMat->materialPath.empty()) {
                        materialPath = subMat->materialPath;
                    } else if (!meshRender.defaultMaterialPath.empty()) {
                        materialPath = meshRender.defaultMaterialPath;
                    }

                    if (!materialPath.empty()) {
                        // Resolve each texture slot
                        obj.textureIndices0 = glm::uvec4(
                            textureResolver(materialPath, TextureSlotType::Albedo),
                            textureResolver(materialPath, TextureSlotType::Normal),
                            textureResolver(materialPath, TextureSlotType::ORM),
                            textureResolver(materialPath, TextureSlotType::Metallic)
                        );
                        obj.textureIndices1 = glm::uvec4(
                            textureResolver(materialPath, TextureSlotType::Roughness),
                            textureResolver(materialPath, TextureSlotType::AO),
                            textureResolver(materialPath, TextureSlotType::Emission),
                            textureResolver(materialPath, TextureSlotType::Height)
                        );
                    } else {
                        obj.textureIndices0 = glm::uvec4(INVALID_TEXTURE_INDEX);
                        obj.textureIndices1 = glm::uvec4(INVALID_TEXTURE_INDEX);
                    }
                } else {
                    // No resolver - use invalid indices
                    obj.textureIndices0 = glm::uvec4(INVALID_TEXTURE_INDEX);
                    obj.textureIndices1 = glm::uvec4(INVALID_TEXTURE_INDEX);
                }

                // Flags
                obj.flags = ObjectFlags::Visible | ObjectFlags::CastShadow | ObjectFlags::ReceiveShadow;
                if (subMat && subMat->blendMode == 2) {
                    obj.flags |= ObjectFlags::Transparent;
                } else if (subMat && subMat->blendMode == 1) {
                    obj.flags |= ObjectFlags::AlphaMask;
                }

                obj.entityId = currentObjectCount; // Simple ID for now

                currentObjectCount++;
            }
        }
    }

    void MergedMeshBuffer::uploadObjects(vk::CommandBuffer cmd)
    {
        if (currentObjectCount == 0) return;

        // Copy CPU data to staging buffer
        size_t copySize = currentObjectCount * sizeof(GPUObjectData);
        std::memcpy(objectStagingMapped, cpuObjectData.data(), copySize);

        // Copy from staging to device-local buffer
        vk::BufferCopy copyRegion;
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = copySize;
        cmd.copyBuffer(objectStagingBuffer, objectBuffer, copyRegion);

        // Barrier to ensure copy completes before compute shader reads
        vk::BufferMemoryBarrier barrier;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = objectBuffer;
        barrier.offset = 0;
        barrier.size = copySize;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader,
            {},
            {},
            barrier,
            {}
        );
    }

    const SubmeshLocation* MergedMeshBuffer::getSubmeshLocation(const std::string& meshPath,
                                                                 const std::string& submeshName) const
    {
        std::string key = makeSubmeshKey(meshPath, submeshName);
        auto it = submeshKeyToIndex.find(key);
        if (it != submeshKeyToIndex.end()) {
            return &allSubmeshLocations[it->second];
        }
        return nullptr;
    }

    void MergedMeshBuffer::resizeObjectBuffer(uint32_t newMaxObjects)
    {
        if (newMaxObjects <= maxObjectCount) return;

        device.getLogicalDevice().waitIdle();

        // Destroy old buffers
        if (objectStagingMapped) {
            device.getLogicalDevice().unmapMemory(objectStagingMemory);
            objectStagingMapped = nullptr;
        }
        if (objectStagingBuffer) {
            device.getLogicalDevice().destroyBuffer(objectStagingBuffer);
            device.getLogicalDevice().freeMemory(objectStagingMemory);
        }
        if (objectBuffer) {
            device.getLogicalDevice().destroyBuffer(objectBuffer);
            device.getLogicalDevice().freeMemory(objectBufferMemory);
        }

        maxObjectCount = newMaxObjects;
        cpuObjectData.resize(maxObjectCount);

        // Recreate buffers
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxObjectCount * sizeof(GPUObjectData);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                           vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(request, objectBuffer, objectBufferMemory);
        }

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxObjectCount * sizeof(GPUObjectData);
            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent;
            core::Utilities::createBuffer(request, objectStagingBuffer, objectStagingMemory);
            objectStagingMapped = logicalDevice.mapMemory(
                objectStagingMemory, 0, request.size, vk::MemoryMapFlags{}
            );
        }

        loggerInfo("MergedMeshBuffer: resized object buffer to {} objects", maxObjectCount);
    }

}
