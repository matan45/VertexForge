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

        if (!materialChangeCallbackId)
        {
            materialChangeCallbackId = material::MaterialManager::instance().registerChangeCallback(
                [this](const std::string& materialPath) {
                    pbrCache.erase(materialPath);
                    instanceToParentCache.erase(materialPath);
                    if (!material::isInstanceFile(materialPath))
                    {
                        std::erase_if(pbrCache, [](const auto& pair) {
                            return material::isInstanceFile(pair.first);
                        });
                        std::erase_if(instanceToParentCache, [](const auto& pair) {
                            return material::isInstanceFile(pair.first);
                        });
                    }
                });
        }

        initialized = true;

        loggerInfo("MergedMeshBuffer initialized: {} max vertices, {} max indices, {} max objects",
                   maxVertexCount, maxIndexCount, maxObjectCount);
    }

    void MergedMeshBuffer::cleanup()
    {
        if (!initialized) return;

        if (materialChangeCallbackId)
        {
            material::MaterialManager::instance().unregisterChangeCallback(materialChangeCallbackId);
            materialChangeCallbackId = {};
        }

        pbrCache.clear();
        instanceToParentCache.clear();

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

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxVertexCount * vertexStride;
            request.usage = vk::BufferUsageFlagBits::eVertexBuffer |
                vk::BufferUsageFlagBits::eTransferDst |
                vk::BufferUsageFlagBits::eStorageBuffer;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, vertexBuffer, vertexBufferMemory);
        }

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxIndexCount * sizeof(uint32_t);
            request.usage = vk::BufferUsageFlagBits::eIndexBuffer |
                vk::BufferUsageFlagBits::eTransferDst |
                vk::BufferUsageFlagBits::eStorageBuffer;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, indexBuffer, indexBufferMemory);
        }

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxObjectCount * sizeof(GPUObjectData);
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer |
                vk::BufferUsageFlagBits::eTransferDst;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, objectBuffer, objectBufferMemory);
        }

        {
            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = maxObjectCount * sizeof(GPUObjectData);
            request.usage = vk::BufferUsageFlagBits::eTransferSrc;
            request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(request, objectStagingBuffer, objectStagingMemory);

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

        size_t copySize = currentObjectCount * sizeof(GPUObjectData);
        std::memcpy(objectStagingMapped, cpuObjectData.data(), copySize);

        vk::BufferCopy copyRegion;
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = copySize;
        cmd.copyBuffer(objectStagingBuffer, objectBuffer, copyRegion);

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

        if (lodInfo.vertexCount != vertexCount || lodInfo.indexCount != indexCount)
        {
            loggerError("MergedMeshBuffer::uploadLOD: Count mismatch for {}:{} LOD{}: "
                        "expected {}v/{}i, got {}v/{}i",
                        meshPath, submeshName, lodLevel,
                        lodInfo.vertexCount, lodInfo.indexCount,
                        vertexCount, indexCount);
            return false;
        }

        if (vertexData && vertexCount > 0)
        {
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

        if (indexData && indexCount > 0)
        {
            uploadIndexDataAt(lodInfo.indexOffset, indexData, indexCount);
        }

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

    void MergedMeshBuffer::populateObjectData(GPUObjectData& obj,
                                              const mesh::MeshRenderData& meshRender,
                                              const SubmeshLocation& submeshLoc,
                                              const TextureIndexResolver& textureResolver,
                                              const ShaderGroupResolver& shaderGroupResolver,
                                              const BoneOffsetResolver& boneOffsetResolver,
                                              float time)
    {
        obj.modelMatrix = meshRender.modelMatrix;
        // Note: aabbMin is interpreted as boundingSphere by GPU shader for frustum culling
        // Store bounding sphere (center.xyz, radius) instead of actual AABB min
        obj.aabbMin = submeshLoc.boundingSphere;
        obj.aabbMax = glm::vec4(submeshLoc.aabbMax, 0.0f);

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

        uint32_t boneOffset = INVALID_BONE_OFFSET;
        if (boneOffsetResolver && meshRender.entity != entt::null)
        {
            boneOffset = boneOffsetResolver(meshRender.entity);
        }

        obj.meshletLod3 = glm::uvec4(
            submeshLoc.meshletLods[3].meshletOffset,
            submeshLoc.meshletLods[3].meshletCount,
            submeshLoc.meshletLods[3].baseVertexOffset,
            boneOffset
        );

        float bias = meshRender.lodBias;
        obj.lodThresholds = glm::vec4(
            LOD_THRESHOLD_0 * std::pow(2.0f, -bias),
            LOD_THRESHOLD_1 * std::pow(2.0f, -bias),
            LOD_THRESHOLD_2 * std::pow(2.0f, -bias),
            bias
        );

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
                auto it = pbrCache.find(materialPath);
                if (it == pbrCache.end())
                {
                    it = pbrCache.emplace(materialPath,
                                          mesh::MaterialPBRExtractor::extractPBRFromPath(materialPath)).first;
                }
                iblDiffuse = it->second.iblDiffuse;
                iblSpecular = it->second.iblSpecular;
            }
            obj.iblParams = glm::vec4(iblDiffuse, iblSpecular, 0.0f, 0.0f);
            obj.materialParams = glm::vec4(meshRender.metallic, meshRender.roughness, meshRender.ao,
                                           meshRender.emission);
        }

        if (!materialPath.empty() && time > 0.0f)
        {
            std::string effectiveMaterialPath = materialPath;
            if (material::isInstanceFile(materialPath))
            {
                auto cacheIt = instanceToParentCache.find(materialPath);
                if (cacheIt != instanceToParentCache.end())
                {
                    effectiveMaterialPath = cacheIt->second;
                }
                else
                {
                    auto instanceData = resource::ResourceManager::loadMaterialInstance(materialPath);
                    if (instanceData && !instanceData->parentMaterialPath.empty())
                    {
                        effectiveMaterialPath = instanceData->parentMaterialPath;
                        instanceToParentCache[materialPath] = effectiveMaterialPath;
                    }
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

        obj.flags = 0;
        if (subMat && subMat->blendMode == 1)
        {
            obj.flags |= ObjectFlags::AlphaMask;
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

                currentObjectCount++;
            }
        }
    }

    SubmeshLocation* MergedMeshBuffer::allocateTerrainTile(
        const std::string& tileKey,
        const std::array<uint32_t, 4>& vertexCounts,
        const std::array<uint32_t, 4>& indexCounts,
        const glm::vec3& aabbMin,
        const glm::vec3& aabbMax)
    {
        if (!initialized)
        {
            loggerError("MergedMeshBuffer: Not initialized");
            return nullptr;
        }

        // Check if already allocated
        auto existingIt = submeshKeyToIndex.find(tileKey);
        if (existingIt != submeshKeyToIndex.end())
        {
            return &allSubmeshLocations[existingIt->second];
        }

        // Create submesh location for terrain tile
        SubmeshLocation loc;
        loc.meshPath = tileKey;
        loc.submeshName = "terrain";
        loc.submeshIndex = 0;
        loc.aabbMin = aabbMin;
        loc.aabbMax = aabbMax;
        loc.calculateBoundingSphere();

        // Allocate space for each LOD
        for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
        {
            if (vertexCounts[lod] > 0 && indexCounts[lod] > 0)
            {
                if (!allocateLODSpace(loc, lod, vertexCounts[lod], indexCounts[lod], tileKey))
                {
                    // Failed, free previously allocated LODs
                    for (uint32_t prevLod = 0; prevLod < lod; ++prevLod)
                    {
                        if (loc.lods[prevLod].vertexCount > 0)
                        {
                            vertexAllocator.free(loc.lods[prevLod].vertexOffset, loc.lods[prevLod].vertexCount);
                            indexAllocator.free(loc.lods[prevLod].indexOffset, loc.lods[prevLod].indexCount);
                        }
                    }
                    loggerError("MergedMeshBuffer: Failed to allocate terrain tile {}", tileKey);
                    return nullptr;
                }
                loc.lodStates[lod] = LODStreamState::Ready;
            }
        }

        // Store location
        size_t locIndex = allSubmeshLocations.size();
        allSubmeshLocations.push_back(std::move(loc));
        submeshKeyToIndex[tileKey] = locIndex;

        // Also register as a "mesh" for updateObjects to find
        if (meshPathToIndex.find(tileKey) == meshPathToIndex.end())
        {
            MergedMeshInfo meshInfo;
            meshInfo.meshPath = tileKey;
            meshInfo.firstSubmeshIndex = static_cast<uint32_t>(locIndex);
            meshInfo.submeshCount = 1;
            meshInfo.aabbMin = aabbMin;
            meshInfo.aabbMax = aabbMax;

            size_t meshIndex = registeredMeshes.size();
            registeredMeshes.push_back(std::move(meshInfo));
            meshPathToIndex[tileKey] = meshIndex;
        }

        return &allSubmeshLocations[locIndex];
    }

    bool MergedMeshBuffer::uploadTerrainLOD(
        const std::string& tileKey,
        uint32_t lodLevel,
        const resource::Vertex* vertexData, uint32_t vertexCount,
        const uint32_t* indexData, uint32_t indexCount)
    {
        if (!initialized || lodLevel >= LOD_LEVEL_COUNT)
        {
            return false;
        }

        auto it = submeshKeyToIndex.find(tileKey);
        if (it == submeshKeyToIndex.end())
        {
            loggerError("MergedMeshBuffer::uploadTerrainLOD: Tile not found: {}", tileKey);
            return false;
        }

        auto& loc = allSubmeshLocations[it->second];
        const auto& lodInfo = loc.lods[lodLevel];

        if (lodInfo.vertexCount != vertexCount || lodInfo.indexCount != indexCount)
        {
            loggerError("MergedMeshBuffer::uploadTerrainLOD: Count mismatch for {} LOD{}: "
                        "expected {}v/{}i, got {}v/{}i",
                        tileKey, lodLevel,
                        lodInfo.vertexCount, lodInfo.indexCount,
                        vertexCount, indexCount);
            return false;
        }

        // Upload vertex data
        if (vertexData && vertexCount > 0)
        {
            uploadVertexDataAt(lodInfo.vertexOffset, vertexData, vertexCount);
        }

        // Upload index data
        if (indexData && indexCount > 0)
        {
            uploadIndexDataAt(lodInfo.indexOffset, indexData, indexCount);
        }

        loc.lodStates[lodLevel] = LODStreamState::Ready;
        return true;
    }

    void MergedMeshBuffer::freeTerrainTile(const std::string& tileKey)
    {
        auto locIt = submeshKeyToIndex.find(tileKey);
        if (locIt == submeshKeyToIndex.end())
        {
            return;
        }

        auto& loc = allSubmeshLocations[locIt->second];

        // Free all LOD allocations
        for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
        {
            if (loc.lods[lod].vertexCount > 0)
            {
                vertexAllocator.free(loc.lods[lod].vertexOffset, loc.lods[lod].vertexCount);
            }
            if (loc.lods[lod].indexCount > 0)
            {
                indexAllocator.free(loc.lods[lod].indexOffset, loc.lods[lod].indexCount);
            }
            loc.lods[lod] = LODDrawInfo{};
            loc.lodStates[lod] = LODStreamState::NotRequested;
        }

        // Note: We don't remove from allSubmeshLocations to preserve indices
        // The allocation will be overwritten if the tile is re-created

        // Remove from mesh registry
        auto meshIt = meshPathToIndex.find(tileKey);
        if (meshIt != meshPathToIndex.end())
        {
            // Mark as invalid by clearing submesh count
            registeredMeshes[meshIt->second].submeshCount = 0;
        }
    }
}
