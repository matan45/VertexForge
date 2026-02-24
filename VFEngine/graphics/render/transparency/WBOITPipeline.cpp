#include "WBOITPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/Shader.hpp"
#include "../../core/ImageUtilities.hpp"
#include "print/Logger.hpp"

namespace render::transparency
{
    WBOITPipeline::WBOITPipeline(core::Device& device, core::SwapChain& swapChain,
                                  core::OffscreenResources& offscreenResources)
        : device(device), swapChain(swapChain), offscreenResources(offscreenResources)
    {
    }

    WBOITPipeline::~WBOITPipeline()
    {
        cleanup();
    }

    void WBOITPipeline::init()
    {
        createSampler();
        createRenderTargets();
        createWBOITRenderPass();
        createWBOITFramebuffers();
        createCompositeRenderPass();
        createCompositeFramebuffers();
        createCompositeDescriptorResources();
        createCompositePipeline();
        initialized = true;
        loggerInfo("WBOITPipeline: Initialized");
    }

    void WBOITPipeline::cleanup()
    {
        if (!initialized) return;

        vk::Device vkDevice = device.getLogicalDevice();

        if (compositePipeline)
        {
            vkDevice.destroyPipeline(compositePipeline);
            compositePipeline = nullptr;
        }
        if (compositePipelineLayout)
        {
            vkDevice.destroyPipelineLayout(compositePipelineLayout);
            compositePipelineLayout = nullptr;
        }
        if (compositeDescriptorPool)
        {
            vkDevice.destroyDescriptorPool(compositeDescriptorPool);
            compositeDescriptorPool = nullptr;
        }
        if (compositeDescriptorSetLayout)
        {
            vkDevice.destroyDescriptorSetLayout(compositeDescriptorSetLayout);
            compositeDescriptorSetLayout = nullptr;
        }

        for (auto& fb : compositeFramebuffers)
        {
            if (fb) vkDevice.destroyFramebuffer(fb);
        }
        compositeFramebuffers.clear();

        if (compositeRenderPass)
        {
            vkDevice.destroyRenderPass(compositeRenderPass);
            compositeRenderPass = nullptr;
        }

        for (auto& fb : wboitFramebuffers)
        {
            if (fb) vkDevice.destroyFramebuffer(fb);
        }
        wboitFramebuffers.clear();

        if (wboitRenderPass)
        {
            vkDevice.destroyRenderPass(wboitRenderPass);
            wboitRenderPass = nullptr;
        }

        cleanupRenderTargets();

        if (linearSampler)
        {
            vkDevice.destroySampler(linearSampler);
            linearSampler = nullptr;
        }

        initialized = false;
    }

    void WBOITPipeline::recreate()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        for (auto& fb : compositeFramebuffers)
        {
            if (fb) vkDevice.destroyFramebuffer(fb);
        }
        compositeFramebuffers.clear();

        for (auto& fb : wboitFramebuffers)
        {
            if (fb) vkDevice.destroyFramebuffer(fb);
        }
        wboitFramebuffers.clear();

        if (compositePipeline)
        {
            vkDevice.destroyPipeline(compositePipeline);
            compositePipeline = nullptr;
        }

        cleanupRenderTargets();
        createRenderTargets();
        createWBOITFramebuffers();
        createCompositeFramebuffers();

        // Update descriptor set with new image views
        std::array<vk::DescriptorImageInfo, 2> imageInfos{};
        imageInfos[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfos[0].imageView = accumImageView;
        imageInfos[0].sampler = linearSampler;
        imageInfos[1].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfos[1].imageView = revealageImageView;
        imageInfos[1].sampler = linearSampler;

        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0].dstSet = compositeDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &imageInfos[0];
        writes[1].dstSet = compositeDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &imageInfos[1];

        vkDevice.updateDescriptorSets(writes, {});

        createCompositePipeline();
    }

    void WBOITPipeline::createRenderTargets()
    {
        vk::Device vkDevice = device.getLogicalDevice();
        auto extent = swapChain.getSwapchainExtent();

        // Accumulation target (RGBA16F)
        {
            core::ImageInfoRequest req(vkDevice, device.getPhysicalDevice());
            req.width = extent.width;
            req.height = extent.height;
            req.format = vk::Format::eR16G16B16A16Sfloat;
            req.tiling = vk::ImageTiling::eOptimal;
            req.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
            req.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::ImageUtilities::createImage(req, accumImage, accumMemory);

            core::ImageViewInfoRequest viewReq(vkDevice, accumImage, vk::Format::eR16G16B16A16Sfloat);
            core::ImageUtilities::createImageView(viewReq, accumImageView);
        }

        // Revealage target (R8)
        {
            core::ImageInfoRequest req(vkDevice, device.getPhysicalDevice());
            req.width = extent.width;
            req.height = extent.height;
            req.format = vk::Format::eR8Unorm;
            req.tiling = vk::ImageTiling::eOptimal;
            req.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
            req.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::ImageUtilities::createImage(req, revealageImage, revealageMemory);

            core::ImageViewInfoRequest viewReq(vkDevice, revealageImage, vk::Format::eR8Unorm);
            core::ImageUtilities::createImageView(viewReq, revealageImageView);
        }
    }

    void WBOITPipeline::cleanupRenderTargets()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        if (accumImageView) { vkDevice.destroyImageView(accumImageView); accumImageView = nullptr; }
        if (accumImage) { vkDevice.destroyImage(accumImage); accumImage = nullptr; }
        if (accumMemory) { vkDevice.freeMemory(accumMemory); accumMemory = nullptr; }

        if (revealageImageView) { vkDevice.destroyImageView(revealageImageView); revealageImageView = nullptr; }
        if (revealageImage) { vkDevice.destroyImage(revealageImage); revealageImage = nullptr; }
        if (revealageMemory) { vkDevice.freeMemory(revealageMemory); revealageMemory = nullptr; }
    }

    void WBOITPipeline::createWBOITRenderPass()
    {
        // Attachment 0: Accumulation (RGBA16F) - clear to (0,0,0,0), blend: src=One, dst=One
        vk::AttachmentDescription accumAttachment{};
        accumAttachment.format = vk::Format::eR16G16B16A16Sfloat;
        accumAttachment.samples = vk::SampleCountFlagBits::e1;
        accumAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        accumAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        accumAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        accumAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        accumAttachment.initialLayout = vk::ImageLayout::eUndefined;
        accumAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        // Attachment 1: Revealage (R8) - clear to 1.0, blend: src=Zero, dst=OneMinusSrcColor
        vk::AttachmentDescription revealageAttachment{};
        revealageAttachment.format = vk::Format::eR8Unorm;
        revealageAttachment.samples = vk::SampleCountFlagBits::e1;
        revealageAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        revealageAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        revealageAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        revealageAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        revealageAttachment.initialLayout = vk::ImageLayout::eUndefined;
        revealageAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        // Attachment 2: Depth (read-only from opaque pass)
        vk::AttachmentDescription depthAttachment{};
        depthAttachment.format = swapChain.getSwapchainDepthStencilFormat();
        depthAttachment.samples = vk::SampleCountFlagBits::e1;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
        depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.initialLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference accumRef{0, vk::ImageLayout::eColorAttachmentOptimal};
        vk::AttachmentReference revealageRef{1, vk::ImageLayout::eColorAttachmentOptimal};
        vk::AttachmentReference depthRef{2, vk::ImageLayout::eDepthStencilReadOnlyOptimal};

        std::array<vk::AttachmentReference, 2> colorRefs = {accumRef, revealageRef};

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
        subpass.pColorAttachments = colorRefs.data();
        subpass.pDepthStencilAttachment = &depthRef;

        std::array<vk::AttachmentDescription, 3> attachments = {
            accumAttachment, revealageAttachment, depthAttachment
        };

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        rpInfo.pAttachments = attachments.data();
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;

        wboitRenderPass = device.getLogicalDevice().createRenderPass(rpInfo);
    }

    void WBOITPipeline::createWBOITFramebuffers()
    {
        auto extent = swapChain.getSwapchainExtent();
        uint32_t imageCount = swapChain.getSwapchainImageCount();
        wboitFramebuffers.resize(imageCount);

        for (uint32_t i = 0; i < imageCount; ++i)
        {
            std::array<vk::ImageView, 3> attachments = {
                accumImageView,
                revealageImageView,
                offscreenResources.depthImage.depthImageView
            };

            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = wboitRenderPass;
            fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            fbInfo.pAttachments = attachments.data();
            fbInfo.width = extent.width;
            fbInfo.height = extent.height;
            fbInfo.layers = 1;

            wboitFramebuffers[i] = device.getLogicalDevice().createFramebuffer(fbInfo);
        }
    }

    void WBOITPipeline::createCompositeRenderPass()
    {
        // Single color attachment - loads the existing scene color and blends WBOIT result on top
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChain.getSwapchainImageFormat();
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorRef{0, vk::ImageLayout::eColorAttachmentOptimal};

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;

        compositeRenderPass = device.getLogicalDevice().createRenderPass(rpInfo);
    }

    void WBOITPipeline::createCompositeFramebuffers()
    {
        auto extent = swapChain.getSwapchainExtent();
        uint32_t imageCount = swapChain.getSwapchainImageCount();
        compositeFramebuffers.resize(imageCount);

        for (uint32_t i = 0; i < imageCount; ++i)
        {
            vk::ImageView colorView = offscreenResources.colorImages[i].colorImageView;

            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = compositeRenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &colorView;
            fbInfo.width = extent.width;
            fbInfo.height = extent.height;
            fbInfo.layers = 1;

            compositeFramebuffers[i] = device.getLogicalDevice().createFramebuffer(fbInfo);
        }
    }

    void WBOITPipeline::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;

        linearSampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void WBOITPipeline::createCompositeDescriptorResources()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Descriptor set layout: 2 combined image samplers
        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        compositeDescriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        // Descriptor pool
        vk::DescriptorPoolSize poolSize{vk::DescriptorType::eCombinedImageSampler, 2};
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        compositeDescriptorPool = vkDevice.createDescriptorPool(poolInfo);

        // Allocate descriptor set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = compositeDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &compositeDescriptorSetLayout;
        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        compositeDescriptorSet = sets[0];

        // Update descriptor set
        std::array<vk::DescriptorImageInfo, 2> imageInfos{};
        imageInfos[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfos[0].imageView = accumImageView;
        imageInfos[0].sampler = linearSampler;
        imageInfos[1].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfos[1].imageView = revealageImageView;
        imageInfos[1].sampler = linearSampler;

        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0].dstSet = compositeDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &imageInfos[0];
        writes[1].dstSet = compositeDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &imageInfos[1];

        vkDevice.updateDescriptorSets(writes, {});
    }

    void WBOITPipeline::createCompositePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        core::Shader shader(device);
        shader.readShader("../../resources/shaders/transparency/wboit_composite.glsl");
        const auto& stages = shader.getShaderStages();
        if (stages.size() < 2)
        {
            loggerError("WBOITPipeline: Failed to load composite shader");
            return;
        }

        // Pipeline layout
        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &compositeDescriptorSetLayout;
        compositePipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        // Vertex input (none - fullscreen triangle)
        vk::PipelineVertexInputStateCreateInfo vertexInput{};
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        auto extent = swapChain.getSwapchainExtent();
        vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(extent.width),
                              static_cast<float>(extent.height), 0.0f, 1.0f};
        vk::Rect2D scissor{{0, 0}, extent};
        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eNone;
        rasterizer.frontFace = vk::FrontFace::eCounterClockwise;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;

        // Blend: standard alpha blending to composite WBOIT result onto opaque
        vk::PipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.blendEnable = VK_TRUE;
        blendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        blendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        blendAttachment.colorBlendOp = vk::BlendOp::eAdd;
        blendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha;
        blendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        blendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
        blendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                          vk::ColorComponentFlagBits::eG |
                                          vk::ColorComponentFlagBits::eB |
                                          vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &blendAttachment;

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = compositePipelineLayout;
        pipelineInfo.renderPass = compositeRenderPass;
        pipelineInfo.subpass = 0;

        auto result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            loggerError("WBOITPipeline: Failed to create composite pipeline");
            return;
        }
        compositePipeline = result.value;

        shader.cleanUp();
    }

    void WBOITPipeline::beginWBOITPass(const vk::CommandBuffer& cmd, uint32_t imageIndex)
    {
        // Transition depth to read-only
        vk::ImageMemoryBarrier depthBarrier{};
        depthBarrier.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthBarrier.newLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.image = offscreenResources.depthImage.depthImage;
        depthBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth |
                                                    vk::ImageAspectFlagBits::eStencil;
        depthBarrier.subresourceRange.baseMipLevel = 0;
        depthBarrier.subresourceRange.levelCount = 1;
        depthBarrier.subresourceRange.baseArrayLayer = 0;
        depthBarrier.subresourceRange.layerCount = 1;
        depthBarrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        depthBarrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eLateFragmentTests,
            vk::PipelineStageFlagBits::eEarlyFragmentTests,
            {}, {}, {}, depthBarrier);

        auto extent = swapChain.getSwapchainExtent();

        std::array<vk::ClearValue, 3> clearValues{};
        clearValues[0].color = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
        clearValues[1].color = vk::ClearColorValue(std::array<float, 4>{1.0f, 0.0f, 0.0f, 0.0f});
        clearValues[2].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

        vk::RenderPassBeginInfo rpBegin{};
        rpBegin.renderPass = wboitRenderPass;
        rpBegin.framebuffer = wboitFramebuffers[imageIndex];
        rpBegin.renderArea.extent = extent;
        rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
        rpBegin.pClearValues = clearValues.data();

        cmd.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

        vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(extent.width),
                              static_cast<float>(extent.height), 0.0f, 1.0f};
        cmd.setViewport(0, viewport);
        vk::Rect2D scissor{{0, 0}, extent};
        cmd.setScissor(0, scissor);
    }

    void WBOITPipeline::endWBOITPass(const vk::CommandBuffer& cmd)
    {
        cmd.endRenderPass();

        // Transition depth back to attachment optimal
        vk::ImageMemoryBarrier depthBarrier{};
        depthBarrier.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthBarrier.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.image = offscreenResources.depthImage.depthImage;
        depthBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth |
                                                    vk::ImageAspectFlagBits::eStencil;
        depthBarrier.subresourceRange.baseMipLevel = 0;
        depthBarrier.subresourceRange.levelCount = 1;
        depthBarrier.subresourceRange.baseArrayLayer = 0;
        depthBarrier.subresourceRange.layerCount = 1;
        depthBarrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead;
        depthBarrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                                     vk::AccessFlagBits::eDepthStencilAttachmentWrite;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eLateFragmentTests,
            vk::PipelineStageFlagBits::eEarlyFragmentTests,
            {}, {}, {}, depthBarrier);
    }

    void WBOITPipeline::composite(const vk::CommandBuffer& cmd, uint32_t imageIndex)
    {
        auto extent = swapChain.getSwapchainExtent();

        vk::RenderPassBeginInfo rpBegin{};
        rpBegin.renderPass = compositeRenderPass;
        rpBegin.framebuffer = compositeFramebuffers[imageIndex];
        rpBegin.renderArea.extent = extent;

        cmd.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, compositePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               compositePipelineLayout, 0, compositeDescriptorSet, {});
        cmd.draw(3, 1, 0, 0); // Fullscreen triangle

        cmd.endRenderPass();
    }
}
