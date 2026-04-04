#include "EnvironmentCubemapGenerator.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/Texture.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "print/Log.hpp"

namespace render::ibl
{
    static constexpr uint32_t ENV_CUBE_MAP_SIZE = 1024;

    EnvironmentCubemapGenerator::EnvironmentCubemapGenerator(core::Device& device)
        : device{device}
    {
        shaderEnvCubemap = std::make_shared<core::Shader>(device);
        shaderEnvCubemap->readShader("../../resources/shaders/ibl/equirectangular_to_cubemap.glsl");
    }

    void EnvironmentCubemapGenerator::generate(const core::Texture& hdrTexture, const vk::CommandPool& commandPool)
    {
        // Image and Sampler Create
        core::ImageInfoRequest cubeMapImageRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        cubeMapImageRequest.format = vk::Format::eR16G16B16A16Sfloat;
        cubeMapImageRequest.layers = 6;
        cubeMapImageRequest.width = ENV_CUBE_MAP_SIZE;
        cubeMapImageRequest.height = ENV_CUBE_MAP_SIZE;
        cubeMapImageRequest.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled |
            vk::ImageUsageFlagBits::eColorAttachment;
        cubeMapImageRequest.imageFlags = vk::ImageCreateFlagBits::eCubeCompatible;
        core::ImageUtilities::createImage(cubeMapImageRequest, imageEnvCubemap.image,
            imageEnvCubemap.imageAllocation, device.getMemoryManager());

        core::ImageViewInfoRequest cubeMapImageViewRequest(device.getLogicalDevice(), imageEnvCubemap.image);
        cubeMapImageViewRequest.format = vk::Format::eR16G16B16A16Sfloat;
        cubeMapImageViewRequest.layerCount = 6;
        cubeMapImageViewRequest.imageType = vk::ImageViewType::eCube;
        core::ImageUtilities::createImageView(cubeMapImageViewRequest, imageEnvCubemap.imageView);

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

        imageEnvCubemap.sampler = device.getLogicalDevice().createSampler(samplerInfo);

        // Dynamic rendering format for pipeline creation
        vk::Format colorFormat = vk::Format::eR16G16B16A16Sfloat;

        // DEFINE THE VERTEX BUFFER LAYOUT
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

        // DEFINE THE VERTEX BUFFER
        vk::Buffer vertexBuffer;
        core::VulkanAllocation vertexBufferAllocation;

        core::BufferInfoRequest vertexCubeVerticesBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        vertexCubeVerticesBufferRequest.size = sizeof(cubeVertices[0]) * cubeVertices.size();
        vertexCubeVerticesBufferRequest.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        vertexCubeVerticesBufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(vertexCubeVerticesBufferRequest, vertexBuffer, vertexBufferAllocation, device.getMemoryManager());

        void* data = vertexBufferAllocation.mappedPtr;
        if (!data)
        {
            vfLogError("failed to map memory");
        }
        memcpy(data, cubeVertices.data(), vertexCubeVerticesBufferRequest.size);

        // DEFINE THE UNIFORM BUFFER LAYOUT
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

        vk::DescriptorSetLayout descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);

        // DEFINE THE UNIFORM BUFFER AND SAMPLER
        vk::DescriptorSet descriptorSet;

        vk::DescriptorSetAllocateInfo allocInfo;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        descriptorSet = device.getLogicalDevice().allocateDescriptorSets(allocInfo)[0];

        vk::DescriptorImageInfo hdrImageInfo;
        hdrImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        hdrImageInfo.imageView = hdrTexture.getImageView();
        hdrImageInfo.sampler = hdrTexture.getSampler();

        vk::WriteDescriptorSet hdrDescriptorWrite;
        hdrDescriptorWrite.dstSet = descriptorSet;
        hdrDescriptorWrite.dstBinding = 1;
        hdrDescriptorWrite.dstArrayElement = 0;
        hdrDescriptorWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        hdrDescriptorWrite.descriptorCount = 1;
        hdrDescriptorWrite.pImageInfo = &hdrImageInfo;

        device.getLogicalDevice().updateDescriptorSets(hdrDescriptorWrite, nullptr);

        vk::Buffer uboUniformBuffer;
        core::VulkanAllocation uboUniformBufferAllocation;
        core::BufferInfoRequest uboBufferRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        uboBufferRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        uboBufferRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent;
        uboBufferRequest.size = sizeof(UniformBufferObject);
        core::BufferUtilities::createBuffer(uboBufferRequest, uboUniformBuffer, uboUniformBufferAllocation, device.getMemoryManager());

        vk::DescriptorBufferInfo uboBufferInfo;
        uboBufferInfo.buffer = uboUniformBuffer;
        uboBufferInfo.offset = 0;
        uboBufferInfo.range = sizeof(UniformBufferObject);

        vk::WriteDescriptorSet uboDescriptorWrite;
        uboDescriptorWrite.dstSet = descriptorSet;
        uboDescriptorWrite.dstBinding = 0;
        uboDescriptorWrite.dstArrayElement = 0;
        uboDescriptorWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        uboDescriptorWrite.descriptorCount = 1;
        uboDescriptorWrite.pBufferInfo = &uboBufferInfo;

        device.getLogicalDevice().updateDescriptorSets(uboDescriptorWrite, nullptr);

        // DEFINE GRAPHICS PIPELINE
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        vk::Viewport viewport;
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(ENV_CUBE_MAP_SIZE);
        viewport.height = static_cast<float>(ENV_CUBE_MAP_SIZE);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor;
        scissor.offset = vk::Offset2D(0, 0);
        scissor.extent = vk::Extent2D(ENV_CUBE_MAP_SIZE, ENV_CUBE_MAP_SIZE);

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
        vk::PipelineLayout pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);

        vk::GraphicsPipelineCreateInfo pipelineInfo;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderEnvCubemap->getShaderStages().size());
        pipelineInfo.pStages = shaderEnvCubemap->getShaderStages().data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = nullptr;

        vk::PipelineRenderingCreateInfo pipelineRenderingInfo{};
        pipelineRenderingInfo.colorAttachmentCount = 1;
        pipelineRenderingInfo.pColorAttachmentFormats = &colorFormat;
        pipelineInfo.pNext = &pipelineRenderingInfo;

        vk::Pipeline graphicsPipeline = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;

        // ImageHelper
        OffScreenHelper imageHelper;

        core::ImageInfoRequest imageRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        imageRequest.format = vk::Format::eR16G16B16A16Sfloat;
        imageRequest.width = ENV_CUBE_MAP_SIZE;
        imageRequest.height = ENV_CUBE_MAP_SIZE;
        imageRequest.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc;
        core::ImageUtilities::createImage(imageRequest, imageHelper.image, imageHelper.allocation, device.getMemoryManager());

        core::ImageViewInfoRequest imageViewRequest(device.getLogicalDevice(), imageHelper.image);
        imageViewRequest.format = vk::Format::eR16G16B16A16Sfloat;
        core::ImageUtilities::createImageView(imageViewRequest, imageHelper.view);

        // transition helper image layout
        vk::UniqueCommandBuffer commandBufferInitHelperImageTransition = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool);

        core::ImageUtilities::transitionImageLayout(commandBufferInitHelperImageTransition.get(), imageHelper.image,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::Utilities::endSingleTimeCommands(device, commandBufferInitHelperImageTransition);

        // transition cube image layout
        vk::UniqueCommandBuffer commandBufferInitCubeImage = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool);

        core::ImageUtilities::transitionImageLayout(commandBufferInitCubeImage.get(), imageEnvCubemap.image,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal,
            vk::ImageAspectFlagBits::eColor, 6);

        core::Utilities::endSingleTimeCommands(device, commandBufferInitCubeImage);

        // DRAW COMMAND - render each face separately to ensure uniform buffer is correct
        for (uint32_t face = 0; face < 6; ++face)
        {
            // Update uniform buffer with this face's view matrix
            updateUniformBuffer(CameraViewMatrix::captureViews[face], CameraViewMatrix::captureProjection,
                uboUniformBufferAllocation);

            // Create a new command buffer for each face
            vk::UniqueCommandBuffer faceCommandBuffer = core::Utilities::beginSingleTimeCommands(
                device.getLogicalDevice(), commandPool);

            // Begin dynamic rendering
            core::DynamicRenderingInfo renderingInfo{};
            renderingInfo.extent = vk::Extent2D{ENV_CUBE_MAP_SIZE, ENV_CUBE_MAP_SIZE};
            renderingInfo.colorAttachments = {
                core::colorClear(imageHelper.view, vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}})
            };

            core::beginDynamicRendering(faceCommandBuffer.get(), renderingInfo);

            // Bind pipeline, descriptor sets, and draw commands
            faceCommandBuffer.get().bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
            faceCommandBuffer.get().bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0,
                descriptorSet,
                {});

            // Bind vertex buffer
            vk::DeviceSize offsets[] = {0};
            faceCommandBuffer.get().bindVertexBuffers(0, vertexBuffer, offsets);
            faceCommandBuffer.get().draw(static_cast<uint32_t>(cubeVertices.size()), 1, 0, 0);

            core::endDynamicRendering(faceCommandBuffer.get());

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
            copyRegion.dstSubresource.mipLevel = 0;
            copyRegion.dstSubresource.layerCount = 1;
            copyRegion.dstOffset = vk::Offset3D{0, 0, 0};

            copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
            copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
            copyRegion.extent.depth = 1;

            // Copy the image from the framebuffer to the cube map face
            faceCommandBuffer.get().copyImage(imageHelper.image, vk::ImageLayout::eTransferSrcOptimal,
                imageEnvCubemap.image,
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
                vfLogError("Failed to wait for Fence IBL face {}:", face);
            }
            device.getLogicalDevice().destroyFence(faceFence);
        }

        // transition cube image layout to the final layout
        vk::UniqueCommandBuffer commandBufferEndTransition = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool);

        core::ImageUtilities::transitionImageLayout(commandBufferEndTransition.get(), imageEnvCubemap.image,
            vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor, 6);

        core::Utilities::endSingleTimeCommands(device, commandBufferEndTransition);

        // cleanUp
        core::BufferUtilities::destroyBuffer(device.getLogicalDevice(), vertexBuffer, vertexBufferAllocation, device.getMemoryManager());

        core::BufferUtilities::destroyBuffer(device.getLogicalDevice(), uboUniformBuffer, uboUniformBufferAllocation, device.getMemoryManager());

        device.getMemoryManager().free(imageHelper.allocation);
        device.getLogicalDevice().destroyImageView(imageHelper.view);
        device.getLogicalDevice().destroyImage(imageHelper.image);

        device.getLogicalDevice().destroyDescriptorPool(descriptorPool);
        device.getLogicalDevice().destroyDescriptorSetLayout(descriptorSetLayout);
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
    }

    void EnvironmentCubemapGenerator::updateUniformBuffer(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix,
        core::VulkanAllocation& uniformBufferAllocation) const
    {
        UniformBufferObject ubo;
        ubo.view = viewMatrix;
        ubo.projection = projectionMatrix;

        if (uniformBufferAllocation.mappedPtr)
        {
            memcpy(uniformBufferAllocation.mappedPtr, &ubo, sizeof(ubo));
        }
    }

    void EnvironmentCubemapGenerator::cleanUp()
    {
        device.getLogicalDevice().destroyImage(imageEnvCubemap.image);
        device.getLogicalDevice().destroyImageView(imageEnvCubemap.imageView);
        device.getLogicalDevice().destroySampler(imageEnvCubemap.sampler);
        device.getMemoryManager().free(imageEnvCubemap.imageAllocation); imageEnvCubemap.imageAllocation = {};
    }

    void EnvironmentCubemapGenerator::cleanUpShader()
    {
        shaderEnvCubemap->cleanUp();
    }
}
