#include "UICanvasDebugRenderer.hpp"
#include "FrustumDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"

namespace render::mesh
{
    UICanvasDebugRenderer::UICanvasDebugRenderer(core::Device& device, core::SwapChain& swapChain)
        : DebugRendererBase{device, swapChain}
    {
    }

    UICanvasDebugRenderer::~UICanvasDebugRenderer() = default;

    void UICanvasDebugRenderer::init(vk::Format colorFormat, vk::Format depthFormat)
    {
        loadShader();
        createPipeline(colorFormat, depthFormat);
        createBuffers();
        initialized = true;
    }

    void UICanvasDebugRenderer::recreate(vk::Format colorFormat, vk::Format depthFormat)
    {
        destroyPipelineAndLayout(wireframePipeline, wireframePipelineLayout);
        createPipeline(colorFormat, depthFormat);
    }

    void UICanvasDebugRenderer::cleanUp()
    {
        destroyPipelineAndLayout(wireframePipeline, wireframePipelineLayout);
        destroyBufferPair(vertexBuffer, vertexBufferAllocation);
        destroyBufferPair(indexBuffer, indexBufferAllocation);
        initialized = false;
    }

    void UICanvasDebugRenderer::cleanUpShader()
    {
        if (wireframeShader)
        {
            wireframeShader->cleanUp();
        }
    }

    void UICanvasDebugRenderer::loadShader()
    {
        wireframeShader = std::make_shared<core::Shader>(device);
        wireframeShader->readShader("../../resources/shaders/tools/wireframe.glsl");
    }

    void UICanvasDebugRenderer::createPipeline(vk::Format colorFormat, vk::Format depthFormat)
    {
        core::WireframePipelineConfig config{
            .device = device.getLogicalDevice(),
            .extent = swapChain.getSwapchainExtent(),
            .colorAttachmentFormats = {colorFormat},
            .depthAttachmentFormat = depthFormat,
            .pushConstantSize = sizeof(FrustumPushConstants),
            .shaderStages = wireframeShader->getShaderStages()
        };

        auto result = core::PipelineUtilities::createWireframePipeline(config);
        wireframePipeline = result.pipeline;
        wireframePipelineLayout = result.pipelineLayout;
    }

    void UICanvasDebugRenderer::createBuffers()
    {
        // Create static vertex buffer
        vk::DeviceSize vertexBufferSize = sizeof(vertices);
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

        // Create index buffer
        vk::DeviceSize indexBufferSize = sizeof(indices);
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

    void UICanvasDebugRenderer::render(const vk::CommandBuffer& commandBuffer,
                                        const std::vector<UICanvasOutlineRenderData>& canvasDrawList,
                                        const glm::mat4& editorView,
                                        const glm::mat4& editorProjection) const
    {
        if (!initialized || !wireframePipeline || !vertexBuffer || canvasDrawList.empty())
        {
            return;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, wireframePipeline);
        commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);

        vk::Buffer vertexBuffers[] = {vertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

        glm::mat4 editorViewProj = editorProjection * editorView;

        for (const auto& canvas : canvasDrawList)
        {
            FrustumPushConstants pushConstants{};
            pushConstants.viewProj = editorViewProj;
            pushConstants.inverseViewProj = canvas.modelMatrix;

            // Draw outline (white) - first 8 indices (4 edges)
            pushConstants.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            commandBuffer.pushConstants(wireframePipelineLayout,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0, sizeof(FrustumPushConstants), &pushConstants);
            commandBuffer.drawIndexed(8, 1, 0, 0, 0);

            // Draw corner anchors (blue) - next 16 indices (8 L-marks)
            pushConstants.color = glm::vec4(0.2f, 0.5f, 1.0f, 1.0f);
            commandBuffer.pushConstants(wireframePipelineLayout,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0, sizeof(FrustumPushConstants), &pushConstants);
            commandBuffer.drawIndexed(16, 1, 8, 0, 0);
        }
    }
}
