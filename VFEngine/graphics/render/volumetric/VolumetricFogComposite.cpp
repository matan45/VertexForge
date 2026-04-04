#include "VolumetricFogComposite.hpp"
#include "VolumetricPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include <cstring>

namespace render::volumetric
{
    VolumetricFogComposite::VolumetricFogComposite(core::Device& device, core::SwapChain& swapChain,
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

    VolumetricFogComposite::~VolumetricFogComposite()
    {
        cleanup();
    }

    void VolumetricFogComposite::init(VolumetricPipeline* volPipeline)
    {
        volumetricPipeline = volPipeline;
        currentExtent = swapChain.getSwapchainExtent();

        createSampler();
        createRenderPass();
        createFramebuffers();
        createDepthImageView();
        createParamsBuffer();
        createDescriptorSetLayout();
        createDescriptorPool();
        createDescriptorSet();
        loadShader();
        createPipeline();

        initialized = true;
    }

    void VolumetricFogComposite::cleanup()
    {
        if (!initialized)
            return;

        auto& dev = device.getLogicalDevice();

        cleanupPipeline();
        cleanupFramebuffers();

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

        if (descriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        if (renderPass)
        {
            dev.destroyRenderPass(renderPass);
            renderPass = nullptr;
        }

        if (paramsBuffer)
        {
            paramsBufferMapped = nullptr;
            core::BufferUtilities::destroyBuffer(dev, paramsBuffer, paramsBufferAllocation, device.getMemoryManager());
        }

        if (sampler)
        {
            dev.destroySampler(sampler);
            sampler = nullptr;
        }

        if (shader)
        {
            shader->cleanUp();
            shader.reset();
        }

        initialized = false;
    }

    void VolumetricFogComposite::recreate()
    {
        if (!initialized)
            return;

        auto& dev = device.getLogicalDevice();
        currentExtent = swapChain.getSwapchainExtent();

        cleanupPipeline();
        cleanupFramebuffers();

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

        if (renderPass)
        {
            dev.destroyRenderPass(renderPass);
            renderPass = nullptr;
        }

        createRenderPass();
        createFramebuffers();
        createDepthImageView();
        createDescriptorPool();
        createDescriptorSet();
        createPipeline();
    }

    void VolumetricFogComposite::execute(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        if (!initialized || !volumetricPipeline || !volumetricPipeline->isEnabled())
            return;

        updateParamsBuffer();

        vk::Image sceneColor = offscreenResources.colorImages[imageIndex].colorImage;
        core::ImageUtilities::transitionImageLayout(commandBuffer, sceneColor,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            depthAspectMask);

        vk::RenderPassBeginInfo rpBegin{};
        rpBegin.renderPass = renderPass;
        rpBegin.framebuffer = framebuffers[imageIndex];
        rpBegin.renderArea.offset = vk::Offset2D{0, 0};
        rpBegin.renderArea.extent = currentExtent;

        commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                          pipelineLayout, 0, descriptorSet, nullptr);
        commandBuffer.draw(3, 1, 0, 0);

        commandBuffer.endRenderPass();

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            depthAspectMask);

        // Scene color is now in eShaderReadOnlyOptimal (render pass finalLayout)
    }

    void VolumetricFogComposite::executeGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        if (!initialized || !volumetricPipeline || !volumetricPipeline->isEnabled())
            return;

        updateParamsBuffer();

        // Scene color and depth transitions handled by render graph

        vk::RenderPassBeginInfo rpBegin{};
        rpBegin.renderPass = renderPass;
        rpBegin.framebuffer = framebuffers[imageIndex];
        rpBegin.renderArea.offset = vk::Offset2D{0, 0};
        rpBegin.renderArea.extent = currentExtent;

        commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                          pipelineLayout, 0, descriptorSet, nullptr);
        commandBuffer.draw(3, 1, 0, 0);

        commandBuffer.endRenderPass();
    }

    void VolumetricFogComposite::createRenderPass()
    {
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChain.getSceneColorFormat();
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
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
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead
                                 | vk::AccessFlagBits::eColorAttachmentWrite;

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;

        renderPass = device.getLogicalDevice().createRenderPass(rpInfo);
    }

    void VolumetricFogComposite::createFramebuffers()
    {
        uint32_t imageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());
        framebuffers.resize(imageCount);

        for (uint32_t i = 0; i < imageCount; ++i)
        {
            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = renderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &offscreenResources.colorImages[i].colorImageView;
            fbInfo.width = currentExtent.width;
            fbInfo.height = currentExtent.height;
            fbInfo.layers = 1;

            framebuffers[i] = device.getLogicalDevice().createFramebuffer(fbInfo);
        }
    }

    void VolumetricFogComposite::createSampler()
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

    void VolumetricFogComposite::createDepthImageView()
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

    void VolumetricFogComposite::createParamsBuffer()
    {
        auto& dev = device.getLogicalDevice();

        core::BufferInfoRequest bufReq(dev, device.getPhysicalDevice());
        bufReq.size = sizeof(VolumetricCompositeParams);
        bufReq.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufReq.properties = vk::MemoryPropertyFlagBits::eHostVisible
                          | vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(bufReq, paramsBuffer, paramsBufferAllocation, device.getMemoryManager());
        paramsBufferMapped = paramsBufferAllocation.mappedPtr;
    }

    void VolumetricFogComposite::createDescriptorSetLayout()
    {
        std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};

        // Binding 0: depth texture
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

        // Binding 1: 3D integrated volume
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

        // Binding 2: params UBO
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void VolumetricFogComposite::createDescriptorPool()
    {
        std::array<vk::DescriptorPoolSize, 2> poolSizes{};

        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = 2; // depth + 3D volume

        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void VolumetricFogComposite::createDescriptorSet()
    {
        auto& dev = device.getLogicalDevice();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        descriptorSet = dev.allocateDescriptorSets(allocInfo)[0];
        vk::DescriptorImageInfo depthImageInfo{};
        depthImageInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        depthImageInfo.imageView = depthOnlyImageView;
        depthImageInfo.sampler = sampler;

        vk::DescriptorImageInfo volumeImageInfo{};
        volumeImageInfo.imageLayout = vk::ImageLayout::eGeneral;
        if (volumetricPipeline)
        {
            volumeImageInfo.imageView = volumetricPipeline->getIntegratedVolumeImageView();
            volumeImageInfo.sampler = volumetricPipeline->getVolumeSampler();
        }
        else
        {
            volumeImageInfo.imageView = depthOnlyImageView;
            volumeImageInfo.sampler = sampler;
        }

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = paramsBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(VolumetricCompositeParams);

        std::array<vk::WriteDescriptorSet, 3> writes{};

        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &depthImageInfo;

        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &volumeImageInfo;

        writes[2].dstSet = descriptorSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &bufferInfo;

        dev.updateDescriptorSets(writes, nullptr);
    }

    void VolumetricFogComposite::loadShader()
    {
        shader = std::make_shared<core::Shader>(device);
        shader->readShader("../../resources/shaders/postprocess/volumetric_fog_sample.glsl");
    }

    void VolumetricFogComposite::createPipeline()
    {
        auto& dev = device.getLogicalDevice();

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;

        pipelineLayout = dev.createPipelineLayout(layoutInfo);

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

        // Alpha blending: result = src.rgb * ONE + dst.rgb * src.alpha
        // Fragment outputs vec4(inScattered, transmittance)
        // => result = inScattered + sceneColor * transmittance
        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eOne;
        colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
        colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eZero;
        colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOne;
        colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                              vk::ColorComponentFlagBits::eG |
                                              vk::ColorComponentFlagBits::eB |
                                              vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        const auto& stages = shader->getShaderStages();

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
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        pipeline = dev.createGraphicsPipeline(nullptr, pipelineInfo).value;
    }

    void VolumetricFogComposite::updateParamsBuffer()
    {
        if (!paramsBufferMapped || !volumetricPipeline)
            return;

        const auto& dims = volumetricPipeline->getDimensions();

        VolumetricCompositeParams params{};
        params.nearPlane = cachedNear;
        params.farPlane = cachedFar;
        params.intensity = currentIntensity;
        params.gridWidth = dims.width;
        params.gridHeight = dims.height;
        params.gridDepth = dims.depth;

        std::memcpy(paramsBufferMapped, &params, sizeof(VolumetricCompositeParams));
    }

    void VolumetricFogComposite::cleanupFramebuffers()
    {
        auto& dev = device.getLogicalDevice();
        for (auto& fb : framebuffers)
        {
            if (fb)
            {
                dev.destroyFramebuffer(fb);
                fb = nullptr;
            }
        }
        framebuffers.clear();
    }

    void VolumetricFogComposite::cleanupPipeline()
    {
        auto& dev = device.getLogicalDevice();

        if (pipeline)
        {
            dev.destroyPipeline(pipeline);
            pipeline = nullptr;
        }

        if (pipelineLayout)
        {
            dev.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }
    }
}
