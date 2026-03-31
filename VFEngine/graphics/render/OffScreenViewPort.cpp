#include "OffScreenViewPort.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "../core/CommandPool.hpp"
#include "../core/ImageUtilities.hpp"
#include "../core/Utilities.hpp"
#include "../core/RenderManager.hpp"
#include "../core/AsyncComputeManager.hpp"
#include "../render/RenderPassHandler.hpp"
#include "upscaling/UpscaleManager.hpp"
#include "types/CameraTypes.hpp"
#include <imgui.h>
#include <imgui_impl_vulkan.h>

namespace render
{
    OffScreenViewPort::OffScreenViewPort(core::Device& device, core::SwapChain& swapChain) : device{device}
        , swapChain{swapChain}
        , commandPool{std::make_unique<core::CommandPool>(device, swapChain)}
    {
    }

    OffScreenViewPort::~OffScreenViewPort()
    {
        if (commandPool)
        {
            commandPool->cleanUp();
        }
    }

    void OffScreenViewPort::init()
    {
        createSampler();
        createOffscreenResources();

        vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};
        inFlightFences.resize(swapChain.getImageCount());
        for (auto& fence : inFlightFences)
        {
            fence = device.getLogicalDevice().createFence(fenceInfo);
        }

        renderPassHandler = std::make_unique<render::RenderPassHandler>(device, swapChain, offscreenResources);
        renderPassHandler->init();

        renderPassHandler->initHiZ(types::MAIN_CAMERA_ID,
                                   offscreenResources.depthImage.depthImage,
                                   offscreenResources.depthImage.depthImageView,
                                   swapChain.getSwapchainDepthStencilFormat());
    }

    vk::DescriptorSet OffScreenViewPort::render(const PreRenderCallback& preRenderCallback)
    {
        uint32_t imageIndex = core::RenderManager::getImageIndex();

        vk::Result result = device.getLogicalDevice().waitForFences(
            1, &inFlightFences[imageIndex], VK_TRUE, UINT64_MAX);

        // Also wait for any other swapchain image that shares the same frame-in-flight
        // secondary command buffer slot (e.g., images 0 and 2 both map to fi=0 when
        // MAX_FRAMES_IN_FLIGHT=2). Without this, resetFrame() may reset secondary
        // command buffers still pending from a different image's submission.
        uint32_t fi = imageIndex % core::MAX_FRAMES_IN_FLIGHT;
        for (uint32_t i = 0; i < static_cast<uint32_t>(inFlightFences.size()); i++)
        {
            if (i != imageIndex && (i % core::MAX_FRAMES_IN_FLIGHT) == fi)
            {
                (void)device.getLogicalDevice().waitForFences(1, &inFlightFences[i], VK_TRUE, UINT64_MAX);
            }
        }

        result = device.getLogicalDevice().resetFences(1, &inFlightFences[imageIndex]);
        (void)result;

        // Read back previous frame's results and update brush overlay BEFORE rendering
        // so the overlay position matches the current raycast hit in this frame's render
        renderPassHandler->readBackLightOcclusionResults();
        renderPassHandler->readBackTerrainRaycastResults();
        renderPassHandler->updateBrushOverlayFromHitResult();

        if (preRenderCallback)
        {
            preRenderCallback();
        }

        bool useAsyncCompute = asyncComputeManager && asyncComputeManager->isEnabled() && renderPassHandler;

        // Set async compute state on render pass handler
        if (renderPassHandler)
        {
            renderPassHandler->setAsyncComputeActive(useAsyncCompute);
        }

        uint32_t currentFrame = imageIndex % core::MAX_FRAMES_IN_FLIGHT;
        
        if (useAsyncCompute)
        {
            vk::CommandBuffer asyncCmd = asyncComputeManager->beginFrame(currentFrame);
            renderPassHandler->recordAsyncCompute(asyncCmd);
            asyncComputeManager->submitComputeWork(currentFrame);
        }

        vk::CommandBuffer commandBuffer = commandPool->getCommandBuffer(imageIndex);
        commandBuffer.reset();

        commandBuffer.begin(vk::CommandBufferBeginInfo{});

        draw(commandBuffer, imageIndex);

        commandBuffer.end();

        if (useAsyncCompute)
        {
            // Graphics submit waits on async compute completion before fragment shading
            std::array<vk::Semaphore, 1> waitSemaphores = {
                asyncComputeManager->getComputeTimelineSemaphore()
            };
            std::array<vk::PipelineStageFlags, 1> waitStages = {
                vk::PipelineStageFlagBits::eFragmentShader |
                vk::PipelineStageFlagBits::eComputeShader |
                vk::PipelineStageFlagBits::eTaskShaderEXT
            };
            std::array<uint64_t, 1> waitValues = {
                asyncComputeManager->getComputeWaitValue()
            };

            vk::TimelineSemaphoreSubmitInfo timelineInfo{};
            timelineInfo.waitSemaphoreValueCount = static_cast<uint32_t>(waitValues.size());
            timelineInfo.pWaitSemaphoreValues = waitValues.data();
            timelineInfo.signalSemaphoreValueCount = 0;
            timelineInfo.pSignalSemaphoreValues = nullptr;

            vk::SubmitInfo submitInfo{};
            submitInfo.pNext = &timelineInfo;
            submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
            submitInfo.pWaitSemaphores = waitSemaphores.data();
            submitInfo.pWaitDstStageMask = waitStages.data();
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;
            submitInfo.signalSemaphoreCount = 0;
            submitInfo.pSignalSemaphores = nullptr;

            device.submitGraphics(submitInfo, inFlightFences[imageIndex]);
        }
        else
        {
            vk::SubmitInfo submitInfo(
                0, nullptr, nullptr,
                1, &commandBuffer,
                0, nullptr
            );

            device.submitGraphics(submitInfo, inFlightFences[imageIndex]);
        }

        // Fence-based sync: inFlightFences[imageIndex] is waited on at the top of render()
        // when this imageIndex comes around again. No need to stall the entire queue.

        return offscreenResources.colorImages[imageIndex].descriptorSet;
    }

    void OffScreenViewPort::setAsyncComputeManager(core::AsyncComputeManager* manager)
    {
        asyncComputeManager = manager;
    }

    void OffScreenViewPort::cleanUp()
    {
        device.getLogicalDevice().waitIdle();

        renderPassHandler->cleanUp();
        commandPool->cleanUp();

        for (auto& fence : inFlightFences)
        {
            if (fence)
            {
                device.getLogicalDevice().destroyFence(fence);
            }
        }
        inFlightFences.clear();

        if (ImGui::GetCurrentContext())
        {
            for (auto const& resources : offscreenResources.colorImages)
            {
                if (resources.descriptorSet)
                {
                    ImGui_ImplVulkan_RemoveTexture(resources.descriptorSet);
                }
            }
        }

        device.getLogicalDevice().destroySampler(sampler);

        cleanupOffscreenResources();
    }

    void OffScreenViewPort::cleanupOffscreenResources()
    {
        for (auto const& resources : offscreenResources.colorImages)
        {
            device.getLogicalDevice().destroyImageView(resources.colorImageView);
            device.getLogicalDevice().destroyImage(resources.colorImage);
            device.getMemoryManager().free(resources.colorImageAllocation);
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
        if (offscreenResources.depthImage.depthImageAllocation)
        {
            device.getMemoryManager().free(offscreenResources.depthImage.depthImageAllocation);
            offscreenResources.depthImage.depthImageAllocation = {};
        }

        // Cleanup UI stencil image
        if (offscreenResources.uiStencilImage.stencilImageView)
        {
            device.getLogicalDevice().destroyImageView(offscreenResources.uiStencilImage.stencilImageView);
            offscreenResources.uiStencilImage.stencilImageView = nullptr;
        }
        if (offscreenResources.uiStencilImage.stencilImage)
        {
            device.getLogicalDevice().destroyImage(offscreenResources.uiStencilImage.stencilImage);
            offscreenResources.uiStencilImage.stencilImage = nullptr;
        }
        if (offscreenResources.uiStencilImage.stencilImageAllocation)
        {
            device.getMemoryManager().free(offscreenResources.uiStencilImage.stencilImageAllocation);
            offscreenResources.uiStencilImage.stencilImageAllocation = {};
        }

        cleanupUpscaleResources();
    }

    void OffScreenViewPort::recreate()
    {
        device.getLogicalDevice().waitIdle();

        if (ImGui::GetCurrentContext())
        {
            for (auto const& resources : offscreenResources.colorImages)
            {
                if (resources.descriptorSet)
                {
                    ImGui_ImplVulkan_RemoveTexture(resources.descriptorSet);
                }
            }
            if (offscreenResources.upscaleOutput.descriptorSet)
            {
                ImGui_ImplVulkan_RemoveTexture(offscreenResources.upscaleOutput.descriptorSet);
                offscreenResources.upscaleOutput.descriptorSet = nullptr;
            }
        }

        cleanupOffscreenResources();
        createOffscreenResources();

        renderPassHandler->recreate();
    }

    vk::Image OffScreenViewPort::getColorImage(uint32_t index) const
    {
        if (index < offscreenResources.colorImages.size())
        {
            return offscreenResources.colorImages[index].colorImage;
        }
        return {};
    }

    void OffScreenViewPort::draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        renderPassHandler->draw(commandBuffer, imageIndex);
    }

    void OffScreenViewPort::createOffscreenResources()
    {
        vk::Format colorFormat = swapChain.getSwapchainImageFormat();
        vk::Format depthFormat = swapChain.getSwapchainDepthStencilFormat();

        // Determine render resolution: when upscaling is active, render at internal resolution
        uint32_t renderWidth = swapChain.getSwapchainExtent().width;
        uint32_t renderHeight = swapChain.getSwapchainExtent().height;
        uint32_t displayWidth = renderWidth;
        uint32_t displayHeight = renderHeight;

        auto* upscaleManager = device.getUpscaleManager();
        if (upscaleManager && upscaleManager->isActive())
        {
            auto renderRes = upscaleManager->getResolutionManager().getRenderResolution();
            renderWidth = renderRes.width;
            renderHeight = renderRes.height;
        }

        core::ImageInfoRequest imageColorInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageColorInfo.width = renderWidth;
        imageColorInfo.height = renderHeight;
        imageColorInfo.format = colorFormat;
        imageColorInfo.tiling = vk::ImageTiling::eOptimal;
        imageColorInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc;
        imageColorInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::ImageInfoRequest imageDepthInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageDepthInfo.width = renderWidth;
        imageDepthInfo.height = renderHeight;
        imageDepthInfo.format = depthFormat;
        imageDepthInfo.tiling = vk::ImageTiling::eOptimal;
        imageDepthInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
        imageDepthInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::DepthImage depth;
        core::ImageUtilities::createImage(imageDepthInfo, depth.depthImage, depth.depthImageAllocation, device.getMemoryManager());
        core::ImageViewInfoRequest imageDepthRequest(device.getLogicalDevice(), depth.depthImage);

        imageDepthRequest.format = depthFormat;
        imageDepthRequest.aspectFlags = vk::ImageAspectFlagBits::eDepth;
        core::ImageUtilities::createImageView(imageDepthRequest, depth.depthImageView);

        vk::UniqueCommandBuffer trasitionDepthImage = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool->getCommandPool());
        core::ImageUtilities::transitionImageLayout(trasitionDepthImage.get(), depth.depthImage, vk::ImageLayout::eUndefined,
                                               vk::ImageLayout::eDepthStencilAttachmentOptimal,
                                               vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);
        core::Utilities::endSingleTimeCommands(device, trasitionDepthImage);

        offscreenResources.depthImage = std::move(depth);

        // Create UI stencil image (eS8Uint, 1 byte per pixel)
        {
            core::ImageInfoRequest stencilInfo(device.getLogicalDevice(), device.getPhysicalDevice());
            stencilInfo.width = swapChain.getSwapchainExtent().width;
            stencilInfo.height = swapChain.getSwapchainExtent().height;
            stencilInfo.format = vk::Format::eS8Uint;
            stencilInfo.tiling = vk::ImageTiling::eOptimal;
            stencilInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
            stencilInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

            core::StencilImage stencil;
            core::ImageUtilities::createImage(stencilInfo, stencil.stencilImage, stencil.stencilImageAllocation, device.getMemoryManager());

            core::ImageViewInfoRequest stencilViewRequest(device.getLogicalDevice(), stencil.stencilImage);
            stencilViewRequest.format = vk::Format::eS8Uint;
            stencilViewRequest.aspectFlags = vk::ImageAspectFlagBits::eStencil;
            core::ImageUtilities::createImageView(stencilViewRequest, stencil.stencilImageView);

            vk::UniqueCommandBuffer transitionStencilImage = core::Utilities::beginSingleTimeCommands(
                device.getLogicalDevice(), commandPool->getCommandPool());
            core::ImageUtilities::transitionImageLayout(transitionStencilImage.get(), stencil.stencilImage,
                                                   vk::ImageLayout::eUndefined,
                                                   vk::ImageLayout::eDepthStencilAttachmentOptimal,
                                                   vk::ImageAspectFlagBits::eStencil);
            core::Utilities::endSingleTimeCommands(device, transitionStencilImage);

            offscreenResources.uiStencilImage = std::move(stencil);
        }

        offscreenResources.colorImages.reserve(swapChain.getImageCount());

        for (size_t i = 0; i < swapChain.getImageCount(); i++)
        {
            core::ColorImage color;
            core::ImageUtilities::createImage(imageColorInfo, color.colorImage, color.colorImageAllocation, device.getMemoryManager());
            core::ImageViewInfoRequest imageColorViewRequest(device.getLogicalDevice(), color.colorImage);
            imageColorViewRequest.format = colorFormat;
            core::ImageUtilities::createImageView(imageColorViewRequest, color.colorImageView);

            vk::UniqueCommandBuffer trasitionColorImage = core::Utilities::beginSingleTimeCommands(
                device.getLogicalDevice(), commandPool->getCommandPool());
            core::ImageUtilities::transitionImageLayout(trasitionColorImage.get(), color.colorImage,
                                                   vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal,
                                                   vk::ImageAspectFlagBits::eColor);
            core::Utilities::endSingleTimeCommands(device, trasitionColorImage);

            updateDescriptorSets(color.descriptorSet, color.colorImageView);

            offscreenResources.colorImages.push_back(std::move(color));
        }

        // Create upscale resources when upscaling is active
        if (upscaleManager && upscaleManager->isActive())
        {
            createUpscaleResources(renderWidth, renderHeight, displayWidth, displayHeight);
        }
    }

    void OffScreenViewPort::updateDescriptorSets(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const
    {
        // ImGui descriptor sets are only needed in editor mode (to display offscreen result via ImGui::Image)
        if (ImGui::GetCurrentContext())
        {
            descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    void OffScreenViewPort::createSampler()
    {
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;

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

    void OffScreenViewPort::createUpscaleResources(uint32_t renderWidth, uint32_t renderHeight,
                                                     uint32_t displayWidth, uint32_t displayHeight)
    {
        if (offscreenResources.upscaleResourcesCreated) return;

        vk::Format colorFormat = swapChain.getSwapchainImageFormat();

        // Motion vector image (R16G16_SFLOAT) at render resolution
        {
            core::ImageInfoRequest mvInfo(device.getLogicalDevice(), device.getPhysicalDevice(),
                renderWidth, renderHeight, 1, 1,
                vk::Format::eR16G16Sfloat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal);

            core::ImageUtilities::createImage(mvInfo,
                offscreenResources.motionVectors.image,
                offscreenResources.motionVectors.allocation,
                device.getMemoryManager());

            // Storage view (for compute write)
            core::ImageViewInfoRequest storageView(device.getLogicalDevice(),
                offscreenResources.motionVectors.image,
                vk::Format::eR16G16Sfloat, vk::ImageAspectFlagBits::eColor,
                vk::ImageViewType::e2D, 1, 1);
            core::ImageUtilities::createImageView(storageView, offscreenResources.motionVectors.imageView);

            // Sampled view (for upscaler read)
            core::ImageViewInfoRequest sampledView(device.getLogicalDevice(),
                offscreenResources.motionVectors.image,
                vk::Format::eR16G16Sfloat, vk::ImageAspectFlagBits::eColor,
                vk::ImageViewType::e2D, 1, 1);
            core::ImageUtilities::createImageView(sampledView, offscreenResources.motionVectors.sampledView);
        }

        // Upscale output image at display resolution
        {
            core::ImageInfoRequest outputInfo(device.getLogicalDevice(), device.getPhysicalDevice(),
                displayWidth, displayHeight, 1, 1,
                colorFormat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
                vk::ImageUsageFlagBits::eColorAttachment,
                vk::MemoryPropertyFlagBits::eDeviceLocal);

            core::ImageUtilities::createImage(outputInfo,
                offscreenResources.upscaleOutput.image,
                offscreenResources.upscaleOutput.allocation,
                device.getMemoryManager());

            core::ImageViewInfoRequest outputView(device.getLogicalDevice(),
                offscreenResources.upscaleOutput.image,
                colorFormat, vk::ImageAspectFlagBits::eColor,
                vk::ImageViewType::e2D, 1, 1);
            core::ImageUtilities::createImageView(outputView, offscreenResources.upscaleOutput.imageView);

            // Create ImGui descriptor for display
            updateDescriptorSets(offscreenResources.upscaleOutput.descriptorSet,
                                 offscreenResources.upscaleOutput.imageView);
        }

        offscreenResources.upscaleResourcesCreated = true;
    }

    void OffScreenViewPort::cleanupUpscaleResources()
    {
        if (!offscreenResources.upscaleResourcesCreated) return;

        vk::Device vkDevice = device.getLogicalDevice();

        // Motion vectors
        if (offscreenResources.motionVectors.sampledView)
            vkDevice.destroyImageView(offscreenResources.motionVectors.sampledView);
        if (offscreenResources.motionVectors.imageView)
            vkDevice.destroyImageView(offscreenResources.motionVectors.imageView);
        if (offscreenResources.motionVectors.image)
            vkDevice.destroyImage(offscreenResources.motionVectors.image);
        if (offscreenResources.motionVectors.allocation)
            device.getMemoryManager().free(offscreenResources.motionVectors.allocation);
        offscreenResources.motionVectors = {};

        // Upscale output
        if (offscreenResources.upscaleOutput.descriptorSet && ImGui::GetCurrentContext())
            ImGui_ImplVulkan_RemoveTexture(offscreenResources.upscaleOutput.descriptorSet);
        if (offscreenResources.upscaleOutput.imageView)
            vkDevice.destroyImageView(offscreenResources.upscaleOutput.imageView);
        if (offscreenResources.upscaleOutput.image)
            vkDevice.destroyImage(offscreenResources.upscaleOutput.image);
        if (offscreenResources.upscaleOutput.allocation)
            device.getMemoryManager().free(offscreenResources.upscaleOutput.allocation);
        offscreenResources.upscaleOutput = {};

        offscreenResources.upscaleResourcesCreated = false;
    }

    void OffScreenViewPort::setRaycastCursorUV(const glm::vec2& uv)
    {
        renderPassHandler->setRaycastCursorUV(uv);
    }

    void OffScreenViewPort::clearRaycastCursor()
    {
        renderPassHandler->clearRaycastCursor();
    }

    terrain::TerrainHitResult OffScreenViewPort::getTerrainHitResult() const
    {
        return renderPassHandler->getTerrainHitResult();
    }

    void OffScreenViewPort::setBrushOverlayParams(float radius, float falloff, float shape)
    {
        renderPassHandler->setBrushOverlayParams(radius, falloff, shape);
    }
}
