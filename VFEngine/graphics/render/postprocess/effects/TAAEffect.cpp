#include "TAAEffect.hpp"
#include "../PostProcessPipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/OffScreen.hpp"
#include "../../../core/ImageUtilities.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <cstring>

namespace render::postprocess
{
    TAAEffect::TAAEffect(core::Device& device, core::SwapChain& swapChain,
                         core::OffscreenResources& offscreenResources,
                         PostProcessPipeline& pipeline)
        : device{device}, swapChain{swapChain},
          offscreenResources{offscreenResources}, pipeline{pipeline}
    {
        enabled = false;

        vk::Format depthFormat = swapChain.getSwapchainDepthStencilFormat();
        depthAspectMask = vk::ImageAspectFlagBits::eDepth;
        if (depthFormat == vk::Format::eD16UnormS8Uint ||
            depthFormat == vk::Format::eD24UnormS8Uint ||
            depthFormat == vk::Format::eD32SfloatS8Uint)
        {
            depthAspectMask |= vk::ImageAspectFlagBits::eStencil;
        }
    }

    void TAAEffect::init(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        currentExtent = extent;

        createSampler();
        createRenderPass();
        createHistoryBuffers();
        createDepthImageView();
        createParamsBuffer();
        createDescriptorSetLayouts();
        createDescriptorPool();
        createDescriptorSets();
        loadShaders();
        createResolvePipeline();
        createSharpenPipeline(renderPass);

        initialized = true;
    }

    void TAAEffect::cleanup()
    {
        auto& dev = device.getLogicalDevice();

        cleanupPipelines();
        cleanupHistoryBuffers();

        if (depthOnlyImageView)
        {
            dev.destroyImageView(depthOnlyImageView);
            depthOnlyImageView = nullptr;
        }

        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (resolveDescriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(resolveDescriptorSetLayout);
            resolveDescriptorSetLayout = nullptr;
        }

        if (sharpenDescriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(sharpenDescriptorSetLayout);
            sharpenDescriptorSetLayout = nullptr;
        }

        if (taaRenderPass)
        {
            dev.destroyRenderPass(taaRenderPass);
            taaRenderPass = nullptr;
        }

        if (paramsBuffer)
        {
            if (paramsBufferMapped)
            {
                dev.unmapMemory(paramsBufferMemory);
                paramsBufferMapped = nullptr;
            }
            core::BufferUtilities::destroyBuffer(dev, paramsBuffer, paramsBufferMemory);
        }

        if (sampler)
        {
            dev.destroySampler(sampler);
            sampler = nullptr;
        }

        if (resolveShader) { resolveShader->cleanUp(); resolveShader.reset(); }
        if (sharpenShader) { sharpenShader->cleanUp(); sharpenShader.reset(); }

        historyValid = false;
        initialized = false;
    }

    void TAAEffect::recreate(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        currentExtent = extent;
        auto& dev = device.getLogicalDevice();

        cleanupPipelines();
        cleanupHistoryBuffers();

        if (depthOnlyImageView)
        {
            dev.destroyImageView(depthOnlyImageView);
            depthOnlyImageView = nullptr;
        }

        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (taaRenderPass)
        {
            dev.destroyRenderPass(taaRenderPass);
            taaRenderPass = nullptr;
        }

        createRenderPass();
        createHistoryBuffers();
        createDepthImageView();
        createDescriptorPool();
        createDescriptorSets();
        createResolvePipeline();
        createSharpenPipeline(renderPass);

        historyValid = false;
    }

    void TAAEffect::preRecord(const vk::CommandBuffer& commandBuffer,
                               vk::DescriptorSet inputDescriptorSet)
    {
        updateParamsBuffer(inputDescriptorSet);

        // Transition depth to read-only for TAA reprojection.
        // Assumes depth is in eDepthStencilAttachmentOptimal after the geometry pass.
        // TAA runs at priority 5 (first post-process effect), so no prior effect modifies depth layout.
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            depthAspectMask);

        // TAA resolve pass: read from history[currentHistoryIndex], write to history[1 - currentHistoryIndex]
        uint32_t readIdx = currentHistoryIndex;
        uint32_t writeIdx = 1 - currentHistoryIndex;

        // On first frame (or after recreate), history buffers are in UNDEFINED layout.
        // Transition the read buffer so the descriptor binding is valid.
        if (!historyValid)
        {
            core::ImageUtilities::transitionImageLayout(commandBuffer,
                historyBuffers[readIdx].image,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);
        }

        {
            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = taaRenderPass;
            rpBegin.framebuffer = historyBuffers[writeIdx].framebuffer;
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = currentExtent;

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, resolvePipeline);

            std::array<vk::DescriptorSet, 2> sets = {inputDescriptorSet, resolveDescriptorSets[readIdx]};
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                              resolvePipelineLayout, 0,
                                              static_cast<uint32_t>(sets.size()),
                                              sets.data(), 0, nullptr);

            commandBuffer.draw(3, 1, 0, 0);
            commandBuffer.endRenderPass();
        }

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            depthAspectMask);

        // Swap history index so record() reads from the just-written buffer
        currentHistoryIndex = writeIdx;
        historyValid = true;
    }

    void TAAEffect::record(const vk::CommandBuffer& commandBuffer,
                            vk::DescriptorSet inputDescriptorSet)
    {
        // Sharpen pass: read from history[currentHistoryIndex] (just written in preRecord)
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, sharpenPipeline);

        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                          sharpenPipelineLayout, 0,
                                          1, &sharpenDescriptorSets[currentHistoryIndex], 0, nullptr);

        TAASharpenPushConstants pc{};
        pc.texelSizeX = 1.0f / static_cast<float>(currentExtent.width);
        pc.texelSizeY = 1.0f / static_cast<float>(currentExtent.height);
        pc.sharpenStrength = currentSharpenStrength;

        commandBuffer.pushConstants(sharpenPipelineLayout,
                                     vk::ShaderStageFlagBits::eFragment,
                                     0, sizeof(TAASharpenPushConstants), &pc);

        commandBuffer.draw(3, 1, 0, 0);
    }

    void TAAEffect::updateParameters(const ::postprocess::PostProcessSettings& settings)
    {
        const auto& t = settings.taa;
        enabled = t.enabled;
        currentBlendFactor = t.blendFactor;
        currentSharpenStrength = t.sharpenStrength;
        currentUseVarianceClipping = t.useVarianceClipping;
    }

    // ---- Resource creation ----

    void TAAEffect::createSampler()
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

        sampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void TAAEffect::createRenderPass()
    {
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChain.getSwapchainImageFormat();
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

        taaRenderPass = device.getLogicalDevice().createRenderPass(rpInfo);
    }

    void TAAEffect::createHistoryBuffers()
    {
        auto& dev = device.getLogicalDevice();
        vk::Format format = swapChain.getSwapchainImageFormat();

        for (int i = 0; i < 2; i++)
        {
            core::ImageInfoRequest req(dev, device.getPhysicalDevice());
            req.width = currentExtent.width;
            req.height = currentExtent.height;
            req.format = format;
            req.tiling = vk::ImageTiling::eOptimal;
            req.usage = vk::ImageUsageFlagBits::eColorAttachment
                      | vk::ImageUsageFlagBits::eSampled;
            req.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

            core::ImageUtilities::createImage(req, historyBuffers[i].image, historyBuffers[i].memory);

            core::ImageViewInfoRequest viewReq(dev, historyBuffers[i].image);
            viewReq.format = format;
            core::ImageUtilities::createImageView(viewReq, historyBuffers[i].imageView);

            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = taaRenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &historyBuffers[i].imageView;
            fbInfo.width = currentExtent.width;
            fbInfo.height = currentExtent.height;
            fbInfo.layers = 1;

            historyBuffers[i].framebuffer = dev.createFramebuffer(fbInfo);
        }
    }

    void TAAEffect::createDepthImageView()
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

        depthOnlyImageView = device.getLogicalDevice().createImageView(viewInfo);
    }

    void TAAEffect::createParamsBuffer()
    {
        auto& dev = device.getLogicalDevice();

        core::BufferInfoRequest bufReq(dev, device.getPhysicalDevice());
        bufReq.size = sizeof(TAAParamsUBO);
        bufReq.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufReq.properties = vk::MemoryPropertyFlagBits::eHostVisible
                          | vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(bufReq, paramsBuffer, paramsBufferMemory);
        paramsBufferMapped = dev.mapMemory(paramsBufferMemory, 0, sizeof(TAAParamsUBO));
    }

    void TAAEffect::createDescriptorSetLayouts()
    {
        auto& dev = device.getLogicalDevice();

        // Resolve pass set 1: binding 0 = history sampler, binding 1 = depth sampler, binding 2 = UBO
        {
            std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};

            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[2].binding = 2;
            bindings[2].descriptorType = vk::DescriptorType::eUniformBuffer;
            bindings[2].descriptorCount = 1;
            bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            resolveDescriptorSetLayout = dev.createDescriptorSetLayout(layoutInfo);
        }

        // Sharpen pass: binding 0 = resolved TAA color sampler
        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;

            sharpenDescriptorSetLayout = dev.createDescriptorSetLayout(layoutInfo);
        }
    }

    void TAAEffect::createDescriptorPool()
    {
        std::array<vk::DescriptorPoolSize, 2> poolSizes{};

        // 2 resolve sets * (1 history + 1 depth) + 2 sharpen sets * 1 = 6 samplers
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = 6;

        // 2 resolve sets * 1 UBO = 2
        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 2;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = 4; // 2 resolve + 2 sharpen
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void TAAEffect::createDescriptorSets()
    {
        auto& dev = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 4> layouts = {
            resolveDescriptorSetLayout, resolveDescriptorSetLayout,
            sharpenDescriptorSetLayout, sharpenDescriptorSetLayout
        };

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocInfo.pSetLayouts = layouts.data();

        auto sets = dev.allocateDescriptorSets(allocInfo);
        resolveDescriptorSets[0] = sets[0]; // reads from history[0]
        resolveDescriptorSets[1] = sets[1]; // reads from history[1]
        sharpenDescriptorSets[0] = sets[2]; // reads from history[0]
        sharpenDescriptorSets[1] = sets[3]; // reads from history[1]

        // Update resolve descriptor sets
        for (int i = 0; i < 2; i++)
        {
            vk::DescriptorImageInfo historyInfo{};
            historyInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            historyInfo.imageView = historyBuffers[i].imageView;
            historyInfo.sampler = sampler;

            vk::DescriptorImageInfo depthInfo{};
            depthInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
            depthInfo.imageView = depthOnlyImageView;
            depthInfo.sampler = sampler;

            vk::DescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = paramsBuffer;
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(TAAParamsUBO);

            std::array<vk::WriteDescriptorSet, 3> writes{};

            writes[0].dstSet = resolveDescriptorSets[i];
            writes[0].dstBinding = 0;
            writes[0].dstArrayElement = 0;
            writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[0].descriptorCount = 1;
            writes[0].pImageInfo = &historyInfo;

            writes[1].dstSet = resolveDescriptorSets[i];
            writes[1].dstBinding = 1;
            writes[1].dstArrayElement = 0;
            writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &depthInfo;

            writes[2].dstSet = resolveDescriptorSets[i];
            writes[2].dstBinding = 2;
            writes[2].dstArrayElement = 0;
            writes[2].descriptorType = vk::DescriptorType::eUniformBuffer;
            writes[2].descriptorCount = 1;
            writes[2].pBufferInfo = &bufferInfo;

            dev.updateDescriptorSets(writes, nullptr);
        }

        // Update sharpen descriptor sets
        for (int i = 0; i < 2; i++)
        {
            vk::DescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            imageInfo.imageView = historyBuffers[i].imageView;
            imageInfo.sampler = sampler;

            vk::WriteDescriptorSet write{};
            write.dstSet = sharpenDescriptorSets[i];
            write.dstBinding = 0;
            write.dstArrayElement = 0;
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfo;

            dev.updateDescriptorSets(write, nullptr);
        }
    }

    void TAAEffect::loadShaders()
    {
        resolveShader = std::make_shared<core::Shader>(device);
        resolveShader->readShader("../../resources/shaders/postprocess/taa_resolve.glsl");

        sharpenShader = std::make_shared<core::Shader>(device);
        sharpenShader->readShader("../../resources/shaders/postprocess/taa_sharpen.glsl");
    }

    void TAAEffect::createResolvePipeline()
    {
        auto& dev = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {
            pipeline.getInputDescriptorSetLayout(), resolveDescriptorSetLayout
        };

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();

        resolvePipelineLayout = dev.createPipelineLayout(layoutInfo);

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        vk::Viewport viewport{};
        viewport.width = static_cast<float>(currentExtent.width);
        viewport.height = static_cast<float>(currentExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.extent = currentExtent;

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

        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR
                                            | vk::ColorComponentFlagBits::eG
                                            | vk::ColorComponentFlagBits::eB
                                            | vk::ColorComponentFlagBits::eA;
        colorBlendAttachment.blendEnable = VK_FALSE;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        const auto& stages = resolveShader->getShaderStages();

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
        pipelineInfo.layout = resolvePipelineLayout;
        pipelineInfo.renderPass = taaRenderPass;
        pipelineInfo.subpass = 0;

        resolvePipeline = dev.createGraphicsPipeline(nullptr, pipelineInfo).value;
    }

    void TAAEffect::createSharpenPipeline(vk::RenderPass externalRenderPass)
    {
        core::GraphicsPipelineConfig config{};
        config.device = device.getLogicalDevice();
        config.renderPass = externalRenderPass;
        config.extent = currentExtent;
        config.shaderStages = sharpenShader->getShaderStages();
        config.descriptorSetLayouts = {sharpenDescriptorSetLayout};
        config.pushConstantSize = sizeof(TAASharpenPushConstants);
        config.pushConstantStages = vk::ShaderStageFlagBits::eFragment;
        config.depthTestEnable = false;
        config.depthWriteEnable = false;
        config.blendEnable = false;
        config.cullMode = vk::CullModeFlagBits::eNone;

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        sharpenPipeline = result.pipeline;
        sharpenPipelineLayout = result.pipelineLayout;
    }

    void TAAEffect::cleanupHistoryBuffers()
    {
        auto& dev = device.getLogicalDevice();

        for (int i = 0; i < 2; i++)
        {
            auto& buf = historyBuffers[i];
            if (buf.framebuffer) { dev.destroyFramebuffer(buf.framebuffer); buf.framebuffer = nullptr; }
            if (buf.imageView) { dev.destroyImageView(buf.imageView); buf.imageView = nullptr; }
            if (buf.image) { dev.destroyImage(buf.image); buf.image = nullptr; }
            if (buf.memory) { dev.freeMemory(buf.memory); buf.memory = nullptr; }
        }
    }

    void TAAEffect::cleanupPipelines()
    {
        auto& dev = device.getLogicalDevice();

        auto destroyPipeline = [&](vk::Pipeline& p, vk::PipelineLayout& pl) {
            if (p) { dev.destroyPipeline(p); p = nullptr; }
            if (pl) { dev.destroyPipelineLayout(pl); pl = nullptr; }
        };

        destroyPipeline(resolvePipeline, resolvePipelineLayout);
        destroyPipeline(sharpenPipeline, sharpenPipelineLayout);
    }

    void TAAEffect::updateParamsBuffer(vk::DescriptorSet inputDescriptorSet)
    {
        const auto& camInfo = pipeline.getCameraData();

        glm::mat4 jitteredVP = camInfo.projectionMatrix * camInfo.viewMatrix;
        glm::mat4 prevUnjitteredVP = camInfo.prevProjectionMatrix * camInfo.prevViewMatrix;

        TAAParamsUBO data{};
        data.invViewProjection = glm::inverse(jitteredVP);
        data.prevViewProjection = prevUnjitteredVP;
        data.jitterOffset = camInfo.jitterOffset;
        data.texelSize = glm::vec2(1.0f / static_cast<float>(currentExtent.width),
                                    1.0f / static_cast<float>(currentExtent.height));
        data.blendFactor = currentBlendFactor;
        data.frameIndex = camInfo.frameIndex;
        data.useVarianceClipping = currentUseVarianceClipping ? 1u : 0u;
        data.historyValid = historyValid ? 1u : 0u;

        std::memcpy(paramsBufferMapped, &data, sizeof(TAAParamsUBO));
    }
}
