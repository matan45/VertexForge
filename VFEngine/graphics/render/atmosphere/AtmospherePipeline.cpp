#include "AtmospherePipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/MemoryUtilities.hpp"
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>

// Windows defines MemoryBarrier as a macro - undefine it to use vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::atmosphere
{
    AtmospherePipeline::AtmospherePipeline(core::Device& device, core::SwapChain& swapChain,
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

    AtmospherePipeline::~AtmospherePipeline()
    {
        cleanup();
    }

    void AtmospherePipeline::init()
    {
        currentExtent = swapChain.getSwapchainExtent();

        createSampler();
        createParamsBuffer();

        // Create LUT images
        createTransmittanceLUT();
        createMultiScatterLUT();
        createSkyViewLUT();
        createAerialPerspectiveLUT();

        // Create graphics passes
        createSkyRenderer();
        createComposite();

        initialized = true;
        paramsDirty = true;
    }

    void AtmospherePipeline::cleanup()
    {
        if (!initialized)
            return;

        auto& dev = device.getLogicalDevice();

        cleanupGraphicsPipelines();
        cleanupComputePipelines();

        cleanupSkyFramebuffers();
        cleanupCompositeFramebuffers();

        // Destroy render passes
        if (skyRenderPass) { dev.destroyRenderPass(skyRenderPass); skyRenderPass = nullptr; }
        if (compositeRenderPass) { dev.destroyRenderPass(compositeRenderPass); compositeRenderPass = nullptr; }

        // Destroy LUT images
        destroyImage(transmittanceImage, transmittanceMemory, transmittanceView);
        destroyImage(multiScatterImage, multiScatterMemory, multiScatterView);
        destroyImage(skyViewImage, skyViewMemory, skyViewView);
        destroyImage(aerialImage, aerialMemory, aerialView);

        // Destroy depth-only view
        if (depthOnlyImageView) { dev.destroyImageView(depthOnlyImageView); depthOnlyImageView = nullptr; }

        // Destroy descriptor pools/layouts
        auto destroyDS = [&](vk::DescriptorPool& pool, vk::DescriptorSetLayout& layout) {
            if (pool) { dev.destroyDescriptorPool(pool); pool = nullptr; }
            if (layout) { dev.destroyDescriptorSetLayout(layout); layout = nullptr; }
        };
        destroyDS(transmittanceDSPool, transmittanceDSLayout);
        destroyDS(multiScatterDSPool, multiScatterDSLayout);
        destroyDS(skyViewDSPool, skyViewDSLayout);
        destroyDS(aerialDSPool, aerialDSLayout);
        destroyDS(skyRendererDSPool, skyRendererDSLayout);
        destroyDS(compositeDSPool, compositeDSLayout);

        // Destroy params buffers
        if (paramsBufferMapped) { dev.unmapMemory(paramsBufferMemory); paramsBufferMapped = nullptr; }
        if (paramsBuffer) { core::BufferUtilities::destroyBuffer(dev, paramsBuffer, paramsBufferMemory); }

        if (compositeParamsMapped) { dev.unmapMemory(compositeParamsMemory); compositeParamsMapped = nullptr; }
        if (compositeParamsBuffer) { core::BufferUtilities::destroyBuffer(dev, compositeParamsBuffer, compositeParamsMemory); }

        // Destroy sampler
        if (lutSampler) { dev.destroySampler(lutSampler); lutSampler = nullptr; }

        // Clean up shaders
        auto cleanShader = [](std::shared_ptr<core::Shader>& s) { if (s) { s->cleanUp(); s.reset(); } };
        cleanShader(transmittanceShader);
        cleanShader(multiScatterShader);
        cleanShader(skyViewShader);
        cleanShader(aerialShader);
        cleanShader(skyRendererShader);
        cleanShader(compositeShader);

        initialized = false;
    }

    void AtmospherePipeline::recreate()
    {
        if (!initialized) return;

        auto& dev = device.getLogicalDevice();
        currentExtent = swapChain.getSwapchainExtent();

        // Destroy only pipelines (not layouts — they don't depend on swapchain)
        auto& devRef = device.getLogicalDevice();
        if (skyRendererPipeline) { devRef.destroyPipeline(skyRendererPipeline); skyRendererPipeline = nullptr; }
        if (compositePipeline) { devRef.destroyPipeline(compositePipeline); compositePipeline = nullptr; }

        cleanupSkyFramebuffers();
        cleanupCompositeFramebuffers();

        if (depthOnlyImageView) { dev.destroyImageView(depthOnlyImageView); depthOnlyImageView = nullptr; }

        // Rebuild descriptor pools for graphics passes
        if (skyRendererDSPool) { dev.destroyDescriptorPool(skyRendererDSPool); skyRendererDSPool = nullptr; }
        if (compositeDSPool) { dev.destroyDescriptorPool(compositeDSPool); compositeDSPool = nullptr; }

        if (skyRenderPass) { dev.destroyRenderPass(skyRenderPass); skyRenderPass = nullptr; }
        if (compositeRenderPass) { dev.destroyRenderPass(compositeRenderPass); compositeRenderPass = nullptr; }

        createSkyRenderPass();
        createSkyFramebuffers();
        createCompositeRenderPass();
        createCompositeFramebuffers();
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

            // Update descriptor set
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

        // Recreate graphics pipelines
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

            // Sky: opaque write
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
            skyPipelineInfo.renderPass = skyRenderPass;
            skyRendererPipeline = dev.createGraphicsPipeline(nullptr, skyPipelineInfo).value;
        }

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

            // Composite: src*ONE + dst*srcAlpha (same as volumetric fog)
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
            compositePipelineInfo.renderPass = compositeRenderPass;
            compositePipeline = dev.createGraphicsPipeline(nullptr, compositePipelineInfo).value;
        }
    }

    void AtmospherePipeline::updateSettings(const AtmosphereSettings& newSettings)
    {
        bool paramChanged = (settings.planetRadius != newSettings.planetRadius ||
                             settings.atmosphereRadius != newSettings.atmosphereRadius ||
                             settings.rayleighScattering != newSettings.rayleighScattering ||
                             settings.rayleighDensityExpScale != newSettings.rayleighDensityExpScale ||
                             settings.mieScattering != newSettings.mieScattering ||
                             settings.mieAbsorption != newSettings.mieAbsorption ||
                             settings.mieAnisotropy != newSettings.mieAnisotropy ||
                             settings.mieDensityExpScale != newSettings.mieDensityExpScale ||
                             settings.ozoneAbsorption != newSettings.ozoneAbsorption ||
                             settings.ozoneCenterAlt != newSettings.ozoneCenterAlt ||
                             settings.ozoneWidth != newSettings.ozoneWidth ||
                             settings.groundAlbedo != newSettings.groundAlbedo);

        settings = newSettings;
        enabled = newSettings.enabled;
        if (paramChanged)
            paramsDirty = true;
    }

    void AtmospherePipeline::setCameraData(const glm::mat4& view, const glm::mat4& projection,
                                            const glm::vec3& cameraPos, float nearPlane, float farPlane)
    {
        cachedView = view;
        cachedProjection = projection;
        cachedCameraPos = cameraPos;
        cachedNear = nearPlane;
        cachedFar = farPlane;
    }

    void AtmospherePipeline::dispatchCompute(const vk::CommandBuffer& cmd)
    {
        if (!initialized || !enabled) return;

        // Transition all LUT images from UNDEFINED to GENERAL on first use
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

        // Memory barrier: ensure params are visible
        vk::MemoryBarrier barrier{vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eShaderRead};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eComputeShader,
                            {}, barrier, {}, {});

        if (paramsDirty)
        {
            // Transmittance LUT (only on param change)
            cmd.bindPipeline(vk::PipelineBindPoint::eCompute, transmittancePipeline);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, transmittancePipelineLayout, 0, transmittanceDS, nullptr);
            cmd.dispatch((256 + 15) / 16, (64 + 15) / 16, 1);

            // Barrier: transmittance must be ready for multi-scatter
            vk::MemoryBarrier computeBarrier{vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead};
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                                {}, computeBarrier, {}, {});

            // Multi-Scatter LUT (only on param change)
            cmd.bindPipeline(vk::PipelineBindPoint::eCompute, multiScatterPipeline);
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, multiScatterPipelineLayout, 0, multiScatterDS, nullptr);
            cmd.dispatch((32 + 15) / 16, (32 + 15) / 16, 1);

            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                                {}, computeBarrier, {}, {});

            paramsDirty = false;
        }

        // Sky-View LUT (every frame)
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, skyViewPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, skyViewPipelineLayout, 0, skyViewDS, nullptr);
        cmd.dispatch((192 + 15) / 16, (108 + 15) / 16, 1);

        // Barrier
        vk::MemoryBarrier computeBarrier{vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                            {}, computeBarrier, {}, {});

        // Aerial Perspective LUT (every frame)
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, aerialPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, aerialPipelineLayout, 0, aerialDS, nullptr);
        cmd.dispatch((32 + 7) / 8, (32 + 7) / 8, 32);

        // Barrier for subsequent graphics reads
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eFragmentShader,
                            {}, computeBarrier, {}, {});
    }

    void AtmospherePipeline::renderSky(const vk::CommandBuffer& cmd, uint32_t imageIndex)
    {
        if (!initialized || !enabled) return;

        // Transition scene color to attachment
        vk::Image sceneColor = offscreenResources.colorImages[imageIndex].colorImage;
        core::ImageUtilities::transitionImageLayout(cmd, sceneColor,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        vk::RenderPassBeginInfo rpBegin{};
        rpBegin.renderPass = skyRenderPass;
        rpBegin.framebuffer = skyFramebuffers[imageIndex];
        rpBegin.renderArea.offset = vk::Offset2D{0, 0};
        rpBegin.renderArea.extent = currentExtent;

        cmd.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, skyRendererPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, skyRendererPipelineLayout, 0, skyRendererDS, nullptr);
        cmd.draw(3, 1, 0, 0);
        cmd.endRenderPass();

        // Scene color is now in eShaderReadOnlyOptimal (render pass finalLayout)
    }

    void AtmospherePipeline::renderComposite(const vk::CommandBuffer& cmd, uint32_t imageIndex)
    {
        if (!initialized || !enabled) return;

        // Update composite params
        if (compositeParamsMapped)
        {
            AtmosphereCompositeParams params{};
            params.nearPlane = cachedNear;
            params.farPlane = cachedFar;
            params.aerialMaxDist = settings.aerialMaxDist;
            params.intensity = settings.aerialIntensity;
            std::memcpy(compositeParamsMapped, &params, sizeof(params));
        }

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
        rpBegin.renderPass = compositeRenderPass;
        rpBegin.framebuffer = compositeFramebuffers[imageIndex];
        rpBegin.renderArea.offset = vk::Offset2D{0, 0};
        rpBegin.renderArea.extent = currentExtent;

        cmd.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, compositePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, compositePipelineLayout, 0, compositeDS, nullptr);
        cmd.draw(3, 1, 0, 0);
        cmd.endRenderPass();

        // Transition depth back
        core::ImageUtilities::transitionImageLayout(cmd,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            depthAspectMask);
    }

    // ---------- Private helpers ----------

    void AtmospherePipeline::createSampler()
    {
        vk::SamplerCreateInfo info{};
        info.magFilter = vk::Filter::eLinear;
        info.minFilter = vk::Filter::eLinear;
        info.mipmapMode = vk::SamplerMipmapMode::eLinear;
        info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        info.anisotropyEnable = VK_FALSE;
        lutSampler = device.getLogicalDevice().createSampler(info);
    }

    void AtmospherePipeline::createParamsBuffer()
    {
        auto& dev = device.getLogicalDevice();
        core::BufferInfoRequest bufReq(dev, device.getPhysicalDevice());
        bufReq.size = sizeof(AtmosphereGPUParams);
        bufReq.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufReq.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(bufReq, paramsBuffer, paramsBufferMemory);
        paramsBufferMapped = dev.mapMemory(paramsBufferMemory, 0, sizeof(AtmosphereGPUParams));

        // Composite params buffer
        core::BufferInfoRequest compositeReq(dev, device.getPhysicalDevice());
        compositeReq.size = sizeof(AtmosphereCompositeParams);
        compositeReq.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        compositeReq.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(compositeReq, compositeParamsBuffer, compositeParamsMemory);
        compositeParamsMapped = dev.mapMemory(compositeParamsMemory, 0, sizeof(AtmosphereCompositeParams));
    }

    void AtmospherePipeline::updateParamsBuffer()
    {
        if (!paramsBufferMapped) return;

        glm::vec3 sunDir = hasSunOverride
            ? glm::normalize(sunDirectionOverride)  // use directional light direction directly
            : sunDirectionFromAngles(settings.sunAzimuth, settings.sunElevation);
        glm::mat4 vp = cachedProjection * cachedView;

        AtmosphereGPUParams gpu{};
        gpu.planetParams = glm::vec4(settings.planetRadius, settings.atmosphereRadius, 0.0f, 0.0f);
        gpu.rayleighScattering = glm::vec4(settings.rayleighScattering, settings.rayleighDensityExpScale);
        gpu.mieParams = glm::vec4(settings.mieScattering, settings.mieAbsorption, settings.mieAnisotropy, settings.mieDensityExpScale);
        gpu.ozoneAbsorption = glm::vec4(settings.ozoneAbsorption, settings.ozoneCenterAlt);
        gpu.ozoneParams = glm::vec4(settings.ozoneWidth, 0.0f, 0.0f, 0.0f);
        gpu.sunIrradiance = glm::vec4(settings.sunIrradiance, settings.sunAngularRadius);
        gpu.sunDirection = glm::vec4(sunDir, 0.0f);
        gpu.groundAlbedo = glm::vec4(settings.groundAlbedo, 0.0f);

        // Camera altitude above planet surface (assume Y-up, planet center at origin for atmosphere)
        float altitude = std::max(cachedCameraPos.y, 1.0f); // minimum 1m above "surface"
        gpu.cameraPosition = glm::vec4(cachedCameraPos, altitude);

        gpu.invViewProjection = glm::inverse(vp);
        gpu.viewProjection = vp;
        gpu.screenParams = glm::vec4(cachedNear, cachedFar, settings.aerialMaxDist, settings.aerialIntensity);
        gpu.screenSize = glm::uvec4(currentExtent.width, currentExtent.height, 0, 0);

        std::memcpy(paramsBufferMapped, &gpu, sizeof(gpu));
    }

    void AtmospherePipeline::create2DImage(uint32_t width, uint32_t height,
                                            vk::Image& image, vk::DeviceMemory& memory, vk::ImageView& view)
    {
        auto& dev = device.getLogicalDevice();
        auto& physDev = device.getPhysicalDevice();

        vk::ImageCreateInfo imageInfo{};
        imageInfo.imageType = vk::ImageType::e2D;
        imageInfo.extent = vk::Extent3D{width, height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = vk::Format::eR16G16B16A16Sfloat;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;
        imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;

        image = dev.createImage(imageInfo);
        vk::MemoryRequirements memReqs = dev.getImageMemoryRequirements(image);
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = core::MemoryUtilities::findMemoryType(
            physDev, memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        memory = dev.allocateMemory(allocInfo);
        dev.bindImageMemory(image, memory, 0);

        core::ImageViewInfoRequest viewReq(dev, image);
        viewReq.format = vk::Format::eR16G16B16A16Sfloat;
        viewReq.imageType = vk::ImageViewType::e2D;
        viewReq.aspectFlags = vk::ImageAspectFlagBits::eColor;
        core::ImageUtilities::createImageView(viewReq, view);
    }

    void AtmospherePipeline::create3DImage(uint32_t width, uint32_t height, uint32_t depth,
                                            vk::Image& image, vk::DeviceMemory& memory, vk::ImageView& view)
    {
        auto& dev = device.getLogicalDevice();
        auto& physDev = device.getPhysicalDevice();

        vk::ImageCreateInfo imageInfo{};
        imageInfo.imageType = vk::ImageType::e3D;
        imageInfo.extent = vk::Extent3D{width, height, depth};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = vk::Format::eR16G16B16A16Sfloat;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;
        imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;

        image = dev.createImage(imageInfo);
        vk::MemoryRequirements memReqs = dev.getImageMemoryRequirements(image);
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = core::MemoryUtilities::findMemoryType(
            physDev, memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        memory = dev.allocateMemory(allocInfo);
        dev.bindImageMemory(image, memory, 0);

        core::ImageViewInfoRequest viewReq(dev, image);
        viewReq.format = vk::Format::eR16G16B16A16Sfloat;
        viewReq.imageType = vk::ImageViewType::e3D;
        viewReq.aspectFlags = vk::ImageAspectFlagBits::eColor;
        core::ImageUtilities::createImageView(viewReq, view);
    }

    void AtmospherePipeline::destroyImage(vk::Image& image, vk::DeviceMemory& memory, vk::ImageView& view)
    {
        auto& dev = device.getLogicalDevice();
        if (view) { dev.destroyImageView(view); view = nullptr; }
        if (image) { dev.destroyImage(image); image = nullptr; }
        if (memory) { dev.freeMemory(memory); memory = nullptr; }
    }

    // Helper to create a compute descriptor set layout, pool, set, pipeline for a LUT compute
    namespace
    {
        struct ComputePipelineKit
        {
            vk::DescriptorSetLayout layout;
            vk::DescriptorPool pool;
            vk::DescriptorSet set;
            vk::PipelineLayout pipelineLayout;
            vk::Pipeline pipeline;
        };

        ComputePipelineKit createComputeKit(
            core::Device& device,
            const std::vector<vk::DescriptorSetLayoutBinding>& bindings,
            const std::vector<vk::DescriptorPoolSize>& poolSizes,
            core::Shader& shader)
        {
            auto& dev = device.getLogicalDevice();
            ComputePipelineKit kit{};

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();
            kit.layout = dev.createDescriptorSetLayout(layoutInfo);

            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
            poolInfo.maxSets = 1;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            kit.pool = dev.createDescriptorPool(poolInfo);

            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = kit.pool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &kit.layout;
            kit.set = dev.allocateDescriptorSets(allocInfo)[0];

            vk::PipelineLayoutCreateInfo plInfo{};
            plInfo.setLayoutCount = 1;
            plInfo.pSetLayouts = &kit.layout;
            kit.pipelineLayout = dev.createPipelineLayout(plInfo);

            const auto& stages = shader.getShaderStages();
            vk::ComputePipelineCreateInfo cpInfo{};
            cpInfo.stage = stages[0];
            cpInfo.layout = kit.pipelineLayout;
            kit.pipeline = dev.createComputePipeline(nullptr, cpInfo).value;

            return kit;
        }
    }

    void AtmospherePipeline::createTransmittanceLUT()
    {
        create2DImage(256, 64, transmittanceImage, transmittanceMemory, transmittanceView);

        std::vector<vk::DescriptorSetLayoutBinding> bindings{
            {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
            {1, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute}
        };
        std::vector<vk::DescriptorPoolSize> poolSizes{
            {vk::DescriptorType::eStorageImage, 1},
            {vk::DescriptorType::eUniformBuffer, 1}
        };

        transmittanceShader = std::make_shared<core::Shader>(device);
        transmittanceShader->readShader("../../resources/shaders/atmosphere/transmittance_lut.glsl");

        auto kit = createComputeKit(device, bindings, poolSizes, *transmittanceShader);
        transmittanceDSLayout = kit.layout;
        transmittanceDSPool = kit.pool;
        transmittanceDS = kit.set;
        transmittancePipelineLayout = kit.pipelineLayout;
        transmittancePipeline = kit.pipeline;

        // Write descriptors
        auto& dev = device.getLogicalDevice();
        vk::DescriptorImageInfo imgInfo{nullptr, transmittanceView, vk::ImageLayout::eGeneral};
        vk::DescriptorBufferInfo bufInfo{paramsBuffer, 0, sizeof(AtmosphereGPUParams)};

        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0] = {transmittanceDS, 0, 0, 1, vk::DescriptorType::eStorageImage, &imgInfo};
        writes[1] = {transmittanceDS, 1, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo};
        dev.updateDescriptorSets(writes, nullptr);
    }

    void AtmospherePipeline::createMultiScatterLUT()
    {
        create2DImage(32, 32, multiScatterImage, multiScatterMemory, multiScatterView);

        std::vector<vk::DescriptorSetLayoutBinding> bindings{
            {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
            {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
            {2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute}
        };
        std::vector<vk::DescriptorPoolSize> poolSizes{
            {vk::DescriptorType::eStorageImage, 1},
            {vk::DescriptorType::eCombinedImageSampler, 1},
            {vk::DescriptorType::eUniformBuffer, 1}
        };

        multiScatterShader = std::make_shared<core::Shader>(device);
        multiScatterShader->readShader("../../resources/shaders/atmosphere/multiscatter_lut.glsl");

        auto kit = createComputeKit(device, bindings, poolSizes, *multiScatterShader);
        multiScatterDSLayout = kit.layout;
        multiScatterDSPool = kit.pool;
        multiScatterDS = kit.set;
        multiScatterPipelineLayout = kit.pipelineLayout;
        multiScatterPipeline = kit.pipeline;

        auto& dev = device.getLogicalDevice();
        vk::DescriptorImageInfo msImgInfo{nullptr, multiScatterView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo transInfo{lutSampler, transmittanceView, vk::ImageLayout::eGeneral};
        vk::DescriptorBufferInfo bufInfo{paramsBuffer, 0, sizeof(AtmosphereGPUParams)};

        std::array<vk::WriteDescriptorSet, 3> writes{};
        writes[0] = {multiScatterDS, 0, 0, 1, vk::DescriptorType::eStorageImage, &msImgInfo};
        writes[1] = {multiScatterDS, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &transInfo};
        writes[2] = {multiScatterDS, 2, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo};
        dev.updateDescriptorSets(writes, nullptr);
    }

    void AtmospherePipeline::createSkyViewLUT()
    {
        create2DImage(192, 108, skyViewImage, skyViewMemory, skyViewView);

        std::vector<vk::DescriptorSetLayoutBinding> bindings{
            {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
            {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
            {2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
            {3, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute}
        };
        std::vector<vk::DescriptorPoolSize> poolSizes{
            {vk::DescriptorType::eStorageImage, 1},
            {vk::DescriptorType::eCombinedImageSampler, 2},
            {vk::DescriptorType::eUniformBuffer, 1}
        };

        skyViewShader = std::make_shared<core::Shader>(device);
        skyViewShader->readShader("../../resources/shaders/atmosphere/skyview_lut.glsl");

        auto kit = createComputeKit(device, bindings, poolSizes, *skyViewShader);
        skyViewDSLayout = kit.layout;
        skyViewDSPool = kit.pool;
        skyViewDS = kit.set;
        skyViewPipelineLayout = kit.pipelineLayout;
        skyViewPipeline = kit.pipeline;

        auto& dev = device.getLogicalDevice();
        vk::DescriptorImageInfo svImgInfo{nullptr, skyViewView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo transInfo{lutSampler, transmittanceView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo msInfo{lutSampler, multiScatterView, vk::ImageLayout::eGeneral};
        vk::DescriptorBufferInfo bufInfo{paramsBuffer, 0, sizeof(AtmosphereGPUParams)};

        std::array<vk::WriteDescriptorSet, 4> writes{};
        writes[0] = {skyViewDS, 0, 0, 1, vk::DescriptorType::eStorageImage, &svImgInfo};
        writes[1] = {skyViewDS, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &transInfo};
        writes[2] = {skyViewDS, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &msInfo};
        writes[3] = {skyViewDS, 3, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo};
        dev.updateDescriptorSets(writes, nullptr);
    }

    void AtmospherePipeline::createAerialPerspectiveLUT()
    {
        create3DImage(32, 32, 32, aerialImage, aerialMemory, aerialView);

        std::vector<vk::DescriptorSetLayoutBinding> bindings{
            {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
            {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
            {2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
            {3, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute}
        };
        std::vector<vk::DescriptorPoolSize> poolSizes{
            {vk::DescriptorType::eStorageImage, 1},
            {vk::DescriptorType::eCombinedImageSampler, 2},
            {vk::DescriptorType::eUniformBuffer, 1}
        };

        aerialShader = std::make_shared<core::Shader>(device);
        aerialShader->readShader("../../resources/shaders/atmosphere/aerial_perspective_lut.glsl");

        auto kit = createComputeKit(device, bindings, poolSizes, *aerialShader);
        aerialDSLayout = kit.layout;
        aerialDSPool = kit.pool;
        aerialDS = kit.set;
        aerialPipelineLayout = kit.pipelineLayout;
        aerialPipeline = kit.pipeline;

        auto& dev = device.getLogicalDevice();
        vk::DescriptorImageInfo apImgInfo{nullptr, aerialView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo transInfo{lutSampler, transmittanceView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo msInfo{lutSampler, multiScatterView, vk::ImageLayout::eGeneral};
        vk::DescriptorBufferInfo bufInfo{paramsBuffer, 0, sizeof(AtmosphereGPUParams)};

        std::array<vk::WriteDescriptorSet, 4> writes{};
        writes[0] = {aerialDS, 0, 0, 1, vk::DescriptorType::eStorageImage, &apImgInfo};
        writes[1] = {aerialDS, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &transInfo};
        writes[2] = {aerialDS, 2, 0, 1, vk::DescriptorType::eCombinedImageSampler, &msInfo};
        writes[3] = {aerialDS, 3, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo};
        dev.updateDescriptorSets(writes, nullptr);
    }

    void AtmospherePipeline::createDepthOnlyView()
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

    void AtmospherePipeline::createSkyRenderPass()
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

        skyRenderPass = device.getLogicalDevice().createRenderPass(rpInfo);
    }

    void AtmospherePipeline::createSkyFramebuffers()
    {
        uint32_t imageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());
        skyFramebuffers.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = skyRenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &offscreenResources.colorImages[i].colorImageView;
            fbInfo.width = currentExtent.width;
            fbInfo.height = currentExtent.height;
            fbInfo.layers = 1;
            skyFramebuffers[i] = device.getLogicalDevice().createFramebuffer(fbInfo);
        }
    }

    void AtmospherePipeline::createCompositeRenderPass()
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

        compositeRenderPass = device.getLogicalDevice().createRenderPass(rpInfo);
    }

    void AtmospherePipeline::createCompositeFramebuffers()
    {
        uint32_t imageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());
        compositeFramebuffers.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = compositeRenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &offscreenResources.colorImages[i].colorImageView;
            fbInfo.width = currentExtent.width;
            fbInfo.height = currentExtent.height;
            fbInfo.layers = 1;
            compositeFramebuffers[i] = device.getLogicalDevice().createFramebuffer(fbInfo);
        }
    }

    void AtmospherePipeline::createSkyRenderer()
    {
        auto& dev = device.getLogicalDevice();

        createSkyRenderPass();
        createSkyFramebuffers();

        // Descriptor set layout: skyViewLUT (0), transmittanceLUT (1), params (2)
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

        // Write descriptors
        vk::DescriptorImageInfo skyViewInfo{lutSampler, skyViewView, vk::ImageLayout::eGeneral};
        vk::DescriptorImageInfo transInfo{lutSampler, transmittanceView, vk::ImageLayout::eGeneral};
        vk::DescriptorBufferInfo bufInfo{paramsBuffer, 0, sizeof(AtmosphereGPUParams)};

        std::array<vk::WriteDescriptorSet, 3> writes{};
        writes[0] = {skyRendererDS, 0, 0, 1, vk::DescriptorType::eCombinedImageSampler, &skyViewInfo};
        writes[1] = {skyRendererDS, 1, 0, 1, vk::DescriptorType::eCombinedImageSampler, &transInfo};
        writes[2] = {skyRendererDS, 2, 0, 1, vk::DescriptorType::eUniformBuffer, nullptr, &bufInfo};
        dev.updateDescriptorSets(writes, nullptr);

        // Pipeline layout
        vk::PipelineLayoutCreateInfo plInfo{};
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &skyRendererDSLayout;
        skyRendererPipelineLayout = dev.createPipelineLayout(plInfo);

        // Shader
        skyRendererShader = std::make_shared<core::Shader>(device);
        skyRendererShader->readShader("../../resources/shaders/atmosphere/atmosphere_sky.glsl");

        // Graphics pipeline (opaque sky write)
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
        pipelineInfo.renderPass = skyRenderPass;
        skyRendererPipeline = dev.createGraphicsPipeline(nullptr, pipelineInfo).value;
    }

    void AtmospherePipeline::createComposite()
    {
        auto& dev = device.getLogicalDevice();

        createCompositeRenderPass();
        createCompositeFramebuffers();
        createDepthOnlyView();

        // Descriptor: depth (0), aerialLUT (1), params (2)
        std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};
        bindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};
        bindings[1] = {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};
        bindings[2] = {2, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment};

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        compositeDSLayout = dev.createDescriptorSetLayout(layoutInfo);

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

        // Pipeline layout
        vk::PipelineLayoutCreateInfo plInfo{};
        plInfo.setLayoutCount = 1;
        plInfo.pSetLayouts = &compositeDSLayout;
        compositePipelineLayout = dev.createPipelineLayout(plInfo);

        // Shader
        compositeShader = std::make_shared<core::Shader>(device);
        compositeShader->readShader("../../resources/shaders/atmosphere/atmosphere_composite.glsl");

        // Graphics pipeline (additive blend with transmittance)
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
        vk::PipelineDepthStencilStateCreateInfo depthStencilState{};
        depthStencilState.depthTestEnable = VK_FALSE;
        depthStencilState.depthWriteEnable = VK_FALSE;

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
        vk::PipelineColorBlendStateCreateInfo blending{};
        blending.attachmentCount = 1; blending.pAttachments = &compositeBlend;

        const auto& stages = compositeShader->getShaderStages();
        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencilState;
        pipelineInfo.pColorBlendState = &blending;
        pipelineInfo.layout = compositePipelineLayout;
        pipelineInfo.renderPass = compositeRenderPass;
        compositePipeline = dev.createGraphicsPipeline(nullptr, pipelineInfo).value;
    }

    void AtmospherePipeline::cleanupSkyFramebuffers()
    {
        auto& dev = device.getLogicalDevice();
        for (auto& fb : skyFramebuffers)
        {
            if (fb) { dev.destroyFramebuffer(fb); fb = nullptr; }
        }
        skyFramebuffers.clear();
    }

    void AtmospherePipeline::cleanupCompositeFramebuffers()
    {
        auto& dev = device.getLogicalDevice();
        for (auto& fb : compositeFramebuffers)
        {
            if (fb) { dev.destroyFramebuffer(fb); fb = nullptr; }
        }
        compositeFramebuffers.clear();
    }

    void AtmospherePipeline::cleanupComputePipelines()
    {
        auto& dev = device.getLogicalDevice();
        auto destroyPipeline = [&](vk::Pipeline& p, vk::PipelineLayout& l) {
            if (p) { dev.destroyPipeline(p); p = nullptr; }
            if (l) { dev.destroyPipelineLayout(l); l = nullptr; }
        };
        destroyPipeline(transmittancePipeline, transmittancePipelineLayout);
        destroyPipeline(multiScatterPipeline, multiScatterPipelineLayout);
        destroyPipeline(skyViewPipeline, skyViewPipelineLayout);
        destroyPipeline(aerialPipeline, aerialPipelineLayout);
    }

    void AtmospherePipeline::cleanupGraphicsPipelines()
    {
        auto& dev = device.getLogicalDevice();
        auto destroyPipeline = [&](vk::Pipeline& p, vk::PipelineLayout& l) {
            if (p) { dev.destroyPipeline(p); p = nullptr; }
            if (l) { dev.destroyPipelineLayout(l); l = nullptr; }
        };
        destroyPipeline(skyRendererPipeline, skyRendererPipelineLayout);
        destroyPipeline(compositePipeline, compositePipelineLayout);
    }
}
