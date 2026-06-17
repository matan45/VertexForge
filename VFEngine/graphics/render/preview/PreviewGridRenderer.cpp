#include "PreviewGridRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include "../../core/ImageUtilities.hpp"

namespace render::preview
{
    PreviewGridRenderer::PreviewGridRenderer(core::Device& device, core::SwapChain& swapChain,
                                              core::OffscreenResources& offscreenResources)
        : device{device}
        , swapChain{swapChain}
        , offscreenResources{offscreenResources}
        , gridRenderer{std::make_unique<mesh::GridRenderer>(device, swapChain)}
    {
    }

    PreviewGridRenderer::~PreviewGridRenderer() = default;

    void PreviewGridRenderer::init()
    {
        gridRenderer->init(swapChain.getSceneColorFormat(), swapChain.getSwapchainDepthStencilFormat());
        initialized = true;
    }

    void PreviewGridRenderer::cleanUp()
    {
        if (!initialized) return;

        gridRenderer->cleanUp();

        initialized = false;
    }

    void PreviewGridRenderer::cleanUpShader()
    {
        gridRenderer->cleanUpShader();
    }

    void PreviewGridRenderer::render(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                      const glm::mat4& view, const glm::mat4& projection,
                                      bool visible) const
    {
        if (!initialized || !visible) return;

        vk::Image colorImage = offscreenResources.colorImages[imageIndex].colorImage;
        vk::ImageView colorView = offscreenResources.colorImages[imageIndex].colorImageView;
        vk::ImageView depthView = offscreenResources.depthImage.depthImageView;

        core::ImageUtilities::transitionImageLayout(commandBuffer, colorImage,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        core::DynamicRenderingInfo renderingInfo{};
        renderingInfo.extent = swapChain.getSwapchainExtent();
        renderingInfo.colorAttachments = {core::colorLoad(colorView)};
        renderingInfo.depthAttachment = core::depthLoad(depthView);

        core::beginDynamicRendering(commandBuffer, renderingInfo);
        // The grid pipeline enables VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT, so the
        // sample count must be set to match this (single-sample) preview target before
        // drawing. The main viewport inherits it from the scene pass; previews don't.
        commandBuffer.setRasterizationSamplesEXT(offscreenResources.sampleCount);
        gridRenderer->render(commandBuffer, view, projection);
        core::endDynamicRendering(commandBuffer);

        core::ImageUtilities::transitionImageLayout(commandBuffer, colorImage,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);
    }
}
