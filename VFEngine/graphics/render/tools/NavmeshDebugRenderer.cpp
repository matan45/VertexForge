#include "NavmeshDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"
#include <unordered_set>

namespace render::mesh
{
    NavmeshDebugRenderer::NavmeshDebugRenderer(core::Device& device, core::SwapChain& swapChain)
        : DebugRendererBase{device, swapChain}
    {
    }

    NavmeshDebugRenderer::~NavmeshDebugRenderer() = default;

    void NavmeshDebugRenderer::init(vk::RenderPass renderPass)
    {
        loadShader();
        createPipeline(renderPass);
        initialized = true;
    }

    void NavmeshDebugRenderer::recreate(vk::RenderPass renderPass)
    {
        destroyPipelineAndLayout(wireframePipeline, wireframePipelineLayout);
        createPipeline(renderPass);
    }

    void NavmeshDebugRenderer::cleanUp()
    {
        destroyPipelineAndLayout(wireframePipeline, wireframePipelineLayout);
        destroyMeshBuffers();
        initialized = false;
    }

    void NavmeshDebugRenderer::cleanUpShader()
    {
        if (wireframeShader)
        {
            wireframeShader->cleanUp();
        }
    }

    void NavmeshDebugRenderer::loadShader()
    {
        wireframeShader = std::make_shared<core::Shader>(device);
        wireframeShader->readShader("../../resources/shaders/tools/sphere_wireframe.glsl");
    }

    void NavmeshDebugRenderer::createPipeline(vk::RenderPass renderPass)
    {
        core::WireframePipelineConfig config{
            .device = device.getLogicalDevice(),
            .renderPass = renderPass,
            .extent = swapChain.getSwapchainExtent(),
            .pushConstantSize = sizeof(NavmeshDebugPushConstants),
            .shaderStages = wireframeShader->getShaderStages()
        };

        auto result = core::PipelineUtilities::createWireframePipeline(config);
        wireframePipeline = result.pipeline;
        wireframePipelineLayout = result.pipelineLayout;
    }

    void NavmeshDebugRenderer::destroyMeshBuffers()
    {
        destroyBufferPair(vertexBuffer, vertexMemory);
        destroyBufferPair(indexBuffer, indexMemory);
        lineIndexCount = 0;
        hasData = false;
    }

    void NavmeshDebugRenderer::updateMesh(const std::vector<glm::vec3>& vertices,
                                           const std::vector<uint32_t>& triangleIndices)
    {
        destroyMeshBuffers();

        if (vertices.empty() || triangleIndices.empty())
        {
            return;
        }

        // Convert triangle indices to line indices (deduplicated edges)
        struct EdgeHash
        {
            size_t operator()(const std::pair<uint32_t, uint32_t>& e) const
            {
                return std::hash<uint64_t>()(static_cast<uint64_t>(e.first) << 32 | e.second);
            }
        };

        std::unordered_set<std::pair<uint32_t, uint32_t>, EdgeHash> edgeSet;
        std::vector<uint32_t> lineIndices;

        for (size_t i = 0; i + 2 < triangleIndices.size(); i += 3)
        {
            uint32_t a = triangleIndices[i];
            uint32_t b = triangleIndices[i + 1];
            uint32_t c = triangleIndices[i + 2];

            auto addEdge = [&](uint32_t v0, uint32_t v1)
            {
                auto edge = std::make_pair(std::min(v0, v1), std::max(v0, v1));
                if (edgeSet.insert(edge).second)
                {
                    lineIndices.push_back(v0);
                    lineIndices.push_back(v1);
                }
            };

            addEdge(a, b);
            addEdge(b, c);
            addEdge(c, a);
        }

        if (lineIndices.empty())
        {
            return;
        }

        // Create vertex buffer
        vk::DeviceSize vertexBufferSize = static_cast<vk::DeviceSize>(vertices.size() * sizeof(glm::vec3));
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, vertexBuffer, vertexMemory);

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            vertexBuffer,
            vertices.data(),
            vertexBufferSize
        );

        // Create index buffer
        vk::DeviceSize indexBufferSize = static_cast<vk::DeviceSize>(lineIndices.size() * sizeof(uint32_t));
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, indexBuffer, indexMemory);

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            indexBuffer,
            lineIndices.data(),
            indexBufferSize
        );

        lineIndexCount = static_cast<uint32_t>(lineIndices.size());
        hasData = true;
    }

    void NavmeshDebugRenderer::clearMesh()
    {
        destroyMeshBuffers();
    }

    void NavmeshDebugRenderer::render(const vk::CommandBuffer& commandBuffer,
                                       const glm::mat4& view,
                                       const glm::mat4& projection) const
    {
        if (!initialized || !hasData || !wireframePipeline || !vertexBuffer || lineIndexCount == 0)
        {
            return;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, wireframePipeline);
        commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);

        vk::Buffer vertexBuffers[] = {vertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

        NavmeshDebugPushConstants pushConstants{};
        pushConstants.mvp = projection * view; // World-space vertices, identity model
        pushConstants.color = glm::vec4(0.0f, 0.8f, 0.4f, 1.0f); // Green tint

        commandBuffer.pushConstants(wireframePipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, sizeof(NavmeshDebugPushConstants), &pushConstants);

        commandBuffer.drawIndexed(lineIndexCount, 1, 0, 0, 0);
    }
}
