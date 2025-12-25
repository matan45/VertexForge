#include "AudioSphereDebugRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Utilities.hpp"
#include <cmath>

namespace render::mesh
{
    AudioSphereDebugRenderer::AudioSphereDebugRenderer(core::Device& device, core::SwapChain& swapChain)
        : device{device}, swapChain{swapChain}
    {
    }

    AudioSphereDebugRenderer::~AudioSphereDebugRenderer() = default;

    void AudioSphereDebugRenderer::init(vk::RenderPass renderPass)
    {
        loadShader();
        createPipeline(renderPass);
        createBuffers();
        initialized = true;
    }

    void AudioSphereDebugRenderer::recreate(vk::RenderPass renderPass)
    {
        if (wireframePipeline)
        {
            device.getLogicalDevice().destroyPipeline(wireframePipeline);
            wireframePipeline = nullptr;
        }

        createPipeline(renderPass);
    }

    void AudioSphereDebugRenderer::cleanUp()
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

    void AudioSphereDebugRenderer::cleanUpShader()
    {
        if (wireframeShader)
        {
            wireframeShader->cleanUp();
        }
    }

    void AudioSphereDebugRenderer::loadShader()
    {
        wireframeShader = std::make_shared<core::Shader>(device);
        wireframeShader->readShader("../../resources/shaders/tools/sphere_wireframe.glsl");
    }

    void AudioSphereDebugRenderer::createPipeline(vk::RenderPass renderPass)
    {
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(AudioSpherePushConstants);

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 0;
        pipelineLayoutInfo.pSetLayouts = nullptr;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        wireframePipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);

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
            throw std::runtime_error("Failed to create audio sphere wireframe graphics pipeline");
        }
        wireframePipeline = result.value;
    }

    void AudioSphereDebugRenderer::createBuffers()
    {
        // Create a unit sphere wireframe with 3 rings (XY, XZ, YZ planes)
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;

        const float pi = 3.14159265358979323846f;
        const int segments = SPHERE_SEGMENTS;

        // Generate circle vertices for 3 planes
        // XY plane (horizontal ring at z=0)
        int baseIndex = 0;
        for (int i = 0; i < segments; ++i)
        {
            float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
            vertices.push_back(glm::vec3(std::cos(angle), std::sin(angle), 0.0f));
        }
        for (int i = 0; i < segments; ++i)
        {
            indices.push_back(baseIndex + i);
            indices.push_back(baseIndex + (i + 1) % segments);
        }

        // XZ plane (vertical ring at y=0)
        baseIndex = static_cast<int>(vertices.size());
        for (int i = 0; i < segments; ++i)
        {
            float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
            vertices.push_back(glm::vec3(std::cos(angle), 0.0f, std::sin(angle)));
        }
        for (int i = 0; i < segments; ++i)
        {
            indices.push_back(baseIndex + i);
            indices.push_back(baseIndex + (i + 1) % segments);
        }

        // YZ plane (vertical ring at x=0)
        baseIndex = static_cast<int>(vertices.size());
        for (int i = 0; i < segments; ++i)
        {
            float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
            vertices.push_back(glm::vec3(0.0f, std::cos(angle), std::sin(angle)));
        }
        for (int i = 0; i < segments; ++i)
        {
            indices.push_back(baseIndex + i);
            indices.push_back(baseIndex + (i + 1) % segments);
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

    void AudioSphereDebugRenderer::render(const vk::CommandBuffer& commandBuffer,
                                           const std::vector<AudioSphereRenderData>& audioSourceDrawList,
                                           const glm::mat4& view,
                                           const glm::mat4& projection) const
    {
        if (!initialized || !wireframePipeline || !vertexBuffer)
        {
            return;
        }

        bool hasSpheresToRender = false;
        for (const auto& audioSource : audioSourceDrawList)
        {
            if (audioSource.showDebugSpheres)
            {
                hasSpheresToRender = true;
                break;
            }
        }

        if (!hasSpheresToRender)
        {
            return;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, wireframePipeline);
        commandBuffer.bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);

        vk::Buffer vertexBuffers[] = {vertexBuffer};
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);

        glm::mat4 viewProj = projection * view;

        for (const auto& audioSource : audioSourceDrawList)
        {
            if (!audioSource.showDebugSpheres)
            {
                continue;
            }

            // Draw min distance sphere (green)
            {
                glm::mat4 model = glm::translate(glm::mat4(1.0f), audioSource.position);
                model = glm::scale(model, glm::vec3(audioSource.minDistance));

                AudioSpherePushConstants pushConstants{};
                pushConstants.mvp = viewProj * model;
                pushConstants.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f); // Green

                commandBuffer.pushConstants(wireframePipelineLayout,
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                    0, sizeof(AudioSpherePushConstants), &pushConstants);

                commandBuffer.drawIndexed(indexCount, 1, 0, 0, 0);
            }

            // Draw max distance sphere (orange/yellow)
            {
                glm::mat4 model = glm::translate(glm::mat4(1.0f), audioSource.position);
                model = glm::scale(model, glm::vec3(audioSource.maxDistance));

                AudioSpherePushConstants pushConstants{};
                pushConstants.mvp = viewProj * model;
                pushConstants.color = glm::vec4(1.0f, 0.6f, 0.0f, 1.0f); // Orange

                commandBuffer.pushConstants(wireframePipelineLayout,
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                    0, sizeof(AudioSpherePushConstants), &pushConstants);

                commandBuffer.drawIndexed(indexCount, 1, 0, 0, 0);
            }
        }
    }
}
