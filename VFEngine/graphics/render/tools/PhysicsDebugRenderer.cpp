#include "PhysicsDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"
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
        : DebugRendererBase{device, swapChain}
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
        destroyPipelineAndLayout(wireframePipeline, wireframePipelineLayout);
        createPipeline(renderPass);
    }

    void PhysicsDebugRenderer::cleanUp()
    {
        destroyPipelineAndLayout(wireframePipeline, wireframePipelineLayout);
        destroyBufferPair(boxVertexBuffer, boxVertexMemory);
        destroyBufferPair(boxIndexBuffer, boxIndexMemory);
        destroyBufferPair(sphereVertexBuffer, sphereVertexMemory);
        destroyBufferPair(sphereIndexBuffer, sphereIndexMemory);
        destroyBufferPair(capsuleVertexBuffer, capsuleVertexMemory);
        destroyBufferPair(capsuleIndexBuffer, capsuleIndexMemory);
        cleanupMeshCache();
        cleanupHeightFieldCache();
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

    void PhysicsDebugRenderer::cleanupHeightFieldCache()
    {
        auto& dev = device.getLogicalDevice();

        for (auto& [key, entry] : heightfieldCache)
        {
            if (entry.buffers.vertexBuffer)
            {
                dev.destroyBuffer(entry.buffers.vertexBuffer);
                dev.freeMemory(entry.buffers.vertexMemory);
            }
            if (entry.buffers.indexBuffer)
            {
                dev.destroyBuffer(entry.buffers.indexBuffer);
                dev.freeMemory(entry.buffers.indexMemory);
            }
        }
        heightfieldCache.clear();
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
        const float halfHeight = 1.0f;

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
}
