#include "ClearColor.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "../core/ImageUtilities.hpp"
#include "../core/DynamicRenderingHelpers.hpp"

namespace render
{
    ClearColor::ClearColor(core::Device& device, core::SwapChain& swapChain,
                           core::OffscreenResources& offscreenResources) : device{device},
                                                                           swapChain{swapChain},
                                                                           offscreenResources{offscreenResources}
    {
    }

    void ClearColor::init()
    {
    }

    void ClearColor::recreate()
    {
    }

    void ClearColor::cleanUp() const
    {
    }

    void ClearColor::recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        auto colorAttach = core::colorClear(
            offscreenResources.colorImages[imageIndex].colorImageView,
            vk::ClearColorValue(std::array<float, 4>{
                clearColorValue.r, clearColorValue.g, clearColorValue.b, clearColorValue.a}));

        auto depthAttach = core::depthClear(
            offscreenResources.depthImage.depthImageView, 1.0f, 0);

        core::DynamicRenderingInfo info{};
        info.extent = swapChain.getSwapchainExtent();
        info.colorAttachments = {colorAttach};
        info.depthAttachment = depthAttach;

        core::beginDynamicRendering(commandBuffer, info);
        core::endDynamicRendering(commandBuffer);

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImage,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);
    }
}
