#include "FrustumDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Utilities.hpp"
#include <glm/gtc/matrix_inverse.hpp>

namespace render::mesh
{
    FrustumDebugRenderer::FrustumDebugRenderer(core::Device& device, core::SwapChain& swapChain)
        : device{device}, swapChain{swapChain}
    {
    }

    FrustumDebugRenderer::~FrustumDebugRenderer() = default;

    void FrustumDebugRenderer::init(vk::RenderPass renderPass)
    {
        loadShader();
        createPipeline(renderPass);
        createBuffers();
        initialized = true;
    }

    void FrustumDebugRenderer::recreate(vk::RenderPass renderPass)
    {
        if (wireframePipeline)
        {
            device.getLogicalDevice().destroyPipeline(wireframePipeline);
            wireframePipeline = nullptr;
        }

        createPipeline(renderPass);
    }

    void FrustumDebugRenderer::cleanUp()
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
        wireframeShader->readShader("../../resources/shaders/mesh/wireframe.glsl");
    }

    void FrustumDebugRenderer::createPipeline(vk::RenderPass renderPass)
    {
        // Push constant range for MVP + color
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(FrustumPushConstants);

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
            throw std::runtime_error("Failed to create frustum wireframe graphics pipeline");
        }
        wireframePipeline = result.value;
    }

    void FrustumDebugRenderer::createBuffers()
    {
        // Create vertex buffer for 8 frustum corners (updated dynamically)
        vk::DeviceSize vertexBufferSize = sizeof(glm::vec3) * 8;
        core::BufferInfoRequest vertexRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexRequest.size = vertexBufferSize;
        vertexRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        vertexRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent;
        core::Utilities::createBuffer(vertexRequest, vertexBuffer, vertexBufferMemory);

        // Line indices for 12 edges of the frustum
        // Near plane: 0-1-2-3, Far plane: 4-5-6-7
        std::vector<uint32_t> indices = {
            // Near plane edges
            0, 1,  1, 2,  2, 3,  3, 0,
            // Far plane edges
            4, 5,  5, 6,  6, 7,  7, 4,
            // Connecting edges (near to far)
            0, 4,  1, 5,  2, 6,  3, 7
        };

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

    std::vector<glm::vec3> FrustumDebugRenderer::computeFrustumCorners(const glm::mat4& inverseViewProj) const
    {
        // NDC corners of a unit cube (clip space frustum)
        // Near plane z = -1, Far plane z = 1 (OpenGL convention)
        // For Vulkan (z = 0 to 1), adjust accordingly
        std::vector<glm::vec4> ndcCorners = {
            // Near plane (z = 0 in Vulkan)
            {-1.0f, -1.0f, 0.0f, 1.0f},  // bottom-left
            { 1.0f, -1.0f, 0.0f, 1.0f},  // bottom-right
            { 1.0f,  1.0f, 0.0f, 1.0f},  // top-right
            {-1.0f,  1.0f, 0.0f, 1.0f},  // top-left
            // Far plane (z = 1 in Vulkan)
            {-1.0f, -1.0f, 1.0f, 1.0f},  // bottom-left
            { 1.0f, -1.0f, 1.0f, 1.0f},  // bottom-right
            { 1.0f,  1.0f, 1.0f, 1.0f},  // top-right
            {-1.0f,  1.0f, 1.0f, 1.0f},  // top-left
        };

        std::vector<glm::vec3> worldCorners;
        worldCorners.reserve(8);

        for (const auto& ndc : ndcCorners)
        {
            glm::vec4 worldPos = inverseViewProj * ndc;
            worldPos /= worldPos.w;  // Perspective divide
            worldCorners.emplace_back(worldPos.x, worldPos.y, worldPos.z);
        }

        return worldCorners;
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

        for (const auto& camera : cameraDrawList)
        {
            if (!camera.showFrustum)
            {
                continue;
            }

            // Compute frustum using the entity's world transform and projection
            // The worldMatrix represents where the camera entity IS in the scene
            // We need to compute the view matrix from the world matrix (view = inverse(world))
            glm::mat4 cameraViewFromWorld = glm::inverse(camera.worldMatrix);
            glm::mat4 viewProj = camera.projectionMatrix * cameraViewFromWorld;
            glm::mat4 inverseViewProj = glm::inverse(viewProj);

            // Get frustum corners in world space
            std::vector<glm::vec3> corners = computeFrustumCorners(inverseViewProj);

            // Update vertex buffer with frustum corners
            void* data;
            vk::Result mapResult = device.getLogicalDevice().mapMemory(
                vertexBufferMemory, 0, sizeof(glm::vec3) * 8, {}, &data);
            if (mapResult == vk::Result::eSuccess)
            {
                std::memcpy(data, corners.data(), sizeof(glm::vec3) * 8);
                device.getLogicalDevice().unmapMemory(vertexBufferMemory);
            }

            // Bind vertex buffer
            vk::Buffer vertexBuffers[] = {vertexBuffer};
            vk::DeviceSize offsets[] = {0};
            commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

            // MVP = editor's view-projection * identity (corners already in world space)
            glm::mat4 mvp = editorProjection * editorView;

            FrustumPushConstants pushConstants{};
            pushConstants.mvp = mvp;
            pushConstants.color = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);  // Cyan wireframe

            commandBuffer.pushConstants(wireframePipelineLayout,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0, sizeof(FrustumPushConstants), &pushConstants);

            // Draw frustum wireframe (24 indices for 12 lines)
            commandBuffer.drawIndexed(24, 1, 0, 0, 0);
        }
    }
}
