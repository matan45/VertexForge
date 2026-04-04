#include "NavmeshDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"

namespace render::mesh
{
    NavmeshDebugRenderer::NavmeshDebugRenderer(core::Device& device, core::SwapChain& swapChain)
        : DebugRendererBase{device, swapChain}
    {
    }

    NavmeshDebugRenderer::~NavmeshDebugRenderer() = default;

    void NavmeshDebugRenderer::init(vk::Format colorFormat, vk::Format depthFormat)
    {
        loadShader();
        createPipeline(colorFormat, depthFormat);
        initialized = true;
    }

    void NavmeshDebugRenderer::recreate(vk::Format colorFormat, vk::Format depthFormat)
    {
        destroyPipelineAndLayout(wireframePipeline, wireframePipelineLayout);
        createPipeline(colorFormat, depthFormat);
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

    void NavmeshDebugRenderer::createPipeline(vk::Format colorFormat, vk::Format depthFormat)
    {
        vk::VertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(glm::vec3);
        binding.inputRate = vk::VertexInputRate::eVertex;

        vk::VertexInputAttributeDescription attribute{};
        attribute.binding = 0;
        attribute.location = 0;
        attribute.format = vk::Format::eR32G32B32Sfloat;
        attribute.offset = 0;

        core::GraphicsPipelineConfig config{};
        config.device = device.getLogicalDevice();
        config.colorAttachmentFormats = {colorFormat};
        config.depthAttachmentFormat = depthFormat;
        config.extent = swapChain.getSwapchainExtent();
        config.shaderStages = wireframeShader->getShaderStages();
        config.vertexBindings = {binding};
        config.vertexAttributes = {attribute};
        config.topology = vk::PrimitiveTopology::eTriangleList;
        config.pushConstantSize = sizeof(NavmeshDebugPushConstants);
        config.pushConstantStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        config.cullMode = vk::CullModeFlagBits::eNone;
        config.depthTestEnable = true;
        config.depthCompareOp = vk::CompareOp::eLessOrEqual;
        config.depthWriteEnable = true;
        config.depthBiasEnable = true;
        config.depthBiasConstantFactor = -2.0f;
        config.depthBiasSlopeFactor = -2.0f;
        config.blendEnable = true;

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        wireframePipeline = result.pipeline;
        wireframePipelineLayout = result.pipelineLayout;
    }

    void NavmeshDebugRenderer::destroyMeshBuffers()
    {
        destroyBufferPair(vertexBuffer, vertexAllocation);
        destroyBufferPair(indexBuffer, indexAllocation);
        indexCount = 0;
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

        vk::DeviceSize vertexBufferSize = static_cast<vk::DeviceSize>(vertices.size() * sizeof(glm::vec3));
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(vertexRequest, vertexBuffer, vertexAllocation, device.getMemoryManager());

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            vertexBuffer,
            vertices.data(),
            vertexBufferSize
        );

        vk::DeviceSize indexBufferSize = static_cast<vk::DeviceSize>(triangleIndices.size() * sizeof(uint32_t));
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::BufferUtilities::createBuffer(indexRequest, indexBuffer, indexAllocation, device.getMemoryManager());

        core::BufferUtilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            indexBuffer,
            triangleIndices.data(),
            indexBufferSize
        );

        indexCount = static_cast<uint32_t>(triangleIndices.size());
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
        if (!initialized || !hasData || !wireframePipeline || !vertexBuffer || indexCount == 0)
        {
            return;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, wireframePipeline);
        commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);

        vk::Buffer vertexBuffers[] = {vertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

        NavmeshDebugPushConstants pushConstants{};
        pushConstants.mvp = projection * view;
        pushConstants.color = glm::vec4(1.0f, 0.0f, 0.0f, 0.4f);

        commandBuffer.pushConstants(wireframePipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, sizeof(NavmeshDebugPushConstants), &pushConstants);

        commandBuffer.drawIndexed(indexCount, 1, 0, 0, 0);
    }
}
