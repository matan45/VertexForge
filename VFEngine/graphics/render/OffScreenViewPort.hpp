#pragma once
#include "../core/OffScreen.hpp"
#include "terrain/TerrainHitResult.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <functional>

namespace core
{
    class Device;
    class SwapChain;
    class CommandPool;
    class AsyncComputeManager;
}

namespace render
{
    class RenderPassHandler;

    using PreRenderCallback = std::function<void()>;

    class OffScreenViewPort
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        std::unique_ptr<core::CommandPool> commandPool;

        std::unique_ptr<render::RenderPassHandler> renderPassHandler;

        vk::Sampler sampler;
        core::OffscreenResources offscreenResources;
        std::vector<vk::Fence> inFlightFences;

        // Async compute (non-owning, set by controller)
        core::AsyncComputeManager* asyncComputeManager = nullptr;
        bool asyncComputeLoggedOnce = false;

    public:
        explicit OffScreenViewPort(core::Device& device, core::SwapChain& swapChain);
        ~OffScreenViewPort();

        void init();
        void recreate();
        vk::DescriptorSet render(const PreRenderCallback& preRenderCallback = nullptr);

        void cleanUp();
        render::RenderPassHandler* getRenderPassHandler() const { return renderPassHandler.get(); }

        // Get the offscreen color image for a given swapchain index (for runtime blit)
        vk::Image getColorImage(uint32_t index) const;

        void setRaycastCursorUV(const glm::vec2& uv);
        void clearRaycastCursor();
        terrain::TerrainHitResult getTerrainHitResult() const;

        void setBrushOverlayParams(float radius, float falloff, float shape);
        void setAsyncComputeManager(core::AsyncComputeManager* manager);

    private:
        void draw(const vk::CommandBuffer& commandBuffer) const;

        void createOffscreenResources();
        void cleanupOffscreenResources();
        void updateDescriptorSets(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const;
        void createSampler();
    };
}
