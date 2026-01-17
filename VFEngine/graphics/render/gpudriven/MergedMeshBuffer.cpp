#include "MergedMeshBuffer.hpp"
#include "../mesh/MeshTypes.hpp"
#include "../material/MaterialPBRExtractor.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/TransferManager.hpp"
#include "resource/Types.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "print/Logger.hpp"
#include <material/MaterialInstanceTypes.hpp>
#include <cstring>
#include <cmath>
#include <unordered_set>

// Verify vertex stride matches actual Vertex struct (64 bytes with bone data)
static_assert(sizeof(resource::Vertex) == 64, "Vertex size must be 64 bytes for MergedMeshBuffer");

namespace render::gpudriven
{
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
        if (initialized)
        {
            loggerWarning("MergedMeshBuffer already initialized");
            return;
        }

        maxVertexCount = maxVertices;
        maxIndexCount = maxIndices;
        maxObjectCount = MAX_GPU_OBJECTS;

        cpuObjectData.resize(maxObjectCount);

        vertexAllocator.reset(maxVertexCount);
        indexAllocator.reset(maxIndexCount);

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
            core::BufferUtilities::createBuffer(request, vertexBuffer, vertexBufferMemory);
        }

        // Create merged index buffer (device-local)
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxIndexCount * sizeof(uint32_t);
            request.usage = vk::BufferUsageFlagBits::eIndexBuffer |
                vk::BufferUsageFlagBits::eTransferDst |
                vk::BufferUsageFlagBits::eStorageBuffer;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, indexBuffer, indexBufferMemory);
        }

        // Create object buffer (device-local storage buffer)
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxObjectCount * sizeof(GPUObjectData);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, objectBuffer, objectBufferMemory);
        }

        // Create staging buffer for object updates (host-visible, persistently mapped)
        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxObjectCount * sizeof(GPUObjectData);
            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, objectStagingBuffer, objectStagingMemory);

            // Persistently map staging buffer
            objectStagingMapped = logicalDevice.mapMemory(
                objectStagingMemory, 0, request.size, vk::MemoryMapFlags{}
            );
        }
    }

    void MergedMeshBuffer::destroyBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (objectStagingMapped)
        {
            logicalDevice.unmapMemory(objectStagingMemory);
            objectStagingMapped = nullptr;
        }

        core::BufferUtilities::destroyBuffer(logicalDevice, objectStagingBuffer, objectStagingMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, objectBuffer, objectBufferMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, indexBuffer, indexBufferMemory);
        core::BufferUtilities::destroyBuffer(logicalDevice, vertexBuffer, vertexBufferMemory);
    }

    void MergedMeshBuffer::uploadObjects(vk::CommandBuffer cmd)
    {
        if (currentObjectCount == 0) return;

        if (currentObjectCount > maxObjectCount)
        {
            loggerError("MergedMeshBuffer: object count {} exceeds max {}", currentObjectCount, maxObjectCount);
            return;
        }

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
                                                                const std::string& submeshName,
                                                                uint32_t submeshIndex) const
    {
        std::string key = makeSubmeshKey(meshPath, submeshName, submeshIndex);
        auto it = submeshKeyToIndex.find(key);
        if (it != submeshKeyToIndex.end())
        {
            return &allSubmeshLocations[it->second];
        }
        return nullptr;
    }

    // ===== STREAMING SUPPORT IMPLEMENTATION =====

    void MergedMeshBuffer::uploadVertexDataAt(uint32_t offset, const void* data, uint32_t vertexCount)
    {
        if (!data || vertexCount == 0) return;

        size_t dataSize = vertexCount * vertexStride;
        size_t dstOffset = offset * vertexStride;

        transferManager->copyToBufferAsync(vertexBuffer, data, dataSize, dstOffset);
    }

    void MergedMeshBuffer::uploadIndexDataAt(uint32_t offset, const uint32_t* data, uint32_t indexCount)
    {
        if (!data || indexCount == 0) return;

        size_t dataSize = indexCount * sizeof(uint32_t);
        size_t dstOffset = offset * sizeof(uint32_t);

        transferManager->copyToBufferAsync(indexBuffer, data, dataSize, dstOffset);
    }

    bool MergedMeshBuffer::allocateLODSpace(SubmeshLocation& loc, uint32_t lodLevel,
                                            uint32_t vertexCount, uint32_t indexCount,
                                            const std::string& meshPath)
    {
        if (vertexCount == 0)
        {
            if (lodLevel > 0)
            {
                loc.lods[lodLevel] = loc.lods[lodLevel - 1];
                loc.lodStates[lodLevel] = loc.lodStates[lodLevel - 1];
            }
            else
            {
                loc.lods[lodLevel] = {0, 0, 0, 0};
                loc.lodStates[lodLevel] = LODStreamState::NotRequested;
            }
            return true;
        }

        uint32_t vertOffset = vertexAllocator.allocate(vertexCount);
        if (vertOffset == FreeListAllocator::ALLOCATION_FAILED)
        {
            loggerError("MergedMeshBuffer: Failed to allocate {} vertices for {} LOD{}",
                        vertexCount, meshPath, lodLevel);
            return false;
        }

        uint32_t idxOffset = indexAllocator.allocate(indexCount);
        if (idxOffset == FreeListAllocator::ALLOCATION_FAILED)
        {
            vertexAllocator.free(vertOffset, vertexCount);
            loggerError("MergedMeshBuffer: Failed to allocate {} indices for {} LOD{}",
                        indexCount, meshPath, lodLevel);
            return false;
        }

        loc.lods[lodLevel].vertexOffset = vertOffset;
        loc.lods[lodLevel].indexOffset = idxOffset;
        loc.lods[lodLevel].vertexCount = vertexCount;
        loc.lods[lodLevel].indexCount = indexCount;
        loc.lodStates[lodLevel] = LODStreamState::NotRequested;

        return true;
    }

    MergedMeshInfo* MergedMeshBuffer::reserveMesh(const std::string& meshPath,
                                                  const resource::MeshStreamHeader& header)
    {
        if (!initialized)
        {
            loggerError("MergedMeshBuffer not initialized");
            return nullptr;
        }

        auto it = meshPathToIndex.find(meshPath);
        if (it != meshPathToIndex.end())
        {
            return &registeredMeshes[it->second];
        }

        MergedMeshInfo meshInfo;
        meshInfo.meshPath = meshPath;
        meshInfo.firstSubmeshIndex = static_cast<uint32_t>(allSubmeshLocations.size());
        meshInfo.submeshCount = 0;

        for (uint32_t subIdx = 0; subIdx < header.numSubmeshes; ++subIdx)
        {
            const auto& submeshStreamInfo = header.submeshes[subIdx];

            SubmeshLocation loc;
            loc.meshPath = meshPath;
            loc.submeshName = submeshStreamInfo.name;
            loc.submeshIndex = subIdx;
            loc.aabbMin = glm::vec3(0.0f);
            loc.aabbMax = glm::vec3(0.0f);
            loc.boundingSphere = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

            for (uint32_t lodIdx = 0; lodIdx < LOD_LEVEL_COUNT; ++lodIdx)
            {
                const auto& lodFileInfo = submeshStreamInfo.lods[lodIdx];
                if (!allocateLODSpace(loc, lodIdx, lodFileInfo.vertexCount, lodFileInfo.indexCount, meshPath))
                {
                    return nullptr;
                }
            }

            std::string key = makeSubmeshKey(meshPath, submeshStreamInfo.name, subIdx);
            submeshKeyToIndex[key] = allSubmeshLocations.size();
            allSubmeshLocations.push_back(std::move(loc));
            meshInfo.submeshes.push_back(allSubmeshLocations.back());
            meshInfo.submeshCount++;
        }

        meshPathToIndex[meshPath] = registeredMeshes.size();
        registeredMeshes.push_back(std::move(meshInfo));

        loggerInfo("MergedMeshBuffer: Reserved space for mesh {} with {} submeshes",
                   meshPath, header.numSubmeshes);
        return &registeredMeshes.back();
    }

    bool MergedMeshBuffer::uploadLOD(const std::string& meshPath,
                                     const std::string& submeshName,
                                     uint32_t submeshIndex,
                                     uint32_t lodLevel,
                                     const resource::Vertex* vertexData, uint32_t vertexCount,
                                     const uint32_t* indexData, uint32_t indexCount)
    {
        if (!initialized || lodLevel >= LOD_LEVEL_COUNT)
        {
            return false;
        }

        SubmeshLocation* loc = getSubmeshLocationMutable(meshPath, submeshName, submeshIndex);
        if (!loc)
        {
            loggerError("MergedMeshBuffer::uploadLOD: Submesh not found: {}:{}#{}", meshPath, submeshName,
                        submeshIndex);
            return false;
        }

        const auto& lodInfo = loc->lods[lodLevel];

        // Validate counts match reservation
        if (lodInfo.vertexCount != vertexCount || lodInfo.indexCount != indexCount)
        {
            loggerError("MergedMeshBuffer::uploadLOD: Count mismatch for {}:{} LOD{}: "
                        "expected {}v/{}i, got {}v/{}i",
                        meshPath, submeshName, lodLevel,
                        lodInfo.vertexCount, lodInfo.indexCount,
                        vertexCount, indexCount);
            return false;
        }

        // Upload vertex data
        if (vertexData && vertexCount > 0)
        {
            // Debug: log first vertex bone data per mesh to verify it's being read correctly
            static std::unordered_set<std::string> loggedMeshes;
            if (loggedMeshes.find(meshPath) == loggedMeshes.end())
            {
                const auto& v = vertexData[0];
                loggerInfo("MergedMeshBuffer: First vertex bone data for {}: indices=[{},{},{},{}] weights=[{:.3f},{:.3f},{:.3f},{:.3f}]",
                    meshPath,
                    v.boneIndices.x, v.boneIndices.y, v.boneIndices.z, v.boneIndices.w,
                    v.boneWeights.x, v.boneWeights.y, v.boneWeights.z, v.boneWeights.w);
                loggedMeshes.insert(meshPath);
            }
            uploadVertexDataAt(lodInfo.vertexOffset, vertexData, vertexCount);
        }

        // Upload index data
        if (indexData && indexCount > 0)
        {
            uploadIndexDataAt(lodInfo.indexOffset, indexData, indexCount);
        }

        // Update state to uploading
        loc->lodStates[lodLevel] = LODStreamState::Uploading;

       
        bool boundsNotComputed = (loc->aabbMin == glm::vec3(0.0f) && loc->aabbMax == glm::vec3(0.0f));
        if (vertexData && vertexCount > 0 && boundsNotComputed)
        {
            glm::vec3 minBounds(std::numeric_limits<float>::max());
            glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

            for (uint32_t i = 0; i < vertexCount; ++i)
            {
                const auto& v = vertexData[i];
                minBounds = glm::min(minBounds, v.position);
                maxBounds = glm::max(maxBounds, v.position);
            }

            loc->aabbMin = minBounds;
            loc->aabbMax = maxBounds;
            loc->calculateBoundingSphere();
        }

        return true;
    }

    void MergedMeshBuffer::markLODReady(const std::string& meshPath,
                                        const std::string& submeshName,
                                        uint32_t submeshIndex,
                                        uint32_t lodLevel)
    {
        if (lodLevel >= LOD_LEVEL_COUNT) return;

        SubmeshLocation* loc = getSubmeshLocationMutable(meshPath, submeshName, submeshIndex);
        if (loc)
        {
            loc->lodStates[lodLevel] = LODStreamState::Ready;

            vertexAllocator.markUsed(loc->lods[lodLevel].vertexCount);
            indexAllocator.markUsed(loc->lods[lodLevel].indexCount);

            totalVertexCount = vertexAllocator.getUsedCount();
            totalIndexCount = indexAllocator.getUsedCount();
        }
    }

    bool MergedMeshBuffer::hasRenderableData(const std::string& meshPath) const
    {
        auto it = meshPathToIndex.find(meshPath);
        if (it == meshPathToIndex.end()) return false;

        const auto& meshInfo = registeredMeshes[it->second];
        for (const auto& submesh : meshInfo.submeshes)
        {
            if (submesh.hasRenderableLOD())
            {
                return true;
            }
        }
        return false;
    }

    SubmeshLocation* MergedMeshBuffer::getSubmeshLocationMutable(const std::string& meshPath,
                                                                 const std::string& submeshName,
                                                                 uint32_t submeshIndex)
    {
        std::string key = makeSubmeshKey(meshPath, submeshName, submeshIndex);
        auto it = submeshKeyToIndex.find(key);
        if (it != submeshKeyToIndex.end())
        {
            return &allSubmeshLocations[it->second];
        }
        return nullptr;
    }

    void MergedMeshBuffer::flushPendingTransfers()
    {
        if (transferManager && transferManager->hasPendingTransfers())
        {
            transferManager->waitAll();
        }
    }

    // ===== METADATA-BASED REGISTRATION (for streaming path) =====

    MergedMeshInfo* MergedMeshBuffer::registerMeshFromMetadata(const std::string& meshPath,
                                                               const mesh::MeshMetadata& metadata)
    {
        if (!initialized)
        {
            loggerError("MergedMeshBuffer not initialized");
            return nullptr;
        }

        auto it = meshPathToIndex.find(meshPath);
        if (it != meshPathToIndex.end())
        {
            return &registeredMeshes[it->second];
        }

        MergedMeshInfo meshInfo;
        meshInfo.meshPath = meshPath;
        meshInfo.firstSubmeshIndex = static_cast<uint32_t>(allSubmeshLocations.size());
        meshInfo.submeshCount = 0;

        for (size_t subIdx = 0; subIdx < metadata.subMeshes.size(); ++subIdx)
        {
            const auto& subMeta = metadata.subMeshes[subIdx];

            SubmeshLocation loc;
            loc.meshPath = meshPath;
            loc.submeshName = subMeta.name;
            loc.submeshIndex = static_cast<uint32_t>(subIdx);
            loc.aabbMin = subMeta.boundingBox.min;
            loc.aabbMax = subMeta.boundingBox.max;
            loc.calculateBoundingSphere();

            if (loc.boundingSphere.w <= 0.0f || std::isnan(loc.boundingSphere.w) || std::isinf(loc.boundingSphere.w))
            {
                loc.boundingSphere.w = 1.0f;
            }

            for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
            {
                const auto& lodMeta = subMeta.lodLevels[lod];
                if (!allocateLODSpace(loc, lod, lodMeta.vertexCount, lodMeta.indexCount, meshPath))
                {
                    return nullptr;
                }
            }

            if (meshInfo.submeshCount == 0)
            {
                meshInfo.aabbMin = loc.aabbMin;
                meshInfo.aabbMax = loc.aabbMax;
            }
            else
            {
                meshInfo.aabbMin = glm::min(meshInfo.aabbMin, loc.aabbMin);
                meshInfo.aabbMax = glm::max(meshInfo.aabbMax, loc.aabbMax);
            }

            std::string key = makeSubmeshKey(meshPath, subMeta.name, static_cast<uint32_t>(subIdx));
            submeshKeyToIndex[key] = allSubmeshLocations.size();
            allSubmeshLocations.push_back(std::move(loc));
            meshInfo.submeshes.push_back(allSubmeshLocations.back());
            meshInfo.submeshCount++;
        }

        meshPathToIndex[meshPath] = registeredMeshes.size();
        registeredMeshes.push_back(std::move(meshInfo));

        loggerInfo("MergedMeshBuffer: Reserved space for mesh {} ({} submeshes, metadata path)",
                   meshPath, metadata.subMeshes.size());
        return &registeredMeshes.back();
    }

    void MergedMeshBuffer::populateObjectData(GPUObjectData& obj,
                                              const mesh::MeshRenderData& meshRender,
                                              const SubmeshLocation& submeshLoc,
                                              const TextureIndexResolver& textureResolver,
                                              const ShaderGroupResolver& shaderGroupResolver,
                                              const BoneOffsetResolver& boneOffsetResolver,
                                              float time)
    {
        obj.modelMatrix = meshRender.modelMatrix;
        obj.boundingSphere = submeshLoc.boundingSphere;

        // LOD data (vertex/index based)
        for (uint32_t i = 0; i < LOD_LEVEL_COUNT; ++i)
        {
            glm::uvec4& lodData = (i == 0)
                                      ? obj.lod0Data
                                      : (i == 1)
                                      ? obj.lod1Data
                                      : (i == 2)
                                      ? obj.lod2Data
                                      : obj.lod3Data;
            lodData = glm::uvec4(
                submeshLoc.lods[i].vertexOffset,
                submeshLoc.lods[i].indexOffset,
                submeshLoc.lods[i].indexCount,
                submeshLoc.lods[i].vertexCount
            );
        }

        // Meshlet LOD data (for mesh shader path)
        // Each uvec4: (meshletOffset, meshletCount, baseVertexOffset, padding)
        obj.meshletLod0 = glm::uvec4(
            submeshLoc.meshletLods[0].meshletOffset,
            submeshLoc.meshletLods[0].meshletCount,
            submeshLoc.meshletLods[0].baseVertexOffset,
            0
        );
        obj.meshletLod1 = glm::uvec4(
            submeshLoc.meshletLods[1].meshletOffset,
            submeshLoc.meshletLods[1].meshletCount,
            submeshLoc.meshletLods[1].baseVertexOffset,
            0
        );
        obj.meshletLod2 = glm::uvec4(
            submeshLoc.meshletLods[2].meshletOffset,
            submeshLoc.meshletLods[2].meshletCount,
            submeshLoc.meshletLods[2].baseVertexOffset,
            0
        );
        // Determine bone offset: use resolver if provided and entity is valid
        uint32_t boneOffset = INVALID_BONE_OFFSET;
        if (boneOffsetResolver && meshRender.entity != entt::null)
        {
            boneOffset = boneOffsetResolver(meshRender.entity);
            static int logCounter = 0;
            if (logCounter++ % 300 == 0 || boneOffset != INVALID_BONE_OFFSET)
            {
                loggerInfo("MergedMeshBuffer: Entity {} mesh {} boneOffset={}",
                           static_cast<uint32_t>(meshRender.entity),
                           meshRender.meshPath,
                           boneOffset == INVALID_BONE_OFFSET ? -1 : static_cast<int>(boneOffset));
            }
        }

        obj.meshletLod3 = glm::uvec4(
            submeshLoc.meshletLods[3].meshletOffset,
            submeshLoc.meshletLods[3].meshletCount,
            submeshLoc.meshletLods[3].baseVertexOffset,
            boneOffset  // Bone matrix offset (INVALID_BONE_OFFSET for static meshes)
        );

        float bias = meshRender.lodBias;
        obj.lodThresholds = glm::vec4(
            LOD_THRESHOLD_0 * std::pow(2.0f, -bias),
            LOD_THRESHOLD_1 * std::pow(2.0f, -bias),
            LOD_THRESHOLD_2 * std::pow(2.0f, -bias),
            bias
        );

        // Material data
        const auto* subMat = meshRender.getMaterialForSubmesh(submeshLoc.submeshName);
        std::string materialPath;

        if (subMat)
        {
            obj.albedo = subMat->albedo;
            obj.iblParams = glm::vec4(subMat->iblDiffuse, subMat->iblSpecular, 0.0f, 0.0f);
            obj.materialParams = glm::vec4(subMat->metallic, subMat->roughness, subMat->ao, subMat->emission);
            materialPath = subMat->materialPath;
        }
        else
        {
            obj.albedo = meshRender.albedo;
            materialPath = meshRender.defaultMaterialPath;

            float iblDiffuse = 1.0f, iblSpecular = 0.5f;
            if (!materialPath.empty())
            {
                // Use unified extraction that handles both .vfMat and .vfMatInstance
                auto pbrValues = mesh::MaterialPBRExtractor::extractPBRFromPath(materialPath);
                iblDiffuse = pbrValues.iblDiffuse;
                iblSpecular = pbrValues.iblSpecular;
            }
            obj.iblParams = glm::vec4(iblDiffuse, iblSpecular, 0.0f, 0.0f);
            obj.materialParams = glm::vec4(meshRender.metallic, meshRender.roughness, meshRender.ao,
                                           meshRender.emission);
        }

        // Dynamic emission - need parent material for shader graph
        if (!materialPath.empty() && time > 0.0f)
        {
            // For instances, load the parent material to get the shader graph
            std::string effectiveMaterialPath = materialPath;
            if (material::isInstanceFile(materialPath))
            {
                auto instanceData = resource::ResourceManager::loadMaterialInstance(materialPath);
                if (instanceData && !instanceData->parentMaterialPath.empty())
                {
                    effectiveMaterialPath = instanceData->parentMaterialPath;
                }
            }

            auto matData = resource::ResourceManager::loadMaterial(effectiveMaterialPath);
            if (matData)
            {
                const auto* outputNode = matData->graph.findOutputNode();
                if (outputNode)
                {
                    float dynamicEmission = mesh::MaterialPBRExtractor::evaluateEmissionStrength(
                        matData->graph, outputNode->id, time);
                    if (dynamicEmission != 0.0f)
                    {
                        obj.materialParams.w = dynamicEmission;
                    }
                }
            }
        }

        // Texture indices
        if (textureResolver && !materialPath.empty())
        {
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
        }
        else
        {
            obj.textureIndices0 = glm::uvec4(INVALID_TEXTURE_INDEX);
            obj.textureIndices1 = glm::uvec4(INVALID_TEXTURE_INDEX);
        }

        // Flags
        obj.flags = 0;
        if (subMat && subMat->blendMode == 1)
        {
            obj.flags |= ObjectFlags::AlphaMask;
        }

        // Disable culling for animated meshes - bounding sphere is in bind pose, not animated pose
        if (boneOffset != INVALID_BONE_OFFSET)
        {
            obj.flags |= ObjectFlags::NoCull;
            obj.flags |= ObjectFlags::NoOcclude;
        }

        float scaleX = glm::length(glm::vec3(obj.modelMatrix[0]));
        float scaleY = glm::length(glm::vec3(obj.modelMatrix[1]));
        float scaleZ = glm::length(glm::vec3(obj.modelMatrix[2]));
        constexpr float uniformScaleEpsilon = 0.001f;
        if (std::abs(scaleX - scaleY) < uniformScaleEpsilon &&
            std::abs(scaleY - scaleZ) < uniformScaleEpsilon)
        {
            obj.flags |= ObjectFlags::UniformScale;
        }

        obj.availableLODMask = submeshLoc.getAvailableLODMask();

        // DEBUG: Log LOD mask for each object
        static int lodLogCounter = 0;
        if (lodLogCounter++ % 300 == 0 || obj.availableLODMask == 0)
        {
            loggerInfo("MergedMeshBuffer: Object {} mesh {} LODMask={} (binary: {:04b}) boneOffset={}",
                       currentObjectCount, meshRender.meshPath, obj.availableLODMask, obj.availableLODMask,
                       boneOffset == INVALID_BONE_OFFSET ? -1 : static_cast<int>(boneOffset));
        }

        obj.shaderGroupIndex = (shaderGroupResolver && !materialPath.empty())
                                   ? shaderGroupResolver(materialPath)
                                   : 0;
    }

    void MergedMeshBuffer::updateObjects(const std::vector<mesh::MeshRenderData>& renderData,
                                         const TextureIndexResolver& textureResolver,
                                         const ShaderGroupResolver& shaderGroupResolver,
                                         const BoneOffsetResolver& boneOffsetResolver,
                                         float time)
    {
        currentObjectCount = 0;

        for (const auto& meshRender : renderData)
        {
            auto meshIt = meshPathToIndex.find(meshRender.meshPath);
            if (meshIt == meshPathToIndex.end())
            {
                continue;
            }

            const auto& meshInfo = registeredMeshes[meshIt->second];

            for (uint32_t subIdx = 0; subIdx < meshInfo.submeshCount; ++subIdx)
            {
                const auto& submeshLoc = allSubmeshLocations[meshInfo.firstSubmeshIndex + subIdx];

                if (!submeshLoc.hasRenderableLOD())
                {
                    // DEBUG: Log when a mesh is skipped due to no renderable LOD
                    static int skipLogCounter = 0;
                    if (skipLogCounter++ % 60 == 0)  // More frequent logging
                    {
                        // Log detailed LOD state info
                        loggerInfo("MergedMeshBuffer: SKIPPING mesh {} submesh {} - no renderable LOD. States: LOD0={} LOD1={} LOD2={} LOD3={}",
                                   meshRender.meshPath, subIdx,
                                   static_cast<int>(submeshLoc.lodStates[0]),
                                   static_cast<int>(submeshLoc.lodStates[1]),
                                   static_cast<int>(submeshLoc.lodStates[2]),
                                   static_cast<int>(submeshLoc.lodStates[3]));
                    }
                    continue;
                }

                if (currentObjectCount >= maxObjectCount)
                {
                    loggerWarning("MergedMeshBuffer: max object count reached");
                    return;
                }

                GPUObjectData& obj = cpuObjectData[currentObjectCount];
                populateObjectData(obj, meshRender, submeshLoc, textureResolver, shaderGroupResolver, boneOffsetResolver, time);
                obj.entityId = currentObjectCount;

                // DEBUG: Log ALL objects once every ~300 frames
                static int logCycle = 0;
                if (currentObjectCount == 0) logCycle++;
                if (logCycle % 300 == 1)  // Log on cycle 1, 301, 601, etc.
                {
                    loggerInfo("MergedMeshBuffer: Object {} -> mesh={} LODMask={:04b} flags={}",
                               currentObjectCount, meshRender.meshPath,
                               obj.availableLODMask, obj.flags);
                    loggerInfo("  LOD0=({},{},{}) LOD1=({},{},{}) LOD2=({},{},{}) LOD3=({},{},{},{})",
                               obj.meshletLod0.x, obj.meshletLod0.y, obj.meshletLod0.z,
                               obj.meshletLod1.x, obj.meshletLod1.y, obj.meshletLod1.z,
                               obj.meshletLod2.x, obj.meshletLod2.y, obj.meshletLod2.z,
                               obj.meshletLod3.x, obj.meshletLod3.y, obj.meshletLod3.z, obj.meshletLod3.w);
                }

                currentObjectCount++;
            }
        }

        // DEBUG: Log summary once per frame
        static int summaryLogCounter = 0;
        if (summaryLogCounter++ % 300 == 0)
        {
            loggerInfo("MergedMeshBuffer: updateObjects complete - {} objects queued for GPU from {} meshes",
                       currentObjectCount, renderData.size());
        }
    }
}
