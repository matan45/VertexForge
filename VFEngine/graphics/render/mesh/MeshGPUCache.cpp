#include "MeshGPUCache.hpp"
#include "../../core/Device.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/TransferManager.hpp"
#include "resource/MeshResource.hpp"
#include "resource/ResourceManager.hpp"
#include "print/Logger.hpp"

namespace render::mesh
{
    MeshGPUCache::MeshGPUCache(core::Device& device)
        : device(device)
    {
        // Create async transfer manager - uses dedicated transfer queue if available
        uint32_t transferQueueFamily = device.getQueueFamilyIndices().transferFamily.value();
        transferManager = std::make_unique<core::TransferManager>(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getTransferQueue(),
            transferQueueFamily
        );
    }

    MeshGPUCache::~MeshGPUCache()
    {
        unloadAllMeshes();
    }

    void MeshGPUCache::uploadLODLevel(LODGPUBuffers& lodBuffers, const resource::LODLevel& lodLevel)
    {
        if (lodLevel.vertices.empty())
        {
            return;
        }

        lodBuffers.vertexCount = static_cast<uint32_t>(lodLevel.vertices.size());
        lodBuffers.indexCount = static_cast<uint32_t>(lodLevel.indices.size());

        vk::DeviceSize vertexBufferSize = sizeof(resource::Vertex) * lodLevel.vertices.size();

        core::BufferInfoRequest vertexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexBufferRequest.size = vertexBufferSize;
        vertexBufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst |
            vk::BufferUsageFlagBits::eTransferSrc;
        vertexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::Utilities::createBuffer(vertexBufferRequest, lodBuffers.vertexBuffer, lodBuffers.vertexBufferMemory);

        transferManager->copyToBufferAsync(
            lodBuffers.vertexBuffer,
            lodLevel.vertices.data(),
            vertexBufferSize
        );

        if (!lodLevel.indices.empty())
        {
            vk::DeviceSize indexBufferSize = sizeof(uint32_t) * lodLevel.indices.size();

            core::BufferInfoRequest indexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            indexBufferRequest.size = indexBufferSize;
            indexBufferRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst |
                vk::BufferUsageFlagBits::eTransferSrc;
            indexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(indexBufferRequest, lodBuffers.indexBuffer, lodBuffers.indexBufferMemory);

            transferManager->copyToBufferAsync(
                lodBuffers.indexBuffer,
                lodLevel.indices.data(),
                indexBufferSize
            );
        }
    }

    void MeshGPUCache::destroyLODBuffers(LODGPUBuffers& lodBuffers)
    {
        if (lodBuffers.vertexBuffer)
        {
            device.getLogicalDevice().destroyBuffer(lodBuffers.vertexBuffer);
            device.getLogicalDevice().freeMemory(lodBuffers.vertexBufferMemory);
            lodBuffers.vertexBuffer = nullptr;
            lodBuffers.vertexBufferMemory = nullptr;
        }
        if (lodBuffers.indexBuffer)
        {
            device.getLogicalDevice().destroyBuffer(lodBuffers.indexBuffer);
            device.getLogicalDevice().freeMemory(lodBuffers.indexBufferMemory);
            lodBuffers.indexBuffer = nullptr;
            lodBuffers.indexBufferMemory = nullptr;
        }
        lodBuffers.vertexCount = 0;
        lodBuffers.indexCount = 0;
    }

    bool MeshGPUCache::processMeshData(const resource::MeshesData& meshesData,
                                       const std::string& meshId,
                                       MeshGPUData& outGpuData)
    {
        bool boundingBoxInitialized = false;

        for (const auto& meshData : meshesData.meshes)
        {
            if (meshData.lodLevels.empty() || meshData.lodLevels[0].vertices.empty())
            {
                loggerWarning("Skipping empty submesh in: {}", meshId);
                continue;
            }

            SubMeshGPUData subMesh{};
            subMesh.name = meshData.name;

            // Compute bounding boxes from LOD0 vertices
            const auto& lod0 = meshData.lodLevels[0];
            bool subMeshBBInitialized = false;
            for (const auto& vertex : lod0.vertices)
            {
                if (!subMeshBBInitialized)
                {
                    subMesh.boundingBox.min = vertex.position;
                    subMesh.boundingBox.max = vertex.position;
                    subMeshBBInitialized = true;
                }
                else
                {
                    subMesh.boundingBox.expand(vertex.position);
                }

                if (!boundingBoxInitialized)
                {
                    outGpuData.boundingBox.min = vertex.position;
                    outGpuData.boundingBox.max = vertex.position;
                    boundingBoxInitialized = true;
                }
                else
                {
                    outGpuData.boundingBox.expand(vertex.position);
                }
            }
            
            uint32_t lodCount = static_cast<uint32_t>(std::min(meshData.lodLevels.size(),
                                                               static_cast<size_t>(resource::LOD_LEVEL_COUNT)));
            for (uint32_t lod = 0; lod < lodCount; ++lod)
            {
                uploadLODLevel(subMesh.lodLevels[lod], meshData.lodLevels[lod]);
            }
            
            for (uint32_t lod = lodCount; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                uploadLODLevel(subMesh.lodLevels[lod], meshData.lodLevels[lodCount - 1]);
            }

            outGpuData.subMeshes.push_back(std::move(subMesh));
        }

        return !outGpuData.subMeshes.empty();
    }

    std::string MeshGPUCache::loadMesh(std::string_view meshPath)
    {
        std::string pathStr(meshPath);

        if (loadedMeshes.contains(pathStr))
        {
            return pathStr;
        }

        auto meshFuture = resource::ResourceManager::loadMeshAsync(meshPath);
        auto meshesDataPtr = meshFuture.get();

        if (!meshesDataPtr || meshesDataPtr->meshes.empty())
        {
            loggerError("Failed to load mesh from: {}", meshPath);
            return "";
        }

        MeshGPUData gpuData{};
        if (!processMeshData(*meshesDataPtr, pathStr, gpuData))
        {
            loggerError("Mesh has no valid submeshes: {}", meshPath);
            return "";
        }

        transferManager->waitAll();

        loadedMeshes[pathStr] = std::move(gpuData);
        loggerInfo("Loaded mesh: {} ({} submeshes)", meshPath, loadedMeshes[pathStr].subMeshes.size());

        return pathStr;
    }

    std::string MeshGPUCache::uploadMesh(const std::string& meshId, const resource::MeshesData& meshesData)
    {
        if (loadedMeshes.contains(meshId))
        {
            return meshId;
        }

        if (meshesData.meshes.empty())
        {
            loggerError("Cannot upload empty mesh data for: {}", meshId);
            return "";
        }

        MeshGPUData gpuData{};
        if (!processMeshData(meshesData, meshId, gpuData))
        {
            loggerError("Procedural mesh has no valid submeshes: {}", meshId);
            return "";
        }

        transferManager->waitAll();

        loadedMeshes[meshId] = std::move(gpuData);
        loggerInfo("Uploaded procedural mesh: {} ({} submeshes)", meshId, loadedMeshes[meshId].subMeshes.size());

        return meshId;
    }

    void MeshGPUCache::unloadMesh(const std::string& meshId)
    {
        auto it = loadedMeshes.find(meshId);
        if (it == loadedMeshes.end())
        {
            return;
        }
        
        device.getLogicalDevice().waitIdle();
        
        for (auto& subMesh : it->second.subMeshes)
        {
            for (auto& lodBuffers : subMesh.lodLevels)
            {
                destroyLODBuffers(lodBuffers);
            }
        }

        loadedMeshes.erase(it);
        loggerInfo("Unloaded mesh: {}", meshId);
    }

    void MeshGPUCache::unloadAllMeshes()
    {
        if (loadedMeshes.empty())
        {
            return;
        }
        
        device.getLogicalDevice().waitIdle();

        for (auto& [path, gpuData] : loadedMeshes)
        {
            for (auto& subMesh : gpuData.subMeshes)
            {
                for (auto& lodBuffers : subMesh.lodLevels)
                {
                    destroyLODBuffers(lodBuffers);
                }
            }
        }

        loadedMeshes.clear();
        loggerInfo("Unloaded all meshes");
    }

    const MeshGPUData* MeshGPUCache::getMesh(const std::string& meshId) const
    {
        auto it = loadedMeshes.find(meshId);
        if (it != loadedMeshes.end())
        {
            return &it->second;
        }
        return nullptr;
    }

    bool MeshGPUCache::isMeshLoaded(const std::string& meshId) const
    {
        return loadedMeshes.contains(meshId);
    }

    const math::AABB* MeshGPUCache::getMeshBoundingBox(const std::string& meshId) const
    {
        auto it = loadedMeshes.find(meshId);
        if (it == loadedMeshes.end())
        {
            return nullptr;
        }
        return &it->second.boundingBox;
    }

    std::vector<std::string> MeshGPUCache::getLoadedMeshIds() const
    {
        std::vector<std::string> ids;
        ids.reserve(loadedMeshes.size());
        for (const auto& [path, _] : loadedMeshes)
        {
            ids.push_back(path);
        }
        return ids;
    }
}
