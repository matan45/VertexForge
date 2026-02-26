#include "RenderTextureViewPort.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "../core/CommandPool.hpp"
#include "../core/ImageUtilities.hpp"
#include "../core/Utilities.hpp"
#include "../core/RenderManager.hpp"
#include "RenderPassHandler.hpp"
#include "mesh/StaticMeshPipeline.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
#include "print/Logger.hpp"
#include <imgui_impl_vulkan.h>

namespace render
{
    RenderTextureViewPort::RenderTextureViewPort(core::Device& device, core::SwapChain& swapChain)
        : device{device}
        , swapChain{swapChain}
        , commandPool{std::make_unique<core::CommandPool>(device, swapChain)}
    {
    }

    RenderTextureViewPort::~RenderTextureViewPort()
    {
        if (commandPool)
        {
            commandPool->cleanUp();
        }
    }

    void RenderTextureViewPort::init(uint32_t w, uint32_t h)
    {
        width = w;
        height = h;

        createSampler();
        createRenderPass();
        createOffscreenResources();
        createFramebuffers();

        vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};
        inFlightFences.resize(swapChain.getImageCount());
        for (auto& fence : inFlightFences)
        {
            fence = device.getLogicalDevice().createFence(fenceInfo);
        }

        initialized = true;
    }

    void RenderTextureViewPort::resize(uint32_t newWidth, uint32_t newHeight)
    {
        if (newWidth == width && newHeight == height)
            return;

        device.getLogicalDevice().waitIdle();

        width = newWidth;
        height = newHeight;

        for (auto const& resources : offscreenResources.colorImages)
        {
            if (resources.descriptorSet)
            {
                ImGui_ImplVulkan_RemoveTexture(resources.descriptorSet);
            }
        }

        cleanupFramebuffers();
        cleanupOffscreenResources();
        createOffscreenResources();
        createFramebuffers();
    }

    vk::DescriptorSet RenderTextureViewPort::render(
        RenderPassHandler* mainPassHandler,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        float nearPlane,
        float farPlane)
    {
        if (!initialized || !mainPassHandler)
        {
            return nullptr;
        }

        auto* gpuRenderer = mainPassHandler->getGPUDrivenRenderer();
        if (!gpuRenderer || !gpuRenderer->isEnabled())
        {
            return nullptr;
        }

        auto* meshPipeline = mainPassHandler->getMeshPipeline();
        if (!meshPipeline)
        {
            return nullptr;
        }

        uint32_t imageIndex = core::RenderManager::getImageIndex();
        lastRenderedImageIndex = imageIndex;

        vk::Result result = device.getLogicalDevice().waitForFences(
            1, &inFlightFences[imageIndex], VK_TRUE, UINT64_MAX);
        result = device.getLogicalDevice().resetFences(1, &inFlightFences[imageIndex]);
        (void)result;

        // Update both camera buffers for RTT rendering:
        // 1. GPUDrivenCameraBuffer — used by the compute cull pipeline for object-level frustum culling
        // 2. StaticMeshPipeline CameraUBO — used by mesh/task/fragment shaders for vertex
        //    transformation, meshlet-level frustum culling, and lighting calculations
        gpuRenderer->updateCameraForRTT(
            view,
            projection,
            cameraPosition,
            nearPlane,
            farPlane,
            width,
            height
        );

        meshPipeline->updateCameraUBO(view, projection, cameraPosition);

        vk::CommandBuffer commandBuffer = commandPool->getCommandBuffer(imageIndex);
        commandBuffer.reset();
        commandBuffer.begin(vk::CommandBufferBeginInfo{});

        // Dispatch compute culling with RTT camera frustum
        gpuRenderer->dispatchCompute(commandBuffer);

        // Begin render pass on RTT framebuffer
        vk::RenderPassBeginInfo renderPassInfo{};
        renderPassInfo.renderPass = compatibleRenderPass;
        renderPassInfo.framebuffer = framebuffers[imageIndex];
        renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderPassInfo.renderArea.extent = vk::Extent2D{width, height};

        std::array<vk::ClearValue, 2> clearValues{};
        clearValues[0].color = vk::ClearColorValue{
            std::array{clearColor.r, clearColor.g, clearColor.b, clearColor.a}};
        clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        // Set viewport and scissor for RTT dimensions
        vk::Viewport viewport{0.0f, 0.0f,
                               static_cast<float>(width), static_cast<float>(height),
                               0.0f, 1.0f};
        commandBuffer.setViewport(0, viewport);

        vk::Rect2D scissor{{0, 0}, {width, height}};
        commandBuffer.setScissor(0, scissor);

        vk::DescriptorSet iblDescriptorSet = meshPipeline->getIBLDescriptorSet(imageIndex);

        // Draw opaque meshes
        gpuRenderer->renderDraw(commandBuffer, iblDescriptorSet, width, height);

        // Draw transparent meshes
        gpuRenderer->renderTransparentDraw(commandBuffer, iblDescriptorSet, width, height);

        // Draw blend/additive meshes
        gpuRenderer->renderBlendDraw(commandBuffer, iblDescriptorSet, width, height);

        // Draw terrain
        if (gpuRenderer->isTerrainRenderingEnabled())
        {
            gpuRenderer->renderTerrainDraw(commandBuffer, iblDescriptorSet, width, height);
        }

        // Draw water
        if (gpuRenderer->isWaterRenderingEnabled())
        {
            gpuRenderer->renderWaterDraw(commandBuffer, iblDescriptorSet);
        }

        commandBuffer.endRenderPass();

        commandBuffer.end();

        vk::SubmitInfo submitInfo(
            0, nullptr, nullptr,
            1, &commandBuffer,
            0, nullptr
        );

        device.getGraphicsQueue().submit(submitInfo, inFlightFences[imageIndex]);
        device.getGraphicsQueue().waitIdle();

        // Restore the main camera's data to the GPU buffer so the main render pass
        // uses the correct frustum/projection for culling
        gpuRenderer->restoreMainCamera();

        return offscreenResources.colorImages[imageIndex].descriptorSet;
    }

    void RenderTextureViewPort::cleanUp()
    {
        if (!initialized)
            return;

        device.getLogicalDevice().waitIdle();

        commandPool->cleanUp();

        for (auto& fence : inFlightFences)
        {
            if (fence)
            {
                device.getLogicalDevice().destroyFence(fence);
            }
        }
        inFlightFences.clear();

        for (auto const& resources : offscreenResources.colorImages)
        {
            if (resources.descriptorSet)
            {
                ImGui_ImplVulkan_RemoveTexture(resources.descriptorSet);
            }
        }

        device.getLogicalDevice().destroySampler(sampler);

        cleanupFramebuffers();

        if (compatibleRenderPass)
        {
            device.getLogicalDevice().destroyRenderPass(compatibleRenderPass);
            compatibleRenderPass = nullptr;
        }

        cleanupOffscreenResources();

        initialized = false;
    }

    vk::ImageView RenderTextureViewPort::getLastRenderedImageView() const
    {
        if (lastRenderedImageIndex < offscreenResources.colorImages.size())
            return offscreenResources.colorImages[lastRenderedImageIndex].colorImageView;
        return {};
    }

    void RenderTextureViewPort::createRenderPass()
    {
        // Create a render pass compatible with the main mesh pipeline's render pass.
        // Same attachment formats and sample counts ensure pipeline compatibility.
        // Uses eClear for loadOp since RTT starts fresh each frame.
        vk::AttachmentDescription colorAttachment{};
        colorAttachment.format = swapChain.getSwapchainImageFormat();
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::AttachmentDescription depthAttachment{};
        depthAttachment.format = swapChain.getSwapchainDepthStencilFormat();
        depthAttachment.samples = vk::SampleCountFlagBits::e1;
        depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        depthAttachment.initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::AttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        vk::SubpassDescription subpass{};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;

        std::array<vk::AttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

        vk::RenderPassCreateInfo renderPassInfo{};
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        compatibleRenderPass = device.getLogicalDevice().createRenderPass(renderPassInfo);
    }

    void RenderTextureViewPort::createFramebuffers()
    {
        framebuffers.resize(offscreenResources.colorImages.size());
        vk::ImageView depth = offscreenResources.depthImage.depthImageView;

        for (uint32_t i = 0; i < framebuffers.size(); i++)
        {
            vk::ImageView colorView = offscreenResources.colorImages[i].colorImageView;
            std::array<vk::ImageView, 2> attachments = {colorView, depth};

            vk::FramebufferCreateInfo framebufferInfo{};
            framebufferInfo.renderPass = compatibleRenderPass;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = width;
            framebufferInfo.height = height;
            framebufferInfo.layers = 1;

            framebuffers[i] = device.getLogicalDevice().createFramebuffer(framebufferInfo);
        }
    }

    void RenderTextureViewPort::cleanupFramebuffers()
    {
        for (auto& fb : framebuffers)
        {
            if (fb)
            {
                device.getLogicalDevice().destroyFramebuffer(fb);
            }
        }
        framebuffers.clear();
    }

    void RenderTextureViewPort::createOffscreenResources()
    {
        vk::Format colorFormat = swapChain.getSwapchainImageFormat();
        vk::Format depthFormat = swapChain.getSwapchainDepthStencilFormat();

        // Create color images - one per swapchain image for double/triple buffering
        core::ImageInfoRequest imageColorInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageColorInfo.width = width;
        imageColorInfo.height = height;
        imageColorInfo.format = colorFormat;
        imageColorInfo.tiling = vk::ImageTiling::eOptimal;
        imageColorInfo.usage = vk::ImageUsageFlagBits::eColorAttachment
                             | vk::ImageUsageFlagBits::eSampled
                             | vk::ImageUsageFlagBits::eTransferDst;
        imageColorInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        // Create depth image
        core::ImageInfoRequest imageDepthInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageDepthInfo.width = width;
        imageDepthInfo.height = height;
        imageDepthInfo.format = depthFormat;
        imageDepthInfo.tiling = vk::ImageTiling::eOptimal;
        imageDepthInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment
                             | vk::ImageUsageFlagBits::eSampled;
        imageDepthInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::DepthImage depth;
        core::ImageUtilities::createImage(imageDepthInfo, depth.depthImage, depth.depthImageMemory);
        core::ImageViewInfoRequest imageDepthRequest(device.getLogicalDevice(), depth.depthImage);
        imageDepthRequest.format = depthFormat;
        imageDepthRequest.aspectFlags = vk::ImageAspectFlagBits::eDepth;
        core::ImageUtilities::createImageView(imageDepthRequest, depth.depthImageView);

        vk::UniqueCommandBuffer transitionDepthImage = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool->getCommandPool());
        core::ImageUtilities::transitionImageLayout(transitionDepthImage.get(), depth.depthImage,
                                                    vk::ImageLayout::eUndefined,
                                                    vk::ImageLayout::eDepthStencilAttachmentOptimal,
                                                    vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);
        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), transitionDepthImage);

        offscreenResources.depthImage = std::move(depth);

        offscreenResources.colorImages.reserve(swapChain.getImageCount());

        for (size_t i = 0; i < swapChain.getImageCount(); i++)
        {
            core::ColorImage color;
            core::ImageUtilities::createImage(imageColorInfo, color.colorImage, color.colorImageMemory);
            core::ImageViewInfoRequest imageColorViewRequest(device.getLogicalDevice(), color.colorImage);
            imageColorViewRequest.format = colorFormat;
            core::ImageUtilities::createImageView(imageColorViewRequest, color.colorImageView);

            vk::UniqueCommandBuffer transitionColorImage = core::Utilities::beginSingleTimeCommands(
                device.getLogicalDevice(), commandPool->getCommandPool());
            core::ImageUtilities::transitionImageLayout(transitionColorImage.get(), color.colorImage,
                                                        vk::ImageLayout::eUndefined,
                                                        vk::ImageLayout::eShaderReadOnlyOptimal,
                                                        vk::ImageAspectFlagBits::eColor);
            core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), transitionColorImage);

            updateDescriptorSets(color.descriptorSet, color.colorImageView);

            offscreenResources.colorImages.push_back(std::move(color));
        }
    }

    void RenderTextureViewPort::cleanupOffscreenResources()
    {
        for (auto const& resources : offscreenResources.colorImages)
        {
            device.getLogicalDevice().destroyImageView(resources.colorImageView);
            device.getLogicalDevice().destroyImage(resources.colorImage);
            device.getLogicalDevice().freeMemory(resources.colorImageMemory);
        }
        offscreenResources.colorImages.clear();

        if (offscreenResources.depthImage.depthImageView)
        {
            device.getLogicalDevice().destroyImageView(offscreenResources.depthImage.depthImageView);
            offscreenResources.depthImage.depthImageView = nullptr;
        }
        if (offscreenResources.depthImage.depthImage)
        {
            device.getLogicalDevice().destroyImage(offscreenResources.depthImage.depthImage);
            offscreenResources.depthImage.depthImage = nullptr;
        }
        if (offscreenResources.depthImage.depthImageMemory)
        {
            device.getLogicalDevice().freeMemory(offscreenResources.depthImage.depthImageMemory);
            offscreenResources.depthImage.depthImageMemory = nullptr;
        }
    }

    void RenderTextureViewPort::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;

        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;

        vk::PhysicalDeviceProperties properties = device.getPhysicalDevice().getProperties();
        float maxAnisotropy = properties.limits.maxSamplerAnisotropy;

        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = maxAnisotropy;
        samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eAlways;

        sampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void RenderTextureViewPort::updateDescriptorSets(vk::DescriptorSet& descriptorSet,
                                                      const vk::ImageView& imageView) const
    {
        descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}
