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

    // Helper function to upload a single LOD level to GPU
    void MeshGPUCache::uploadLODLevel(LODGPUBuffers& lodBuffers, const resource::LODLevel& lodLevel)
    {
        if (lodLevel.vertices.empty()) {
            return;
        }

        lodBuffers.vertexCount = static_cast<uint32_t>(lodLevel.vertices.size());
        lodBuffers.indexCount = static_cast<uint32_t>(lodLevel.indices.size());

        // Create vertex buffer (device local for best performance)
        vk::DeviceSize vertexBufferSize = sizeof(resource::Vertex) * lodLevel.vertices.size();

        core::BufferInfoRequest vertexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexBufferRequest.size = vertexBufferSize;
        vertexBufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::Utilities::createBuffer(vertexBufferRequest, lodBuffers.vertexBuffer, lodBuffers.vertexBufferMemory);

        // Copy vertex data to GPU using async transfer (non-blocking)
        transferManager->copyToBufferAsync(
            lodBuffers.vertexBuffer,
            lodLevel.vertices.data(),
            vertexBufferSize
        );

        // Create index buffer if indices exist
        if (!lodLevel.indices.empty())
        {
            vk::DeviceSize indexBufferSize = sizeof(uint32_t) * lodLevel.indices.size();

            core::BufferInfoRequest indexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            indexBufferRequest.size = indexBufferSize;
            indexBufferRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
            indexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(indexBufferRequest, lodBuffers.indexBuffer, lodBuffers.indexBufferMemory);

            // Copy index data to GPU using async transfer (non-blocking)
            transferManager->copyToBufferAsync(
                lodBuffers.indexBuffer,
                lodLevel.indices.data(),
                indexBufferSize
            );
        }
    }

    // Helper function to destroy LOD buffers
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

        uint32_t totalVertices = 0;
        uint32_t totalIndices = 0;
        uint32_t totalLODBuffers = 0;

        // Initialize bounding box with first vertex we find
        bool boundingBoxInitialized = false;

        // Upload all submeshes to GPU
        for (const auto& meshData : meshesDataPtr->meshes)
        {
            if (meshData.lodLevels.empty() || meshData.lodLevels[0].vertices.empty())
            {
                loggerWarning("Skipping empty submesh in: {}", meshPath);
                continue;
            }

            SubMeshGPUData subMesh{};
            subMesh.name = meshData.name;

            // Compute per-submesh bounding box from LOD0 vertices
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

                // Also expand the combined mesh bounding box
                if (!boundingBoxInitialized)
                {
                    gpuData.boundingBox.min = vertex.position;
                    gpuData.boundingBox.max = vertex.position;
                    boundingBoxInitialized = true;
                }
                else
                {
                    gpuData.boundingBox.expand(vertex.position);
                }
            }

            // Upload all LOD levels
            uint32_t lodCount = static_cast<uint32_t>(std::min(meshData.lodLevels.size(),
                                                               static_cast<size_t>(resource::LOD_LEVEL_COUNT)));
            for (uint32_t lod = 0; lod < lodCount; ++lod)
            {
                uploadLODLevel(subMesh.lodLevels[lod], meshData.lodLevels[lod]);
                totalLODBuffers++;
            }

            // If fewer than 4 LOD levels were provided, duplicate the last one
            for (uint32_t lod = lodCount; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                uploadLODLevel(subMesh.lodLevels[lod], meshData.lodLevels[lodCount - 1]);
                totalLODBuffers++;
            }

            // Track totals from LOD0
            totalVertices += subMesh.lodLevels[0].vertexCount;
            totalIndices += subMesh.lodLevels[0].indexCount;
            gpuData.subMeshes.push_back(std::move(subMesh));
        }

        if (gpuData.subMeshes.empty())
        {
            loggerError("Mesh has no valid submeshes: {}", meshPath);
            return "";
        }

        // Wait for all async transfers to complete before the mesh can be used
        transferManager->waitAll();

        loadedMeshes[pathStr] = std::move(gpuData);
        loggerInfo("Loaded mesh: {} ({} submeshes, {} LOD buffers, {} vertices LOD0, {} indices LOD0)",
                   meshPath, loadedMeshes[pathStr].subMeshes.size(), totalLODBuffers, totalVertices, totalIndices);

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

        uint32_t totalVertices = 0;
        uint32_t totalIndices = 0;

        bool boundingBoxInitialized = false;

        for (const auto& meshData : meshesData.meshes)
        {
            if (meshData.lodLevels.empty() || meshData.lodLevels[0].vertices.empty())
            {
                loggerWarning("Skipping empty submesh in procedural mesh: {}", meshId);
                continue;
            }

            SubMeshGPUData subMesh{};
            subMesh.name = meshData.name;

            // Compute per-submesh bounding box from LOD0 vertices
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
                    gpuData.boundingBox.min = vertex.position;
                    gpuData.boundingBox.max = vertex.position;
                    boundingBoxInitialized = true;
                }
                else
                {
                    gpuData.boundingBox.expand(vertex.position);
                }
            }

            // Upload all LOD levels
            uint32_t lodCount = static_cast<uint32_t>(std::min(meshData.lodLevels.size(),
                                                               static_cast<size_t>(resource::LOD_LEVEL_COUNT)));
            for (uint32_t lod = 0; lod < lodCount; ++lod)
            {
                uploadLODLevel(subMesh.lodLevels[lod], meshData.lodLevels[lod]);
            }

            // If fewer than 4 LOD levels, duplicate the last one
            for (uint32_t lod = lodCount; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                uploadLODLevel(subMesh.lodLevels[lod], meshData.lodLevels[lodCount - 1]);
            }

            totalVertices += subMesh.lodLevels[0].vertexCount;
            totalIndices += subMesh.lodLevels[0].indexCount;
            gpuData.subMeshes.push_back(std::move(subMesh));
        }

        if (gpuData.subMeshes.empty())
        {
            loggerError("Procedural mesh has no valid submeshes: {}", meshId);
            return "";
        }

        transferManager->waitAll();

        loadedMeshes[meshId] = std::move(gpuData);
        loggerInfo("Uploaded procedural mesh: {} ({} submeshes, {} vertices, {} indices)",
                   meshId, loadedMeshes[meshId].subMeshes.size(), totalVertices, totalIndices);

        return meshId;
    }

    void MeshGPUCache::unloadMesh(const std::string& meshId)
    {
        auto it = loadedMeshes.find(meshId);
        if (it == loadedMeshes.end())
        {
            return;
        }

        const auto& gpuData = it->second;

        // Wait for device to finish using the buffers
        device.getLogicalDevice().waitIdle();

        // Destroy all submesh LOD buffers
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

        // Wait for device to finish
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
