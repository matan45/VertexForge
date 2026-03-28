#include "PrefilteredEnvGenerator.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "print/Log.hpp"

namespace render::ibl
{
    PrefilteredEnvGenerator::PrefilteredEnvGenerator(core::Device& device)
        : device{device}
    {
        prefilterShader = std::make_shared<core::Shader>(device);
        prefilterShader->readShader("../../resources/shaders/ibl/prefilter.glsl");
    }

    void PrefilteredEnvGenerator::generate(const ImageData& irradianceCube, const vk::CommandPool& commandPool)
    {
        const uint32_t mipLevels = static_cast<uint32_t>(floor(log2(CUBE_MAP_SIZE))) + 1;

        // Image and Sampler Create
        core::ImageInfoRequest cubeMapImageRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        cubeMapImageRequest.format = vk::Format::eR16G16B16A16Sfloat;
        cubeMapImageRequest.layers = 6;
        cubeMapImageRequest.mipLevels = mipLevels;
        cubeMapImageRequest.width = CUBE_MAP_SIZE;
        cubeMapImageRequest.height = CUBE_MAP_SIZE;
        cubeMapImageRequest.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled |
            vk::ImageUsageFlagBits::eColorAttachment;
        cubeMapImageRequest.imageFlags = vk::ImageCreateFlagBits::eCubeCompatible;
        core::ImageUtilities::createImage(cubeMapImageRequest, prefilterImage.image,
            prefilterImage.imageAllocation, device.getMemoryManager());

        core::ImageViewInfoRequest cubeMapImageViewRequest(device.getLogicalDevice(), prefilterImage.image);
        cubeMapImageViewRequest.format = vk::Format::eR16G16B16A16Sfloat;
        cubeMapImageViewRequest.layerCount = 6;
        cubeMapImageViewRequest.mipLevels = mipLevels;
        cubeMapImageViewRequest.imageType = vk::ImageViewType::eCube;
        core::ImageUtilities::createImageView(cubeMapImageViewRequest, prefilterImage.imageView);

        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 16;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        prefilterImage.sampler = device.getLogicalDevice().createSampler(samplerInfo);

        // RenderPass
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = vk::Format::eR16G16B16A16Sfloat;
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
        colorAttachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::AttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        // Use subpass dependencies for layout transitions
        std::array<vk::SubpassDependency, 2> dependencies;
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
        dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependencies[0].srcAccessMask = vk::AccessFlagBits::eMemoryRead;
        dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
            vk::AccessFlagBits::eColorAttachmentWrite;
        dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
        dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
            vk::AccessFlagBits::eColorAttachmentWrite;
        dependencies[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;
        dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 2;
        renderPassInfo.pDependencies = dependencies.data();

        vk::RenderPass renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);

        // VertexBuffer
        vk::VertexInputBindingDescription vertexInputBindingDescription;
        vertexInputBindingDescription.binding = 0;
        vertexInputBindingDescription.stride = sizeof(glm::vec3);
        vertexInputBindingDescription.inputRate = vk::VertexInputRate::eVertex;

        std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes;
        vertexInputAttributes.push_back({0, 0, vk::Format::eR32G32B32Sfloat, 0});

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &vertexInputBindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = vertexInputAttributes.data();

        vk::Buffer cubeVertexBuffer;
        vk::DeviceMemory cubeVertexBufferMemory;

        core::BufferInfoRequest cubeVertexBufferInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        cubeVertexBufferInfo.size = sizeof(cubeVertices[0]) * cubeVertices.size();
        cubeVertexBufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        cubeVertexBufferInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(cubeVertexBufferInfo, cubeVertexBuffer, cubeVertexBufferMemory);

        void* data;
        if (vk::Result result = device.getLogicalDevice().mapMemory(cubeVertexBufferMemory, 0,
            cubeVertexBufferInfo.size, {},
            &data); result != vk::Result::eSuccess)
        {
            vfLogError("failed to map memory");
        }
        memcpy(data, cubeVertices.data(), cubeVertexBufferInfo.size);
        device.getLogicalDevice().unmapMemory(cubeVertexBufferMemory);

        // UniformBuffer
        vk::DescriptorPool descriptorPool;

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

        vk::DescriptorSetLayout descriptorSetLayout;

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

        vk::DescriptorSet descriptorSet;

        vk::DescriptorSetAllocateInfo allocInfo;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        vk::DescriptorImageInfo cubeImageInfo;
        cubeImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        cubeImageInfo.imageView = irradianceCube.imageView;
        cubeImageInfo.sampler = irradianceCube.sampler;

        vk::WriteDescriptorSet descriptorWriteCube;
        descriptorWriteCube.dstSet = descriptorSet;
        descriptorWriteCube.dstBinding = 1;
        descriptorWriteCube.dstArrayElement = 0;
        descriptorWriteCube.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        descriptorWriteCube.descriptorCount = 1;
        descriptorWriteCube.pImageInfo = &cubeImageInfo;

        device.getLogicalDevice().updateDescriptorSets(descriptorWriteCube, nullptr);

        vk::Buffer uboUniformBuffer;
        vk::DeviceMemory uboUniformBufferMemory;
        core::BufferInfoRequest uboBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        uboBufferRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        uboBufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent;
        uboBufferRequest.size = sizeof(UniformBufferObject);
        core::BufferUtilities::createBuffer(uboBufferRequest, uboUniformBuffer, uboUniformBufferMemory);

        vk::DescriptorBufferInfo uboBufferInfo;
        uboBufferInfo.buffer = uboUniformBuffer;
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
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(float);

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        vk::Viewport viewport;
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(CUBE_MAP_SIZE);
        viewport.height = static_cast<float>(CUBE_MAP_SIZE);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor;
        scissor.offset = vk::Offset2D(0, 0);
        scissor.extent = vk::Extent2D(CUBE_MAP_SIZE, CUBE_MAP_SIZE);

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
        rasterizer.cullMode = vk::CullModeFlagBits::eBack;
        rasterizer.frontFace = vk::FrontFace::eClockwise;
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
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
        vk::PipelineLayout pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);

        std::array<vk::DynamicState, 2> dynamicStateEnables = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        vk::PipelineDynamicStateCreateInfo dynamicState;
        dynamicState.pDynamicStates = dynamicStateEnables.data();
        dynamicState.dynamicStateCount = 2;

        vk::GraphicsPipelineCreateInfo pipelineInfo;
        pipelineInfo.stageCount = static_cast<uint32_t>(prefilterShader->getShaderStages().size());
        pipelineInfo.pStages = prefilterShader->getShaderStages().data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.pDynamicState = &dynamicState;

        vk::Pipeline graphicsPipeline = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;

        // ImageHelper
        OffScreenHelper imageHelper;

        core::ImageInfoRequest imageRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        imageRequest.format = vk::Format::eR16G16B16A16Sfloat;
        imageRequest.width = CUBE_MAP_SIZE;
        imageRequest.height = CUBE_MAP_SIZE;
        imageRequest.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
        core::ImageUtilities::createImage(imageRequest, imageHelper.image, imageHelper.memory);

        core::ImageViewInfoRequest imageViewRequest(device.getLogicalDevice(), imageHelper.image);
        imageViewRequest.format = vk::Format::eR16G16B16A16Sfloat;
        core::ImageUtilities::createImageView(imageViewRequest, imageHelper.view);

        vk::FramebufferCreateInfo framebufferInfo{};
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &imageHelper.view;
        framebufferInfo.width = CUBE_MAP_SIZE;
        framebufferInfo.height = CUBE_MAP_SIZE;
        framebufferInfo.layers = 1;

        imageHelper.framebuffer = device.getLogicalDevice().createFramebuffer(framebufferInfo);

        // ImageTransition
        vk::UniqueCommandBuffer commandBufferInitHelperImageTransition = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool);

        core::ImageUtilities::transitionImageLayout(commandBufferInitHelperImageTransition.get(), imageHelper.image,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::Utilities::endSingleTimeCommands(device, commandBufferInitHelperImageTransition);

        vk::UniqueCommandBuffer commandBufferInitCubeImage = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool);

        core::ImageUtilities::transitionImageLayout(commandBufferInitCubeImage.get(), prefilterImage.image,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal,
            vk::ImageAspectFlagBits::eColor, 6, mipLevels);

        core::Utilities::endSingleTimeCommands(device, commandBufferInitCubeImage);

        // Draw
        vk::ClearValue clearColor{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};

        vk::RenderPassBeginInfo renderPassBeginInfo{};
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.framebuffer = imageHelper.framebuffer;
        renderPassBeginInfo.renderArea = vk::Rect2D({0, 0}, {CUBE_MAP_SIZE, CUBE_MAP_SIZE});
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = &clearColor;

        float roughness = 0;
        for (uint32_t m = 0; m < mipLevels; m++)
        {
            roughness = (float)m / (float)(mipLevels - 1);
            for (uint32_t face = 0; face < 6; face++)
            {
                // Update uniform buffer with this face's view matrix
                updateUniformBuffer(CameraViewMatrix::captureViews[face], CameraViewMatrix::captureProjection,
                    uboUniformBufferMemory);

                // Create a new command buffer for each face/mip
                vk::UniqueCommandBuffer faceCommandBuffer = core::Utilities::beginSingleTimeCommands(
                    device.getLogicalDevice(), commandPool);

                viewport.width = static_cast<float>(CUBE_MAP_SIZE * std::pow(0.5f, m));
                viewport.height = static_cast<float>(CUBE_MAP_SIZE * std::pow(0.5f, m));
                faceCommandBuffer.get().setViewport(0, 1, &viewport);
                faceCommandBuffer.get().setScissor(0, 1, &scissor);

                faceCommandBuffer.get().pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0,
                    sizeof(float), &roughness);

                // Begin render pass and render to the specific cube face
                faceCommandBuffer.get().beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);

                // Bind pipeline, descriptor sets, and draw commands
                faceCommandBuffer.get().bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
                faceCommandBuffer.get().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0,
                    descriptorSet,
                    {});

                // Bind vertex buffer
                vk::DeviceSize offsets[] = {0};
                faceCommandBuffer.get().bindVertexBuffers(0, cubeVertexBuffer, offsets);
                faceCommandBuffer.get().draw(static_cast<uint32_t>(cubeVertices.size()), 1, 0, 0);

                faceCommandBuffer.get().endRenderPass();

                // Ensure synchronization between rendering and copying by transitioning the image layout
                core::ImageUtilities::transitionImageLayout(faceCommandBuffer.get(), imageHelper.image,
                    vk::ImageLayout::eColorAttachmentOptimal,
                    vk::ImageLayout::eTransferSrcOptimal,
                    vk::ImageAspectFlagBits::eColor);

                // Set up the copy region for the transfer operation
                vk::ImageCopy copyRegion = {};
                copyRegion.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
                copyRegion.srcSubresource.baseArrayLayer = 0;
                copyRegion.srcSubresource.mipLevel = 0;
                copyRegion.srcSubresource.layerCount = 1;
                copyRegion.srcOffset = vk::Offset3D{0, 0, 0};

                copyRegion.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
                copyRegion.dstSubresource.baseArrayLayer = face;
                copyRegion.dstSubresource.mipLevel = m;
                copyRegion.dstSubresource.layerCount = 1;
                copyRegion.dstOffset = vk::Offset3D{0, 0, 0};

                copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
                copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
                copyRegion.extent.depth = 1;

                // Copy the image from the framebuffer to the cube map face
                faceCommandBuffer.get().copyImage(imageHelper.image, vk::ImageLayout::eTransferSrcOptimal,
                    prefilterImage.image,
                    vk::ImageLayout::eTransferDstOptimal, 1, &copyRegion);

                // Transition the image back to color attachment layout for the next face
                core::ImageUtilities::transitionImageLayout(faceCommandBuffer.get(), imageHelper.image,
                    vk::ImageLayout::eTransferSrcOptimal,
                    vk::ImageLayout::eColorAttachmentOptimal,
                    vk::ImageAspectFlagBits::eColor);

                // Submit and wait for this face to complete before moving to next
                vk::Fence faceFence = device.getLogicalDevice().createFence({});
                core::Utilities::endSingleTimeCommands(device, faceCommandBuffer, faceFence);

                if (vk::Result result = device.getLogicalDevice().waitForFences(faceFence, VK_TRUE, UINT64_MAX); result !=
                    vk::Result::eSuccess)
                {
                    vfLogError("Failed to wait for Fence prefilter mip {} face {}:", m, face);
                }
                device.getLogicalDevice().destroyFence(faceFence);
            }
        }

        // EndImageTransition
        vk::UniqueCommandBuffer commandBufferEndTransition = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool);

        core::ImageUtilities::transitionImageLayout(commandBufferEndTransition.get(), prefilterImage.image,
            vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor, 6, mipLevels);

        core::Utilities::endSingleTimeCommands(device, commandBufferEndTransition);

        // CleanUp
        device.getLogicalDevice().destroyBuffer(cubeVertexBuffer);
        device.getLogicalDevice().freeMemory(cubeVertexBufferMemory);

        device.getLogicalDevice().destroyBuffer(uboUniformBuffer);
        device.getLogicalDevice().freeMemory(uboUniformBufferMemory);

        device.getLogicalDevice().destroyFramebuffer(imageHelper.framebuffer);
        device.getLogicalDevice().freeMemory(imageHelper.memory);
        device.getLogicalDevice().destroyImageView(imageHelper.view);
        device.getLogicalDevice().destroyImage(imageHelper.image);

        device.getLogicalDevice().destroyRenderPass(renderPass);
        device.getLogicalDevice().destroyDescriptorPool(descriptorPool);
        device.getLogicalDevice().destroyDescriptorSetLayout(descriptorSetLayout);
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
    }

    void PrefilteredEnvGenerator::updateUniformBuffer(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix,
        const vk::DeviceMemory& uniformBufferMemory) const
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

    void PrefilteredEnvGenerator::cleanUp()
    {
        device.getLogicalDevice().destroyImage(prefilterImage.image);
        device.getLogicalDevice().destroyImageView(prefilterImage.imageView);
        device.getLogicalDevice().destroySampler(prefilterImage.sampler);
        device.getMemoryManager().free(prefilterImage.imageAllocation); prefilterImage.imageAllocation = {};
    }

    void PrefilteredEnvGenerator::cleanUpShader()
    {
        prefilterShader->cleanUp();
    }
}
