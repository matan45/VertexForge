#include "PhysicsDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "resource/ConvexDecompositionSidecar.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "resource/ConvexHullTypes.hpp"

#include <filesystem>

namespace render::mesh
{
    namespace
    {
        void appendPathStamp(std::string& key, const std::filesystem::path& path)
        {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec) || ec)
                return;

            const auto writeTime = std::filesystem::last_write_time(path, ec);
            if (!ec)
                key += "|t=" + std::to_string(writeTime.time_since_epoch().count());

            ec.clear();
            const auto size = std::filesystem::file_size(path, ec);
            if (!ec)
                key += "|s=" + std::to_string(size);
        }

        std::string makeMeshCacheBaseKey(const PhysicsColliderRenderData& data)
        {
            return data.meshPath
                + "#" + std::to_string(static_cast<int>(data.shape))
                + "@" + std::to_string(data.submeshIndex);
        }

        std::string makeMeshCacheKey(const PhysicsColliderRenderData& data)
        {
            std::string key = makeMeshCacheBaseKey(data);
            appendPathStamp(key, data.meshPath);
            appendPathStamp(key, resource::ConvexDecompositionSidecar::sidecarPathForMesh(data.meshPath));
            return key;
        }

        void appendConvexWireframe(const resource::ConvexDecompositionData& convexData,
                                   std::vector<glm::vec3>& positions,
                                   std::vector<uint32_t>& lineIndices)
        {
            uint32_t vertexOffset = static_cast<uint32_t>(positions.size());
            for (const auto& hull : convexData.hulls)
            {
                for (const auto& v : hull.vertices)
                {
                    positions.push_back(v);
                }

                for (size_t i = 0; i + 2 < hull.indices.size(); i += 3)
                {
                    const uint32_t i0 = hull.indices[i] + vertexOffset;
                    const uint32_t i1 = hull.indices[i + 1] + vertexOffset;
                    const uint32_t i2 = hull.indices[i + 2] + vertexOffset;

                    lineIndices.push_back(i0);
                    lineIndices.push_back(i1);
                    lineIndices.push_back(i1);
                    lineIndices.push_back(i2);
                    lineIndices.push_back(i2);
                    lineIndices.push_back(i0);
                }

                vertexOffset += static_cast<uint32_t>(hull.vertices.size());
            }
        }

        bool appendConvexForSubmesh(resource::MeshStreamHandle& stream,
                                    const std::string& meshPath,
                                    uint32_t submeshIndex,
                                    std::vector<glm::vec3>& positions,
                                    std::vector<uint32_t>& lineIndices)
        {
            resource::ConvexDecompositionData convexData;
            if (!resource::ConvexDecompositionSidecar::loadForSubmesh(meshPath, submeshIndex, convexData))
            {
                if (!stream.readConvexDecomposition(submeshIndex, convexData))
                    return false;
            }

            if (!convexData.isValid())
                return false;

            appendConvexWireframe(convexData, positions, lineIndices);
            return true;
        }

        bool appendLODWireframe(resource::MeshStreamHandle& stream,
                                uint32_t submeshIndex,
                                std::vector<glm::vec3>& positions,
                                std::vector<uint32_t>& lineIndices)
        {
            std::vector<resource::Vertex> vertices;
            std::vector<uint32_t> triangleIndices;
            if (!stream.readLODLevel(submeshIndex, 2, vertices, triangleIndices))
                return false;

            if (vertices.empty() || triangleIndices.empty())
                return false;

            const uint32_t vertexOffset = static_cast<uint32_t>(positions.size());
            positions.reserve(positions.size() + vertices.size());
            for (const auto& v : vertices)
            {
                positions.push_back(v.position);
            }

            lineIndices.reserve(lineIndices.size() + (triangleIndices.size() / 3) * 6);
            for (size_t i = 0; i + 2 < triangleIndices.size(); i += 3)
            {
                const uint32_t i0 = triangleIndices[i] + vertexOffset;
                const uint32_t i1 = triangleIndices[i + 1] + vertexOffset;
                const uint32_t i2 = triangleIndices[i + 2] + vertexOffset;

                lineIndices.push_back(i0);
                lineIndices.push_back(i1);
                lineIndices.push_back(i1);
                lineIndices.push_back(i2);
                lineIndices.push_back(i2);
                lineIndices.push_back(i0);
            }

            return true;
        }

        bool appendAllConvex(resource::MeshStreamHandle& stream,
                             const std::string& meshPath,
                             std::vector<glm::vec3>& positions,
                             std::vector<uint32_t>& lineIndices)
        {
            bool appended = false;
            const auto& header = stream.getHeader();
            for (uint32_t i = 0; i < header.numSubmeshes; ++i)
            {
                appended |= appendConvexForSubmesh(stream, meshPath, i, positions, lineIndices);
            }
            return appended;
        }

        bool appendAllLODWireframes(resource::MeshStreamHandle& stream,
                                    std::vector<glm::vec3>& positions,
                                    std::vector<uint32_t>& lineIndices)
        {
            bool appended = false;
            const auto& header = stream.getHeader();
            for (uint32_t i = 0; i < header.numSubmeshes; ++i)
            {
                appended |= appendLODWireframe(stream, i, positions, lineIndices);
            }
            return appended;
        }
    }

    const MeshDebugData* PhysicsDebugRenderer::getOrCreateHeightFieldBuffers(
        const PhysicsColliderRenderData& data) const
    {
        if (!data.heightfieldVertices || data.heightfieldVertices->empty() ||
            !data.heightfieldLineIndices || data.heightfieldLineIndices->empty())
        {
            return nullptr;
        }

        auto it = heightfieldCache.find(data.heightfieldCacheKey);
        if (it != heightfieldCache.end() && it->second.version == data.heightfieldVersion)
        {
            return it->second.buffers.isValid ? &it->second.buffers : nullptr;
        }

        if (it != heightfieldCache.end())
        {
            auto& dev = device.getLogicalDevice();
            if (it->second.buffers.vertexBuffer)
            {
                dev.destroyBuffer(it->second.buffers.vertexBuffer);
                device.getMemoryManager().free(it->second.buffers.vertexAllocation);
                it->second.buffers.vertexAllocation = {};
            }
            if (it->second.buffers.indexBuffer)
            {
                dev.destroyBuffer(it->second.buffers.indexBuffer);
                device.getMemoryManager().free(it->second.buffers.indexAllocation);
                it->second.buffers.indexAllocation = {};
            }
        }

        HeightFieldCacheEntry entry;
        entry.version = data.heightfieldVersion;

        const auto& vertices = *data.heightfieldVertices;
        const auto& indices = *data.heightfieldLineIndices;

        try
        {
            vk::DeviceSize vertexBufferSize = static_cast<vk::DeviceSize>(vertices.size() * sizeof(glm::vec3));
            core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            vertexRequest.size = vertexBufferSize;
            vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
            vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(vertexRequest, entry.buffers.vertexBuffer, entry.buffers.vertexAllocation, device.getMemoryManager());

            core::BufferUtilities::copyToBuffer(
                device.getLogicalDevice(),
                device.getPhysicalDevice(),
                device.getGraphicsQueue(),
                device.getStagingCommandPool(),
                entry.buffers.vertexBuffer,
                vertices.data(),
                vertexBufferSize
            );

            vk::DeviceSize indexBufferSize = static_cast<vk::DeviceSize>(indices.size() * sizeof(uint32_t));
            core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            indexRequest.size = indexBufferSize;
            indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
            indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(indexRequest, entry.buffers.indexBuffer, entry.buffers.indexAllocation, device.getMemoryManager());

            core::BufferUtilities::copyToBuffer(
                device.getLogicalDevice(),
                device.getPhysicalDevice(),
                device.getGraphicsQueue(),
                device.getStagingCommandPool(),
                entry.buffers.indexBuffer,
                indices.data(),
                indexBufferSize
            );

            entry.buffers.indexCount = static_cast<uint32_t>(indices.size());
            entry.buffers.isValid = true;
        }
        catch (...)
        {
            entry.buffers.isValid = false;
        }

        heightfieldCache[data.heightfieldCacheKey] = entry;
        return entry.buffers.isValid ? &heightfieldCache[data.heightfieldCacheKey].buffers : nullptr;
    }

    const MeshDebugData* PhysicsDebugRenderer::getOrCreateMeshBuffers(
        const PhysicsColliderRenderData& data) const
    {
        if (data.meshPath.empty())
        {
            return nullptr;
        }

        const std::string cacheKey = makeMeshCacheKey(data);
        auto it = meshCache.find(cacheKey);
        if (it != meshCache.end())
        {
            return it->second.isValid ? &it->second : nullptr;
        }

        const std::string cacheBaseKey = makeMeshCacheBaseKey(data);
        const std::string cacheVariantPrefix = cacheBaseKey + "|";
        for (auto stale = meshCache.begin(); stale != meshCache.end();)
        {
            const bool sameMeshVariant = stale->first == cacheBaseKey
                || stale->first.starts_with(cacheVariantPrefix);
            if (!sameMeshVariant)
            {
                ++stale;
                continue;
            }

            auto& dev = device.getLogicalDevice();
            if (stale->second.vertexBuffer)
            {
                dev.destroyBuffer(stale->second.vertexBuffer);
                device.getMemoryManager().free(stale->second.vertexAllocation);
                stale->second.vertexAllocation = {};
            }
            if (stale->second.indexBuffer)
            {
                dev.destroyBuffer(stale->second.indexBuffer);
                device.getMemoryManager().free(stale->second.indexAllocation);
                stale->second.indexAllocation = {};
            }
            stale = meshCache.erase(stale);
        }

        auto streamHandle = resource::MeshStreamResource::openStream(data.meshPath);
        if (!streamHandle)
        {
            meshCache[cacheKey] = MeshDebugData{};
            return nullptr;
        }

        std::vector<glm::vec3> positions;
        std::vector<uint32_t> lineIndices;

        bool appended = false;
        if (data.shape == types::ColliderShape::ConvexMesh)
        {
            appended = data.submeshIndex >= 0
                ? appendConvexForSubmesh(*streamHandle,
                                          data.meshPath,
                                          static_cast<uint32_t>(data.submeshIndex),
                                          positions,
                                          lineIndices)
                : appendAllConvex(*streamHandle, data.meshPath, positions, lineIndices);
        }

        if (!appended)
        {
            appended = data.submeshIndex >= 0
                ? appendLODWireframe(*streamHandle,
                                      static_cast<uint32_t>(data.submeshIndex),
                                      positions,
                                      lineIndices)
                : appendAllLODWireframes(*streamHandle, positions, lineIndices);
        }

        if (positions.empty() || lineIndices.empty())
        {
            meshCache[cacheKey] = MeshDebugData{};
            return nullptr;
        }

        MeshDebugData meshData;

        try
        {
            vk::DeviceSize vertexBufferSize = sizeof(glm::vec3) * positions.size();
            core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            vertexRequest.size = vertexBufferSize;
            vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
            vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(vertexRequest, meshData.vertexBuffer, meshData.vertexAllocation, device.getMemoryManager());

            core::BufferUtilities::copyToBuffer(
                device.getLogicalDevice(),
                device.getPhysicalDevice(),
                device.getGraphicsQueue(),
                device.getStagingCommandPool(),
                meshData.vertexBuffer,
                positions.data(),
                vertexBufferSize
            );

            vk::DeviceSize indexBufferSize = sizeof(uint32_t) * lineIndices.size();
            core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
            indexRequest.size = indexBufferSize;
            indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
            indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(indexRequest, meshData.indexBuffer, meshData.indexAllocation, device.getMemoryManager());

            core::BufferUtilities::copyToBuffer(
                device.getLogicalDevice(),
                device.getPhysicalDevice(),
                device.getGraphicsQueue(),
                device.getStagingCommandPool(),
                meshData.indexBuffer,
                lineIndices.data(),
                indexBufferSize
            );

            meshData.indexCount = static_cast<uint32_t>(lineIndices.size());
            meshData.isValid = true;
        }
        catch (...)
        {
            auto& dev = device.getLogicalDevice();
            if (meshData.vertexBuffer)
            {
                dev.destroyBuffer(meshData.vertexBuffer);
                device.getMemoryManager().free(meshData.vertexAllocation);
                meshData.vertexAllocation = {};
            }
            if (meshData.indexBuffer)
            {
                dev.destroyBuffer(meshData.indexBuffer);
                device.getMemoryManager().free(meshData.indexAllocation);
                meshData.indexAllocation = {};
            }
            meshCache[cacheKey] = MeshDebugData{};
            return nullptr;
        }

        meshCache[cacheKey] = std::move(meshData);
        return &meshCache[cacheKey];
    }

    glm::vec4 PhysicsDebugRenderer::getColorForCollider(const PhysicsColliderRenderData& data) const
    {
        if (data.isTrigger)
        {
            return glm::vec4(0.9f, 0.2f, 0.9f, 0.6f);
        }

        switch (data.bodyType)
        {
        case 0: return glm::vec4(0.2f, 0.4f, 0.9f, 1.0f); // Static
        case 1: return glm::vec4(0.2f, 0.9f, 0.3f, 1.0f); // Dynamic
        case 2: return glm::vec4(0.9f, 0.6f, 0.2f, 1.0f); // Kinematic
        default: return glm::vec4(0.2f, 0.9f, 0.3f, 1.0f);
        }
    }

    void PhysicsDebugRenderer::render(const vk::CommandBuffer& commandBuffer,
                                      const std::vector<PhysicsColliderRenderData>& colliders,
                                      const glm::mat4& view,
                                      const glm::mat4& projection) const
    {
        if (!initialized || !wireframePipeline || colliders.empty())
        {
            return;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, wireframePipeline);

        glm::mat4 viewProj = projection * view;

        for (const auto& collider : colliders)
        {
            PhysicsDebugPushConstants pushConstants{};
            pushConstants.color = getColorForCollider(collider);

            switch (collider.shape)
            {
            case types::ColliderShape::Box:
                {
                    if (!boxVertexBuffer) break;

                    glm::mat4 model = collider.worldMatrix;
                    // Unit cube vertices span -1 to +1, so scale by half-extents (size * 0.5)
                    model = glm::scale(model, collider.size * 0.5f);

                    pushConstants.mvp = viewProj * model;

                    commandBuffer.bindIndexBuffer(boxIndexBuffer, 0, vk::IndexType::eUint32);
                    vk::Buffer vertexBuffers[] = {boxVertexBuffer};
                    vk::DeviceSize offsets[] = {0};
                    commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

                    commandBuffer.pushConstants(wireframePipelineLayout,
                                                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                                0, sizeof(PhysicsDebugPushConstants), &pushConstants);

                    commandBuffer.drawIndexed(boxIndexCount, 1, 0, 0, 0);
                    break;
                }

            case types::ColliderShape::Sphere:
                {
                    if (!sphereVertexBuffer) break;

                    glm::mat4 model = collider.worldMatrix;
                    model = glm::scale(model, glm::vec3(collider.radius));

                    pushConstants.mvp = viewProj * model;

                    commandBuffer.bindIndexBuffer(sphereIndexBuffer, 0, vk::IndexType::eUint32);
                    vk::Buffer vertexBuffers[] = {sphereVertexBuffer};
                    vk::DeviceSize offsets[] = {0};
                    commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

                    commandBuffer.pushConstants(wireframePipelineLayout,
                                                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                                0, sizeof(PhysicsDebugPushConstants), &pushConstants);

                    commandBuffer.drawIndexed(sphereIndexCount, 1, 0, 0, 0);
                    break;
                }

            case types::ColliderShape::Capsule:
                {
                    if (!capsuleVertexBuffer) break;

                    glm::mat4 model = collider.worldMatrix;
                    float cylinderHalfHeight = (collider.height - 2.0f * collider.radius) / 2.0f;
                    if (cylinderHalfHeight < 0.0f) cylinderHalfHeight = 0.0f;
                    float totalHalfHeight = cylinderHalfHeight + collider.radius;
                    // Unit capsule has total half-height 2.0 (cylinder=1.0 + hemisphere=1.0)
                    model = glm::scale(model, glm::vec3(collider.radius, totalHalfHeight * 0.5f, collider.radius));

                    pushConstants.mvp = viewProj * model;

                    commandBuffer.bindIndexBuffer(capsuleIndexBuffer, 0, vk::IndexType::eUint32);
                    vk::Buffer vertexBuffers[] = {capsuleVertexBuffer};
                    vk::DeviceSize offsets[] = {0};
                    commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

                    commandBuffer.pushConstants(wireframePipelineLayout,
                                                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                                0, sizeof(PhysicsDebugPushConstants), &pushConstants);

                    commandBuffer.drawIndexed(capsuleIndexCount, 1, 0, 0, 0);
                    break;
                }

            case types::ColliderShape::ConvexMesh:
            case types::ColliderShape::TriangleMesh:
                {
                    const MeshDebugData* meshData = getOrCreateMeshBuffers(collider);

                    if (meshData && meshData->isValid)
                    {
                        glm::mat4 model = collider.worldMatrix;

                        pushConstants.mvp = viewProj * model;

                        commandBuffer.bindIndexBuffer(meshData->indexBuffer, 0, vk::IndexType::eUint32);
                        vk::Buffer vertexBuffers[] = {meshData->vertexBuffer};
                        vk::DeviceSize offsets[] = {0};
                        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

                        commandBuffer.pushConstants(wireframePipelineLayout,
                                                    vk::ShaderStageFlagBits::eVertex |
                                                    vk::ShaderStageFlagBits::eFragment,
                                                    0, sizeof(PhysicsDebugPushConstants), &pushConstants);

                        commandBuffer.drawIndexed(meshData->indexCount, 1, 0, 0, 0);
                    }
                    else
                    {
                        if (!boxVertexBuffer) break;

                        pushConstants.color = glm::vec4(0.9f, 0.9f, 0.2f, 0.8f);

                        glm::mat4 model = collider.worldMatrix;
                        model = glm::scale(model, collider.size * 0.5f);

                        pushConstants.mvp = viewProj * model;

                        commandBuffer.bindIndexBuffer(boxIndexBuffer, 0, vk::IndexType::eUint32);
                        vk::Buffer vertexBuffers[] = {boxVertexBuffer};
                        vk::DeviceSize offsets[] = {0};
                        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

                        commandBuffer.pushConstants(wireframePipelineLayout,
                                                    vk::ShaderStageFlagBits::eVertex |
                                                    vk::ShaderStageFlagBits::eFragment,
                                                    0, sizeof(PhysicsDebugPushConstants), &pushConstants);

                        commandBuffer.drawIndexed(boxIndexCount, 1, 0, 0, 0);
                    }
                    break;
                }

            case types::ColliderShape::HeightField:
                {
                    const MeshDebugData* hfData = getOrCreateHeightFieldBuffers(collider);
                    if (hfData && hfData->isValid)
                    {
                        // World-space vertices, identity world matrix
                        pushConstants.mvp = viewProj;
                        pushConstants.color = glm::vec4(0.0f, 1.0f, 0.3f, 1.0f);

                        commandBuffer.bindIndexBuffer(hfData->indexBuffer, 0, vk::IndexType::eUint32);
                        vk::Buffer vertexBuffers[] = {hfData->vertexBuffer};
                        vk::DeviceSize offsets[] = {0};
                        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

                        commandBuffer.pushConstants(wireframePipelineLayout,
                                                    vk::ShaderStageFlagBits::eVertex |
                                                    vk::ShaderStageFlagBits::eFragment,
                                                    0, sizeof(PhysicsDebugPushConstants), &pushConstants);

                        commandBuffer.drawIndexed(hfData->indexCount, 1, 0, 0, 0);
                    }
                    break;
                }
            }
        }
    }
}
