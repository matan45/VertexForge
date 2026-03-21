#include "BRDFLUTGenerator.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "resource/TextureResource.hpp"
#include "resource/PathResolver.hpp"
#include "print/Log.hpp"

#include <filesystem>

namespace render::ibl
{
    BRDFLUTGenerator::BRDFLUTGenerator(core::Device& device)
        : device{device}
    {
    }

    bool BRDFLUTGenerator::loadFromFile(const std::string& filePath)
    {
        if (!std::filesystem::exists(filePath))
        {
            return false;
        }

        auto textureData = resource::TextureResource::loadTexture(filePath);
        if (textureData.mipData.empty() || textureData.width == 0 || textureData.height == 0)
        {
            return false;
        }

        const auto& mip0 = textureData.mipData[0];

        core::ImageInfoRequest imageRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        imageRequest.format = vk::Format::eB8G8R8A8Unorm;
        imageRequest.width = mip0.width;
        imageRequest.height = mip0.height;
        imageRequest.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        core::ImageUtilities::createImage(imageRequest, brdfLUTImage.image, brdfLUTImage.imageMemory);

        core::ImageUtilities::uploadStagedPixelData(
            device, brdfLUTImage.image,
            mip0.data.data(),
            static_cast<vk::DeviceSize>(mip0.data.size()),
            mip0.width, mip0.height);

        core::ImageViewInfoRequest viewRequest(device.getLogicalDevice(), brdfLUTImage.image);
        viewRequest.format = vk::Format::eB8G8R8A8Unorm;
        core::ImageUtilities::createImageView(viewRequest, brdfLUTImage.imageView);

        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 1.0f;
        samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        brdfLUTImage.sampler = device.getLogicalDevice().createSampler(samplerInfo);

        vfLogInfo("BRDF LUT loaded from file: {}", filePath);
        return true;
    }

    void BRDFLUTGenerator::generate(const vk::CommandPool& commandPool)
    {
        // Try loading pre-baked BRDF LUT from fixed engine resource path
        std::string lutPath = resource::PathResolver::resolveEnginePath("../../resources/ibl/brdf_lut.vfImage");
        if (loadFromFile(lutPath))
        {
            return;
        }

        // Fallback: GPU generation (file not found — use import pipeline to generate it)
        generateGPU(commandPool);
    }

    void BRDFLUTGenerator::generateGPU(const vk::CommandPool& commandPool)
    {
        brdfLUTShader = std::make_shared<core::Shader>(device);
        brdfLUTShader->readShader("../../resources/shaders/ibl/brdf.glsl");

        core::ImageInfoRequest brdfLUTImageRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        brdfLUTImageRequest.format = vk::Format::eR16G16Sfloat;
        brdfLUTImageRequest.width = CUBE_MAP_SIZE;
        brdfLUTImageRequest.height = CUBE_MAP_SIZE;
        brdfLUTImageRequest.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
        core::ImageUtilities::createImage(brdfLUTImageRequest, brdfLUTImage.image, brdfLUTImage.imageMemory);

        core::ImageViewInfoRequest imageViewRequest(device.getLogicalDevice(), brdfLUTImage.image);
        imageViewRequest.format = vk::Format::eR16G16Sfloat;
        core::ImageUtilities::createImageView(imageViewRequest, brdfLUTImage.imageView);

        vk::SamplerCreateInfo samplerInfo;
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 1.0f;
        samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        brdfLUTImage.sampler = device.getLogicalDevice().createSampler(samplerInfo);

        vk::AttachmentDescription colorAttachment;
        colorAttachment.format = vk::Format::eR16G16Sfloat;
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
        colorAttachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::AttachmentReference colorAttachmentRef;
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subPass;
        subPass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subPass.colorAttachmentCount = 1;
        subPass.pColorAttachments = &colorAttachmentRef;

        vk::SubpassDependency dependency;
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eTransfer;
        dependency.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        vk::RenderPassCreateInfo renderPassInfo;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subPass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        vk::RenderPass renderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);

        vk::VertexInputBindingDescription vertexInputBindingDescription;
        vertexInputBindingDescription.binding = 0;
        vertexInputBindingDescription.stride = sizeof(QuadVertex);
        vertexInputBindingDescription.inputRate = vk::VertexInputRate::eVertex;

        std::vector<vk::VertexInputAttributeDescription> vertexInputAttributes;
        vertexInputAttributes.push_back({0, 0, vk::Format::eR32G32B32Sfloat, offsetof(QuadVertex, position)});
        vertexInputAttributes.push_back({1, 0, vk::Format::eR32G32Sfloat, offsetof(QuadVertex, texture)});

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &vertexInputBindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = vertexInputAttributes.data();

        vk::Buffer quadVertexBuffer;
        vk::DeviceMemory quadVertexBufferMemory;

        core::BufferInfoRequest quadBufferInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        quadBufferInfo.size = sizeof(quad[0]) * quad.size();
        quadBufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        quadBufferInfo.properties = vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(quadBufferInfo, quadVertexBuffer, quadVertexBufferMemory);

        void* data;
        if (vk::Result result = device.getLogicalDevice().mapMemory(quadVertexBufferMemory, 0, quadBufferInfo.size, {},
            &data); result != vk::Result::eSuccess)
        {
            vfLogError("failed to map memory");
        }
        memcpy(data, quad.data(), quadBufferInfo.size);
        device.getLogicalDevice().unmapMemory(quadVertexBufferMemory);

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
        vk::PipelineLayout pipelineLayout = device.getLogicalDevice().createPipelineLayout(pipelineLayoutInfo);

        vk::GraphicsPipelineCreateInfo pipelineInfo;
        pipelineInfo.stageCount = static_cast<uint32_t>(brdfLUTShader->getShaderStages().size());
        pipelineInfo.pStages = brdfLUTShader->getShaderStages().data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        vk::Pipeline graphicsPipeline = device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;

        vk::FramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &brdfLUTImage.imageView;
        framebufferInfo.width = CUBE_MAP_SIZE;
        framebufferInfo.height = CUBE_MAP_SIZE;
        framebufferInfo.layers = 1;

        vk::Framebuffer framebuffer = device.getLogicalDevice().createFramebuffer(framebufferInfo);

        vk::UniqueCommandBuffer commandBuffer = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool);

        vk::ClearValue clearColor{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
        vk::RenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.framebuffer = framebuffer;
        renderPassBeginInfo.renderArea = vk::Rect2D({0, 0}, {CUBE_MAP_SIZE, CUBE_MAP_SIZE});
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = &clearColor;

        commandBuffer->beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
        commandBuffer->bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);

        vk::DeviceSize offsets[] = {0};
        commandBuffer->bindVertexBuffers(0, quadVertexBuffer, offsets);
        commandBuffer->draw(6, 1, 0, 0);
        commandBuffer->endRenderPass();

        vk::Fence renderFence = device.getLogicalDevice().createFence({});
        core::Utilities::endSingleTimeCommands(device, commandBuffer, renderFence);
        if (vk::Result result = device.getLogicalDevice().waitForFences(renderFence, VK_TRUE, UINT64_MAX); result !=
            vk::Result::eSuccess)
        {
            vfLogError("Failed to to wait for Fence BRDFLUT:");
        }

        vk::UniqueCommandBuffer transitionCommandBuffer = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool);

        core::ImageUtilities::transitionImageLayout(
            transitionCommandBuffer.get(),
            brdfLUTImage.image,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor
        );
        core::Utilities::endSingleTimeCommands(device, transitionCommandBuffer);

        device.getLogicalDevice().destroyFramebuffer(framebuffer);
        device.getLogicalDevice().destroyFence(renderFence);

        device.getLogicalDevice().destroyBuffer(quadVertexBuffer);
        device.getLogicalDevice().freeMemory(quadVertexBufferMemory);

        device.getLogicalDevice().destroyRenderPass(renderPass);
        device.getLogicalDevice().destroyPipeline(graphicsPipeline);
        device.getLogicalDevice().destroyPipelineLayout(pipelineLayout);
    }

    void BRDFLUTGenerator::cleanUp()
    {
        device.getLogicalDevice().destroyImage(brdfLUTImage.image);
        device.getLogicalDevice().destroyImageView(brdfLUTImage.imageView);
        device.getLogicalDevice().destroySampler(brdfLUTImage.sampler);
        device.getLogicalDevice().freeMemory(brdfLUTImage.imageMemory);
    }

    void BRDFLUTGenerator::cleanUpShader()
    {
        if (brdfLUTShader)
        {
            brdfLUTShader->cleanUp();
        }
    }
}
