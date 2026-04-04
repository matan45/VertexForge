#include "PreviewGridRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"

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

    void PreviewGridRenderer::recreate()
    {
        // With dynamic rendering, no render pass or framebuffers to recreate.
        // GridRenderer pipeline uses dynamic rendering formats.
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

        vk::ImageView colorView = offscreenResources.colorImages[imageIndex].colorImageView;
        vk::ImageView depthView = offscreenResources.depthImage.depthImageView;

        core::DynamicRenderingInfo renderingInfo{};
        renderingInfo.extent = swapChain.getSwapchainExtent();
        renderingInfo.colorAttachments = {core::colorLoad(colorView)};
        renderingInfo.depthAttachment = core::depthLoad(depthView);

        core::beginDynamicRendering(commandBuffer, renderingInfo);
        gridRenderer->render(commandBuffer, view, projection);
        core::endDynamicRendering(commandBuffer);
    }
}
