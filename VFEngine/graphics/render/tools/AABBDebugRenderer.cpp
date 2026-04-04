#include "AABBDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"

namespace render::mesh
{
    AABBDebugRenderer::AABBDebugRenderer(core::Device& device, core::SwapChain& swapChain)
        : DebugRendererBase{device, swapChain}
    {
    }

    AABBDebugRenderer::~AABBDebugRenderer() = default;

    void AABBDebugRenderer::init(vk::Format colorFormat, vk::Format depthFormat)
    {
        loadShader();
        createPipeline(colorFormat, depthFormat);
        createBuffers();
        initialized = true;
    }

    void AABBDebugRenderer::recreate(vk::Format colorFormat, vk::Format depthFormat)
    {
        destroyPipelineAndLayout(wireframePipeline, wireframePipelineLayout);
        createPipeline(colorFormat, depthFormat);
    }

    void AABBDebugRenderer::cleanUp()
    {
        destroyPipelineAndLayout(wireframePipeline, wireframePipelineLayout);
        destroyBufferPair(vertexBuffer, vertexBufferAllocation);
        destroyBufferPair(indexBuffer, indexBufferAllocation);
        initialized = false;
    }

    void AABBDebugRenderer::cleanUpShader()
    {
        if (wireframeShader)
        {
            wireframeShader->cleanUp();
        }
    }

    void AABBDebugRenderer::loadShader()
    {
        wireframeShader = std::make_shared<core::Shader>(device);
        wireframeShader->readShader("../../resources/shaders/tools/aabbWireframe.glsl");
    }

    void AABBDebugRenderer::createPipeline(vk::Format colorFormat, vk::Format depthFormat)
    {
        core::WireframePipelineConfig config{
            .device = device.getLogicalDevice(),
            .extent = swapChain.getSwapchainExtent(),
            .colorAttachmentFormats = {colorFormat},
            .depthAttachmentFormat = depthFormat,
            .pushConstantSize = sizeof(AABBPushConstants),
            .shaderStages = wireframeShader->getShaderStages()
        };

        auto result = core::PipelineUtilities::createWireframePipeline(config);
        wireframePipeline = result.pipeline;
        wireframePipelineLayout = result.pipelineLayout;
    }

    void AABBDebugRenderer::createBuffers()
    {
        // Create vertex buffer
        vk::DeviceSize vertexBufferSize = sizeof(glm::vec3) * kUnitCubeVertices.size();
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
            kUnitCubeVertices.data(),
            vertexBufferSize
        );

        // Create index buffer
        vk::DeviceSize indexBufferSize = sizeof(uint32_t) * kUnitCubeLineIndices.size();
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
            kUnitCubeLineIndices.data(),
            indexBufferSize
        );
    }

    void AABBDebugRenderer::render(const vk::CommandBuffer& commandBuffer,
                                    const std::vector<MeshRenderData>& meshDrawList,
                                    const glm::mat4& view,
                                    const glm::mat4& projection,
                                    const std::function<const MeshGPUData*(const std::string&)>& getMeshFunc) const
    {
        if (!initialized || !wireframePipeline || !vertexBuffer)
        {
            return;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, wireframePipeline);

        vk::Buffer vertexBuffers[] = {vertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
        commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);

        const glm::mat4 viewProjection = projection * view;
        constexpr glm::vec4 greenColor{0.0f, 1.0f, 0.0f, 1.0f};
        constexpr glm::vec4 yellowColor{1.0f, 1.0f, 0.0f, 1.0f};

        for (const auto& meshData : meshDrawList)
        {
            if (!meshData.showBoundingBox)
            {
                continue;
            }

            const MeshGPUData* gpuData = getMeshFunc(meshData.meshPath);
            if (!gpuData)
            {
                continue;
            }

            renderAABB(commandBuffer, gpuData->boundingBox, meshData.modelMatrix, viewProjection, greenColor);

            if (gpuData->subMeshes.size() > 1)
            {
                for (const auto& subMesh : gpuData->subMeshes)
                {
                    renderAABB(commandBuffer, subMesh.boundingBox, meshData.modelMatrix, viewProjection, yellowColor);
                }
            }
        }
    }

    void AABBDebugRenderer::renderAABB(const vk::CommandBuffer& commandBuffer,
                                        const math::AABB& aabb,
                                        const glm::mat4& modelMatrix,
                                        const glm::mat4& viewProjection,
                                        const glm::vec4& color) const
    {
        glm::vec3 center = aabb.getCenter();
        glm::vec3 extents = aabb.getExtents();

        glm::mat4 aabbModel = glm::scale(glm::translate(modelMatrix, center), extents);
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
