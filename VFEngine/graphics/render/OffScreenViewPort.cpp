#include "OffScreenViewPort.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "../core/CommandPool.hpp"
#include "../core/ImageUtilities.hpp"
#include "../core/Utilities.hpp"
#include "../core/RenderManager.hpp"
#include "../render/RenderPassHandler.hpp"
#include "types/CameraTypes.hpp"
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
        result = device.getLogicalDevice().resetFences(1, &inFlightFences[imageIndex]);
        (void)result;

        if (preRenderCallback)
        {
            preRenderCallback();
        }

        vk::CommandBuffer commandBuffer = commandPool->getCommandBuffer(imageIndex);
        commandBuffer.reset();

        commandBuffer.begin(vk::CommandBufferBeginInfo{});

        draw(commandBuffer);

        commandBuffer.end();

        vk::SubmitInfo submitInfo(
            0, nullptr, nullptr,
            1, &commandBuffer,
            0, nullptr
        );

        device.getGraphicsQueue().submit(submitInfo, inFlightFences[imageIndex]);

        device.getGraphicsQueue().waitIdle();

        renderPassHandler->readBackLightOcclusionResults();

        return offscreenResources.colorImages[imageIndex].descriptorSet;
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

        for (auto const& resources : offscreenResources.colorImages)
        {
            if (resources.descriptorSet)
            {
                ImGui_ImplVulkan_RemoveTexture(resources.descriptorSet);
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

    void OffScreenViewPort::recreate()
    {
        device.getLogicalDevice().waitIdle();

        for (auto const& resources : offscreenResources.colorImages)
        {
            if (resources.descriptorSet)
            {
                ImGui_ImplVulkan_RemoveTexture(resources.descriptorSet);
            }
        }

        cleanupOffscreenResources();
        createOffscreenResources();

        renderPassHandler->recreate();
    }

    void OffScreenViewPort::draw(const vk::CommandBuffer& commandBuffer) const
    {
        renderPassHandler->draw(commandBuffer, core::RenderManager::getImageIndex());
    }

    void OffScreenViewPort::createOffscreenResources()
    {
        vk::Format colorFormat = swapChain.getSwapchainImageFormat();
        vk::Format depthFormat = swapChain.getSwapchainDepthStencilFormat();

        core::ImageInfoRequest imageColorInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageColorInfo.width = swapChain.getSwapchainExtent().width;
        imageColorInfo.height = swapChain.getSwapchainExtent().height;
        imageColorInfo.format = colorFormat;
        imageColorInfo.tiling = vk::ImageTiling::eOptimal;
        imageColorInfo.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
        imageColorInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::ImageInfoRequest imageDepthInfo(device.getLogicalDevice(), device.getPhysicalDevice());
        imageDepthInfo.width = swapChain.getSwapchainExtent().width;
        imageDepthInfo.height = swapChain.getSwapchainExtent().height;
        imageDepthInfo.format = depthFormat;
        imageDepthInfo.tiling = vk::ImageTiling::eOptimal;
        imageDepthInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
        imageDepthInfo.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        core::DepthImage depth;
        core::ImageUtilities::createImage(imageDepthInfo, depth.depthImage, depth.depthImageMemory);
        core::ImageViewInfoRequest imageDepthRequest(device.getLogicalDevice(), depth.depthImage);

        imageDepthRequest.format = depthFormat;
        imageDepthRequest.aspectFlags = vk::ImageAspectFlagBits::eDepth;
        core::ImageUtilities::createImageView(imageDepthRequest, depth.depthImageView);

        vk::UniqueCommandBuffer trasitionDepthImage = core::Utilities::beginSingleTimeCommands(
            device.getLogicalDevice(), commandPool->getCommandPool());
        core::ImageUtilities::transitionImageLayout(trasitionDepthImage.get(), depth.depthImage, vk::ImageLayout::eUndefined,
                                               vk::ImageLayout::eDepthStencilAttachmentOptimal,
                                               vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);
        core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), trasitionDepthImage);

        offscreenResources.depthImage = std::move(depth);

        offscreenResources.colorImages.reserve(swapChain.getImageCount());

        for (size_t i = 0; i < swapChain.getImageCount(); i++)
        {
            core::ColorImage color;
            core::ImageUtilities::createImage(imageColorInfo, color.colorImage, color.colorImageMemory);
            core::ImageViewInfoRequest imageColorViewRequest(device.getLogicalDevice(), color.colorImage);
            imageColorViewRequest.format = colorFormat;
            core::ImageUtilities::createImageView(imageColorViewRequest, color.colorImageView);

            vk::UniqueCommandBuffer trasitionColorImage = core::Utilities::beginSingleTimeCommands(
                device.getLogicalDevice(), commandPool->getCommandPool());
            core::ImageUtilities::transitionImageLayout(trasitionColorImage.get(), color.colorImage,
                                                   vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal,
                                                   vk::ImageAspectFlagBits::eColor);
            core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), trasitionColorImage);

            updateDescriptorSets(color.descriptorSet, color.colorImageView);

            offscreenResources.colorImages.push_back(std::move(color));
        }
    }

    void OffScreenViewPort::updateDescriptorSets(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const
    {
        descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
}
