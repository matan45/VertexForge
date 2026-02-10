#include "BloomEffect.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/ImageUtilities.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include <algorithm>
#include <cmath>

namespace render::postprocess
{
    struct BloomDownsamplePC
    {
        float texelSizeX;
        float texelSizeY;
        float threshold;
        uint32_t isFirstPass;
    };

    struct BloomUpsamplePC
    {
        float radius;
    };

    struct BloomCompositePC
    {
        float intensity;
    };

    BloomEffect::BloomEffect(core::Device& device)
        : device{device}
    {
        enabled = false;
    }

    void BloomEffect::init(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        currentExtent = extent;

        createSampler();
        createRenderPasses();
        createDescriptorSetLayout();
        createMipChain();
        createDescriptorPool();
        createDescriptorSets();
        loadShaders();
        createBloomPipelines();
        createCompositePipeline(renderPass);

        initialized = true;
    }

    void BloomEffect::cleanup()
    {
        auto& dev = device.getLogicalDevice();

        cleanupPipelines();
        cleanupMipChain();

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

        if (downsampleRenderPass)
        {
            dev.destroyRenderPass(downsampleRenderPass);
            downsampleRenderPass = nullptr;
        }

        if (upsampleRenderPass)
        {
            dev.destroyRenderPass(upsampleRenderPass);
            upsampleRenderPass = nullptr;
        }

        if (bloomSampler)
        {
            dev.destroySampler(bloomSampler);
            bloomSampler = nullptr;
        }

        if (downsampleShader)
        {
            downsampleShader->cleanUp();
            downsampleShader.reset();
        }

        if (upsampleShader)
        {
            upsampleShader->cleanUp();
            upsampleShader.reset();
        }

        if (compositeShader)
        {
            compositeShader->cleanUp();
            compositeShader.reset();
        }

        initialized = false;
    }

    void BloomEffect::recreate(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        currentExtent = extent;
        auto& dev = device.getLogicalDevice();

        cleanupPipelines();
        cleanupMipChain();

        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (downsampleRenderPass)
        {
            dev.destroyRenderPass(downsampleRenderPass);
            downsampleRenderPass = nullptr;
        }

        if (upsampleRenderPass)
        {
            dev.destroyRenderPass(upsampleRenderPass);
            upsampleRenderPass = nullptr;
        }

        createRenderPasses();
        createMipChain();
        createDescriptorPool();
        createDescriptorSets();
        createBloomPipelines();
        createCompositePipeline(renderPass);
    }

    void BloomEffect::preRecord(const vk::CommandBuffer& commandBuffer,
                                 vk::DescriptorSet inputDescriptorSet)
    {
        if (mipLevels.empty())
            return;

        // === Phase 1: Downsample ===
        for (uint32_t i = 0; i < mipCount; ++i)
        {
            auto& mip = mipLevels[i];

            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = downsampleRenderPass;
            rpBegin.framebuffer = mip.framebuffer;
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = vk::Extent2D{mip.width, mip.height};

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, downsamplePipeline);

            // Set dynamic viewport and scissor for this mip level
            vk::Viewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(mip.width);
            viewport.height = static_cast<float>(mip.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            commandBuffer.setViewport(0, viewport);

            vk::Rect2D scissor{};
            scissor.offset = vk::Offset2D{0, 0};
            scissor.extent = vk::Extent2D{mip.width, mip.height};
            commandBuffer.setScissor(0, scissor);

            // Bind input: first pass reads scene, subsequent read previous mip
            vk::DescriptorSet input = (i == 0)
                ? inputDescriptorSet
                : mipLevels[i - 1].descriptorSet;
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                              bloomPipelineLayout, 0, input, nullptr);

            // Source dimensions for texel size
            uint32_t srcWidth = (i == 0) ? currentExtent.width : mipLevels[i - 1].width;
            uint32_t srcHeight = (i == 0) ? currentExtent.height : mipLevels[i - 1].height;

            BloomDownsamplePC pc{};
            pc.texelSizeX = 1.0f / static_cast<float>(srcWidth);
            pc.texelSizeY = 1.0f / static_cast<float>(srcHeight);
            pc.threshold = currentThreshold;
            pc.isFirstPass = (i == 0) ? 1u : 0u;

            commandBuffer.pushConstants(bloomPipelineLayout, vk::ShaderStageFlagBits::eFragment,
                                         0, sizeof(BloomDownsamplePC), &pc);

            commandBuffer.draw(3, 1, 0, 0);
            commandBuffer.endRenderPass();
        }

        // === Phase 2: Upsample (with additive blend) ===
        // Start from second-to-last mip, upsample onto the mip above
        for (int32_t i = static_cast<int32_t>(mipCount) - 2; i >= 0; --i)
        {
            auto& mip = mipLevels[i];

            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = upsampleRenderPass;
            rpBegin.framebuffer = mip.framebuffer;
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = vk::Extent2D{mip.width, mip.height};

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, upsamplePipeline);

            vk::Viewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(mip.width);
            viewport.height = static_cast<float>(mip.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            commandBuffer.setViewport(0, viewport);

            vk::Rect2D scissor{};
            scissor.offset = vk::Offset2D{0, 0};
            scissor.extent = vk::Extent2D{mip.width, mip.height};
            commandBuffer.setScissor(0, scissor);

            // Read from the smaller mip (i+1)
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                              bloomPipelineLayout, 0,
                                              mipLevels[i + 1].descriptorSet, nullptr);

            BloomUpsamplePC pc{};
            pc.radius = currentRadius;

            commandBuffer.pushConstants(bloomPipelineLayout, vk::ShaderStageFlagBits::eFragment,
                                         0, sizeof(BloomUpsamplePC), &pc);

            commandBuffer.draw(3, 1, 0, 0);
            commandBuffer.endRenderPass();
        }
    }

    void BloomEffect::record(const vk::CommandBuffer& commandBuffer,
                              vk::DescriptorSet inputDescriptorSet)
    {
        if (mipLevels.empty())
        {
            // Fallback: just pass through
            return;
        }

        // Phase 3: Composite — scene + bloom
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, compositePipeline);

        std::array<vk::DescriptorSet, 2> sets = {inputDescriptorSet, mipLevels[0].descriptorSet};
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                          compositePipelineLayout, 0,
                                          static_cast<uint32_t>(sets.size()),
                                          sets.data(), 0, nullptr);

        BloomCompositePC pc{};
        pc.intensity = currentIntensity;

        commandBuffer.pushConstants(compositePipelineLayout, vk::ShaderStageFlagBits::eFragment,
                                     0, sizeof(BloomCompositePC), &pc);

        commandBuffer.draw(3, 1, 0, 0);
    }

    void BloomEffect::updateParameters(const ::postprocess::PostProcessSettings& settings)
    {
        const auto& b = settings.bloom;
        enabled = b.enabled;
        currentThreshold = b.threshold;
        currentIntensity = b.intensity;
        currentRadius = b.radius;
        currentPasses = b.passes;
    }

    // === Resource creation ===

    void BloomEffect::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        bloomSampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void BloomEffect::createRenderPasses()
    {
        auto& dev = device.getLogicalDevice();

        // Downsample render pass: loadOp = DontCare (we overwrite entirely)
        {
            vk::AttachmentDescription colorAttachment{};
            colorAttachment.format = vk::Format::eR8G8B8A8Unorm;
            colorAttachment.samples = vk::SampleCountFlagBits::e1;
            colorAttachment.loadOp = vk::AttachmentLoadOp::eDontCare;
            colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
            colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
            colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            vk::AttachmentReference colorRef{};
            colorRef.attachment = 0;
            colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

            vk::SubpassDescription subpass{};
            subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorRef;

            vk::SubpassDependency dependency{};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            dependency.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
            dependency.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            vk::RenderPassCreateInfo rpInfo{};
            rpInfo.attachmentCount = 1;
            rpInfo.pAttachments = &colorAttachment;
            rpInfo.subpassCount = 1;
            rpInfo.pSubpasses = &subpass;
            rpInfo.dependencyCount = 1;
            rpInfo.pDependencies = &dependency;

            downsampleRenderPass = dev.createRenderPass(rpInfo);
        }

        // Upsample render pass: loadOp = Load (preserve downsample content for additive blend)
        {
            vk::AttachmentDescription colorAttachment{};
            colorAttachment.format = vk::Format::eR8G8B8A8Unorm;
            colorAttachment.samples = vk::SampleCountFlagBits::e1;
            colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
            colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
            colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            colorAttachment.initialLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

            vk::AttachmentReference colorRef{};
            colorRef.attachment = 0;
            colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

            vk::SubpassDescription subpass{};
            subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorRef;

            vk::SubpassDependency dependency{};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            dependency.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
            dependency.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            vk::RenderPassCreateInfo rpInfo{};
            rpInfo.attachmentCount = 1;
            rpInfo.pAttachments = &colorAttachment;
            rpInfo.subpassCount = 1;
            rpInfo.pSubpasses = &subpass;
            rpInfo.dependencyCount = 1;
            rpInfo.pDependencies = &dependency;

            upsampleRenderPass = dev.createRenderPass(rpInfo);
        }
    }

    void BloomEffect::createDescriptorSetLayout()
    {
        vk::DescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        binding.descriptorCount = 1;
        binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void BloomEffect::createMipChain()
    {
        auto& dev = device.getLogicalDevice();

        uint32_t w = currentExtent.width / 2;
        uint32_t h = currentExtent.height / 2;

        // Cap mip count
        uint32_t maxMips = static_cast<uint32_t>(std::floor(std::log2(
            static_cast<float>(std::max(w, h))))) + 1;
        mipCount = std::min(currentPasses, maxMips);
        mipCount = std::max(mipCount, 1u);

        // Create image with N mip levels
        core::ImageInfoRequest req(dev, device.getPhysicalDevice());
        req.width = w;
        req.height = h;
        req.mipLevels = mipCount;
        req.format = vk::Format::eR8G8B8A8Unorm;
        req.tiling = vk::ImageTiling::eOptimal;
        req.usage = vk::ImageUsageFlagBits::eColorAttachment
                  | vk::ImageUsageFlagBits::eSampled;
        req.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::ImageUtilities::createImage(req, bloomImage, bloomMemory);

        // Create per-mip image views and framebuffers
        mipLevels.resize(mipCount);
        uint32_t mipW = w;
        uint32_t mipH = h;

        for (uint32_t i = 0; i < mipCount; ++i)
        {
            mipLevels[i].width = mipW;
            mipLevels[i].height = mipH;

            // Create image view for this specific mip level
            vk::ImageViewCreateInfo viewInfo{};
            viewInfo.image = bloomImage;
            viewInfo.viewType = vk::ImageViewType::e2D;
            viewInfo.format = vk::Format::eR8G8B8A8Unorm;
            viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            viewInfo.subresourceRange.baseMipLevel = i;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            mipLevels[i].imageView = dev.createImageView(viewInfo);

            // Create framebuffer for this mip (compatible with both render passes)
            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = downsampleRenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &mipLevels[i].imageView;
            fbInfo.width = mipW;
            fbInfo.height = mipH;
            fbInfo.layers = 1;

            mipLevels[i].framebuffer = dev.createFramebuffer(fbInfo);

            mipW = std::max(1u, mipW / 2);
            mipH = std::max(1u, mipH / 2);
        }
    }

    void BloomEffect::createDescriptorPool()
    {
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eCombinedImageSampler;
        poolSize.descriptorCount = mipCount;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = mipCount;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void BloomEffect::createDescriptorSets()
    {
        std::vector<vk::DescriptorSetLayout> layouts(mipCount, descriptorSetLayout);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = mipCount;
        allocInfo.pSetLayouts = layouts.data();

        auto sets = device.getLogicalDevice().allocateDescriptorSets(allocInfo);

        for (uint32_t i = 0; i < mipCount; ++i)
        {
            mipLevels[i].descriptorSet = sets[i];
            updateDescriptorSet(mipLevels[i].descriptorSet, mipLevels[i].imageView);
        }
    }

    void BloomEffect::updateDescriptorSet(vk::DescriptorSet set, vk::ImageView imageView)
    {
        vk::DescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        imageInfo.imageView = imageView;
        imageInfo.sampler = bloomSampler;

        vk::WriteDescriptorSet write{};
        write.dstSet = set;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;

        device.getLogicalDevice().updateDescriptorSets(write, nullptr);
    }

    void BloomEffect::loadShaders()
    {
        downsampleShader = std::make_shared<core::Shader>(device);
        downsampleShader->readShader("../../resources/shaders/postprocess/bloom_downsample.glsl");

        upsampleShader = std::make_shared<core::Shader>(device);
        upsampleShader->readShader("../../resources/shaders/postprocess/bloom_upsample.glsl");

        compositeShader = std::make_shared<core::Shader>(device);
        compositeShader->readShader("../../resources/shaders/postprocess/bloom_composite.glsl");
    }

    void BloomEffect::createBloomPipelines()
    {
        auto& dev = device.getLogicalDevice();

        // Shared pipeline layout for downsample/upsample: 1 descriptor set + push constants
        {
            vk::PushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
            pushConstantRange.offset = 0;
            pushConstantRange.size = sizeof(BloomDownsamplePC); // Largest push constant

            vk::PipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &descriptorSetLayout;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushConstantRange;

            bloomPipelineLayout = dev.createPipelineLayout(layoutInfo);
        }

        // Common state for both pipelines
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        // Dynamic viewport and scissor
        std::array<vk::DynamicState, 2> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

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

        // Downsample pipeline: no blending
        {
            vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                                  vk::ColorComponentFlagBits::eG |
                                                  vk::ColorComponentFlagBits::eB |
                                                  vk::ColorComponentFlagBits::eA;
            colorBlendAttachment.blendEnable = VK_FALSE;

            vk::PipelineColorBlendStateCreateInfo colorBlending{};
            colorBlending.attachmentCount = 1;
            colorBlending.pAttachments = &colorBlendAttachment;

            const auto& stages = downsampleShader->getShaderStages();

            vk::GraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
            pipelineInfo.pStages = stages.data();
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pDepthStencilState = &depthStencil;
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.pDynamicState = &dynamicState;
            pipelineInfo.layout = bloomPipelineLayout;
            pipelineInfo.renderPass = downsampleRenderPass;
            pipelineInfo.subpass = 0;

            downsamplePipeline = dev.createGraphicsPipeline(nullptr, pipelineInfo).value;
        }

        // Upsample pipeline: additive blending (ONE + ONE)
        {
            vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                                  vk::ColorComponentFlagBits::eG |
                                                  vk::ColorComponentFlagBits::eB |
                                                  vk::ColorComponentFlagBits::eA;
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
            colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;

            vk::PipelineColorBlendStateCreateInfo colorBlending{};
            colorBlending.attachmentCount = 1;
            colorBlending.pAttachments = &colorBlendAttachment;

            const auto& stages = upsampleShader->getShaderStages();

            vk::GraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
            pipelineInfo.pStages = stages.data();
            pipelineInfo.pVertexInputState = &vertexInputInfo;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pDepthStencilState = &depthStencil;
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.pDynamicState = &dynamicState;
            pipelineInfo.layout = bloomPipelineLayout;
            pipelineInfo.renderPass = upsampleRenderPass;
            pipelineInfo.subpass = 0;

            upsamplePipeline = dev.createGraphicsPipeline(nullptr, pipelineInfo).value;
        }
    }

    void BloomEffect::createCompositePipeline(vk::RenderPass externalRenderPass)
    {
        // Composite uses 2 descriptor sets: scene (set 0) + bloom (set 1)
        std::array<vk::DescriptorSetLayout, 2> setLayouts = {
            descriptorSetLayout, descriptorSetLayout
        };

        core::GraphicsPipelineConfig config{};
        config.device = device.getLogicalDevice();
        config.renderPass = externalRenderPass;
        config.extent = currentExtent;
        config.shaderStages = compositeShader->getShaderStages();
        config.descriptorSetLayouts = {setLayouts.begin(), setLayouts.end()};
        config.pushConstantSize = sizeof(BloomCompositePC);
        config.pushConstantStages = vk::ShaderStageFlagBits::eFragment;
        config.depthTestEnable = false;
        config.depthWriteEnable = false;
        config.blendEnable = false;
        config.cullMode = vk::CullModeFlagBits::eNone;

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        compositePipeline = result.pipeline;
        compositePipelineLayout = result.pipelineLayout;
    }

    void BloomEffect::cleanupMipChain()
    {
        auto& dev = device.getLogicalDevice();

        for (auto& mip : mipLevels)
        {
            if (mip.framebuffer)
                dev.destroyFramebuffer(mip.framebuffer);
            if (mip.imageView)
                dev.destroyImageView(mip.imageView);
        }
        mipLevels.clear();

        if (bloomImage)
        {
            dev.destroyImage(bloomImage);
            bloomImage = nullptr;
        }
        if (bloomMemory)
        {
            dev.freeMemory(bloomMemory);
            bloomMemory = nullptr;
        }

        mipCount = 0;
    }

    void BloomEffect::cleanupPipelines()
    {
        auto& dev = device.getLogicalDevice();

        if (downsamplePipeline)
        {
            dev.destroyPipeline(downsamplePipeline);
            downsamplePipeline = nullptr;
        }

        if (upsamplePipeline)
        {
            dev.destroyPipeline(upsamplePipeline);
            upsamplePipeline = nullptr;
        }

        if (bloomPipelineLayout)
        {
            dev.destroyPipelineLayout(bloomPipelineLayout);
            bloomPipelineLayout = nullptr;
        }

        if (compositePipeline)
        {
            dev.destroyPipeline(compositePipeline);
            compositePipeline = nullptr;
        }

        if (compositePipelineLayout)
        {
            dev.destroyPipelineLayout(compositePipelineLayout);
            compositePipelineLayout = nullptr;
        }
    }
}
