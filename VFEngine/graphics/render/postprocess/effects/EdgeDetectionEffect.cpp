#include "EdgeDetectionEffect.hpp"
#include "../PostProcessPipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/OffScreen.hpp"
#include "../../../core/ImageUtilities.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "../../../core/DynamicRenderingHelpers.hpp"

namespace render::postprocess
{
    EdgeDetectionEffect::EdgeDetectionEffect(core::Device& device, core::SwapChain& swapChain,
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

    void EdgeDetectionEffect::init(vk::Format colorFormat, vk::Extent2D extent)
    {
        currentExtent = extent;

        createSampler();
        createIntermediateImage();
        createDepthImageView();
        createDescriptorSetLayouts();
        createDescriptorPool();
        createDescriptorSets();
        loadShaders();
        createEdgePipeline();
        createCompositePipeline(colorFormat);

        initialized = true;
    }

    void EdgeDetectionEffect::cleanup()
    {
        auto& dev = device.getLogicalDevice();

        cleanupPipelines();
        cleanupIntermediateImage();

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

        if (edgeDescriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(edgeDescriptorSetLayout);
            edgeDescriptorSetLayout = nullptr;
        }

        if (compositeDescriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(compositeDescriptorSetLayout);
            compositeDescriptorSetLayout = nullptr;
        }

        if (sampler)
        {
            dev.destroySampler(sampler);
            sampler = nullptr;
        }

        if (edgeShader) { edgeShader->cleanUp(); edgeShader.reset(); }
        if (compositeShader) { compositeShader->cleanUp(); compositeShader.reset(); }

        initialized = false;
    }

    void EdgeDetectionEffect::recreate(vk::Format colorFormat, vk::Extent2D extent)
    {
        currentExtent = extent;
        auto& dev = device.getLogicalDevice();

        cleanupPipelines();
        cleanupIntermediateImage();

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

        createIntermediateImage();
        createDepthImageView();
        createDescriptorPool();
        createDescriptorSets();
        createEdgePipeline();
        createCompositePipeline(colorFormat);
    }

    void EdgeDetectionEffect::preRecord(const vk::CommandBuffer& commandBuffer,
                                         vk::DescriptorSet inputDescriptorSet)
    {
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            depthAspectMask);

        auto colorAttach = core::colorDontCare(intermediateImageView);

        core::DynamicRenderingInfo dynInfo{};
        dynInfo.extent = currentExtent;
        dynInfo.colorAttachments = {colorAttach};

        core::beginDynamicRendering(commandBuffer, dynInfo);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, edgePipeline);

        std::array<vk::DescriptorSet, 2> sets = {inputDescriptorSet, edgeDescriptorSet};
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                          edgePipelineLayout, 0,
                                          static_cast<uint32_t>(sets.size()),
                                          sets.data(), 0, nullptr);

        const auto& camInfo = pipeline.getCameraData();
        EdgeDetectionPushConstants pc{};
        pc.threshold = currentThreshold;
        pc.edgeWidth = currentEdgeWidth;
        pc.edgeColorR = currentEdgeColor[0];
        pc.edgeColorG = currentEdgeColor[1];
        pc.edgeColorB = currentEdgeColor[2];
        pc.opacity = currentOpacity;
        pc.nearPlane = camInfo.nearPlane;
        pc.farPlane = camInfo.farPlane;

        commandBuffer.pushConstants(edgePipelineLayout,
                                     vk::ShaderStageFlagBits::eFragment,
                                     0, sizeof(EdgeDetectionPushConstants), &pc);

        commandBuffer.draw(3, 1, 0, 0);
        render::FrameDrawStats::count();
        core::endDynamicRendering(commandBuffer);

        // Transition intermediate image to shader read for composite pass
        core::ImageUtilities::transitionImageLayout(commandBuffer, intermediateImage,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            depthAspectMask);
    }

    void EdgeDetectionEffect::record(const vk::CommandBuffer& commandBuffer,
                                      vk::DescriptorSet inputDescriptorSet)
    {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, compositePipeline);

        std::array<vk::DescriptorSet, 2> sets = {inputDescriptorSet, compositeDescriptorSet};
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                          compositePipelineLayout, 0,
                                          static_cast<uint32_t>(sets.size()),
                                          sets.data(), 0, nullptr);

        commandBuffer.draw(3, 1, 0, 0);
        render::FrameDrawStats::count();
    }

    void EdgeDetectionEffect::updateParameters(const ::postprocess::PostProcessSettings& settings)
    {
        const auto& e = settings.edgeDetection;
        enabled = e.enabled;
        currentThreshold = e.threshold;
        currentEdgeWidth = e.edgeWidth;
        currentEdgeColor[0] = e.edgeColor[0];
        currentEdgeColor[1] = e.edgeColor[1];
        currentEdgeColor[2] = e.edgeColor[2];
        currentOpacity = e.opacity;
    }

    void EdgeDetectionEffect::createSampler()
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

    void EdgeDetectionEffect::createIntermediateImage()
    {
        auto& dev = device.getLogicalDevice();

        core::ImageInfoRequest req(dev, device.getPhysicalDevice());
        req.width = currentExtent.width;
        req.height = currentExtent.height;
        req.format = vk::Format::eR8G8B8A8Unorm;
        req.tiling = vk::ImageTiling::eOptimal;
        req.usage = vk::ImageUsageFlagBits::eColorAttachment
                  | vk::ImageUsageFlagBits::eSampled;
        req.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::ImageUtilities::createImage(req, intermediateImage, intermediateAllocation, device.getMemoryManager());

        core::ImageViewInfoRequest viewReq(dev, intermediateImage);
        viewReq.format = vk::Format::eR8G8B8A8Unorm;
        core::ImageUtilities::createImageView(viewReq, intermediateImageView);
    }

    void EdgeDetectionEffect::createDepthImageView()
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

    void EdgeDetectionEffect::createDescriptorSetLayouts()
    {
        auto& dev = device.getLogicalDevice();

        // Edge pass: binding 0 = depth sampler
        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;

            edgeDescriptorSetLayout = dev.createDescriptorSetLayout(layoutInfo);
        }

        // Composite pass: binding 0 = intermediate image sampler
        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;

            compositeDescriptorSetLayout = dev.createDescriptorSetLayout(layoutInfo);
        }
    }

    void EdgeDetectionEffect::createDescriptorPool()
    {
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eCombinedImageSampler;
        poolSize.descriptorCount = 2;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void EdgeDetectionEffect::createDescriptorSets()
    {
        auto& dev = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 2> layouts = {
            edgeDescriptorSetLayout, compositeDescriptorSetLayout
        };

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocInfo.pSetLayouts = layouts.data();

        auto sets = dev.allocateDescriptorSets(allocInfo);
        edgeDescriptorSet = sets[0];
        compositeDescriptorSet = sets[1];

        // Edge descriptor: depth texture
        {
            vk::DescriptorImageInfo depthImageInfo{};
            depthImageInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
            depthImageInfo.imageView = depthOnlyImageView;
            depthImageInfo.sampler = sampler;

            vk::WriteDescriptorSet write{};
            write.dstSet = edgeDescriptorSet;
            write.dstBinding = 0;
            write.dstArrayElement = 0;
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            write.descriptorCount = 1;
            write.pImageInfo = &depthImageInfo;

            dev.updateDescriptorSets(write, nullptr);
        }

        // Composite descriptor: intermediate image
        {
            vk::DescriptorImageInfo intermediateInfo{};
            intermediateInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            intermediateInfo.imageView = intermediateImageView;
            intermediateInfo.sampler = sampler;

            vk::WriteDescriptorSet write{};
            write.dstSet = compositeDescriptorSet;
            write.dstBinding = 0;
            write.dstArrayElement = 0;
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            write.descriptorCount = 1;
            write.pImageInfo = &intermediateInfo;

            dev.updateDescriptorSets(write, nullptr);
        }
    }

    void EdgeDetectionEffect::loadShaders()
    {
        edgeShader = std::make_shared<core::Shader>(device);
        edgeShader->readShader("../../resources/shaders/postprocess/edge_detection.glsl");

        compositeShader = std::make_shared<core::Shader>(device);
        compositeShader->readShader("../../resources/shaders/postprocess/edge_detection_composite.glsl");
    }

    void EdgeDetectionEffect::createEdgePipeline()
    {
        auto& dev = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {
            pipeline.getInputDescriptorSetLayout(), edgeDescriptorSetLayout
        };

        vk::PushConstantRange pushConstant{};
        pushConstant.stageFlags = vk::ShaderStageFlagBits::eFragment;
        pushConstant.offset = 0;
        pushConstant.size = sizeof(EdgeDetectionPushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstant;

        edgePipelineLayout = dev.createPipelineLayout(layoutInfo);

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
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
                                              vk::ColorComponentFlagBits::eG |
                                              vk::ColorComponentFlagBits::eB |
                                              vk::ColorComponentFlagBits::eA;
        colorBlendAttachment.blendEnable = VK_FALSE;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        const auto& stages = edgeShader->getShaderStages();

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
        pipelineInfo.layout = edgePipelineLayout;
        pipelineInfo.subpass = 0;

        vk::Format edgeFormat = vk::Format::eR8G8B8A8Unorm;
        vk::PipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &edgeFormat;
        pipelineInfo.pNext = &renderingInfo;

        edgePipeline = dev.createGraphicsPipeline(nullptr, pipelineInfo).value;
    }

    void EdgeDetectionEffect::createCompositePipeline(vk::Format colorFormat)
    {
        std::array<vk::DescriptorSetLayout, 2> setLayouts = {
            pipeline.getInputDescriptorSetLayout(), compositeDescriptorSetLayout
        };

        core::GraphicsPipelineConfig config{};
        config.device = device.getLogicalDevice();
        config.colorAttachmentFormats = {colorFormat};
        config.extent = currentExtent;
        config.shaderStages = compositeShader->getShaderStages();
        config.descriptorSetLayouts = {setLayouts.begin(), setLayouts.end()};
        config.depthTestEnable = false;
        config.depthWriteEnable = false;
        config.blendEnable = false;
        config.cullMode = vk::CullModeFlagBits::eNone;

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        compositePipeline = result.pipeline;
        compositePipelineLayout = result.pipelineLayout;
    }

    void EdgeDetectionEffect::cleanupIntermediateImage()
    {
        auto& dev = device.getLogicalDevice();

        if (intermediateImageView) { dev.destroyImageView(intermediateImageView); intermediateImageView = nullptr; }
        if (intermediateImage) { dev.destroyImage(intermediateImage); intermediateImage = nullptr; }
        if (intermediateAllocation.isValid()) { device.getMemoryManager().free(intermediateAllocation); intermediateAllocation = {}; }
    }

    void EdgeDetectionEffect::cleanupPipelines()
    {
        auto& dev = device.getLogicalDevice();

        if (edgePipeline) { dev.destroyPipeline(edgePipeline); edgePipeline = nullptr; }
        if (edgePipelineLayout) { dev.destroyPipelineLayout(edgePipelineLayout); edgePipelineLayout = nullptr; }
        if (compositePipeline) { dev.destroyPipeline(compositePipeline); compositePipeline = nullptr; }
        if (compositePipelineLayout) { dev.destroyPipelineLayout(compositePipelineLayout); compositePipelineLayout = nullptr; }
    }
}
