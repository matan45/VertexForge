#include "GridRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"

namespace render::mesh
{
    GridRenderer::GridRenderer(core::Device& device, core::SwapChain& swapChain)
        : DebugRendererBase{device, swapChain}
    {
    }

    GridRenderer::~GridRenderer() = default;

    void GridRenderer::init(vk::RenderPass renderPass)
    {
        loadShader();
        createPipeline(renderPass);
        createBuffers();
        initialized = true;
    }

    void GridRenderer::recreate(vk::RenderPass renderPass)
    {
        destroyPipelineAndLayout(gridPipeline, gridPipelineLayout);
        createPipeline(renderPass);
    }

    void GridRenderer::cleanUp()
    {
        destroyPipelineAndLayout(gridPipeline, gridPipelineLayout);
        destroyBufferPair(vertexBuffer, vertexBufferAllocation);
        destroyBufferPair(indexBuffer, indexBufferAllocation);
        initialized = false;
    }

    void GridRenderer::cleanUpShader()
    {
        if (gridShader)
        {
            gridShader->cleanUp();
        }
    }

    void GridRenderer::loadShader()
    {
        gridShader = std::make_shared<core::Shader>(device);
        gridShader->readShader("../../resources/shaders/tools/gridOverlay.glsl");
    }

    void GridRenderer::createPipeline(vk::RenderPass renderPass)
    {
        core::WireframePipelineConfig config{
            .device = device.getLogicalDevice(),
            .renderPass = renderPass,
            .extent = swapChain.getSwapchainExtent(),
            .pushConstantSize = sizeof(GridPushConstants),
            .shaderStages = gridShader->getShaderStages(),
            .enableBlending = true
        };

        auto result = core::PipelineUtilities::createWireframePipeline(config);
        gridPipeline = result.pipeline;
        gridPipelineLayout = result.pipelineLayout;
    }

    void GridRenderer::createBuffers()
    {
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;

        int numCells = static_cast<int>(gridSize / cellSize);
        float halfSize = gridSize;

        uint32_t idx = 0;

        // Generate lines parallel to X-axis (running along X, at different Z positions)
        for (int i = -numCells; i <= numCells; ++i)
        {
            float z = i * cellSize;
            vertices.push_back({-halfSize, 0.0f, z});
            vertices.push_back({halfSize, 0.0f, z});
            indices.push_back(idx++);
            indices.push_back(idx++);
        }

        // Generate lines parallel to Z-axis (running along Z, at different X positions)
        for (int i = -numCells; i <= numCells; ++i)
        {
            float x = i * cellSize;
            vertices.push_back({x, 0.0f, -halfSize});
            vertices.push_back({x, 0.0f, halfSize});
            indices.push_back(idx++);
            indices.push_back(idx++);
        }

        indexCount = static_cast<uint32_t>(indices.size());
        
        vk::DeviceSize vertexBufferSize = sizeof(glm::vec3) * vertices.size();
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, vertexBuffer, vertexBufferAllocation, device.getMemoryManager());

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            vertexBuffer,
            vertices.data(),
            vertexBufferSize
        );
        
        vk::DeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, indexBuffer, indexBufferAllocation, device.getMemoryManager());

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            indexBuffer,
            indices.data(),
            indexBufferSize
        );
    }

    void GridRenderer::render(const vk::CommandBuffer& commandBuffer,
                              const glm::mat4& view,
                              const glm::mat4& projection) const
    {
        if (!initialized || !gridPipeline || !vertexBuffer || !visible)
        {
            return;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, gridPipeline);

        vk::Buffer vertexBuffers[] = {vertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
        commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);

        // Calculate view-projection matrix
        glm::mat4 viewProj = projection * view;

        GridPushConstants pushConstants{};
        pushConstants.viewProj = viewProj;
        pushConstants.gridColor = gridColor;
        pushConstants.axisColorX = axisColorX;
        pushConstants.axisColorZ = axisColorZ;
        pushConstants.gridParams = glm::vec4(gridSize, cellSize, fadeStart, fadeEnd);

        commandBuffer.pushConstants(gridPipelineLayout,
                                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                                    0, sizeof(GridPushConstants), &pushConstants);

        commandBuffer.drawIndexed(indexCount, 1, 0, 0, 0);
    }
}
