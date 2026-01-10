#include "PhysicsDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "resource/MeshStreamHandle.hpp"
#include "resource/ConvexHullTypes.hpp"
#include <cmath>
#include <stdexcept>

namespace render::mesh
{
    static constexpr std::array<glm::vec3, 8> kUnitCubeVertices = {
        {
            {-1.0f, -1.0f, -1.0f}, // 0: back-bottom-left
            {1.0f, -1.0f, -1.0f}, // 1: back-bottom-right
            {1.0f, 1.0f, -1.0f}, // 2: back-top-right
            {-1.0f, 1.0f, -1.0f}, // 3: back-top-left
            {-1.0f, -1.0f, 1.0f}, // 4: front-bottom-left
            {1.0f, -1.0f, 1.0f}, // 5: front-bottom-right
            {1.0f, 1.0f, 1.0f}, // 6: front-top-right
            {-1.0f, 1.0f, 1.0f}, // 7: front-top-left
        }
    };

    static constexpr std::array<uint32_t, 24> kUnitCubeLineIndices = {
        {
            0, 1, 1, 2, 2, 3, 3, 0, // Back face edges
            4, 5, 5, 6, 6, 7, 7, 4, // Front face edges
            0, 4, 1, 5, 2, 6, 3, 7 // Connecting edges
        }
    };

    PhysicsDebugRenderer::PhysicsDebugRenderer(core::Device& device, core::SwapChain& swapChain)
        : device{device}, swapChain{swapChain}
    {
    }

    PhysicsDebugRenderer::~PhysicsDebugRenderer() = default;

    void PhysicsDebugRenderer::init(vk::RenderPass renderPass)
    {
        // Note: BufferUtilities and vulkan-hpp throw exceptions on allocation failure.
        // If any step fails, the exception propagates and initialized remains false.
        // The render() method has null checks for each buffer as additional safety.
        loadShader();
        createPipeline(renderPass);
        createBuffers();

        if (!wireframePipeline || !wireframePipelineLayout)
        {
            throw std::runtime_error("PhysicsDebugRenderer: Failed to create pipeline");
        }
        if (!boxVertexBuffer || !sphereVertexBuffer || !capsuleVertexBuffer)
        {
            throw std::runtime_error("PhysicsDebugRenderer: Failed to create geometry buffers");
        }

        initialized = true;
    }

    void PhysicsDebugRenderer::recreate(vk::RenderPass renderPass)
    {
        if (wireframePipeline)
        {
            device.getLogicalDevice().destroyPipeline(wireframePipeline);
            wireframePipeline = nullptr;
        }
        if (wireframePipelineLayout)
        {
            device.getLogicalDevice().destroyPipelineLayout(wireframePipelineLayout);
            wireframePipelineLayout = nullptr;
        }

        createPipeline(renderPass);
    }

    void PhysicsDebugRenderer::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        if (wireframePipeline)
        {
            dev.destroyPipeline(wireframePipeline);
            wireframePipeline = nullptr;
        }
        if (wireframePipelineLayout)
        {
            dev.destroyPipelineLayout(wireframePipelineLayout);
            wireframePipelineLayout = nullptr;
        }

        if (boxVertexBuffer)
        {
            dev.destroyBuffer(boxVertexBuffer);
            dev.freeMemory(boxVertexMemory);
            boxVertexBuffer = nullptr;
            boxVertexMemory = nullptr;
        }
        if (boxIndexBuffer)
        {
            dev.destroyBuffer(boxIndexBuffer);
            dev.freeMemory(boxIndexMemory);
            boxIndexBuffer = nullptr;
            boxIndexMemory = nullptr;
        }

        if (sphereVertexBuffer)
        {
            dev.destroyBuffer(sphereVertexBuffer);
            dev.freeMemory(sphereVertexMemory);
            sphereVertexBuffer = nullptr;
            sphereVertexMemory = nullptr;
        }
        if (sphereIndexBuffer)
        {
            dev.destroyBuffer(sphereIndexBuffer);
            dev.freeMemory(sphereIndexMemory);
            sphereIndexBuffer = nullptr;
            sphereIndexMemory = nullptr;
        }

        if (capsuleVertexBuffer)
        {
            dev.destroyBuffer(capsuleVertexBuffer);
            dev.freeMemory(capsuleVertexMemory);
            capsuleVertexBuffer = nullptr;
            capsuleVertexMemory = nullptr;
        }
        if (capsuleIndexBuffer)
        {
            dev.destroyBuffer(capsuleIndexBuffer);
            dev.freeMemory(capsuleIndexMemory);
            capsuleIndexBuffer = nullptr;
            capsuleIndexMemory = nullptr;
        }

        cleanupMeshCache();

        initialized = false;
    }

    void PhysicsDebugRenderer::cleanupMeshCache()
    {
        auto& dev = device.getLogicalDevice();

        for (auto& [path, meshData] : meshCache)
        {
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
        }
        meshCache.clear();
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

        // First try to load convex decomposition data for accurate debug visualization
        resource::ConvexDecompositionData convexData;
        if (streamHandle->readConvexDecomposition(0, convexData) && convexData.isValid())
        {
            // Build wireframe from convex hulls
            uint32_t vertexOffset = 0;
            for (const auto& hull : convexData.hulls)
            {
                // Add hull vertices
                for (const auto& v : hull.vertices)
                {
                    positions.push_back(v);
                }

                // Convert hull triangles to line indices
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
            // Fallback: use mesh LOD data
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

    void PhysicsDebugRenderer::cleanUpShader()
    {
        if (wireframeShader)
        {
            wireframeShader->cleanUp();
        }
    }

    void PhysicsDebugRenderer::loadShader()
    {
        wireframeShader = std::make_shared<core::Shader>(device);
        wireframeShader->readShader("../../resources/shaders/tools/sphere_wireframe.glsl");
    }

    void PhysicsDebugRenderer::createPipeline(vk::RenderPass renderPass)
    {
        core::WireframePipelineConfig config{
            .device = device.getLogicalDevice(),
            .renderPass = renderPass,
            .extent = swapChain.getSwapchainExtent(),
            .pushConstantSize = sizeof(PhysicsDebugPushConstants),
            .shaderStages = wireframeShader->getShaderStages()
        };

        auto result = core::PipelineUtilities::createWireframePipeline(config);
        wireframePipeline = result.pipeline;
        wireframePipelineLayout = result.pipelineLayout;
    }

    void PhysicsDebugRenderer::createBuffers()
    {
        createBoxBuffers();
        createSphereBuffers();
        createCapsuleBuffers();
    }

    void PhysicsDebugRenderer::createBoxBuffers()
    {
        std::vector<glm::vec3> vertices(kUnitCubeVertices.begin(), kUnitCubeVertices.end());
        std::vector<uint32_t> indices(kUnitCubeLineIndices.begin(), kUnitCubeLineIndices.end());

        boxIndexCount = static_cast<uint32_t>(indices.size());

        vk::DeviceSize vertexBufferSize = sizeof(glm::vec3) * vertices.size();
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, boxVertexBuffer, boxVertexMemory);

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            boxVertexBuffer,
            vertices.data(),
            vertexBufferSize
        );

        vk::DeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, boxIndexBuffer, boxIndexMemory);

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            boxIndexBuffer,
            indices.data(),
            indexBufferSize
        );
    }

    void PhysicsDebugRenderer::createSphereBuffers()
    {
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;

        const float pi = 3.14159265358979323846f;
        const int segments = SPHERE_SEGMENTS;

        // XY plane circle
        int baseIndex = 0;
        for (int i = 0; i < segments; ++i)
        {
            float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
            vertices.push_back(glm::vec3(std::cos(angle), std::sin(angle), 0.0f));
        }
        for (int i = 0; i < segments; ++i)
        {
            indices.push_back(baseIndex + i);
            indices.push_back(baseIndex + (i + 1) % segments);
        }

        // XZ plane circle
        baseIndex = static_cast<int>(vertices.size());
        for (int i = 0; i < segments; ++i)
        {
            float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
            vertices.push_back(glm::vec3(std::cos(angle), 0.0f, std::sin(angle)));
        }
        for (int i = 0; i < segments; ++i)
        {
            indices.push_back(baseIndex + i);
            indices.push_back(baseIndex + (i + 1) % segments);
        }

        // YZ plane circle
        baseIndex = static_cast<int>(vertices.size());
        for (int i = 0; i < segments; ++i)
        {
            float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
            vertices.push_back(glm::vec3(0.0f, std::cos(angle), std::sin(angle)));
        }
        for (int i = 0; i < segments; ++i)
        {
            indices.push_back(baseIndex + i);
            indices.push_back(baseIndex + (i + 1) % segments);
        }

        sphereIndexCount = static_cast<uint32_t>(indices.size());

        vk::DeviceSize vertexBufferSize = sizeof(glm::vec3) * vertices.size();
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, sphereVertexBuffer, sphereVertexMemory);

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            sphereVertexBuffer,
            vertices.data(),
            vertexBufferSize
        );

        vk::DeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, sphereIndexBuffer, sphereIndexMemory);

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            sphereIndexBuffer,
            indices.data(),
            indexBufferSize
        );
    }

    void PhysicsDebugRenderer::createCapsuleBuffers()
    {
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;

        const float pi = 3.14159265358979323846f;
        const int segments = SPHERE_SEGMENTS;
        const float halfHeight = 0.5f;

        // Top hemisphere
        int baseIndex = 0;
        for (int i = 0; i <= segments / 2; ++i)
        {
            float angle = pi * static_cast<float>(i) / static_cast<float>(segments / 2);
            vertices.push_back(glm::vec3(std::cos(angle), std::sin(angle) + halfHeight, 0.0f));
        }
        for (int i = 0; i < segments / 2; ++i)
        {
            indices.push_back(baseIndex + i);
            indices.push_back(baseIndex + i + 1);
        }

        baseIndex = static_cast<int>(vertices.size());
        for (int i = 0; i < segments; ++i)
        {
            float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
            vertices.push_back(glm::vec3(std::cos(angle), halfHeight, std::sin(angle)));
        }
        for (int i = 0; i < segments; ++i)
        {
            indices.push_back(baseIndex + i);
            indices.push_back(baseIndex + (i + 1) % segments);
        }

        baseIndex = static_cast<int>(vertices.size());
        for (int i = 0; i <= segments / 2; ++i)
        {
            float angle = pi * static_cast<float>(i) / static_cast<float>(segments / 2);
            vertices.push_back(glm::vec3(0.0f, std::sin(angle) + halfHeight, std::cos(angle)));
        }
        for (int i = 0; i < segments / 2; ++i)
        {
            indices.push_back(baseIndex + i);
            indices.push_back(baseIndex + i + 1);
        }

        // Bottom hemisphere
        baseIndex = static_cast<int>(vertices.size());
        for (int i = 0; i <= segments / 2; ++i)
        {
            float angle = pi + pi * static_cast<float>(i) / static_cast<float>(segments / 2);
            vertices.push_back(glm::vec3(std::cos(angle), std::sin(angle) - halfHeight, 0.0f));
        }
        for (int i = 0; i < segments / 2; ++i)
        {
            indices.push_back(baseIndex + i);
            indices.push_back(baseIndex + i + 1);
        }

        baseIndex = static_cast<int>(vertices.size());
        for (int i = 0; i < segments; ++i)
        {
            float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
            vertices.push_back(glm::vec3(std::cos(angle), -halfHeight, std::sin(angle)));
        }
        for (int i = 0; i < segments; ++i)
        {
            indices.push_back(baseIndex + i);
            indices.push_back(baseIndex + (i + 1) % segments);
        }

        baseIndex = static_cast<int>(vertices.size());
        for (int i = 0; i <= segments / 2; ++i)
        {
            float angle = pi + pi * static_cast<float>(i) / static_cast<float>(segments / 2);
            vertices.push_back(glm::vec3(0.0f, std::sin(angle) - halfHeight, std::cos(angle)));
        }
        for (int i = 0; i < segments / 2; ++i)
        {
            indices.push_back(baseIndex + i);
            indices.push_back(baseIndex + i + 1);
        }

        // Vertical connecting lines
        baseIndex = static_cast<int>(vertices.size());
        vertices.push_back(glm::vec3(0.0f, halfHeight, 1.0f));
        vertices.push_back(glm::vec3(0.0f, -halfHeight, 1.0f));
        vertices.push_back(glm::vec3(0.0f, halfHeight, -1.0f));
        vertices.push_back(glm::vec3(0.0f, -halfHeight, -1.0f));
        vertices.push_back(glm::vec3(1.0f, halfHeight, 0.0f));
        vertices.push_back(glm::vec3(1.0f, -halfHeight, 0.0f));
        vertices.push_back(glm::vec3(-1.0f, halfHeight, 0.0f));
        vertices.push_back(glm::vec3(-1.0f, -halfHeight, 0.0f));

        indices.push_back(baseIndex + 0);
        indices.push_back(baseIndex + 1);
        indices.push_back(baseIndex + 2);
        indices.push_back(baseIndex + 3);
        indices.push_back(baseIndex + 4);
        indices.push_back(baseIndex + 5);
        indices.push_back(baseIndex + 6);
        indices.push_back(baseIndex + 7);

        capsuleIndexCount = static_cast<uint32_t>(indices.size());

        vk::DeviceSize vertexBufferSize = sizeof(glm::vec3) * vertices.size();
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, capsuleVertexBuffer, capsuleVertexMemory);

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            capsuleVertexBuffer,
            vertices.data(),
            vertexBufferSize
        );

        vk::DeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, capsuleIndexBuffer, capsuleIndexMemory);

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            capsuleIndexBuffer,
            indices.data(),
            indexBufferSize
        );
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
            }
        }
    }
}
