#include "CloudComposite.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/ImageUtilities.hpp"

// Windows defines MemoryBarrier as a macro - undefine it to use vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::cloud
{
    CloudComposite::CloudComposite(core::Device& device, core::SwapChain& swapChain,
                                   core::OffscreenResources& offscreenResources)
        : device{device}, swapChain{swapChain}, offscreenResources{offscreenResources}
    {
        vk::Format depthFormat = swapChain.getSwapchainDepthStencilFormat();
        depthAspectMask = vk::ImageAspectFlagBits::eDepth;
        if (depthFormat == vk::Format::eD16UnormS8Uint ||
            depthFormat == vk::Format::eD24UnormS8Uint ||
            depthFormat == vk::Format::eD32SfloatS8Uint)
        {
            depthAspectMask |= vk::ImageAspectFlagBits::eStencil;
        }
    }

    CloudComposite::~CloudComposite()
    {
        cleanup();
    }

    void CloudComposite::init(vk::ImageView cloudResultView, vk::Sampler cloudSampler)
    {
        auto& dev = device.getLogicalDevice();
        vk::Extent2D extent = swapChain.getSwapchainExtent();

        // --- Depth-only image view ---
        {
            vk::ImageViewCreateInfo viewInfo{};
            viewInfo.image = offscreenResources.depthImage.depthImage;
            viewInfo.viewType = vk::ImageViewType::e2D;
            viewInfo.format = swapChain.getSwapchainDepthStencilFormat();
            viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
            depthOnlyImageView = dev.createImageView(viewInfo);
        }

        // --- Sampler for depth (nearest) ---
        {
            vk::SamplerCreateInfo info{};
            info.magFilter = vk::Filter::eNearest;
            info.minFilter = vk::Filter::eNearest;
            info.mipmapMode = vk::SamplerMipmapMode::eNearest;
            info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
            info.anisotropyEnable = VK_FALSE;
            sampler = dev.createSampler(info);
        }

        // --- Render pass ---
        {
            vk::AttachmentDescription colorAttachment{};
            colorAttachment.format = swapChain.getSwapchainImageFormat();
            colorAttachment.samples = vk::SampleCountFlagBits::e1;
            colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
            colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
            colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            colorAttachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
            colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            vk::AttachmentReference colorRef{0, vk::ImageLayout::eColorAttachmentOptimal};
            vk::SubpassDescription subpass{};
            subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorRef;

            vk::SubpassDependency dep{};
            dep.srcSubpass = VK_SUBPASS_EXTERNAL;
            dep.dstSubpass = 0;
            dep.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dep.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dep.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;

            vk::RenderPassCreateInfo rpInfo{};
            rpInfo.attachmentCount = 1; rpInfo.pAttachments = &colorAttachment;
            rpInfo.subpassCount = 1; rpInfo.pSubpasses = &subpass;
            rpInfo.dependencyCount = 1; rpInfo.pDependencies = &dep;

            renderPass = dev.createRenderPass(rpInfo);
        }

        // --- Framebuffers ---
        {
            uint32_t imageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());
            framebuffers.resize(imageCount);
            for (uint32_t i = 0; i < imageCount; ++i)
            {
                vk::FramebufferCreateInfo fbInfo{};
                fbInfo.renderPass = renderPass;
                fbInfo.attachmentCount = 1;
                fbInfo.pAttachments = &offscreenResources.colorImages[i].colorImageView;
                fbInfo.width = extent.width;
                fbInfo.height = extent.height;
                fbInfo.layers = 1;
                framebuffers[i] = dev.createFramebuffer(fbInfo);
            }
        }

        // --- Descriptor set layout: cloud (0), depth (1) ---
        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            bindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};
            bindings[1] = {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();
            dsLayout = dev.createDescriptorSetLayout(layoutInfo);
        }

        // --- Descriptor pool ---
        {
            vk::DescriptorPoolSize poolSize{vk::DescriptorType::eCombinedImageSampler, 2};
            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
            poolInfo.maxSets = 1;
            poolInfo.poolSizeCount = 1;
            poolInfo.pPoolSizes = &poolSize;
            dsPool = dev.createDescriptorPool(poolInfo);
        }

        // --- Allocate descriptor set ---
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = dsPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &dsLayout;
            descriptorSet = dev.allocateDescriptorSets(allocInfo)[0];
        }

        // --- Update descriptor set ---
        {
            vk::DescriptorImageInfo cloudInfo{cloudSampler, cloudResultView, vk::ImageLayout::eShaderReadOnlyOptimal};
            vk::DescriptorImageInfo depthInfo{sampler, depthOnlyImageView, vk::ImageLayout::eDepthStencilReadOnlyOptimal};

            std::array<vk::WriteDescriptorSet, 2> writes{};
            writes[0] = {descriptorSet, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &cloudInfo};
            writes[1] = {descriptorSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &depthInfo};
            dev.updateDescriptorSets(writes, nullptr);
        }

        // --- Pipeline layout with push constants ---
        {
            vk::PushConstantRange pushRange{};
            pushRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
            pushRange.offset = 0;
            pushRange.size = sizeof(CloudCompositePushConstants);

            vk::PipelineLayoutCreateInfo plInfo{};
            plInfo.setLayoutCount = 1;
            plInfo.pSetLayouts = &dsLayout;
            plInfo.pushConstantRangeCount = 1;
            plInfo.pPushConstantRanges = &pushRange;
            pipelineLayout = dev.createPipelineLayout(plInfo);
        }

        // --- Shader ---
        shader = std::make_shared<core::Shader>(device);
        shader->readShader("../../resources/shaders/cloud/cloud_composite.glsl");

        // --- Graphics pipeline ---
        {
            vk::PipelineVertexInputStateCreateInfo vertexInput{};
            vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

            vk::Viewport viewport{0.0f, 0.0f,
                static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
            vk::Rect2D scissor{{0, 0}, extent};
            vk::PipelineViewportStateCreateInfo viewportState{};
            viewportState.viewportCount = 1; viewportState.pViewports = &viewport;
            viewportState.scissorCount = 1; viewportState.pScissors = &scissor;

            vk::PipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.polygonMode = vk::PolygonMode::eFill;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = vk::CullModeFlagBits::eNone;
            vk::PipelineMultisampleStateCreateInfo multisampling{};
            multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
            vk::PipelineDepthStencilStateCreateInfo depthStencil{};
            depthStencil.depthTestEnable = VK_FALSE;
            depthStencil.depthWriteEnable = VK_FALSE;

            // Premultiplied alpha: scene * transmittance + scattering
            // Output: rgb = scattering, a = 1 - transmittance (opacity)
            // result = src.rgb * ONE + dst.rgb * (1 - src.a) = scattering + scene * transmittance
            vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
            colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
            colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eZero;
            colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
            colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            vk::PipelineColorBlendStateCreateInfo blending{};
            blending.attachmentCount = 1; blending.pAttachments = &colorBlendAttachment;

            const auto& stages = shader->getShaderStages();
            vk::GraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
            pipelineInfo.pStages = stages.data();
            pipelineInfo.pVertexInputState = &vertexInput;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pDepthStencilState = &depthStencil;
            pipelineInfo.pColorBlendState = &blending;
            pipelineInfo.layout = pipelineLayout;
            pipelineInfo.renderPass = renderPass;
            graphicsPipeline = dev.createGraphicsPipeline(nullptr, pipelineInfo).value;
        }

        initialized = true;
    }

    void CloudComposite::cleanup()
    {
        if (!initialized)
            return;

        auto& dev = device.getLogicalDevice();

        // Destroy pipeline
        if (graphicsPipeline) { dev.destroyPipeline(graphicsPipeline); graphicsPipeline = nullptr; }
        if (pipelineLayout) { dev.destroyPipelineLayout(pipelineLayout); pipelineLayout = nullptr; }

        // Destroy framebuffers
        for (auto& fb : framebuffers)
        {
            if (fb) { dev.destroyFramebuffer(fb); fb = nullptr; }
        }
        framebuffers.clear();

        // Destroy render pass
        if (renderPass) { dev.destroyRenderPass(renderPass); renderPass = nullptr; }

        // Destroy depth-only view
        if (depthOnlyImageView) { dev.destroyImageView(depthOnlyImageView); depthOnlyImageView = nullptr; }

        // Destroy descriptor pool/layout
        if (dsPool) { dev.destroyDescriptorPool(dsPool); dsPool = nullptr; }
        if (dsLayout) { dev.destroyDescriptorSetLayout(dsLayout); dsLayout = nullptr; }

        // Destroy sampler
        if (sampler) { dev.destroySampler(sampler); sampler = nullptr; }

        // Clean up shader
        if (shader) { shader->cleanUp(); shader.reset(); }

        initialized = false;
    }

    void CloudComposite::recreate(vk::ImageView cloudResultView, vk::Sampler cloudSampler)
    {
        if (!initialized) return;

        auto& dev = device.getLogicalDevice();
        vk::Extent2D extent = swapChain.getSwapchainExtent();

        // Destroy pipeline (not layout — it doesn't depend on swapchain)
        if (graphicsPipeline) { dev.destroyPipeline(graphicsPipeline); graphicsPipeline = nullptr; }

        // Destroy framebuffers
        for (auto& fb : framebuffers)
        {
            if (fb) { dev.destroyFramebuffer(fb); fb = nullptr; }
        }
        framebuffers.clear();

        // Destroy depth-only view
        if (depthOnlyImageView) { dev.destroyImageView(depthOnlyImageView); depthOnlyImageView = nullptr; }

        // Rebuild descriptor pool
        if (dsPool) { dev.destroyDescriptorPool(dsPool); dsPool = nullptr; }

        // Destroy render pass
        if (renderPass) { dev.destroyRenderPass(renderPass); renderPass = nullptr; }

        // --- Recreate render pass ---
        {
            vk::AttachmentDescription colorAttachment{};
            colorAttachment.format = swapChain.getSwapchainImageFormat();
            colorAttachment.samples = vk::SampleCountFlagBits::e1;
            colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
            colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
            colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            colorAttachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
            colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            vk::AttachmentReference colorRef{0, vk::ImageLayout::eColorAttachmentOptimal};
            vk::SubpassDescription subpass{};
            subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorRef;

            vk::SubpassDependency dep{};
            dep.srcSubpass = VK_SUBPASS_EXTERNAL;
            dep.dstSubpass = 0;
            dep.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dep.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dep.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;

            vk::RenderPassCreateInfo rpInfo{};
            rpInfo.attachmentCount = 1; rpInfo.pAttachments = &colorAttachment;
            rpInfo.subpassCount = 1; rpInfo.pSubpasses = &subpass;
            rpInfo.dependencyCount = 1; rpInfo.pDependencies = &dep;

            renderPass = dev.createRenderPass(rpInfo);
        }

        // --- Recreate framebuffers ---
        {
            uint32_t imageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());
            framebuffers.resize(imageCount);
            for (uint32_t i = 0; i < imageCount; ++i)
            {
                vk::FramebufferCreateInfo fbInfo{};
                fbInfo.renderPass = renderPass;
                fbInfo.attachmentCount = 1;
                fbInfo.pAttachments = &offscreenResources.colorImages[i].colorImageView;
                fbInfo.width = extent.width;
                fbInfo.height = extent.height;
                fbInfo.layers = 1;
                framebuffers[i] = dev.createFramebuffer(fbInfo);
            }
        }

        // --- Recreate depth-only view ---
        {
            vk::ImageViewCreateInfo viewInfo{};
            viewInfo.image = offscreenResources.depthImage.depthImage;
            viewInfo.viewType = vk::ImageViewType::e2D;
            viewInfo.format = swapChain.getSwapchainDepthStencilFormat();
            viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
            depthOnlyImageView = dev.createImageView(viewInfo);
        }

        // --- Recreate descriptor set ---
        {
            vk::DescriptorPoolSize poolSize{vk::DescriptorType::eCombinedImageSampler, 2};
            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
            poolInfo.maxSets = 1;
            poolInfo.poolSizeCount = 1;
            poolInfo.pPoolSizes = &poolSize;
            dsPool = dev.createDescriptorPool(poolInfo);

            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = dsPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &dsLayout;
            descriptorSet = dev.allocateDescriptorSets(allocInfo)[0];

            vk::DescriptorImageInfo cloudInfo{cloudSampler, cloudResultView, vk::ImageLayout::eShaderReadOnlyOptimal};
            vk::DescriptorImageInfo depthInfo{sampler, depthOnlyImageView, vk::ImageLayout::eDepthStencilReadOnlyOptimal};

            std::array<vk::WriteDescriptorSet, 2> writes{};
            writes[0] = {descriptorSet, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &cloudInfo};
            writes[1] = {descriptorSet, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &depthInfo};
            dev.updateDescriptorSets(writes, nullptr);
        }

        // --- Recreate graphics pipeline ---
        {
            const auto& stages = shader->getShaderStages();
            vk::PipelineVertexInputStateCreateInfo vertexInput{};
            vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

            vk::Viewport viewport{0.0f, 0.0f,
                static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
            vk::Rect2D scissor{{0, 0}, extent};
            vk::PipelineViewportStateCreateInfo viewportState{};
            viewportState.viewportCount = 1; viewportState.pViewports = &viewport;
            viewportState.scissorCount = 1; viewportState.pScissors = &scissor;

            vk::PipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.polygonMode = vk::PolygonMode::eFill;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = vk::CullModeFlagBits::eNone;
            vk::PipelineMultisampleStateCreateInfo multisampling{};
            multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
            vk::PipelineDepthStencilStateCreateInfo depthStencil{};
            depthStencil.depthTestEnable = VK_FALSE;
            depthStencil.depthWriteEnable = VK_FALSE;

            vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
            colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
            colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eZero;
            colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
            colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            vk::PipelineColorBlendStateCreateInfo blending{};
            blending.attachmentCount = 1; blending.pAttachments = &colorBlendAttachment;

            vk::GraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
            pipelineInfo.pStages = stages.data();
            pipelineInfo.pVertexInputState = &vertexInput;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pDepthStencilState = &depthStencil;
            pipelineInfo.pColorBlendState = &blending;
            pipelineInfo.layout = pipelineLayout;
            pipelineInfo.renderPass = renderPass;
            graphicsPipeline = dev.createGraphicsPipeline(nullptr, pipelineInfo).value;
        }
    }

    void CloudComposite::render(const vk::CommandBuffer& cmd, uint32_t imageIndex,
                                const CloudCompositePushConstants& pushConstants)
    {
        if (!initialized) return;

        vk::Extent2D extent = swapChain.getSwapchainExtent();

        // Transition scene color for attachment
        vk::Image sceneColor = offscreenResources.colorImages[imageIndex].colorImage;
        core::ImageUtilities::transitionImageLayout(cmd, sceneColor,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        // Transition depth for reading
        core::ImageUtilities::transitionImageLayout(cmd,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            depthAspectMask);

        vk::RenderPassBeginInfo rpBegin{};
        rpBegin.renderPass = renderPass;
        rpBegin.framebuffer = framebuffers[imageIndex];
        rpBegin.renderArea.offset = vk::Offset2D{0, 0};
        rpBegin.renderArea.extent = extent;

        cmd.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, descriptorSet, nullptr);
        cmd.pushConstants<CloudCompositePushConstants>(pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, pushConstants);
        cmd.draw(3, 1, 0, 0);
        cmd.endRenderPass();

        // Transition depth back
        core::ImageUtilities::transitionImageLayout(cmd,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            depthAspectMask);
    }
}
