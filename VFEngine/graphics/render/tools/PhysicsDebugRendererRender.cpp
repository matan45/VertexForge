#include "PhysicsDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "resource/ConvexHullTypes.hpp"

namespace render::mesh
{
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
                dev.freeMemory(it->second.buffers.vertexMemory);
            }
            if (it->second.buffers.indexBuffer)
            {
                dev.destroyBuffer(it->second.buffers.indexBuffer);
                dev.freeMemory(it->second.buffers.indexMemory);
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
            core::BufferUtilities::createBuffer(vertexRequest, entry.buffers.vertexBuffer, entry.buffers.vertexMemory);

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
            core::BufferUtilities::createBuffer(indexRequest, entry.buffers.indexBuffer, entry.buffers.indexMemory);

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

    const MeshDebugData* PhysicsDebugRenderer::getOrCreateMeshBuffers(const std::string& meshPath) const
    {
        if (meshPath.empty())
        {
            return nullptr;
        }

        auto it = meshCache.find(meshPath);
        if (it != meshCache.end())
        {
            return it->second.isValid ? &it->second : nullptr;
        }

        auto streamHandle = resource::MeshStreamResource::openStream(meshPath);
        if (!streamHandle)
        {
            meshCache[meshPath] = MeshDebugData{};
            return nullptr;
        }

        std::vector<glm::vec3> positions;
        std::vector<uint32_t> lineIndices;

        resource::ConvexDecompositionData convexData;
        if (streamHandle->readConvexDecomposition(0, convexData) && convexData.isValid())
        {
            uint32_t vertexOffset = 0;
            for (const auto& hull : convexData.hulls)
            {
                for (const auto& v : hull.vertices)
                {
                    positions.push_back(v);
                }

                for (size_t i = 0; i + 2 < hull.indices.size(); i += 3)
                {
                    uint32_t i0 = hull.indices[i] + vertexOffset;
                    uint32_t i1 = hull.indices[i + 1] + vertexOffset;
                    uint32_t i2 = hull.indices[i + 2] + vertexOffset;

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
        else
        {
            std::vector<resource::Vertex> vertices;
            std::vector<uint32_t> triangleIndices;

            if (!streamHandle->readLODLevel(0, 2, vertices, triangleIndices))
            {
                meshCache[meshPath] = MeshDebugData{};
                return nullptr;
            }

            if (vertices.empty() || triangleIndices.empty())
            {
                meshCache[meshPath] = MeshDebugData{};
                return nullptr;
            }

            positions.reserve(vertices.size());
            for (const auto& v : vertices)
            {
                positions.push_back(v.position);
            }

            lineIndices.reserve((triangleIndices.size() / 3) * 6);
            for (size_t i = 0; i + 2 < triangleIndices.size(); i += 3)
            {
                uint32_t i0 = triangleIndices[i];
                uint32_t i1 = triangleIndices[i + 1];
                uint32_t i2 = triangleIndices[i + 2];

                lineIndices.push_back(i0);
                lineIndices.push_back(i1);
                lineIndices.push_back(i1);
                lineIndices.push_back(i2);
                lineIndices.push_back(i2);
                lineIndices.push_back(i0);
            }
        }

        if (positions.empty() || lineIndices.empty())
        {
            meshCache[meshPath] = MeshDebugData{};
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
            core::BufferUtilities::createBuffer(vertexRequest, meshData.vertexBuffer, meshData.vertexMemory);

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
            core::BufferUtilities::createBuffer(indexRequest, meshData.indexBuffer, meshData.indexMemory);

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
                dev.freeMemory(meshData.vertexMemory);
            }
            if (meshData.indexBuffer)
            {
                dev.destroyBuffer(meshData.indexBuffer);
                dev.freeMemory(meshData.indexMemory);
            }
            meshCache[meshPath] = MeshDebugData{};
            return nullptr;
        }

        meshCache[meshPath] = std::move(meshData);
        return &meshCache[meshPath];
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
                    model = glm::scale(model, collider.size);

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
                    model = glm::scale(model, glm::vec3(collider.radius, totalHalfHeight, collider.radius));

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
                    const MeshDebugData* meshData = getOrCreateMeshBuffers(collider.meshPath);

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
                        model = glm::scale(model, collider.size);

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
