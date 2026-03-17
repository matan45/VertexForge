#include "SSGIPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include <glm/gtc/matrix_inverse.hpp>
#include <cstring>

namespace render::gi
{
    SSGIPipeline::SSGIPipeline(core::Device& device, core::SwapChain& swapChain,
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

    SSGIPipeline::~SSGIPipeline()
    {
        cleanup();
    }

    void SSGIPipeline::init()
    {
        currentExtent = swapChain.getSwapchainExtent();
        traceExtent = ssgiHalfResolution
            ? vk::Extent2D{std::max(currentExtent.width / 2, 1u), std::max(currentExtent.height / 2, 1u)}
            : currentExtent;

        createSampler();
        createDepthImageView();
        createIntermediateImages();
        createParamsBuffer();

        createTraceRenderPass();
        createTemporalRenderPass();
        createDenoiseRenderPass();
        createCompositeRenderPass();

        createTraceFramebuffer();
        createTemporalFramebuffer();
        createDenoiseFramebuffer();
        createCompositeFramebuffers();

        createDescriptorSetLayouts();
        createDescriptorPool();
        createDescriptorSets();
        updateDescriptorSets();

        loadShaders();

        createTracePipeline();
        createTemporalPipeline();
        createDenoisePipeline();
        createCompositePipeline();

        initialized = true;
    }

    void SSGIPipeline::cleanup()
    {
        if (!initialized)
            return;

        auto& dev = device.getLogicalDevice();

        cleanupPipelines();
        cleanupFramebuffers();
        cleanupIntermediateImages();
        cleanupRenderPasses();

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

        // Destroy descriptor set layouts
        auto destroyLayout = [&](vk::DescriptorSetLayout& layout)
        {
            if (layout)
            {
                dev.destroyDescriptorSetLayout(layout);
                layout = nullptr;
            }
        };
        destroyLayout(traceSet0Layout);
        destroyLayout(traceSet1Layout);
        destroyLayout(temporalSet0Layout);
        destroyLayout(temporalSet1Layout);
        destroyLayout(denoiseSet0Layout);
        destroyLayout(compositeSet0Layout);

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

        auto cleanupShader = [](std::shared_ptr<core::Shader>& s)
        {
            if (s)
            {
                s->cleanUp();
                s.reset();
            }
        };
        cleanupShader(traceShader);
        cleanupShader(temporalShader);
        cleanupShader(denoiseShader);
        cleanupShader(compositeShader);

        historyValid = false;
        initialized = false;
    }

    void SSGIPipeline::recreate()
    {
        if (!initialized)
            return;

        auto& dev = device.getLogicalDevice();
        currentExtent = swapChain.getSwapchainExtent();
        traceExtent = ssgiHalfResolution
            ? vk::Extent2D{std::max(currentExtent.width / 2, 1u), std::max(currentExtent.height / 2, 1u)}
            : currentExtent;

        cleanupPipelines();
        cleanupFramebuffers();
        cleanupIntermediateImages();
        cleanupRenderPasses();

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

        createDepthImageView();
        createIntermediateImages();

        createTraceRenderPass();
        createTemporalRenderPass();
        createDenoiseRenderPass();
        createCompositeRenderPass();

        createTraceFramebuffer();
        createTemporalFramebuffer();
        createDenoiseFramebuffer();
        createCompositeFramebuffers();

        createDescriptorPool();
        createDescriptorSets();
        updateDescriptorSets();

        createTracePipeline();
        createTemporalPipeline();
        createDenoisePipeline();
        createCompositePipeline();

        historyValid = false;
    }

    void SSGIPipeline::execute(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        if (!initialized)
            return;

        updateParamsBuffer();

        // Transition depth to read-only
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            depthAspectMask);

        // Scene color is already in eShaderReadOnlyOptimal from previous pass (volumetric fog composite)

        // ---- Pass 1: Trace ----
        {
            vk::ClearValue clearValue{};
            clearValue.color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};

            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = traceRenderPass;
            rpBegin.framebuffer = traceFramebuffer;
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = traceExtent;
            rpBegin.clearValueCount = 1;
            rpBegin.pClearValues = &clearValue;

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, tracePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                tracePipelineLayout, 0, traceSet0PerImage[imageIndex], nullptr);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                tracePipelineLayout, 1, traceSet1, nullptr);
            commandBuffer.draw(3, 1, 0, 0);
            commandBuffer.endRenderPass();
        }

        // ssgiRawImage is now in eShaderReadOnlyOptimal (from render pass finalLayout)

        // ---- Pass 2: Temporal ----
        {
            uint32_t readIdx = currentHistoryIdx;
            uint32_t writeIdx = 1 - currentHistoryIdx;

            // On first frame, transition both history images from undefined to shader read
            if (!historyValid)
            {
                for (uint32_t i = 0; i < 2; i++)
                {
                    core::ImageUtilities::transitionImageLayout(commandBuffer,
                        ssgiHistoryImages[i],
                        vk::ImageLayout::eUndefined,
                        vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::ImageAspectFlagBits::eColor);
                }
            }

            vk::ClearValue clearValue{};
            clearValue.color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};

            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = temporalRenderPass;
            rpBegin.framebuffer = temporalFramebuffers[writeIdx];
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = traceExtent;
            rpBegin.clearValueCount = 1;
            rpBegin.pClearValues = &clearValue;

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, temporalPipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                temporalPipelineLayout, 0, temporalSet0, nullptr);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                temporalPipelineLayout, 1, temporalSet1PerHistory[readIdx], nullptr);
            commandBuffer.draw(3, 1, 0, 0);
            commandBuffer.endRenderPass();

            // ssgiHistory[writeIdx] is now in eShaderReadOnlyOptimal (from render pass finalLayout)

            currentHistoryIdx = writeIdx;
            historyValid = true;
        }

        // ---- Pass 3a: Denoise Horizontal ----
        {
            vk::ClearValue clearValue{};
            clearValue.color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};

            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = denoiseRenderPass;
            rpBegin.framebuffer = denoiseHorizFramebuffer;
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = traceExtent;
            rpBegin.clearValueCount = 1;
            rpBegin.pClearValues = &clearValue;

            DenoisePushConstants denoisePush{};
            denoisePush.texelSize = glm::vec2(1.0f / static_cast<float>(traceExtent.width),
                                               1.0f / static_cast<float>(traceExtent.height));
            denoisePush.nearPlane = cachedNear;
            denoisePush.farPlane = cachedFar;
            denoisePush.direction = glm::vec2(1.0f, 0.0f);

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, denoisePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                denoisePipelineLayout, 0, denoiseHorizSet0PerHistory[currentHistoryIdx], nullptr);
            commandBuffer.pushConstants(denoisePipelineLayout,
                vk::ShaderStageFlagBits::eFragment, 0,
                sizeof(DenoisePushConstants), &denoisePush);
            commandBuffer.draw(3, 1, 0, 0);
            commandBuffer.endRenderPass();
        }

        // ssgiDenoiseHorizImage is now in eShaderReadOnlyOptimal

        // ---- Pass 3b: Denoise Vertical ----
        {
            vk::ClearValue clearValue{};
            clearValue.color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};

            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = denoiseRenderPass;
            rpBegin.framebuffer = denoiseFramebuffer;
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = traceExtent;
            rpBegin.clearValueCount = 1;
            rpBegin.pClearValues = &clearValue;

            DenoisePushConstants denoisePush{};
            denoisePush.texelSize = glm::vec2(1.0f / static_cast<float>(traceExtent.width),
                                               1.0f / static_cast<float>(traceExtent.height));
            denoisePush.nearPlane = cachedNear;
            denoisePush.farPlane = cachedFar;
            denoisePush.direction = glm::vec2(0.0f, 1.0f);

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, denoisePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                denoisePipelineLayout, 0, denoiseSet0, nullptr);
            commandBuffer.pushConstants(denoisePipelineLayout,
                vk::ShaderStageFlagBits::eFragment, 0,
                sizeof(DenoisePushConstants), &denoisePush);
            commandBuffer.draw(3, 1, 0, 0);
            commandBuffer.endRenderPass();
        }

        // ssgiDenoisedImage is now in eShaderReadOnlyOptimal

        // ---- Pass 4: Composite ----
        {
            // Transition scene color to eColorAttachmentOptimal for composite write
            vk::Image sceneColor = offscreenResources.colorImages[imageIndex].colorImage;
            core::ImageUtilities::transitionImageLayout(commandBuffer, sceneColor,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageAspectFlagBits::eColor);

            CompositePushConstants compositePush{};
            compositePush.intensity = ssgiIntensity;
            compositePush.halfResolution = ssgiHalfResolution ? 1u : 0u;
            compositePush.nearPlane = cachedNear;
            compositePush.farPlane = cachedFar;
            compositePush.texelSize = glm::vec2(1.0f / static_cast<float>(currentExtent.width),
                                                 1.0f / static_cast<float>(currentExtent.height));

            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = compositeRenderPass;
            rpBegin.framebuffer = compositeFramebuffers[imageIndex];
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = currentExtent;

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, compositePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                compositePipelineLayout, 0, compositeSet0, nullptr);
            commandBuffer.pushConstants(compositePipelineLayout,
                vk::ShaderStageFlagBits::eFragment, 0,
                sizeof(CompositePushConstants), &compositePush);
            commandBuffer.draw(3, 1, 0, 0);
            commandBuffer.endRenderPass();
        }

        // Transition depth back to attachment optimal
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            depthAspectMask);

        // Scene color is now in eShaderReadOnlyOptimal (from composite render pass finalLayout)
    }

    void SSGIPipeline::setCameraData(const glm::mat4& view, const glm::mat4& projection,
                                      const glm::vec3& cameraPosition,
                                      float nearPlane, float farPlane, uint32_t frameIndex)
    {
        // Track previous frame matrices for temporal reprojection
        cachedPrevView = cachedView;
        cachedPrevProjection = cachedProjection;

        cachedView = view;
        cachedProjection = projection;
        cachedCameraPosition = cameraPosition;
        cachedNear = nearPlane;
        cachedFar = farPlane;
        cachedFrameIndex = frameIndex;
    }

    void SSGIPipeline::updateSettings(const gi::GISettings& settings)
    {
        ssgiRadius = settings.ssgiRadius;
        ssgiMaxDistance = settings.ssgiMaxDistance;
        ssgiIntensity = settings.ssgiIntensity;
        ssgiSampleCount = static_cast<uint32_t>(settings.ssgiSampleCount);
        ssgiTemporalBlend = settings.ssgiTemporalBlend;
        ssgiHalfResolution = settings.ssgiHalfResolution;
    }

    // ---- Resource Creation ----

    void SSGIPipeline::createSampler()
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

    void SSGIPipeline::createDepthImageView()
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

    void SSGIPipeline::createImageAndView(vk::Image& image, vk::DeviceMemory& memory,
                                           vk::ImageView& view, vk::Extent2D extent,
                                           vk::Format format)
    {
        core::ImageInfoRequest req(device.getLogicalDevice(), device.getPhysicalDevice());
        req.width = extent.width;
        req.height = extent.height;
        req.format = format;
        req.tiling = vk::ImageTiling::eOptimal;
        req.usage = vk::ImageUsageFlagBits::eColorAttachment
                  | vk::ImageUsageFlagBits::eSampled
                  | vk::ImageUsageFlagBits::eTransferSrc
                  | vk::ImageUsageFlagBits::eTransferDst;
        req.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::ImageUtilities::createImage(req, image, memory);

        core::ImageViewInfoRequest viewReq(device.getLogicalDevice(), image);
        viewReq.format = format;
        core::ImageUtilities::createImageView(viewReq, view);
    }

    void SSGIPipeline::destroyImageAndView(vk::Image& image, vk::DeviceMemory& memory,
                                            vk::ImageView& view)
    {
        auto& dev = device.getLogicalDevice();
        if (view)
        {
            dev.destroyImageView(view);
            view = nullptr;
        }
        if (image)
        {
            dev.destroyImage(image);
            image = nullptr;
        }
        if (memory)
        {
            dev.freeMemory(memory);
            memory = nullptr;
        }
    }

    void SSGIPipeline::createIntermediateImages()
    {
        // Trace output (may be half-res)
        createImageAndView(ssgiRawImage, ssgiRawMemory, ssgiRawImageView,
                           traceExtent, SSGI_FORMAT);

        // Horizontal blur intermediate (same res as trace)
        createImageAndView(ssgiDenoiseHorizImage, ssgiDenoiseHorizMemory, ssgiDenoiseHorizImageView,
                           traceExtent, SSGI_FORMAT);

        // Denoised output / vertical blur (same res as trace)
        createImageAndView(ssgiDenoisedImage, ssgiDenoisedMemory, ssgiDenoisedImageView,
                           traceExtent, SSGI_FORMAT);

        // Double-buffered history
        for (uint32_t i = 0; i < 2; ++i)
        {
            createImageAndView(ssgiHistoryImages[i], ssgiHistoryMemory[i],
                               ssgiHistoryImageViews[i], traceExtent, SSGI_FORMAT);
        }
    }

    void SSGIPipeline::createParamsBuffer()
    {
        auto& dev = device.getLogicalDevice();

        core::BufferInfoRequest bufReq(dev, device.getPhysicalDevice());
        bufReq.size = sizeof(SSGIParamsUBO);
        bufReq.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufReq.properties = vk::MemoryPropertyFlagBits::eHostVisible
                          | vk::MemoryPropertyFlagBits::eHostCoherent;

        core::BufferUtilities::createBuffer(bufReq, paramsBuffer, paramsBufferMemory);
        paramsBufferMapped = dev.mapMemory(paramsBufferMemory, 0, sizeof(SSGIParamsUBO));
    }

    // ---- Render Passes ----

    void SSGIPipeline::createTraceRenderPass()
    {
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = SSGI_FORMAT;
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
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
        dependency.srcAccessMask = {};
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;

        traceRenderPass = device.getLogicalDevice().createRenderPass(rpInfo);
    }

    void SSGIPipeline::createTemporalRenderPass()
    {
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = SSGI_FORMAT;
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
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
        dependency.srcAccessMask = {};
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;

        temporalRenderPass = device.getLogicalDevice().createRenderPass(rpInfo);
    }

    void SSGIPipeline::createDenoiseRenderPass()
    {
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = SSGI_FORMAT;
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
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
        dependency.srcAccessMask = {};
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        vk::RenderPassCreateInfo rpInfo{};
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;

        denoiseRenderPass = device.getLogicalDevice().createRenderPass(rpInfo);
    }

    void SSGIPipeline::createCompositeRenderPass()
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

        compositeRenderPass = device.getLogicalDevice().createRenderPass(rpInfo);
    }

    // ---- Framebuffers ----

    void SSGIPipeline::createTraceFramebuffer()
    {
        vk::FramebufferCreateInfo fbInfo{};
        fbInfo.renderPass = traceRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &ssgiRawImageView;
        fbInfo.width = traceExtent.width;
        fbInfo.height = traceExtent.height;
        fbInfo.layers = 1;

        traceFramebuffer = device.getLogicalDevice().createFramebuffer(fbInfo);
    }

    void SSGIPipeline::createTemporalFramebuffer()
    {
        for (uint32_t i = 0; i < 2; ++i)
        {
            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = temporalRenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &ssgiHistoryImageViews[i];
            fbInfo.width = traceExtent.width;
            fbInfo.height = traceExtent.height;
            fbInfo.layers = 1;

            temporalFramebuffers[i] = device.getLogicalDevice().createFramebuffer(fbInfo);
        }
    }

    void SSGIPipeline::createDenoiseFramebuffer()
    {
        // Horizontal blur → ssgiDenoiseHorizImage
        {
            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = denoiseRenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &ssgiDenoiseHorizImageView;
            fbInfo.width = traceExtent.width;
            fbInfo.height = traceExtent.height;
            fbInfo.layers = 1;

            denoiseHorizFramebuffer = device.getLogicalDevice().createFramebuffer(fbInfo);
        }

        // Vertical blur → ssgiDenoisedImage
        {
            vk::FramebufferCreateInfo fbInfo{};
            fbInfo.renderPass = denoiseRenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &ssgiDenoisedImageView;
            fbInfo.width = traceExtent.width;
            fbInfo.height = traceExtent.height;
            fbInfo.layers = 1;

            denoiseFramebuffer = device.getLogicalDevice().createFramebuffer(fbInfo);
        }
    }

    void SSGIPipeline::createCompositeFramebuffers()
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

    // ---- Descriptor Set Layouts ----

    void SSGIPipeline::createDescriptorSetLayouts()
    {
        auto& dev = device.getLogicalDevice();

        // Trace Set 0: scene color (binding 0)
        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;

            traceSet0Layout = dev.createDescriptorSetLayout(layoutInfo);
        }

        // Trace Set 1: depth (binding 0) + UBO (binding 1)
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

            traceSet1Layout = dev.createDescriptorSetLayout(layoutInfo);
        }

        // Temporal Set 0: ssgiRaw (binding 0)
        {
            vk::DescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            binding.descriptorCount = 1;
            binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &binding;

            temporalSet0Layout = dev.createDescriptorSetLayout(layoutInfo);
        }

        // Temporal Set 1: history (binding 0) + depth (binding 1) + UBO (binding 2)
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

            temporalSet1Layout = dev.createDescriptorSetLayout(layoutInfo);
        }

        // Denoise Set 0: ssgiAccum (binding 0) + depth (binding 1)
        {
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

            denoiseSet0Layout = dev.createDescriptorSetLayout(layoutInfo);
        }

        // Composite Set 0: ssgiDenoised (binding 0) + depth (binding 1)
        {
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

            compositeSet0Layout = dev.createDescriptorSetLayout(layoutInfo);
        }
    }

    void SSGIPipeline::createDescriptorPool()
    {
        uint32_t imageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());

        // Count descriptor needs:
        // Trace: imageCount sets with 1 sampler each (set0) + 1 set with 1 sampler + 1 UBO (set1)
        // Temporal: 1 set with 1 sampler (set0) + 2 sets with 2 samplers + 1 UBO each (set1 per history)
        // Denoise horiz: 2 sets with 2 samplers each (per history)
        // Denoise vert: 1 set with 2 samplers
        // Composite: 1 set with 2 samplers
        // Total samplers: imageCount + 1 + 1 + 2*2 + 2*2 + 2 + 2 = imageCount + 12
        // Total UBOs: 1 + 2 = 3

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = imageCount + 12;

        poolSizes[1].type = vk::DescriptorType::eUniformBuffer;
        poolSizes[1].descriptorCount = 3;

        // Total sets: imageCount(trace0) + 1(trace1) + 1(temp0) + 2(temp1) + 2(denoiseHoriz) + 1(denoiseVert) + 1(composite)
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.maxSets = imageCount + 8;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
    }

    void SSGIPipeline::createDescriptorSets()
    {
        auto& dev = device.getLogicalDevice();
        uint32_t imageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());

        // Allocate trace set0 per image (scene color)
        {
            std::vector<vk::DescriptorSetLayout> layouts(imageCount, traceSet0Layout);
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = imageCount;
            allocInfo.pSetLayouts = layouts.data();

            traceSet0PerImage = dev.allocateDescriptorSets(allocInfo);
        }

        // Allocate trace set1 (depth + UBO)
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &traceSet1Layout;

            traceSet1 = dev.allocateDescriptorSets(allocInfo)[0];
        }

        // Allocate temporal set0 (ssgiRaw)
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &temporalSet0Layout;

            temporalSet0 = dev.allocateDescriptorSets(allocInfo)[0];
        }

        // Allocate temporal set1 per history (2 sets)
        {
            std::array<vk::DescriptorSetLayout, 2> layouts = {temporalSet1Layout, temporalSet1Layout};
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 2;
            allocInfo.pSetLayouts = layouts.data();

            auto sets = dev.allocateDescriptorSets(allocInfo);
            temporalSet1PerHistory[0] = sets[0];
            temporalSet1PerHistory[1] = sets[1];
        }

        // Allocate denoise horizontal set0 per history (reads ssgiHistory[i])
        {
            std::array<vk::DescriptorSetLayout, 2> layouts = {denoiseSet0Layout, denoiseSet0Layout};
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 2;
            allocInfo.pSetLayouts = layouts.data();

            auto sets = dev.allocateDescriptorSets(allocInfo);
            denoiseHorizSet0PerHistory[0] = sets[0];
            denoiseHorizSet0PerHistory[1] = sets[1];
        }

        // Allocate denoise vertical set0 (reads ssgiDenoiseHoriz)
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &denoiseSet0Layout;

            denoiseSet0 = dev.allocateDescriptorSets(allocInfo)[0];
        }

        // Allocate composite set0
        {
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &compositeSet0Layout;

            compositeSet0 = dev.allocateDescriptorSets(allocInfo)[0];
        }
    }

    void SSGIPipeline::updateDescriptorSets()
    {
        auto& dev = device.getLogicalDevice();
        uint32_t imageCount = static_cast<uint32_t>(offscreenResources.colorImages.size());

        // Common image infos
        vk::DescriptorImageInfo depthImageInfo{};
        depthImageInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        depthImageInfo.imageView = depthOnlyImageView;
        depthImageInfo.sampler = sampler;

        vk::DescriptorBufferInfo uboInfo{};
        uboInfo.buffer = paramsBuffer;
        uboInfo.offset = 0;
        uboInfo.range = sizeof(SSGIParamsUBO);

        std::vector<vk::WriteDescriptorSet> writes;

        // ---- Trace Set0 per image: scene color ----
        std::vector<vk::DescriptorImageInfo> sceneColorInfos(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            sceneColorInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            sceneColorInfos[i].imageView = offscreenResources.colorImages[i].colorImageView;
            sceneColorInfos[i].sampler = sampler;

            vk::WriteDescriptorSet w{};
            w.dstSet = traceSet0PerImage[i];
            w.dstBinding = 0;
            w.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w.descriptorCount = 1;
            w.pImageInfo = &sceneColorInfos[i];
            writes.push_back(w);
        }

        // ---- Trace Set1: depth (binding 0) + UBO (binding 1) ----
        {
            vk::WriteDescriptorSet w0{};
            w0.dstSet = traceSet1;
            w0.dstBinding = 0;
            w0.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w0.descriptorCount = 1;
            w0.pImageInfo = &depthImageInfo;
            writes.push_back(w0);

            vk::WriteDescriptorSet w1{};
            w1.dstSet = traceSet1;
            w1.dstBinding = 1;
            w1.descriptorType = vk::DescriptorType::eUniformBuffer;
            w1.descriptorCount = 1;
            w1.pBufferInfo = &uboInfo;
            writes.push_back(w1);
        }

        // ---- Temporal Set0: ssgiRaw (binding 0) ----
        vk::DescriptorImageInfo ssgiRawInfo{};
        ssgiRawInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        ssgiRawInfo.imageView = ssgiRawImageView;
        ssgiRawInfo.sampler = sampler;
        {
            vk::WriteDescriptorSet w{};
            w.dstSet = temporalSet0;
            w.dstBinding = 0;
            w.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w.descriptorCount = 1;
            w.pImageInfo = &ssgiRawInfo;
            writes.push_back(w);
        }

        // ---- Temporal Set1 per history: history (binding 0) + depth (binding 1) + UBO (binding 2) ----
        std::array<vk::DescriptorImageInfo, 2> historyInfos{};
        for (uint32_t i = 0; i < 2; ++i)
        {
            historyInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            historyInfos[i].imageView = ssgiHistoryImageViews[i];
            historyInfos[i].sampler = sampler;

            vk::WriteDescriptorSet wHist{};
            wHist.dstSet = temporalSet1PerHistory[i];
            wHist.dstBinding = 0;
            wHist.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            wHist.descriptorCount = 1;
            wHist.pImageInfo = &historyInfos[i];
            writes.push_back(wHist);

            vk::WriteDescriptorSet wDepth{};
            wDepth.dstSet = temporalSet1PerHistory[i];
            wDepth.dstBinding = 1;
            wDepth.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            wDepth.descriptorCount = 1;
            wDepth.pImageInfo = &depthImageInfo;
            writes.push_back(wDepth);

            vk::WriteDescriptorSet wUbo{};
            wUbo.dstSet = temporalSet1PerHistory[i];
            wUbo.dstBinding = 2;
            wUbo.descriptorType = vk::DescriptorType::eUniformBuffer;
            wUbo.descriptorCount = 1;
            wUbo.pBufferInfo = &uboInfo;
            writes.push_back(wUbo);
        }

        // ---- Denoise Horizontal Set0 per history: ssgiHistory[i] (binding 0) + depth (binding 1) ----
        std::array<vk::DescriptorImageInfo, 2> denoiseHistoryInfos{};
        for (uint32_t i = 0; i < 2; ++i)
        {
            denoiseHistoryInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            denoiseHistoryInfos[i].imageView = ssgiHistoryImageViews[i];
            denoiseHistoryInfos[i].sampler = sampler;

            vk::WriteDescriptorSet w0{};
            w0.dstSet = denoiseHorizSet0PerHistory[i];
            w0.dstBinding = 0;
            w0.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w0.descriptorCount = 1;
            w0.pImageInfo = &denoiseHistoryInfos[i];
            writes.push_back(w0);

            vk::WriteDescriptorSet w1{};
            w1.dstSet = denoiseHorizSet0PerHistory[i];
            w1.dstBinding = 1;
            w1.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w1.descriptorCount = 1;
            w1.pImageInfo = &depthImageInfo;
            writes.push_back(w1);
        }

        // ---- Denoise Vertical Set0: ssgiDenoiseHoriz (binding 0) + depth (binding 1) ----
        vk::DescriptorImageInfo ssgiDenoiseHorizInfo{};
        ssgiDenoiseHorizInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        ssgiDenoiseHorizInfo.imageView = ssgiDenoiseHorizImageView;
        ssgiDenoiseHorizInfo.sampler = sampler;
        {
            vk::WriteDescriptorSet w0{};
            w0.dstSet = denoiseSet0;
            w0.dstBinding = 0;
            w0.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w0.descriptorCount = 1;
            w0.pImageInfo = &ssgiDenoiseHorizInfo;
            writes.push_back(w0);

            vk::WriteDescriptorSet w1{};
            w1.dstSet = denoiseSet0;
            w1.dstBinding = 1;
            w1.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w1.descriptorCount = 1;
            w1.pImageInfo = &depthImageInfo;
            writes.push_back(w1);
        }

        // ---- Composite Set0: ssgiDenoised (binding 0) + depth (binding 1) ----
        vk::DescriptorImageInfo ssgiDenoisedInfo{};
        ssgiDenoisedInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        ssgiDenoisedInfo.imageView = ssgiDenoisedImageView;
        ssgiDenoisedInfo.sampler = sampler;
        {
            vk::WriteDescriptorSet w0{};
            w0.dstSet = compositeSet0;
            w0.dstBinding = 0;
            w0.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w0.descriptorCount = 1;
            w0.pImageInfo = &ssgiDenoisedInfo;
            writes.push_back(w0);

            vk::WriteDescriptorSet w1{};
            w1.dstSet = compositeSet0;
            w1.dstBinding = 1;
            w1.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            w1.descriptorCount = 1;
            w1.pImageInfo = &depthImageInfo;
            writes.push_back(w1);
        }

        dev.updateDescriptorSets(writes, nullptr);
    }

    // ---- Shaders ----

    void SSGIPipeline::loadShaders()
    {
        traceShader = std::make_shared<core::Shader>(device);
        traceShader->readShader("../../resources/shaders/gi/ssgi_trace.glsl");

        temporalShader = std::make_shared<core::Shader>(device);
        temporalShader->readShader("../../resources/shaders/gi/ssgi_temporal.glsl");

        denoiseShader = std::make_shared<core::Shader>(device);
        denoiseShader->readShader("../../resources/shaders/gi/ssgi_denoise.glsl");

        compositeShader = std::make_shared<core::Shader>(device);
        compositeShader->readShader("../../resources/shaders/gi/ssgi_composite.glsl");
    }

    // ---- Pipeline Creation ----

    vk::Pipeline SSGIPipeline::createFullscreenPipeline(vk::PipelineLayout layout,
                                                         vk::RenderPass rp,
                                                         vk::Extent2D extent,
                                                         const std::shared_ptr<core::Shader>& shdr,
                                                         bool additiveBlend)
    {
        vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

        vk::Viewport viewport{};
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.extent = extent;

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
        if (additiveBlend)
        {
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
            colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eZero;
            colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOne;
            colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
        }
        else
        {
            colorBlendAttachment.blendEnable = VK_FALSE;
        }
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR
                                            | vk::ColorComponentFlagBits::eG
                                            | vk::ColorComponentFlagBits::eB
                                            | vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        const auto& stages = shdr->getShaderStages();

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
        pipelineInfo.layout = layout;
        pipelineInfo.renderPass = rp;
        pipelineInfo.subpass = 0;

        return device.getLogicalDevice().createGraphicsPipeline(nullptr, pipelineInfo).value;
    }

    void SSGIPipeline::createTracePipeline()
    {
        auto& dev = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {traceSet0Layout, traceSet1Layout};

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();

        tracePipelineLayout = dev.createPipelineLayout(layoutInfo);
        tracePipeline = createFullscreenPipeline(tracePipelineLayout, traceRenderPass,
                                                  traceExtent, traceShader, false);
    }

    void SSGIPipeline::createTemporalPipeline()
    {
        auto& dev = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {temporalSet0Layout, temporalSet1Layout};

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();

        temporalPipelineLayout = dev.createPipelineLayout(layoutInfo);
        temporalPipeline = createFullscreenPipeline(temporalPipelineLayout, temporalRenderPass,
                                                     traceExtent, temporalShader, false);
    }

    void SSGIPipeline::createDenoisePipeline()
    {
        auto& dev = device.getLogicalDevice();

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
        pushRange.offset = 0;
        pushRange.size = sizeof(DenoisePushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &denoiseSet0Layout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        denoisePipelineLayout = dev.createPipelineLayout(layoutInfo);
        denoisePipeline = createFullscreenPipeline(denoisePipelineLayout, denoiseRenderPass,
                                                    traceExtent, denoiseShader, false);
    }

    void SSGIPipeline::createCompositePipeline()
    {
        auto& dev = device.getLogicalDevice();

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
        pushRange.offset = 0;
        pushRange.size = sizeof(CompositePushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &compositeSet0Layout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        compositePipelineLayout = dev.createPipelineLayout(layoutInfo);
        compositePipeline = createFullscreenPipeline(compositePipelineLayout, compositeRenderPass,
                                                      currentExtent, compositeShader, true);
    }

    // ---- UBO Update ----

    void SSGIPipeline::updateParamsBuffer()
    {
        if (!paramsBufferMapped)
            return;

        SSGIParamsUBO params{};
        params.projection = cachedProjection;
        params.inverseProjection = glm::inverse(cachedProjection);
        params.view = cachedView;
        params.inverseView = glm::inverse(cachedView);
        params.prevViewProjection = cachedPrevProjection * cachedPrevView;
        params.params = glm::vec4(ssgiRadius, ssgiMaxDistance, ssgiIntensity, ssgiTemporalBlend);
        params.resolution = glm::vec2(static_cast<float>(traceExtent.width),
                                       static_cast<float>(traceExtent.height));
        params.texelSize = glm::vec2(1.0f / static_cast<float>(traceExtent.width),
                                      1.0f / static_cast<float>(traceExtent.height));
        params.nearPlane = cachedNear;
        params.farPlane = cachedFar;
        params.sampleCount = ssgiSampleCount;
        params.frameIndex = cachedFrameIndex;
        params.historyValid = historyValid ? 1u : 0u;
        params.halfResolution = ssgiHalfResolution ? 1u : 0u;

        std::memcpy(paramsBufferMapped, &params, sizeof(SSGIParamsUBO));
    }

    // ---- Cleanup Helpers ----

    void SSGIPipeline::cleanupIntermediateImages()
    {
        destroyImageAndView(ssgiRawImage, ssgiRawMemory, ssgiRawImageView);
        destroyImageAndView(ssgiDenoiseHorizImage, ssgiDenoiseHorizMemory, ssgiDenoiseHorizImageView);
        destroyImageAndView(ssgiDenoisedImage, ssgiDenoisedMemory, ssgiDenoisedImageView);

        for (uint32_t i = 0; i < 2; ++i)
        {
            destroyImageAndView(ssgiHistoryImages[i], ssgiHistoryMemory[i], ssgiHistoryImageViews[i]);
        }
    }

    void SSGIPipeline::cleanupFramebuffers()
    {
        auto& dev = device.getLogicalDevice();

        auto destroyFb = [&](vk::Framebuffer& fb)
        {
            if (fb)
            {
                dev.destroyFramebuffer(fb);
                fb = nullptr;
            }
        };

        destroyFb(traceFramebuffer);
        for (auto& fb : temporalFramebuffers)
        {
            destroyFb(fb);
        }
        destroyFb(denoiseHorizFramebuffer);
        destroyFb(denoiseFramebuffer);

        for (auto& fb : compositeFramebuffers)
        {
            destroyFb(fb);
        }
        compositeFramebuffers.clear();
    }

    void SSGIPipeline::cleanupPipelines()
    {
        auto& dev = device.getLogicalDevice();

        auto destroyPipe = [&](vk::Pipeline& p, vk::PipelineLayout& pl)
        {
            if (p)
            {
                dev.destroyPipeline(p);
                p = nullptr;
            }
            if (pl)
            {
                dev.destroyPipelineLayout(pl);
                pl = nullptr;
            }
        };

        destroyPipe(tracePipeline, tracePipelineLayout);
        destroyPipe(temporalPipeline, temporalPipelineLayout);
        destroyPipe(denoisePipeline, denoisePipelineLayout);
        destroyPipe(compositePipeline, compositePipelineLayout);
    }

    void SSGIPipeline::cleanupRenderPasses()
    {
        auto& dev = device.getLogicalDevice();

        auto destroyRP = [&](vk::RenderPass& rp)
        {
            if (rp)
            {
                dev.destroyRenderPass(rp);
                rp = nullptr;
            }
        };

        destroyRP(traceRenderPass);
        destroyRP(temporalRenderPass);
        destroyRP(denoiseRenderPass);
        destroyRP(compositeRenderPass);
    }
}
