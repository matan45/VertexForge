#include "AtmospherePipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include <cstring>

#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::atmosphere
{
    void AtmospherePipeline::dispatchCompute(const vk::CommandBuffer& cmd)
    {
        if (!initialized || !enabled) return;

        if (needsInitialTransition)
        {
            auto transitionToGeneral = [&](vk::Image image) {
                core::ImageUtilities::transitionImageLayout(cmd, image,
                    vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
                    vk::ImageAspectFlagBits::eColor);
            };
            transitionToGeneral(transmittanceImage);
            transitionToGeneral(multiScatterImage);
            transitionToGeneral(skyViewImage);
            transitionToGeneral(aerialImage);
            needsInitialTransition = false;
        }

        updateParamsBuffer();

        vk::MemoryBarrier barrier{vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eShaderRead};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eComputeShader,
                            {}, barrier, {}, {});

        if (paramsDirty)
        {
            cmd.bindPipeline(vk::PipelineBindPoint::eCompute, transmittancePipeline);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, transmittancePipelineLayout, 0, transmittanceDS, nullptr);
            cmd.dispatch((256 + 15) / 16, (64 + 15) / 16, 1);

            vk::MemoryBarrier computeBarrier{vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead};
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                                {}, computeBarrier, {}, {});

            cmd.bindPipeline(vk::PipelineBindPoint::eCompute, multiScatterPipeline);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, multiScatterPipelineLayout, 0, multiScatterDS, nullptr);
            cmd.dispatch((32 + 15) / 16, (32 + 15) / 16, 1);

            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                                {}, computeBarrier, {}, {});

            paramsDirty = false;
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, skyViewPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, skyViewPipelineLayout, 0, skyViewDS, nullptr);
        cmd.dispatch((192 + 15) / 16, (108 + 15) / 16, 1);

        vk::MemoryBarrier computeBarrier{vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                            {}, computeBarrier, {}, {});

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, aerialPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, aerialPipelineLayout, 0, aerialDS, nullptr);
        cmd.dispatch((32 + 7) / 8, (32 + 7) / 8, 32);

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader,
                            {}, computeBarrier, {}, {});
    }

    void AtmospherePipeline::renderSky(const vk::CommandBuffer& cmd, uint32_t imageIndex)
    {
        if (!initialized || !enabled) return;

        vk::Image sceneColor = offscreenResources.colorImages[imageIndex].colorImage;
        core::ImageUtilities::transitionImageLayout(cmd, sceneColor,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        auto colorAttach = core::colorLoad(offscreenResources.colorImages[imageIndex].colorImageView);

        core::DynamicRenderingInfo info{};
        info.extent = currentExtent;
        info.colorAttachments = {colorAttach};

        core::beginDynamicRendering(cmd, info);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, skyRendererPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, skyRendererPipelineLayout, 0, skyRendererDS, nullptr);
        cmd.draw(3, 1, 0, 0);
        core::endDynamicRendering(cmd);

        core::ImageUtilities::transitionImageLayout(cmd, sceneColor,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);
    }

    void AtmospherePipeline::renderComposite(const vk::CommandBuffer& cmd, uint32_t imageIndex)
    {
        if (!initialized || !enabled) return;

        if (compositeParamsMapped)
        {
            AtmosphereCompositeParams params{};
            params.nearPlane = cachedNear;
            params.farPlane = cachedFar;
            params.aerialMaxDist = settings.aerialMaxDist;
            params.intensity = settings.aerialIntensity;
            std::memcpy(compositeParamsMapped, &params, sizeof(params));
        }

        vk::Image sceneColor = offscreenResources.colorImages[imageIndex].colorImage;
        core::ImageUtilities::transitionImageLayout(cmd, sceneColor,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::ImageUtilities::transitionImageLayout(cmd,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            depthAspectMask);

        auto colorAttach = core::colorLoad(offscreenResources.colorImages[imageIndex].colorImageView);

        core::DynamicRenderingInfo info{};
        info.extent = currentExtent;
        info.colorAttachments = {colorAttach};

        core::beginDynamicRendering(cmd, info);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, compositePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, compositePipelineLayout, 0, compositeDS, nullptr);
        cmd.draw(3, 1, 0, 0);
        core::endDynamicRendering(cmd);

        core::ImageUtilities::transitionImageLayout(cmd, sceneColor,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::ImageUtilities::transitionImageLayout(cmd,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            depthAspectMask);
    }

    void AtmospherePipeline::recreate()
    {
        if (!initialized) return;

        auto& dev = device.getLogicalDevice();
        currentExtent = swapChain.getSwapchainExtent();

        if (skyRendererPipeline) { dev.destroyPipeline(skyRendererPipeline); skyRendererPipeline = nullptr; }
        if (compositePipeline) { dev.destroyPipeline(compositePipeline); compositePipeline = nullptr; }

        if (depthOnlyImageView) { dev.destroyImageView(depthOnlyImageView); depthOnlyImageView = nullptr; }

        if (skyRendererDSPool) { dev.destroyDescriptorPool(skyRendererDSPool); skyRendererDSPool = nullptr; }
        if (compositeDSPool) { dev.destroyDescriptorPool(compositeDSPool); compositeDSPool = nullptr; }

        createDepthOnlyView();

        // Recreate sky renderer descriptor set and pipeline
        {
            std::array<vk::DescriptorPoolSize, 2> poolSizes{};
            poolSizes[0] = {vk::DescriptorType::eCombinedImageSampler, 2};
            poolSizes[1] = {vk::DescriptorType::eUniformBuffer, 1};
            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
            poolInfo.maxSets = 1;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            skyRendererDSPool = dev.createDescriptorPool(poolInfo);

            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = skyRendererDSPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &skyRendererDSLayout;
            skyRendererDS = dev.allocateDescriptorSets(allocInfo)[0];

            vk::DescriptorImageInfo skyViewInfo{lutSampler, skyViewView, vk::ImageLayout::eGeneral};
            vk::DescriptorImageInfo transInfo{lutSampler, transmittanceView, vk::ImageLayout::eGeneral};
            vk::DescriptorBufferInfo bufInfo{paramsBuffer, 0, sizeof(AtmosphereGPUParams)};

            std::array<vk::WriteDescriptorSet, 3> writes{};
            writes[0] = {skyRendererDS, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &skyViewInfo};
            writes[1] = {skyRendererDS, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &transInfo};
            writes[2] = {skyRendererDS, 2, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo};
            dev.updateDescriptorSets(writes, nullptr);
        }

        // Recreate composite descriptor set and pipeline
        {
            std::array<vk::DescriptorPoolSize, 2> poolSizes{};
            poolSizes[0] = {vk::DescriptorType::eCombinedImageSampler, 2};
            poolSizes[1] = {vk::DescriptorType::eUniformBuffer, 1};
            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
            poolInfo.maxSets = 1;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            compositeDSPool = dev.createDescriptorPool(poolInfo);

            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = compositeDSPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &compositeDSLayout;
            compositeDS = dev.allocateDescriptorSets(allocInfo)[0];

            vk::DescriptorImageInfo depthInfo{lutSampler, depthOnlyImageView, vk::ImageLayout::eDepthStencilReadOnlyOptimal};
            vk::DescriptorImageInfo aerialInfo{lutSampler, aerialView, vk::ImageLayout::eGeneral};
            vk::DescriptorBufferInfo bufInfo{compositeParamsBuffer, 0, sizeof(AtmosphereCompositeParams)};

            std::array<vk::WriteDescriptorSet, 3> writes{};
            writes[0] = {compositeDS, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &depthInfo};
            writes[1] = {compositeDS, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &aerialInfo};
            writes[2] = {compositeDS, 2, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo};
            dev.updateDescriptorSets(writes, nullptr);
        }

        // Recreate sky graphics pipeline
        {
            auto& stages = skyRendererShader->getShaderStages();
            vk::PipelineVertexInputStateCreateInfo vertexInput{};
            vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

            vk::Viewport viewport{0.0f, 0.0f,
                static_cast<float>(currentExtent.width), static_cast<float>(currentExtent.height), 0.0f, 1.0f};
            vk::Rect2D scissor{{0, 0}, currentExtent};
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

            vk::PipelineColorBlendAttachmentState skyBlend{};
            skyBlend.blendEnable = VK_FALSE;
            skyBlend.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                      vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            vk::PipelineColorBlendStateCreateInfo skyBlending{};
            skyBlending.attachmentCount = 1; skyBlending.pAttachments = &skyBlend;

            vk::GraphicsPipelineCreateInfo skyPipelineInfo{};
            skyPipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
            skyPipelineInfo.pStages = stages.data();
            skyPipelineInfo.pVertexInputState = &vertexInput;
            skyPipelineInfo.pInputAssemblyState = &inputAssembly;
            skyPipelineInfo.pViewportState = &viewportState;
            skyPipelineInfo.pRasterizationState = &rasterizer;
            skyPipelineInfo.pMultisampleState = &multisampling;
            skyPipelineInfo.pDepthStencilState = &depthStencil;
            skyPipelineInfo.pColorBlendState = &skyBlending;
            skyPipelineInfo.layout = skyRendererPipelineLayout;

            vk::Format skyColorFormat = swapChain.getSceneColorFormat();
            vk::PipelineRenderingCreateInfo skyRenderingInfo{};
            skyRenderingInfo.colorAttachmentCount = 1;
            skyRenderingInfo.pColorAttachmentFormats = &skyColorFormat;
            skyPipelineInfo.pNext = &skyRenderingInfo;

            skyRendererPipeline = dev.createGraphicsPipeline(nullptr, skyPipelineInfo).value;
        }

        // Recreate composite graphics pipeline
        {
            auto& stages = compositeShader->getShaderStages();
            vk::PipelineVertexInputStateCreateInfo vertexInput{};
            vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

            vk::Viewport viewport{0.0f, 0.0f,
                static_cast<float>(currentExtent.width), static_cast<float>(currentExtent.height), 0.0f, 1.0f};
            vk::Rect2D scissor{{0, 0}, currentExtent};
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

            vk::PipelineColorBlendAttachmentState compositeBlend{};
            compositeBlend.blendEnable = VK_TRUE;
            compositeBlend.srcColorBlendFactor = vk::BlendFactor::eOne;
            compositeBlend.dstColorBlendFactor = vk::BlendFactor::eSrcAlpha;
            compositeBlend.colorBlendOp = vk::BlendOp::eAdd;
            compositeBlend.srcAlphaBlendFactor = vk::BlendFactor::eZero;
            compositeBlend.dstAlphaBlendFactor = vk::BlendFactor::eOne;
            compositeBlend.alphaBlendOp = vk::BlendOp::eAdd;
            compositeBlend.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                             vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            vk::PipelineColorBlendStateCreateInfo compositeBlending{};
            compositeBlending.attachmentCount = 1; compositeBlending.pAttachments = &compositeBlend;

            vk::GraphicsPipelineCreateInfo compositePipelineInfo{};
            compositePipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
            compositePipelineInfo.pStages = stages.data();
            compositePipelineInfo.pVertexInputState = &vertexInput;
            compositePipelineInfo.pInputAssemblyState = &inputAssembly;
            compositePipelineInfo.pViewportState = &viewportState;
            compositePipelineInfo.pRasterizationState = &rasterizer;
            compositePipelineInfo.pMultisampleState = &multisampling;
            compositePipelineInfo.pDepthStencilState = &depthStencil;
            compositePipelineInfo.pColorBlendState = &compositeBlending;
            compositePipelineInfo.layout = compositePipelineLayout;

            vk::Format compositeColorFormat = swapChain.getSceneColorFormat();
            vk::PipelineRenderingCreateInfo compositeRenderingInfo{};
            compositeRenderingInfo.colorAttachmentCount = 1;
            compositeRenderingInfo.pColorAttachmentFormats = &compositeColorFormat;
            compositePipelineInfo.pNext = &compositeRenderingInfo;

            compositePipeline = dev.createGraphicsPipeline(nullptr, compositePipelineInfo).value;
        }

        needsInitialTransition = true;
    }

    void AtmospherePipeline::createSkyRenderer()
    {
        auto& dev = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};
        bindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};
        bindings[1] = {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};
        bindings[2] = {2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment};

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        skyRendererDSLayout = dev.createDescriptorSetLayout(layoutInfo);

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eCombinedImageSampler, 2};
        poolSizes[1] = {vk::DescriptorType::eUniformBuffer, 1};
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        skyRendererDSPool = dev.createDescriptorPool(poolInfo);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = skyRendererDSPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &skyRendererDSLayout;
        skyRendererDS = dev.allocateDescriptorSets(allocInfo)[0];

        vk::DescriptorImageInfo skyViewInfo{lutSampler, skyViewView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo transInfo{lutSampler, transmittanceView, vk::ImageLayout::eGeneral};
        vk::DescriptorBufferInfo bufInfo{paramsBuffer, 0, sizeof(AtmosphereGPUParams)};

        std::array<vk::WriteDescriptorSet, 3> writes{};
        writes[0] = {skyRendererDS, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &skyViewInfo};
        writes[1] = {skyRendererDS, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &transInfo};
        writes[2] = {skyRendererDS, 2, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo};
        dev.updateDescriptorSets(writes, nullptr);

        vk::PipelineLayoutCreateInfo plInfo{};
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &skyRendererDSLayout;
        skyRendererPipelineLayout = dev.createPipelineLayout(plInfo);

        skyRendererShader = std::make_shared<core::Shader>(device);
        skyRendererShader->readShader("../../resources/shaders/atmosphere/atmosphere_sky.glsl");

        vk::PipelineVertexInputStateCreateInfo vertexInput{};
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        vk::Viewport viewport{0.0f, 0.0f,
            static_cast<float>(currentExtent.width), static_cast<float>(currentExtent.height), 0.0f, 1.0f};
        vk::Rect2D scissor{{0, 0}, currentExtent};
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

        vk::PipelineColorBlendAttachmentState colorBlend{};
        colorBlend.blendEnable = VK_FALSE;
        colorBlend.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                     vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        vk::PipelineColorBlendStateCreateInfo blending{};
        blending.attachmentCount = 1; blending.pAttachments = &colorBlend;

        const auto& stages = skyRendererShader->getShaderStages();
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
        pipelineInfo.layout = skyRendererPipelineLayout;

        vk::Format colorFormat = swapChain.getSceneColorFormat();
        vk::PipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;
        pipelineInfo.pNext = &renderingInfo;

        skyRendererPipeline = dev.createGraphicsPipeline(nullptr, pipelineInfo).value;
    }
}
