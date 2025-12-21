#include "AABBDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Utilities.hpp"

namespace render::mesh
{
    AABBDebugRenderer::AABBDebugRenderer(core::Device& device, core::SwapChain& swapChain)
        : device{device}, swapChain{swapChain}
    {
    }

    AABBDebugRenderer::~AABBDebugRenderer() = default;

    void AABBDebugRenderer::init(vk::RenderPass renderPass)
    {
        loadShader();
        createPipeline(renderPass);
        createBuffers();
        initialized = true;
    }

    void AABBDebugRenderer::recreate(vk::RenderPass renderPass)
    {
        if (wireframePipeline)
        {
            device.getLogicalDevice().destroyPipeline(wireframePipeline);
            wireframePipeline = nullptr;
        }

        createPipeline(renderPass);
    }

    void AABBDebugRenderer::cleanUp()
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

    void AABBDebugRenderer::createPipeline(vk::RenderPass renderPass)
    {
        // Push constant range for MVP + color
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(AABBPushConstants);

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 0;
        pipelineLayoutInfo.pSetLayouts = nullptr;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        wireframePipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);

        // Vertex input - simple vec3 positions
        vk::VertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(glm::vec3);
        bindingDescription.inputRate = vk::VertexInputRate::eVertex;

        vk::VertexInputAttributeDescription attributeDescription{};
        attributeDescription.binding = 0;
        attributeDescription.location = 0;
        attributeDescription.format = vk::Format::eR32G32B32Sfloat;
        attributeDescription.offset = 0;

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = 1;
        vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eLineList;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        vk::Viewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapChain.getSwapchainExtent().width);
        viewport.height = static_cast<float>(swapChain.getSwapchainExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.offset = vk::Offset2D{0, 0};
        scissor.extent = swapChain.getSwapchainExtent();

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eNone;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizer.depthBiasEnable = VK_FALSE;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                               vk::ColorComponentFlagBits::eG |
                                               vk::ColorComponentFlagBits::eB |
                                               vk::ColorComponentFlagBits::eA;
        colorBlendAttachment.blendEnable = VK_FALSE;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(wireframeShader->getShaderStages().size());
        pipelineInfo.pStages = wireframeShader->getShaderStages().data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = wireframePipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        auto result = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Failed to create wireframe graphics pipeline");
        }
        wireframePipeline = result.value;
    }

    void AABBDebugRenderer::createBuffers()
    {
        // Unit cube vertices (8 corners, from -1 to 1)
        std::vector<glm::vec3> vertices = {
            {-1.0f, -1.0f, -1.0f},  // 0: back-bottom-left
            { 1.0f, -1.0f, -1.0f},  // 1: back-bottom-right
            { 1.0f,  1.0f, -1.0f},  // 2: back-top-right
            {-1.0f,  1.0f, -1.0f},  // 3: back-top-left
            {-1.0f, -1.0f,  1.0f},  // 4: front-bottom-left
            { 1.0f, -1.0f,  1.0f},  // 5: front-bottom-right
            { 1.0f,  1.0f,  1.0f},  // 6: front-top-right
            {-1.0f,  1.0f,  1.0f},  // 7: front-top-left
        };

        // Line indices for 12 edges of the cube
        std::vector<uint32_t> indices = {
            // Back face edges
            0, 1,  1, 2,  2, 3,  3, 0,
            // Front face edges
            4, 5,  5, 6,  6, 7,  7, 4,
            // Connecting edges
            0, 4,  1, 5,  2, 6,  3, 7
        };

        // Create vertex buffer
        vk::DeviceSize vertexBufferSize = sizeof(glm::vec3) * vertices.size();
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::Utilities::createBuffer(vertexRequest, vertexBuffer, vertexBufferMemory);

        core::Utilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            vertexBuffer,
            vertices.data(),
            vertexBufferSize
        );

        // Create index buffer
        vk::DeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();
        core::BufferInfoRequest indexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        indexRequest.size = indexBufferSize;
        indexRequest.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::Utilities::createBuffer(indexRequest, indexBuffer, indexBufferMemory);

        core::Utilities::copyToBuffer(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            indexBuffer,
            indices.data(),
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

            // Render combined mesh AABB (green)
            {
                const math::AABB& aabb = gpuData->boundingBox;
                glm::vec3 center = aabb.getCenter();
                glm::vec3 extents = aabb.getExtents();

                // Scale and translate unit cube [-1,1] to AABB bounds
                glm::mat4 aabbModel = meshData.modelMatrix;
                aabbModel = glm::translate(aabbModel, center);
                aabbModel = glm::scale(aabbModel, extents);

                // Calculate MVP
                glm::mat4 mvp = projection * view * aabbModel;

                AABBPushConstants aabbPushConstants{};
                aabbPushConstants.mvp = mvp;
                aabbPushConstants.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);  // Green wireframe

                commandBuffer.pushConstants(wireframePipelineLayout,
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                    0, sizeof(AABBPushConstants), &aabbPushConstants);

                // Draw unit cube wireframe (24 indices for 12 lines)
                commandBuffer.drawIndexed(24, 1, 0, 0, 0);
            }

            // Render per-submesh AABBs (yellow) - only if there are multiple submeshes
            if (gpuData->subMeshes.size() > 1)
            {
                for (const auto& subMesh : gpuData->subMeshes)
                {
                    const math::AABB& aabb = subMesh.boundingBox;
                    glm::vec3 center = aabb.getCenter();
                    glm::vec3 extents = aabb.getExtents();

                    // Scale and translate unit cube [-1,1] to AABB bounds
                    glm::mat4 aabbModel = meshData.modelMatrix;
                    aabbModel = glm::translate(aabbModel, center);
                    aabbModel = glm::scale(aabbModel, extents);

                    // Calculate MVP
                    glm::mat4 mvp = projection * view * aabbModel;

                    AABBPushConstants aabbPushConstants{};
                    aabbPushConstants.mvp = mvp;
                    aabbPushConstants.color = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);  // Yellow wireframe

                    commandBuffer.pushConstants(wireframePipelineLayout,
                        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                        0, sizeof(AABBPushConstants), &aabbPushConstants);

                    // Draw unit cube wireframe (24 indices for 12 lines)
                    commandBuffer.drawIndexed(24, 1, 0, 0, 0);
                }
            }
        }
    }
}
