#include "FrustumDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"

namespace render::mesh
{
    FrustumDebugRenderer::FrustumDebugRenderer(core::Device& device, core::SwapChain& swapChain)
        : DebugRendererBase{device, swapChain}
    {
    }

    FrustumDebugRenderer::~FrustumDebugRenderer() = default;

    void FrustumDebugRenderer::init(vk::Format colorFormat, vk::Format depthFormat)
    {
        loadShader();
        createPipeline(colorFormat, depthFormat);
        createBuffers();
        initialized = true;
    }

    void FrustumDebugRenderer::recreate(vk::Format colorFormat, vk::Format depthFormat)
    {
        destroyPipelineAndLayout(wireframePipeline, wireframePipelineLayout);
        createPipeline(colorFormat, depthFormat);
    }

    void FrustumDebugRenderer::cleanUp()
    {
        destroyPipelineAndLayout(wireframePipeline, wireframePipelineLayout);
        destroyBufferPair(vertexBuffer, vertexBufferAllocation);
        destroyBufferPair(indexBuffer, indexBufferAllocation);
        initialized = false;
    }

    void FrustumDebugRenderer::cleanUpShader()
    {
        if (wireframeShader)
        {
            wireframeShader->cleanUp();
        }
    }

    void FrustumDebugRenderer::loadShader()
    {
        wireframeShader = std::make_shared<core::Shader>(device);
        wireframeShader->readShader("../../resources/shaders/tools/wireframe.glsl");
    }

    void FrustumDebugRenderer::createPipeline(vk::Format colorFormat, vk::Format depthFormat)
    {
        core::WireframePipelineConfig config{
            .device = device.getLogicalDevice(),
            .renderPass = nullptr,
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

    void FrustumDebugRenderer::createBuffers()
    {
        // Create static vertex buffer (device local for best performance)
        vk::DeviceSize vertexBufferSize = sizeof(ndcCorners);
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
            ndcCorners.data(),
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

    void FrustumDebugRenderer::render(const vk::CommandBuffer& commandBuffer,
                                       const std::vector<CameraFrustumRenderData>& cameraDrawList,
                                       const glm::mat4& editorView,
                                       const glm::mat4& editorProjection) const
    {
        if (!initialized || !wireframePipeline || !vertexBuffer)
        {
            return;
        }

        bool hasFrustumToRender = false;
        for (const auto& camera : cameraDrawList)
        {
            if (camera.showFrustum)
            {
                hasFrustumToRender = true;
                break;
            }
        }

        if (!hasFrustumToRender)
        {
            return;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, wireframePipeline);
        commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);
        
        vk::Buffer vertexBuffers[] = {vertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
        
        glm::mat4 editorViewProj = editorProjection * editorView;

        for (const auto& camera : cameraDrawList)
        {
            if (!camera.showFrustum)
            {
                continue;
            }
            
            glm::mat4 cameraViewFromWorld = glm::inverse(camera.worldMatrix);
            glm::mat4 cameraViewProj = camera.projectionMatrix * cameraViewFromWorld;
            glm::mat4 cameraInverseViewProj = glm::inverse(cameraViewProj);
            
            FrustumPushConstants pushConstants{};
            pushConstants.viewProj = editorViewProj;          
            pushConstants.inverseViewProj = cameraInverseViewProj; 
            pushConstants.color = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);

            commandBuffer.pushConstants(wireframePipelineLayout,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0, sizeof(FrustumPushConstants), &pushConstants);
            
            commandBuffer.drawIndexed(24, 1, 0, 0, 0);
        }
    }
}
