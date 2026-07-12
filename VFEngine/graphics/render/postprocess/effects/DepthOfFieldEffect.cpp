#include "DepthOfFieldEffect.hpp"
#include "../PostProcessPipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/OffScreen.hpp"
#include "../../../core/ImageUtilities.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "../../../core/DynamicRenderingHelpers.hpp"
#include <cstring>
#include <cmath>

namespace render::postprocess
{
    DepthOfFieldEffect::DepthOfFieldEffect(core::Device& device, core::SwapChain& swapChain,
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

    void DepthOfFieldEffect::init(vk::Format colorFormat, vk::Extent2D extent)
    {
        currentExtent = extent;

        createSampler();
        createBlurImage();
        createDepthImageView();
        createDoFBuffer();
        createDescriptorSetLayouts();
        createDescriptorPool();
        createDescriptorSets();
        loadShaders();
        createBlurPipeline();
        createCompositePipeline(colorFormat);

        initialized = true;
    }

    void DepthOfFieldEffect::cleanup()
    {
        auto& dev = device.getLogicalDevice();

        cleanupPipelines();
        cleanupBlurImage();

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

        if (blurDescriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(blurDescriptorSetLayout);
            blurDescriptorSetLayout = nullptr;
        }

        if (compositeDescriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(compositeDescriptorSetLayout);
            compositeDescriptorSetLayout = nullptr;
        }

        if (dofBuffer)
        {
            dofBufferMapped = nullptr;
            core::BufferUtilities::destroyBuffer(dev, dofBuffer, dofBufferAllocation, device.getMemoryManager());
        }

        if (sampler)
        {
            dev.destroySampler(sampler);
            sampler = nullptr;
        }

        if (blurShader)
        {
            blurShader->cleanUp();
            blurShader.reset();
        }

        if (compositeShader)
        {
            compositeShader->cleanUp();
            compositeShader.reset();
        }

        initialized = false;
    }

    void DepthOfFieldEffect::recreate(vk::Format colorFormat, vk::Extent2D extent)
    {
        currentExtent = extent;
        auto& dev = device.getLogicalDevice();

        cleanupPipelines();
        cleanupBlurImage();

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

        createBlurImage();
        createDepthImageView();
        createDescriptorPool();
        createDescriptorSets();
        createBlurPipeline();
        createCompositePipeline(colorFormat);
    }

    void DepthOfFieldEffect::preRecord(const vk::CommandBuffer& commandBuffer,
                                         vk::DescriptorSet inputDescriptorSet)
    {
        updateDoFBuffer();

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            depthAspectMask);

        core::ImageUtilities::transitionImageLayout(commandBuffer, blurImage,
            blurImageLayout, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);
        blurImageLayout = vk::ImageLayout::eColorAttachmentOptimal;

        auto colorAttach = core::colorDontCare(blurImageView);

        core::DynamicRenderingInfo dynInfo{};
        dynInfo.extent = currentExtent;
        dynInfo.colorAttachments = {colorAttach};

        core::beginDynamicRendering(commandBuffer, dynInfo);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, blurPipeline);

        std::array<vk::DescriptorSet, 2> blurSets = {inputDescriptorSet, blurDescriptorSet};
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                          blurPipelineLayout, 0,
                                          static_cast<uint32_t>(blurSets.size()),
                                          blurSets.data(), 0, nullptr);

        commandBuffer.draw(3, 1, 0, 0);
        render::FrameDrawStats::count(render::DrawCategory::PostProcess);
        core::endDynamicRendering(commandBuffer);

        // Transition blur image to shader read for composite pass
        core::ImageUtilities::transitionImageLayout(commandBuffer, blurImage,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);
        blurImageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            depthAspectMask);
    }

    void DepthOfFieldEffect::record(const vk::CommandBuffer& commandBuffer,
                                      vk::DescriptorSet inputDescriptorSet)
    {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, compositePipeline);

        std::array<vk::DescriptorSet, 2> sets = {inputDescriptorSet, compositeDescriptorSet};
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                          compositePipelineLayout, 0,
                                          static_cast<uint32_t>(sets.size()),
                                          sets.data(), 0, nullptr);

        commandBuffer.draw(3, 1, 0, 0);
        render::FrameDrawStats::count(render::DrawCategory::PostProcess);
    }

    void DepthOfFieldEffect::updateParameters(const ::postprocess::PostProcessSettings& settings)
    {
        const auto& d = settings.depthOfField;
        enabled = d.enabled;
        currentFocusMode = d.focusMode;
        currentFocalDistance = d.focalDistance;
        currentFocusTarget = glm::vec3(d.focusTargetX, d.focusTargetY, d.focusTargetZ);
        currentFocusSmoothing = d.focusSmoothing;
        currentFocalRange = d.focalRange;
        currentMaxBlurRadius = d.maxBlurRadius;
        currentSampleCount = d.sampleCount;
    }

    void DepthOfFieldEffect::createSampler()
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

    void DepthOfFieldEffect::createBlurImage()
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

        core::ImageUtilities::createImage(req, blurImage, blurAllocation, device.getMemoryManager());
        blurImageLayout = vk::ImageLayout::eUndefined;

        core::ImageViewInfoRequest viewReq(dev, blurImage);
        viewReq.format = vk::Format::eR8G8B8A8Unorm;
        core::ImageUtilities::createImageView(viewReq, blurImageView);
    }

    void DepthOfFieldEffect::createDepthImageView()
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

    void DepthOfFieldEffect::createDoFBuffer()
    {
        auto& dev = device.getLogicalDevice();

        core::BufferInfoRequest bufReq(dev, device.getPhysicalDevice());
        bufReq.size = sizeof(DoFParams);
        bufReq.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufReq.properties = vk::MemoryPropertyFlagBits::eHostVisible
                          | vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(bufReq, dofBuffer, dofBufferAllocation, device.getMemoryManager());
        dofBufferMapped = dofBufferAllocation.mappedPtr;
    }

    void DepthOfFieldEffect::createDescriptorSetLayouts()
    {
        auto& dev = device.getLogicalDevice();

        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};

            bindings[0].binding = 0;
            bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment;

            bindings[1].binding = 1;
            bindings[1].descriptorType = vk::DescriptorType::eUniformBuffer;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();

            blurDescriptorSetLayout = dev.createDescriptorSetLayout(layoutInfo);
        }

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

    void DepthOfFieldEffect::createDescriptorPool()
    {
        std::array<vk::DescriptorPoolSize, 2> poolSizes{};

        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = 2;

        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = 2;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void DepthOfFieldEffect::createDescriptorSets()
    {
        auto& dev = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 2> layouts = {
            blurDescriptorSetLayout, compositeDescriptorSetLayout
        };

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        allocInfo.pSetLayouts = layouts.data();

        auto sets = dev.allocateDescriptorSets(allocInfo);
        blurDescriptorSet = sets[0];
        compositeDescriptorSet = sets[1];

        {
            vk::DescriptorImageInfo depthImageInfo{};
            depthImageInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
            depthImageInfo.imageView = depthOnlyImageView;
            depthImageInfo.sampler = sampler;

            vk::DescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = dofBuffer;
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(DoFParams);

            std::array<vk::WriteDescriptorSet, 2> writes{};

            writes[0].dstSet = blurDescriptorSet;
            writes[0].dstBinding = 0;
            writes[0].dstArrayElement = 0;
            writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[0].descriptorCount = 1;
            writes[0].pImageInfo = &depthImageInfo;

            writes[1].dstSet = blurDescriptorSet;
            writes[1].dstBinding = 1;
            writes[1].dstArrayElement = 0;
            writes[1].descriptorType = vk::DescriptorType::eUniformBuffer;
            writes[1].descriptorCount = 1;
            writes[1].pBufferInfo = &bufferInfo;

            dev.updateDescriptorSets(writes, nullptr);
        }

        {
            vk::DescriptorImageInfo blurImageInfo{};
            blurImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            blurImageInfo.imageView = blurImageView;
            blurImageInfo.sampler = sampler;

            vk::WriteDescriptorSet write{};
            write.dstSet = compositeDescriptorSet;
            write.dstBinding = 0;
            write.dstArrayElement = 0;
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            write.descriptorCount = 1;
            write.pImageInfo = &blurImageInfo;

            dev.updateDescriptorSets(write, nullptr);
        }
    }

    void DepthOfFieldEffect::loadShaders()
    {
        blurShader = std::make_shared<core::Shader>(device);
        blurShader->readShader("../../resources/shaders/postprocess/dof.glsl");

        compositeShader = std::make_shared<core::Shader>(device);
        compositeShader->readShader("../../resources/shaders/postprocess/dof_composite.glsl");
    }

    void DepthOfFieldEffect::createBlurPipeline()
    {
        auto& dev = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 2> blurSetLayouts = {
            pipeline.getInputDescriptorSetLayout(), blurDescriptorSetLayout
        };

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(blurSetLayouts.size());
        layoutInfo.pSetLayouts = blurSetLayouts.data();

        blurPipelineLayout = dev.createPipelineLayout(layoutInfo);

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

        const auto& stages = blurShader->getShaderStages();

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
        pipelineInfo.layout = blurPipelineLayout;
        pipelineInfo.subpass = 0;

        vk::Format blurFormat = vk::Format::eR8G8B8A8Unorm;
        vk::PipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &blurFormat;
        pipelineInfo.pNext = &renderingInfo;

        blurPipeline = dev.createGraphicsPipeline(nullptr, pipelineInfo).value;
    }

    void DepthOfFieldEffect::createCompositePipeline(vk::Format colorFormat)
    {
        std::array<vk::DescriptorSetLayout, 2> setLayouts = {
            compositeDescriptorSetLayout, compositeDescriptorSetLayout
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

    void DepthOfFieldEffect::cleanupBlurImage()
    {
        auto& dev = device.getLogicalDevice();

        if (blurImageView)
        {
            dev.destroyImageView(blurImageView);
            blurImageView = nullptr;
        }

        if (blurImage)
        {
            dev.destroyImage(blurImage);
            blurImage = nullptr;
        }
        blurImageLayout = vk::ImageLayout::eUndefined;

        if (blurAllocation.isValid())
        {
            device.getMemoryManager().free(blurAllocation);
            blurAllocation = {};
        }
    }

    void DepthOfFieldEffect::cleanupPipelines()
    {
        auto& dev = device.getLogicalDevice();

        if (blurPipeline)
        {
            dev.destroyPipeline(blurPipeline);
            blurPipeline = nullptr;
        }

        if (blurPipelineLayout)
        {
            dev.destroyPipelineLayout(blurPipelineLayout);
            blurPipelineLayout = nullptr;
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

    void DepthOfFieldEffect::updateDoFBuffer()
    {
        const auto& camInfo = pipeline.getCameraData();

        float targetFocal = currentFocalDistance;
        if (currentFocusMode == ::postprocess::DoFFocusMode::TargetPoint)
        {
            // Project target into view space and use Z depth (matches shader's linearizeDepth)
            glm::vec4 viewPos = camInfo.viewMatrix * glm::vec4(currentFocusTarget, 1.0f);
            targetFocal = glm::max(-viewPos.z, camInfo.nearPlane);
        }

        float dt = camInfo.time - lastTime;
        lastTime = camInfo.time;
        if (dt > 0.0f && dt < 1.0f)
        {
            smoothedFocalDistance = glm::mix(smoothedFocalDistance, targetFocal,
                                             1.0f - std::exp(-currentFocusSmoothing * dt));
        }
        else
        {
            smoothedFocalDistance = targetFocal;
        }

        DoFParams data{};
        data.focalDistance = smoothedFocalDistance;
        data.focalRange = currentFocalRange;
        data.maxBlurRadius = currentMaxBlurRadius;
        data.nearPlane = camInfo.nearPlane;
        data.farPlane = camInfo.farPlane;
        data.sampleCount = currentSampleCount;

        std::memcpy(dofBufferMapped, &data, sizeof(DoFParams));
    }
}
