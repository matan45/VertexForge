#include "SkyboxRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Logger.hpp"

namespace render::ibl
{
    SkyboxRenderer::SkyboxRenderer(core::Device& device, core::SwapChain& swapChain,
                                   core::OffscreenResources& offscreenResources)
        : device{device}, swapChain{swapChain}, offscreenResources{offscreenResources}
    {
        skyboxShader = std::make_shared<core::Shader>(device);
        skyboxShader->readShader("../../resources/shaders/ibl/skybox.glsl");
    }

    void SkyboxRenderer::init(const ImageData& irradianceCube)
    {
        // RenderPass
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChain.getSwapchainImageFormat();
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);

        // VertexBuffer
        std::vector<vk::VertexInputBindingDescription> vertexInputBindingDescriptiones;
        vk::VertexInputBindingDescription vertexInputBindingDescription1;
        vertexInputBindingDescription1.binding = 0;
        vertexInputBindingDescription1.stride = sizeof(glm::vec3);
        vertexInputBindingDescription1.inputRate = vk::VertexInputRate::eVertex;
        vertexInputBindingDescriptiones.push_back(vertexInputBindingDescription1);

        std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes;
        vertexInputAttributes.push_back({0, 0, vk::Format::eR32G32B32Sfloat, 0});

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = vertexInputBindingDescriptiones.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = vertexInputAttributes.data();

        // DEFINE THE VERTEX BUFFER - use skyboxVertices for rendering from inside
        core::BufferInfoRequest vertexCubeVerticesBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexCubeVerticesBufferRequest.size = sizeof(skyboxVertices[0]) * skyboxVertices.size();
        vertexCubeVerticesBufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        vertexCubeVerticesBufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(vertexCubeVerticesBufferRequest, vertexBuffer, vertexBufferMemory);

        void* data;
        if (vk::Result result = device.getLogicalDevice().mapMemory(vertexBufferMemory, 0,
            vertexCubeVerticesBufferRequest.size, {},
            &data); result != vk::Result::eSuccess)
        {
            loggerError("failed to map memory");
        }
        memcpy(data, skyboxVertices.data(), vertexCubeVerticesBufferRequest.size);
        device.getLogicalDevice().unmapMemory(vertexBufferMemory);

        // UniformBuffer
        std::vector<vk::DescriptorPoolSize> poolSizes(2);
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);

        std::vector<vk::DescriptorSetLayoutBinding> bindings(2);

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex;
        bindings[0].pImmutableSamplers = nullptr;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;
        bindings[1].pImmutableSamplers = nullptr;

        vk::DescriptorSetLayoutCreateInfo layoutInfo;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);

        // DEFINE THE UNIFORM BUFFER AND SAMPLER
        core::BufferInfoRequest bufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        bufferRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent;
        bufferRequest.size = sizeof(UniformBufferObject);
        core::BufferUtilities::createBuffer(bufferRequest, uniformBuffer, uniformBufferMemory);

        vk::DescriptorSetAllocateInfo allocInfo;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        vk::DescriptorImageInfo imageInfo;
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfo.imageView = irradianceCube.imageView;
        imageInfo.sampler = irradianceCube.sampler;

        vk::WriteDescriptorSet descriptorWriteImageSampler;
        descriptorWriteImageSampler.dstSet = descriptorSet;
        descriptorWriteImageSampler.dstBinding = 1;
        descriptorWriteImageSampler.dstArrayElement = 0;
        descriptorWriteImageSampler.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        descriptorWriteImageSampler.descriptorCount = 1;
        descriptorWriteImageSampler.pImageInfo = &imageInfo;

        device.getLogicalDevice().updateDescriptorSets(descriptorWriteImageSampler, nullptr);

        vk::DescriptorBufferInfo uboBufferInfo;
        uboBufferInfo.buffer = uniformBuffer;
        uboBufferInfo.offset = 0;
        uboBufferInfo.range = sizeof(UniformBufferObject);

        vk::WriteDescriptorSet descriptorWriteUbo;
        descriptorWriteUbo.dstSet = descriptorSet;
        descriptorWriteUbo.dstBinding = 0;
        descriptorWriteUbo.dstArrayElement = 0;
        descriptorWriteUbo.descriptorType = vk::DescriptorType::eUniformBuffer;
        descriptorWriteUbo.descriptorCount = 1;
        descriptorWriteUbo.pBufferInfo = &uboBufferInfo;

        device.getLogicalDevice().updateDescriptorSets(descriptorWriteUbo, nullptr);

        // GraphicsPipeline
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        vk::Viewport viewport;
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapChain.getSwapchainExtent().width);
        viewport.height = static_cast<float>(swapChain.getSwapchainExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor;
        scissor.offset = vk::Offset2D(0, 0);
        scissor.extent = vk::Extent2D(swapChain.getSwapchainExtent().width, swapChain.getSwapchainExtent().height);

        vk::PipelineViewportStateCreateInfo viewportState;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        vk::PipelineRasterizationStateCreateInfo rasterizer;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eNone;  // No culling for skybox
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
        rasterizer.depthBiasEnable = VK_FALSE;

        vk::PipelineMultisampleStateCreateInfo multisampling;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineColorBlendAttachmentState colorBlendAttachment;
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
            | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        colorBlendAttachment.blendEnable = VK_FALSE;

        vk::PipelineColorBlendStateCreateInfo colorBlending;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);

        vk::GraphicsPipelineCreateInfo pipelineInfo;
        pipelineInfo.stageCount = static_cast<uint32_t>(skyboxShader->getShaderStages().size());
        pipelineInfo.pStages = skyboxShader->getShaderStages().data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        graphicsPipeline = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;

        // FrameBuffers
        framebuffers.resize(offscreenResources.colorImages.size());

        for (uint32_t i = 0; i < framebuffers.size(); i++)
        {
            vk::ImageView viewImage = offscreenResources.colorImages[i].colorImageView;

            vk::FramebufferCreateInfo framebufferInfo{};
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &viewImage;
            framebufferInfo.width = swapChain.getSwapchainExtent().width;
            framebufferInfo.height = swapChain.getSwapchainExtent().height;
            framebufferInfo.layers = 1;

            framebuffers[i] = device.getLogicalDevice().createFramebuffer(framebufferInfo);
        }
    }

    void SkyboxRenderer::recreate()
    {
        // Destroy old framebuffers
        for (auto const& frame : framebuffers)
        {
            device.getLogicalDevice().destroyFramebuffer(frame);
        }
        framebuffers.clear();

        // Destroy and recreate render pass
        device.getLogicalDevice().destroyRenderPass(renderPass);

        // Recreate render pass
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChain.getSwapchainImageFormat();
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;  // Preserve clear color
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);

        // Recreate framebuffers
        framebuffers.resize(offscreenResources.colorImages.size());

        for (uint32_t i = 0; i < framebuffers.size(); i++)
        {
            vk::ImageView viewImage = offscreenResources.colorImages[i].colorImageView;

            vk::FramebufferCreateInfo framebufferInfo{};
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &viewImage;
            framebufferInfo.width = swapChain.getSwapchainExtent().width;
            framebufferInfo.height = swapChain.getSwapchainExtent().height;
            framebufferInfo.layers = 1;

            framebuffers[i] = device.getLogicalDevice().createFramebuffer(framebufferInfo);
        }
    }

    void SkyboxRenderer::recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        if (isDisplay)
        {
            updateUniformBuffer(viewMatrix, projectionMatrix);
            vk::RenderPassBeginInfo renderPassInfo{};
            renderPassInfo.renderPass = renderPass;
            renderPassInfo.framebuffer = framebuffers[imageIndex];
            renderPassInfo.renderArea.offset.x = 0;
            renderPassInfo.renderArea.offset.y = 0;
            renderPassInfo.renderArea.extent = swapChain.getSwapchainExtent();

            std::array<vk::ClearValue, 1> clearValues{};
            clearValues[0].color = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
            renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
            renderPassInfo.pClearValues = clearValues.data();

            // Begin render pass
            commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

            // Bind the graphics pipeline
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0,
                descriptorSet,
                {});

            // Bind vertex buffer
            vk::DeviceSize offsets[] = {0};
            commandBuffer.bindVertexBuffers(0, vertexBuffer, offsets);
            commandBuffer.draw(static_cast<uint32_t>(skyboxVertices.size()), 1, 0, 0);

            // End render pass
            commandBuffer.endRenderPass();
        }
    }

    void SkyboxRenderer::updateUniformBuffer(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) const
    {
        UniformBufferObject ubo;
        ubo.view = viewMatrix;
        ubo.projection = projectionMatrix;

        void* data;
        vk::Result result = device.getLogicalDevice().mapMemory(uniformBufferMemory, 0, sizeof(ubo), {}, &data);
        if (result == vk::Result::eSuccess)
        {
            memcpy(data, &ubo, sizeof(ubo));
            device.getLogicalDevice().unmapMemory(uniformBufferMemory);
        }
    }

    void SkyboxRenderer::cleanUp()
    {
        for (auto const& frame : framebuffers)
        {
            device.getLogicalDevice().destroyFramebuffer(frame);
        }
        device.getLogicalDevice().destroyBuffer(vertexBuffer);
        device.getLogicalDevice().freeMemory(vertexBufferMemory);

        device.getLogicalDevice().destroyBuffer(uniformBuffer);
        device.getLogicalDevice().freeMemory(uniformBufferMemory);

        device.getLogicalDevice().destroyRenderPass(renderPass);
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
        device.getLogicalDevice().freeDescriptorSets(descriptorPool, descriptorSet);
        device.getLogicalDevice().destroyDescriptorPool(descriptorPool);
        device.getLogicalDevice().destroyDescriptorSetLayout(descriptorSetLayout);
    }

    void SkyboxRenderer::cleanUpShader()
    {
        skyboxShader->cleanUp();
    }
}
