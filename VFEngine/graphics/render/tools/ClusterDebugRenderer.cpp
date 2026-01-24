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
        createDescriptorSetLayout();
        createPipeline(renderPass);
        createBuffers();
        createDescriptorPool();
        createDescriptorSet();
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
        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (descriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
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
        if (instanceBuffer)
        {
            dev.unmapMemory(instanceBufferMemory);
            dev.destroyBuffer(instanceBuffer);
            dev.freeMemory(instanceBufferMemory);
            instanceBuffer = nullptr;
            instanceBufferMemory = nullptr;
            instanceBufferMapped = nullptr;
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
        wireframeShader->readShader("../../resources/shaders/tools/clusterWireframe.glsl");
    }

    void ClusterDebugRenderer::createDescriptorSetLayout()
    {
        // Storage buffer for instance data
        vk::DescriptorSetLayoutBinding instanceBufferBinding{};
        instanceBufferBinding.binding = 0;
        instanceBufferBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
        instanceBufferBinding.descriptorCount = 1;
        instanceBufferBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &instanceBufferBinding;

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void ClusterDebugRenderer::createPipeline(vk::RenderPass renderPass)
    {
        // Push constant range for matrices
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(ClusterPushConstants);

        // Pipeline layout with descriptor set and push constants
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        wireframePipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);

        // Vertex input - just position from unit cube
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

        // Input assembly - line list for wireframe
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eLineList;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // Viewport and scissor
        auto extent = swapChain.getSwapchainExtent();
        vk::Viewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.offset = vk::Offset2D{0, 0};
        scissor.extent = extent;

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        // Rasterization - use eFill since we're rendering line primitives (LineList topology)
        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eNone;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizer.depthBiasEnable = VK_FALSE;

        // Multisampling
        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        // Depth stencil - enable depth test but disable write for overlay
        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE;
        depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // Color blending with alpha
        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                               vk::ColorComponentFlagBits::eG |
                                               vk::ColorComponentFlagBits::eB |
                                               vk::ColorComponentFlagBits::eA;
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
        colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        // Dynamic state
        std::array<vk::DynamicState, 2> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };

        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        // Create pipeline
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
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = wireframePipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        auto result = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Failed to create cluster debug pipeline");
        }
        wireframePipeline = result.value;
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

        // Create instance buffer (host-visible for fast updates)
        vk::DeviceSize instanceBufferSize = sizeof(ClusterInstance) * MAX_INSTANCES;
        core::BufferInfoRequest instanceRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        instanceRequest.size = instanceBufferSize;
        instanceRequest.usage = vk::BufferUsageFlagBits::eStorageBuffer;
        instanceRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                     vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(instanceRequest, instanceBuffer, instanceBufferMemory);

        // Map the instance buffer persistently
        instanceBufferMapped = device.getLogicalDevice().mapMemory(instanceBufferMemory, 0, instanceBufferSize);
    }

    void ClusterDebugRenderer::createDescriptorPool()
    {
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void ClusterDebugRenderer::createDescriptorSet()
    {
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        // Update descriptor with instance buffer
        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = instanceBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(ClusterInstance) * MAX_INSTANCES;

        vk::WriteDescriptorSet descriptorWrite{};
        descriptorWrite.dstSet = descriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = vk::DescriptorType::eStorageBuffer;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        device.getLogicalDevice().updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
    }

    uint32_t ClusterDebugRenderer::uploadInstanceData(const ClusterDebugRenderData& data)
    {
        if (!instanceBufferMapped)
        {
            return 0;
        }

        auto* instances = static_cast<ClusterInstance*>(instanceBufferMapped);

        // Colors for visualization
        constexpr glm::vec4 highlightColor{1.0f, 1.0f, 0.0f, 1.0f};  // Yellow for highlighted
        constexpr glm::vec4 normalColor{0.2f, 0.6f, 1.0f, 0.4f};     // Cyan with transparency

        // Build set of highlighted indices for fast lookup
        std::unordered_set<uint32_t> highlightedSet(
            data.highlightedClusterIndices.begin(),
            data.highlightedClusterIndices.end()
        );

        uint32_t instanceCount = 0;

        if (data.showAllClusters)
        {
            // Upload all clusters
            uint32_t count = std::min(static_cast<uint32_t>(data.clusterAABBs.size()), MAX_INSTANCES);
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto& aabb = data.clusterAABBs[i];
                bool isHighlighted = highlightedSet.count(i) > 0;

                instances[instanceCount].minPoint = aabb.minPoint;
                instances[instanceCount].maxPoint = aabb.maxPoint;
                instances[instanceCount].color = isHighlighted ? highlightColor : normalColor;
                ++instanceCount;
            }
        }
        else
        {
            // Upload only highlighted clusters
            for (uint32_t idx : data.highlightedClusterIndices)
            {
                if (idx < data.clusterAABBs.size() && instanceCount < MAX_INSTANCES)
                {
                    const auto& aabb = data.clusterAABBs[idx];
                    instances[instanceCount].minPoint = aabb.minPoint;
                    instances[instanceCount].maxPoint = aabb.maxPoint;
                    instances[instanceCount].color = highlightColor;
                    ++instanceCount;
                }
            }
        }

        return instanceCount;
    }

    void ClusterDebugRenderer::render(const vk::CommandBuffer& commandBuffer,
                                       const ClusterDebugRenderData& data,
                                       const glm::mat4& view,
                                       const glm::mat4& projection)
    {
        if (!initialized || !wireframePipeline || !vertexBuffer || !visible)
        {
            return;
        }

        // Skip if no clusters to render
        if (data.highlightedClusterIndices.empty() && !data.showAllClusters)
        {
            return;
        }

        // Upload instance data and get count
        uint32_t instanceCount = uploadInstanceData(data);
        if (instanceCount == 0)
        {
            return;
        }

        // Bind pipeline
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, wireframePipeline);

        // Set dynamic viewport and scissor
        auto extent = swapChain.getSwapchainExtent();
        vk::Viewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        commandBuffer.setViewport(0, 1, &viewport);

        vk::Rect2D scissor{};
        scissor.offset = vk::Offset2D{0, 0};
        scissor.extent = extent;
        commandBuffer.setScissor(0, 1, &scissor);

        // Bind descriptor set
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, wireframePipelineLayout,
                                         0, 1, &descriptorSet, 0, nullptr);

        // Bind geometry buffers
        vk::Buffer vertexBuffers[] = {vertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
        commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);

        // Push constants
        ClusterPushConstants pushConstants{};
        pushConstants.viewProjection = projection * view;
        pushConstants.invViewMatrix = data.invViewMatrix;

        commandBuffer.pushConstants(wireframePipelineLayout,
            vk::ShaderStageFlagBits::eVertex,
            0, sizeof(ClusterPushConstants), &pushConstants);

        // Single instanced draw call for all clusters
        commandBuffer.drawIndexed(
            static_cast<uint32_t>(kUnitCubeLineIndices.size()),
            instanceCount,
            0, 0, 0
        );
    }
}
