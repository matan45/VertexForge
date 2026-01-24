#include "ClusterDebugRenderer.hpp"
#include "AABBDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"
#include <unordered_set>

namespace render::mesh
{
    ClusterDebugRenderer::ClusterDebugRenderer(core::Device& device, core::SwapChain& swapChain)
        : device{device}, swapChain{swapChain}
    {
    }

    ClusterDebugRenderer::~ClusterDebugRenderer() = default;

    void ClusterDebugRenderer::init(vk::RenderPass renderPass)
    {
        loadShader();
        createPipeline(renderPass);
        createBuffers();
        initialized = true;
    }

    void ClusterDebugRenderer::recreate(vk::RenderPass renderPass)
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

    void ClusterDebugRenderer::cleanUp()
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

        if (vertexBuffer)
        {
            dev.destroyBuffer(vertexBuffer);
            dev.freeMemory(vertexBufferMemory);
            vertexBuffer = nullptr;
            vertexBufferMemory = nullptr;
        }
        if (indexBuffer)
        {
            dev.destroyBuffer(indexBuffer);
            dev.freeMemory(indexBufferMemory);
            indexBuffer = nullptr;
            indexBufferMemory = nullptr;
        }

        initialized = false;
    }

    void ClusterDebugRenderer::cleanUpShader()
    {
        if (wireframeShader)
        {
            wireframeShader->cleanUp();
        }
    }

    void ClusterDebugRenderer::loadShader()
    {
        wireframeShader = std::make_shared<core::Shader>(device);
        wireframeShader->readShader("../../resources/shaders/tools/aabbWireframe.glsl");
    }

    void ClusterDebugRenderer::createPipeline(vk::RenderPass renderPass)
    {
        core::WireframePipelineConfig config{
            .device = device.getLogicalDevice(),
            .renderPass = renderPass,
            .extent = swapChain.getSwapchainExtent(),
            .pushConstantSize = sizeof(AABBPushConstants),
            .shaderStages = wireframeShader->getShaderStages()
        };

        auto result = core::PipelineUtilities::createWireframePipeline(config);
        wireframePipeline = result.pipeline;
        wireframePipelineLayout = result.pipelineLayout;
    }

    void ClusterDebugRenderer::createBuffers()
    {
        // Create vertex buffer using shared unit cube vertices
        vk::DeviceSize vertexBufferSize = sizeof(glm::vec3) * kUnitCubeVertices.size();
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, vertexBuffer, vertexBufferMemory);

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            vertexBuffer,
            kUnitCubeVertices.data(),
            vertexBufferSize
        );

        // Create index buffer using shared unit cube line indices
        vk::DeviceSize indexBufferSize = sizeof(uint32_t) * kUnitCubeLineIndices.size();
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, indexBuffer, indexBufferMemory);

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            indexBuffer,
            kUnitCubeLineIndices.data(),
            indexBufferSize
        );
    }

    void ClusterDebugRenderer::render(const vk::CommandBuffer& commandBuffer,
                                       const ClusterDebugRenderData& data,
                                       const glm::mat4& view,
                                       const glm::mat4& projection) const
    {
        if (!initialized || !wireframePipeline || !vertexBuffer || !visible)
        {
            return;
        }

        // Skip if no highlighted clusters and not showing all
        if (data.highlightedClusterIndices.empty() && !data.showAllClusters)
        {
            return;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, wireframePipeline);

        vk::Buffer vertexBuffers[] = {vertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
        commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);

        const glm::mat4 viewProjection = projection * view;

        // Colors for visualization
        constexpr glm::vec4 highlightColor{1.0f, 1.0f, 0.0f, 1.0f};  // Yellow for highlighted
        constexpr glm::vec4 normalColor{0.2f, 0.6f, 1.0f, 0.4f};     // Cyan with transparency for all

        // Build set of highlighted indices for fast lookup
        std::unordered_set<uint32_t> highlightedSet(
            data.highlightedClusterIndices.begin(),
            data.highlightedClusterIndices.end()
        );

        if (data.showAllClusters)
        {
            // Render all clusters
            for (size_t i = 0; i < data.clusterAABBs.size(); ++i)
            {
                bool isHighlighted = highlightedSet.count(static_cast<uint32_t>(i)) > 0;
                const glm::vec4& color = isHighlighted ? highlightColor : normalColor;
                renderClusterAABB(commandBuffer, data.clusterAABBs[i], data.invViewMatrix, viewProjection, color);
            }
        }
        else
        {
            // Render only highlighted clusters
            for (uint32_t idx : data.highlightedClusterIndices)
            {
                if (idx < data.clusterAABBs.size())
                {
                    renderClusterAABB(commandBuffer, data.clusterAABBs[idx], data.invViewMatrix, viewProjection, highlightColor);
                }
            }
        }
    }

    void ClusterDebugRenderer::renderClusterAABB(const vk::CommandBuffer& commandBuffer,
                                                  const lighting::GPUClusterAABB& aabb,
                                                  const glm::mat4& invView,
                                                  const glm::mat4& viewProjection,
                                                  const glm::vec4& color) const
    {
        // Cluster AABBs are in view-space, transform to world-space for rendering
        glm::vec3 minView = glm::vec3(aabb.minPoint);
        glm::vec3 maxView = glm::vec3(aabb.maxPoint);

        // Compute center and extents in view-space
        glm::vec3 center = (minView + maxView) * 0.5f;
        glm::vec3 extents = (maxView - minView) * 0.5f;

        // Transform center to world-space
        glm::vec4 centerWorld = invView * glm::vec4(center, 1.0f);

        // Create model matrix: translate to world center, then scale by extents
        // Note: We need to account for view rotation when scaling
        glm::mat3 viewRotation = glm::mat3(invView);
        glm::mat4 aabbModel = glm::mat4(1.0f);

        // Set translation
        aabbModel[3] = centerWorld;

        // Apply rotation and scale
        for (int i = 0; i < 3; ++i)
        {
            aabbModel[i] = glm::vec4(viewRotation[i] * extents[i], 0.0f);
        }

        glm::mat4 mvp = viewProjection * aabbModel;

        AABBPushConstants pushConstants{};
        pushConstants.mvp = mvp;
        pushConstants.color = color;

        commandBuffer.pushConstants(wireframePipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, sizeof(AABBPushConstants), &pushConstants);

        commandBuffer.drawIndexed(static_cast<uint32_t>(kUnitCubeLineIndices.size()), 1, 0, 0, 0);
    }
}
