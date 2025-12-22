#include "GridRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Utilities.hpp"

namespace render::mesh
{
    GridRenderer::GridRenderer(core::Device& device, core::SwapChain& swapChain)
        : device{device}, swapChain{swapChain}
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
        if (gridPipeline)
        {
            device.getLogicalDevice().destroyPipeline(gridPipeline);
            gridPipeline = nullptr;
        }

        createPipeline(renderPass);
    }

    void GridRenderer::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        if (gridPipeline)
        {
            dev.destroyPipeline(gridPipeline);
            gridPipeline = nullptr;
        }
        if (gridPipelineLayout)
        {
            dev.destroyPipelineLayout(gridPipelineLayout);
            gridPipelineLayout = nullptr;
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
        // Push constant range for grid parameters
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(GridPushConstants);

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 0;
        pipelineLayoutInfo.pSetLayouts = nullptr;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        gridPipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);

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
        depthStencil.depthWriteEnable = VK_FALSE;  // Don't write to depth buffer
        depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        // Enable alpha blending for semi-transparent grid
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

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(gridShader->getShaderStages().size());
        pipelineInfo.pStages = gridShader->getShaderStages().data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = gridPipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        auto result = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Failed to create grid graphics pipeline");
        }
        gridPipeline = result.value;
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
            vertices.push_back({ halfSize, 0.0f, z});
            indices.push_back(idx++);
            indices.push_back(idx++);
        }

        // Generate lines parallel to Z-axis (running along Z, at different X positions)
        for (int i = -numCells; i <= numCells; ++i)
        {
            float x = i * cellSize;
            vertices.push_back({x, 0.0f, -halfSize});
            vertices.push_back({x, 0.0f,  halfSize});
            indices.push_back(idx++);
            indices.push_back(idx++);
        }

        indexCount = static_cast<uint32_t>(indices.size());

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
