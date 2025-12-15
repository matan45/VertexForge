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

        // Initialize bounding box with first vertex we find
        bool boundingBoxInitialized = false;

        // Upload all submeshes to GPU
        for (const auto& meshData : meshesDataPtr->meshes)
        {
            if (meshData.vertices.empty())
            {
                loggerWarning("Skipping empty submesh in: {}", meshPath);
                continue;
            }

            SubMeshGPUData subMesh{};
            subMesh.name = meshData.name;  // Store submesh name for material assignment

            // Compute per-submesh bounding box
            bool subMeshBBInitialized = false;
            for (const auto& vertex : meshData.vertices)
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
            subMesh.vertexCount = static_cast<uint32_t>(meshData.vertices.size());
            subMesh.indexCount = static_cast<uint32_t>(meshData.indices.size());

            // Create vertex buffer (device local for best performance)
            vk::DeviceSize vertexBufferSize = sizeof(resource::Vertex) * meshData.vertices.size();

            core::BufferInfoRequest vertexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            vertexBufferRequest.size = vertexBufferSize;
            vertexBufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
            vertexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(vertexBufferRequest, subMesh.vertexBuffer, subMesh.vertexBufferMemory);

            // Copy vertex data to GPU using async transfer (non-blocking)
            transferManager->copyToBufferAsync(
                subMesh.vertexBuffer,
                meshData.vertices.data(),
                vertexBufferSize
            );

            // Create index buffer if indices exist
            if (!meshData.indices.empty())
            {
                vk::DeviceSize indexBufferSize = sizeof(uint32_t) * meshData.indices.size();

                core::BufferInfoRequest indexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
                indexBufferRequest.size = indexBufferSize;
                indexBufferRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
                indexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::Utilities::createBuffer(indexBufferRequest, subMesh.indexBuffer, subMesh.indexBufferMemory);

                // Copy index data to GPU using async transfer (non-blocking)
                transferManager->copyToBufferAsync(
                    subMesh.indexBuffer,
                    meshData.indices.data(),
                    indexBufferSize
                );
            }

            totalVertices += subMesh.vertexCount;
            totalIndices += subMesh.indexCount;
            gpuData.subMeshes.push_back(subMesh);
        }

        if (gpuData.subMeshes.empty())
        {
            loggerError("Mesh has no valid submeshes: {}", meshPath);
            return "";
        }

        // Wait for all async transfers to complete before the mesh can be used
        // This is still faster than synchronous transfers because:
        // 1. Multiple submeshes are uploaded in parallel
        // 2. Uses dedicated transfer queue (if available) without blocking graphics
        transferManager->waitAll();

        loadedMeshes[pathStr] = std::move(gpuData);
        loggerInfo("Loaded mesh: {} ({} submeshes, {} total vertices, {} total indices)",
                   meshPath, loadedMeshes[pathStr].subMeshes.size(), totalVertices, totalIndices);

        return pathStr;
    }

    std::string MeshGPUCache::uploadMesh(const std::string& meshId, const resource::MeshesData& meshesData)
    {
        if (loadedMeshes.contains(meshId))
        {
            return meshId;  // Already loaded
        }

        if (meshesData.meshes.empty())
        {
            loggerError("Cannot upload empty mesh data for: {}", meshId);
            return "";
        }

        MeshGPUData gpuData{};

        uint32_t totalVertices = 0;
        uint32_t totalIndices = 0;

        // Initialize bounding box with first vertex we find
        bool boundingBoxInitialized = false;

        // Upload all submeshes to GPU
        for (const auto& meshData : meshesData.meshes)
        {
            if (meshData.vertices.empty())
            {
                loggerWarning("Skipping empty submesh in procedural mesh: {}", meshId);
                continue;
            }

            SubMeshGPUData subMesh{};
            subMesh.name = meshData.name;

            // Compute per-submesh bounding box
            bool subMeshBBInitialized = false;
            for (const auto& vertex : meshData.vertices)
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
            subMesh.vertexCount = static_cast<uint32_t>(meshData.vertices.size());
            subMesh.indexCount = static_cast<uint32_t>(meshData.indices.size());

            // Create vertex buffer (device local for best performance)
            vk::DeviceSize vertexBufferSize = sizeof(resource::Vertex) * meshData.vertices.size();

            core::BufferInfoRequest vertexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            vertexBufferRequest.size = vertexBufferSize;
            vertexBufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
            vertexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::Utilities::createBuffer(vertexBufferRequest, subMesh.vertexBuffer, subMesh.vertexBufferMemory);

            // Copy vertex data to GPU using async transfer
            transferManager->copyToBufferAsync(
                subMesh.vertexBuffer,
                meshData.vertices.data(),
                vertexBufferSize
            );

            // Create index buffer if indices exist
            if (!meshData.indices.empty())
            {
                vk::DeviceSize indexBufferSize = sizeof(uint32_t) * meshData.indices.size();

                core::BufferInfoRequest indexBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
                indexBufferRequest.size = indexBufferSize;
                indexBufferRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
                indexBufferRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
                core::Utilities::createBuffer(indexBufferRequest, subMesh.indexBuffer, subMesh.indexBufferMemory);

                // Copy index data to GPU using async transfer
                transferManager->copyToBufferAsync(
                    subMesh.indexBuffer,
                    meshData.indices.data(),
                    indexBufferSize
                );
            }

            totalVertices += subMesh.vertexCount;
            totalIndices += subMesh.indexCount;
            gpuData.subMeshes.push_back(subMesh);
        }

        if (gpuData.subMeshes.empty())
        {
            loggerError("Procedural mesh has no valid submeshes: {}", meshId);
            return "";
        }

        // Wait for all async transfers to complete
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

        // Destroy all submesh buffers
        for (const auto& subMesh : gpuData.subMeshes)
        {
            if (subMesh.vertexBuffer)
            {
                device.getLogicalDevice().destroyBuffer(subMesh.vertexBuffer);
                device.getLogicalDevice().freeMemory(subMesh.vertexBufferMemory);
            }
            if (subMesh.indexBuffer)
            {
                device.getLogicalDevice().destroyBuffer(subMesh.indexBuffer);
                device.getLogicalDevice().freeMemory(subMesh.indexBufferMemory);
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
            for (const auto& subMesh : gpuData.subMeshes)
            {
                if (subMesh.vertexBuffer)
                {
                    device.getLogicalDevice().destroyBuffer(subMesh.vertexBuffer);
                    device.getLogicalDevice().freeMemory(subMesh.vertexBufferMemory);
                }
                if (subMesh.indexBuffer)
                {
                    device.getLogicalDevice().destroyBuffer(subMesh.indexBuffer);
                    device.getLogicalDevice().freeMemory(subMesh.indexBufferMemory);
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
