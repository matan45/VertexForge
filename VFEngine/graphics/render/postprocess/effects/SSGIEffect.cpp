#include "SSGIEffect.hpp"
#include "../PostProcessPipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/OffScreen.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "print/Log.hpp"
#include <cstring>
#include <random>

namespace render::postprocess
{
    SSGIEffect::SSGIEffect(core::Device& device, core::SwapChain& swapChain,
                           core::OffscreenResources& offscreenResources,
                           PostProcessPipeline& pipeline)
        : device(device)
        , swapChain(swapChain)
        , offscreenResources(offscreenResources)
        , pipeline(pipeline)
    {
    }

    void SSGIEffect::init(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        currentExtent = extent;

        createSampler();
        createRenderPasses();
        createImages();
        createDepthImageView();
        createParamsBuffer();
        createDescriptorSetLayouts();
        createDescriptorPool();
        createDescriptorSets();
        loadShaders();
        createTracePipeline();
        createDenoisePipeline();
        createCompositePipeline(renderPass);

        vfLogInfo("SSGIEffect: Initialized at {}x{}", extent.width, extent.height);
    }

    void SSGIEffect::cleanup()
    {
        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        cleanupPipelines();
        cleanupImages();

        if (descriptorPool) { vkDevice.destroyDescriptorPool(descriptorPool); descriptorPool = nullptr; }
        if (traceDescriptorSetLayout) { vkDevice.destroyDescriptorSetLayout(traceDescriptorSetLayout); traceDescriptorSetLayout = nullptr; }
        if (denoiseDescriptorSetLayout) { vkDevice.destroyDescriptorSetLayout(denoiseDescriptorSetLayout); denoiseDescriptorSetLayout = nullptr; }
        if (compositeDescriptorSetLayout) { vkDevice.destroyDescriptorSetLayout(compositeDescriptorSetLayout); compositeDescriptorSetLayout = nullptr; }

        if (paramsBufferMapped) { vkDevice.unmapMemory(paramsBufferMemory); paramsBufferMapped = nullptr; }
        core::BufferUtilities::destroyBuffer(vkDevice, paramsBuffer, paramsBufferMemory);

        if (sampler) { vkDevice.destroySampler(sampler); sampler = nullptr; }
        if (ssgiRenderPass) { vkDevice.destroyRenderPass(ssgiRenderPass); ssgiRenderPass = nullptr; }
        if (denoiseRenderPass) { vkDevice.destroyRenderPass(denoiseRenderPass); denoiseRenderPass = nullptr; }

        if (depthOnlyImageView) { vkDevice.destroyImageView(depthOnlyImageView); depthOnlyImageView = nullptr; }

        traceShader.reset();
        denoiseShader.reset();
        compositeShader.reset();
    }

    void SSGIEffect::recreate(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        cleanupImages();
        cleanupPipelines();

        if (depthOnlyImageView)
        {
            vkDevice.destroyImageView(depthOnlyImageView);
            depthOnlyImageView = nullptr;
        }

        currentExtent = extent;

        createImages();
        createDepthImageView();
        createDescriptorSets();
        createTracePipeline();
        createDenoisePipeline();
        createCompositePipeline(renderPass);
    }

    void SSGIEffect::preRecord(const vk::CommandBuffer& commandBuffer,
                               vk::DescriptorSet inputDescriptorSet)
    {
        ++frameCounter;
        updateParamsBuffer();

        // Trace pass: render to ssgiRawImage
        vk::ClearValue clearValue;
        clearValue.color = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});

        vk::RenderPassBeginInfo traceRPInfo{};
        traceRPInfo.renderPass = ssgiRenderPass;
        traceRPInfo.framebuffer = ssgiRawFramebuffer;
        traceRPInfo.renderArea.extent = currentExtent;
        traceRPInfo.clearValueCount = 1;
        traceRPInfo.pClearValues = &clearValue;

        commandBuffer.beginRenderPass(traceRPInfo, vk::SubpassContents::eInline);
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, tracePipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                          tracePipelineLayout, 0, 1,
                                          &traceDescriptorSet, 0, nullptr);
        commandBuffer.draw(3, 1, 0, 0);
        commandBuffer.endRenderPass();

        // Denoise pass: render to ssgiDenoisedImage
        vk::RenderPassBeginInfo denoiseRPInfo{};
        denoiseRPInfo.renderPass = denoiseRenderPass;
        denoiseRPInfo.framebuffer = ssgiDenoisedFramebuffer;
        denoiseRPInfo.renderArea.extent = currentExtent;
        denoiseRPInfo.clearValueCount = 1;
        denoiseRPInfo.pClearValues = &clearValue;

        commandBuffer.beginRenderPass(denoiseRPInfo, vk::SubpassContents::eInline);
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, denoisePipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                          denoisePipelineLayout, 0, 1,
                                          &denoiseDescriptorSet, 0, nullptr);
        commandBuffer.draw(3, 1, 0, 0);
        commandBuffer.endRenderPass();
    }

    void SSGIEffect::record(const vk::CommandBuffer& commandBuffer,
                            vk::DescriptorSet inputDescriptorSet)
    {
        // Composite pass: blend denoised SSGI onto scene
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, compositePipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                          compositePipelineLayout, 0, 1,
                                          &compositeDescriptorSet, 0, nullptr);
        commandBuffer.draw(3, 1, 0, 0);
    }

    void SSGIEffect::updateParameters(const ::postprocess::PostProcessSettings& settings)
    {
        // SSGI settings are managed through GISettings, not PostProcessSettings
        // This is called but SSGI params are set via updateGIParameters
    }

    void SSGIEffect::updateGIParameters(const gi::GISettings& giSettings)
    {
        currentIntensity = giSettings.ssgiIntensity;
        currentRadius = giSettings.ssgiRadius;
        currentRayCount = giSettings.ssgiRayCount;
        currentStepCount = giSettings.ssgiStepCount;
        currentThickness = giSettings.ssgiThickness;
        setEnabled(giSettings.enabled && giSettings.quality >= gi::GIQuality::Low);
    }

    void SSGIEffect::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        sampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void SSGIEffect::createRenderPasses()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = vk::Format::eR16G16B16A16Sfloat;
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
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
        dep.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dep.srcAccessMask = {};
        dep.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dep;

        ssgiRenderPass = vkDevice.createRenderPass(rpInfo);
        denoiseRenderPass = vkDevice.createRenderPass(rpInfo);
    }

    void SSGIEffect::createImages()
    {
        vk::Device vkDevice = device.getLogicalDevice();
        vk::PhysicalDevice physDevice = device.getPhysicalDevice();

        auto createImage = [&](vk::Image& image, vk::DeviceMemory& memory,
                               vk::ImageView& imageView, vk::Framebuffer& framebuffer,
                               vk::RenderPass renderPass) {
            vk::ImageCreateInfo imgInfo{};
            imgInfo.imageType = vk::ImageType::e2D;
            imgInfo.format = vk::Format::eR16G16B16A16Sfloat;
            imgInfo.extent = vk::Extent3D(currentExtent.width, currentExtent.height, 1);
            imgInfo.mipLevels = 1;
            imgInfo.arrayLayers = 1;
            imgInfo.samples = vk::SampleCountFlagBits::e1;
            imgInfo.tiling = vk::ImageTiling::eOptimal;
            imgInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
            imgInfo.initialLayout = vk::ImageLayout::eUndefined;

            image = vkDevice.createImage(imgInfo);

            auto memReqs = vkDevice.getImageMemoryRequirements(image);
            auto memProps = physDevice.getMemoryProperties();
            uint32_t memTypeIdx = 0;
            for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
            {
                if ((memReqs.memoryTypeBits & (1 << i)) &&
                    (memProps.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal))
                {
                    memTypeIdx = i;
                    break;
                }
            }

            vk::MemoryAllocateInfo allocInfo{memReqs.size, memTypeIdx};
            memory = vkDevice.allocateMemory(allocInfo);
            vkDevice.bindImageMemory(image, memory, 0);

            vk::ImageViewCreateInfo viewInfo{};
            viewInfo.image = image;
            viewInfo.viewType = vk::ImageViewType::e2D;
            viewInfo.format = vk::Format::eR16G16B16A16Sfloat;
            viewInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
            imageView = vkDevice.createImageView(viewInfo);

            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = renderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &imageView;
            fbInfo.width = currentExtent.width;
            fbInfo.height = currentExtent.height;
            fbInfo.layers = 1;
            framebuffer = vkDevice.createFramebuffer(fbInfo);
        };

        createImage(ssgiRawImage, ssgiRawMemory, ssgiRawImageView,
                    ssgiRawFramebuffer, ssgiRenderPass);
        createImage(ssgiDenoisedImage, ssgiDenoisedMemory, ssgiDenoisedImageView,
                    ssgiDenoisedFramebuffer, denoiseRenderPass);
    }

    void SSGIEffect::cleanupImages()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        auto destroyImage = [&](vk::Image& image, vk::DeviceMemory& memory,
                                vk::ImageView& imageView, vk::Framebuffer& framebuffer) {
            if (framebuffer) { vkDevice.destroyFramebuffer(framebuffer); framebuffer = nullptr; }
            if (imageView) { vkDevice.destroyImageView(imageView); imageView = nullptr; }
            if (image) { vkDevice.destroyImage(image); image = nullptr; }
            if (memory) { vkDevice.freeMemory(memory); memory = nullptr; }
        };

        destroyImage(ssgiRawImage, ssgiRawMemory, ssgiRawImageView, ssgiRawFramebuffer);
        destroyImage(ssgiDenoisedImage, ssgiDenoisedMemory, ssgiDenoisedImageView, ssgiDenoisedFramebuffer);
    }

    void SSGIEffect::createDepthImageView()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::Format depthFormat = swapChain.getSwapchainDepthStencilFormat();
        depthAspectMask = vk::ImageAspectFlagBits::eDepth;

        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = offscreenResources.depthImage.depthImage;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = depthFormat;
        viewInfo.subresourceRange = {depthAspectMask, 0, 1, 0, 1};
        depthOnlyImageView = vkDevice.createImageView(viewInfo);
    }

    void SSGIEffect::createParamsBuffer()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        vk::DeviceSize bufferSize = sizeof(SSGIParamsUBO);

        core::BufferInfoRequest request(logicalDevice, physicalDevice);
        request.size = bufferSize;
        request.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        request.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(request, paramsBuffer, paramsBufferMemory);

        paramsBufferMapped = logicalDevice.mapMemory(paramsBufferMemory, 0, bufferSize, vk::MemoryMapFlags{});
    }

    void SSGIEffect::updateParamsBuffer()
    {
        if (!paramsBufferMapped) return;

        const auto& camInfo = pipeline.getCameraData();

        SSGIParamsUBO params{};
        params.projection = camInfo.projectionMatrix;
        params.inverseProjection = glm::inverse(camInfo.projectionMatrix);
        params.viewMatrix = camInfo.viewMatrix;
        params.params = glm::vec4(currentIntensity, currentRadius, currentThickness,
                                  static_cast<float>(currentRayCount));
        params.screenParams = glm::vec4(
            static_cast<float>(currentExtent.width),
            static_cast<float>(currentExtent.height),
            1.0f / static_cast<float>(currentExtent.width),
            1.0f / static_cast<float>(currentExtent.height));
        params.nearPlane = camInfo.nearPlane;
        params.farPlane = camInfo.farPlane;
        params.stepCount = currentStepCount;

        static std::mt19937 rng(42);
        static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        params.frameRandom = dist(rng);

        std::memcpy(paramsBufferMapped, &params, sizeof(SSGIParamsUBO));
    }

    void SSGIEffect::createDescriptorSetLayouts()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Trace layout: scene color + depth + params
        {
            std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};
            bindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1,
                          vk::ShaderStageFlagBits::eFragment};
            bindings[1] = {1, vk::DescriptorType::eCombinedImageSampler, 1,
                          vk::ShaderStageFlagBits::eFragment};
            bindings[2] = {2, vk::DescriptorType::eUniformBuffer, 1,
                          vk::ShaderStageFlagBits::eFragment};

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();
            traceDescriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
        }

        // Denoise layout: raw SSGI + depth + params
        {
            std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};
            bindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1,
                          vk::ShaderStageFlagBits::eFragment};
            bindings[1] = {1, vk::DescriptorType::eCombinedImageSampler, 1,
                          vk::ShaderStageFlagBits::eFragment};
            bindings[2] = {2, vk::DescriptorType::eUniformBuffer, 1,
                          vk::ShaderStageFlagBits::eFragment};

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();
            denoiseDescriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
        }

        // Composite layout: scene input + denoised SSGI
        {
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            bindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1,
                          vk::ShaderStageFlagBits::eFragment};
            bindings[1] = {1, vk::DescriptorType::eCombinedImageSampler, 1,
                          vk::ShaderStageFlagBits::eFragment};

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();
            compositeDescriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
        }
    }

    void SSGIEffect::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eCombinedImageSampler, 8};
        poolSizes[1] = {vk::DescriptorType::eUniformBuffer, 2};

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 3;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
    }

    void SSGIEffect::createDescriptorSets()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Allocate sets
        auto allocSet = [&](vk::DescriptorSetLayout layout) -> vk::DescriptorSet {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &layout;
            return vkDevice.allocateDescriptorSets(allocInfo)[0];
        };

        traceDescriptorSet = allocSet(traceDescriptorSetLayout);
        denoiseDescriptorSet = allocSet(denoiseDescriptorSetLayout);
        compositeDescriptorSet = allocSet(compositeDescriptorSetLayout);

        // Get color image view from offscreen resources
        vk::ImageView sceneColorView = offscreenResources.colorImages.empty()
            ? vk::ImageView{} : offscreenResources.colorImages[0].colorImageView;

        // Update trace set (scene color + depth + params)
        {
            vk::DescriptorImageInfo sceneColorInfo(sampler, sceneColorView,
                                                    vk::ImageLayout::eShaderReadOnlyOptimal);
            vk::DescriptorImageInfo depthInfo(sampler, depthOnlyImageView,
                                              vk::ImageLayout::eShaderReadOnlyOptimal);
            vk::DescriptorBufferInfo paramsInfo(paramsBuffer, 0, sizeof(SSGIParamsUBO));

            std::array<vk::WriteDescriptorSet, 3> writes{};
            writes[0].dstSet = traceDescriptorSet;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[0].pImageInfo = &sceneColorInfo;

            writes[1].dstSet = traceDescriptorSet;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[1].pImageInfo = &depthInfo;

            writes[2].dstSet = traceDescriptorSet;
            writes[2].dstBinding = 2;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = vk::DescriptorType::eUniformBuffer;
            writes[2].pBufferInfo = &paramsInfo;

            vkDevice.updateDescriptorSets(writes, {});
        }

        // Update denoise set (raw SSGI + depth + params)
        {
            vk::DescriptorImageInfo rawSSGIInfo(sampler, ssgiRawImageView,
                                                 vk::ImageLayout::eShaderReadOnlyOptimal);
            vk::DescriptorImageInfo depthInfo(sampler, depthOnlyImageView,
                                              vk::ImageLayout::eShaderReadOnlyOptimal);
            vk::DescriptorBufferInfo paramsInfo(paramsBuffer, 0, sizeof(SSGIParamsUBO));

            std::array<vk::WriteDescriptorSet, 3> writes{};
            writes[0].dstSet = denoiseDescriptorSet;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[0].pImageInfo = &rawSSGIInfo;

            writes[1].dstSet = denoiseDescriptorSet;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[1].pImageInfo = &depthInfo;

            writes[2].dstSet = denoiseDescriptorSet;
            writes[2].dstBinding = 2;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = vk::DescriptorType::eUniformBuffer;
            writes[2].pBufferInfo = &paramsInfo;

            vkDevice.updateDescriptorSets(writes, {});
        }

        // Update composite set (scene + denoised)
        {
            vk::DescriptorImageInfo sceneInfo(sampler, sceneColorView,
                                               vk::ImageLayout::eShaderReadOnlyOptimal);
            vk::DescriptorImageInfo denoisedInfo(sampler, ssgiDenoisedImageView,
                                                  vk::ImageLayout::eShaderReadOnlyOptimal);

            std::array<vk::WriteDescriptorSet, 2> writes{};
            writes[0].dstSet = compositeDescriptorSet;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[0].pImageInfo = &sceneInfo;

            writes[1].dstSet = compositeDescriptorSet;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[1].pImageInfo = &denoisedInfo;

            vkDevice.updateDescriptorSets(writes, {});
        }
    }

    void SSGIEffect::loadShaders()
    {
        traceShader = std::make_shared<core::Shader>(device);
        traceShader->readShader("../../resources/shaders/postprocess/ssgi_trace.glsl");

        denoiseShader = std::make_shared<core::Shader>(device);
        denoiseShader->readShader("../../resources/shaders/postprocess/ssgi_denoise.glsl");

        compositeShader = std::make_shared<core::Shader>(device);
        compositeShader->readShader("../../resources/shaders/postprocess/ssgi_composite.glsl");
    }

    void SSGIEffect::createTracePipeline()
    {
        // Pipeline creation follows the same pattern as SSAOEffect
        // Full-screen triangle with fragment shader doing screen-space ray tracing
        vk::Device vkDevice = device.getLogicalDevice();

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &traceDescriptorSetLayout;
        tracePipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        // Pipeline created using shader stages from traceShader
        // (Simplified - actual implementation uses full pipeline builder)
        const auto& stages = traceShader->getShaderStages();

        vk::PipelineVertexInputStateCreateInfo vertexInput{};
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        vk::Viewport viewport{0.0f, 0.0f,
                              static_cast<float>(currentExtent.width),
                              static_cast<float>(currentExtent.height),
                              0.0f, 1.0f};
        vk::Rect2D scissor{{0, 0}, currentExtent};

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eNone;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.layout = tracePipelineLayout;
        pipelineInfo.renderPass = ssgiRenderPass;

        auto result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
        tracePipeline = result.value;
    }

    void SSGIEffect::createDenoisePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &denoiseDescriptorSetLayout;
        denoisePipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        const auto& stages = denoiseShader->getShaderStages();

        vk::PipelineVertexInputStateCreateInfo vertexInput{};
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        vk::Viewport viewport{0.0f, 0.0f,
                              static_cast<float>(currentExtent.width),
                              static_cast<float>(currentExtent.height),
                              0.0f, 1.0f};
        vk::Rect2D scissor{{0, 0}, currentExtent};

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eNone;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.layout = denoisePipelineLayout;
        pipelineInfo.renderPass = denoiseRenderPass;

        auto result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
        denoisePipeline = result.value;
    }

    void SSGIEffect::createCompositePipeline(vk::RenderPass externalRenderPass)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &compositeDescriptorSetLayout;
        compositePipelineLayout = vkDevice.createPipelineLayout(layoutInfo);

        const auto& stages = compositeShader->getShaderStages();

        vk::PipelineVertexInputStateCreateInfo vertexInput{};
        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        vk::Viewport viewport{0.0f, 0.0f,
                              static_cast<float>(currentExtent.width),
                              static_cast<float>(currentExtent.height),
                              0.0f, 1.0f};
        vk::Rect2D scissor{{0, 0}, currentExtent};

        vk::PipelineViewportStateCreateInfo viewportState{};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        vk::PipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eNone;

        vk::PipelineMultisampleStateCreateInfo multisampling{};
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

        // Additive blending for indirect lighting
        vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eOne;
        colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOne;
        colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
        colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
        colorBlendAttachment.colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        vk::PipelineDepthStencilStateCreateInfo depthStencil{};

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.layout = compositePipelineLayout;
        pipelineInfo.renderPass = externalRenderPass;

        auto result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
        compositePipeline = result.value;
    }

    void SSGIEffect::cleanupPipelines()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        if (tracePipeline) { vkDevice.destroyPipeline(tracePipeline); tracePipeline = nullptr; }
        if (tracePipelineLayout) { vkDevice.destroyPipelineLayout(tracePipelineLayout); tracePipelineLayout = nullptr; }
        if (denoisePipeline) { vkDevice.destroyPipeline(denoisePipeline); denoisePipeline = nullptr; }
        if (denoisePipelineLayout) { vkDevice.destroyPipelineLayout(denoisePipelineLayout); denoisePipelineLayout = nullptr; }
        if (compositePipeline) { vkDevice.destroyPipeline(compositePipeline); compositePipeline = nullptr; }
        if (compositePipelineLayout) { vkDevice.destroyPipelineLayout(compositePipelineLayout); compositePipelineLayout = nullptr; }
    }
}
