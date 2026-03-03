#include "ImmediateDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"

namespace render::mesh
{
    ImmediateDebugRenderer::ImmediateDebugRenderer(core::Device& device, core::SwapChain& swapChain)
        : DebugRendererBase{device, swapChain}
    {
    }

    ImmediateDebugRenderer::~ImmediateDebugRenderer() = default;

    void ImmediateDebugRenderer::init(vk::RenderPass renderPass)
    {
        loadShader();
        createPipeline(renderPass);
        initialized = true;
    }

    void ImmediateDebugRenderer::recreate(vk::RenderPass renderPass)
    {
        destroyPipelineAndLayout(pipeline, pipelineLayout);
        createPipeline(renderPass);
    }

    void ImmediateDebugRenderer::cleanUp()
    {
        destroyPipelineAndLayout(pipeline, pipelineLayout);
        destroyVertexBuffer();
        initialized = false;
    }

    void ImmediateDebugRenderer::cleanUpShader()
    {
        if (shader)
        {
            shader->cleanUp();
        }
    }

    void ImmediateDebugRenderer::loadShader()
    {
        shader = std::make_shared<core::Shader>(device);
        shader->readShader("../../resources/shaders/tools/immediate_debug.glsl");
    }

    void ImmediateDebugRenderer::createPipeline(vk::RenderPass renderPass)
    {
        // Vertex layout: vec3 position + vec4 color
        vk::VertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(DebugLineVertex);
        binding.inputRate = vk::VertexInputRate::eVertex;

        std::array<vk::VertexInputAttributeDescription, 2> attributes{};
        // Position
        attributes[0].binding = 0;
        attributes[0].location = 0;
        attributes[0].format = vk::Format::eR32G32B32Sfloat;
        attributes[0].offset = offsetof(DebugLineVertex, position);
        // Color
        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = vk::Format::eR32G32B32A32Sfloat;
        attributes[1].offset = offsetof(DebugLineVertex, color);

        core::GraphicsPipelineConfig config{};
        config.device = device.getLogicalDevice();
        config.renderPass = renderPass;
        config.extent = swapChain.getSwapchainExtent();
        config.shaderStages = shader->getShaderStages();
        config.vertexBindings = {binding};
        config.vertexAttributes = {attributes[0], attributes[1]};
        config.topology = vk::PrimitiveTopology::eLineList;
        config.pushConstantSize = sizeof(ImmediateDebugPushConstants);
        config.pushConstantStages = vk::ShaderStageFlagBits::eVertex;
        config.cullMode = vk::CullModeFlagBits::eNone;
        config.depthTestEnable = true;
        config.depthCompareOp = vk::CompareOp::eLessOrEqual;
        config.depthWriteEnable = false;
        config.blendEnable = true;

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        pipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
    }

    void ImmediateDebugRenderer::destroyVertexBuffer()
    {
        destroyBufferPair(vertexBuffer, vertexMemory);
        currentVertexCount = 0;
        currentBufferSize = 0;
    }

    void ImmediateDebugRenderer::updateDrawList(ImmediateDebugDrawList drawList)
    {
        destroyVertexBuffer();

        if (drawList.empty())
        {
            return;
        }

        currentVertexCount = static_cast<uint32_t>(drawList.vertexCount());
        vk::DeviceSize bufferSize = static_cast<vk::DeviceSize>(currentVertexCount * sizeof(DebugLineVertex));

        core::BufferInfoRequest request(device.getLogicalDevice(), device.getPhysicalDevice());
        request.size = bufferSize;
        request.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(request, vertexBuffer, vertexMemory);

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            vertexBuffer,
            drawList.lineVertices.data(),
            bufferSize
        );

        currentBufferSize = bufferSize;
    }

    void ImmediateDebugRenderer::render(const vk::CommandBuffer& commandBuffer,
                                         const glm::mat4& view,
                                         const glm::mat4& projection) const
    {
        if (!initialized || currentVertexCount == 0 || !pipeline || !vertexBuffer)
        {
            return;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

        vk::Buffer vertexBuffers[] = {vertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

        ImmediateDebugPushConstants pushConstants{};
        pushConstants.viewProj = projection * view;

        commandBuffer.pushConstants(pipelineLayout,
            vk::ShaderStageFlagBits::eVertex,
            0, sizeof(ImmediateDebugPushConstants), &pushConstants);

        commandBuffer.draw(currentVertexCount, 1, 0, 0);
    }
}
