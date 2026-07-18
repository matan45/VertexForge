#pragma once
#include "../core/OffScreen.hpp"
#include "terrain/TerrainHitResult.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>

namespace core
{
    class Device;
    class SwapChain;
    class CommandPool;
    class AsyncComputeManager;
}

namespace types
{
    struct DynamicResolutionSettings; // VK-1531
}

namespace render
{
    class RenderPassHandler;

    using PreRenderCallback = std::function<void()>;

    class OffScreenViewPort
    {
    public:
        struct PendingRenderWait
        {
            vk::Semaphore semaphore{};
            vk::PipelineStageFlags stageMask{vk::PipelineStageFlagBits::eFragmentShader};
            uint64_t timelineValue = 0;
        };

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
        uint32_t skipAsyncComputeFrames = 0;
        bool upscaleResourcesDirty = false;

        // VK-1531 dynamic-resolution controller state (owned here; ticked in render()).
        bool drEnabled = false;
        float drTargetMs = 16.6f;
        float drMinScale = 0.5f;
        float drAppliedScale = 1.0f;
        uint32_t drFramesOver = 0;
        uint32_t drFramesUnder = 0;

        inline static std::mutex pendingRenderWaitsMutex;
        inline static std::vector<PendingRenderWait> pendingRenderWaits;

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

        void setBrushOverlayParams(float radius, float falloff, float shape, float stampRotation = 0.0f);
        void setStampOverlay(vk::Buffer buffer, uint32_t width, uint32_t height, float rotation);
        void clearStampOverlay();
        void setAsyncComputeManager(core::AsyncComputeManager* manager);

        const core::OffscreenResources& getOffscreenResources() const { return offscreenResources; }
        core::OffscreenResources& getOffscreenResources() { return offscreenResources; }

        void setUpscaleResourcesDirty(bool dirty) { upscaleResourcesDirty = dirty; }
        bool isUpscaleResourcesDirty() const { return upscaleResourcesDirty; }

        // VK-1531 adaptive dynamic resolution. The controller is ticked once per frame from
        // render(); the settings ride the RenderSettings apply path. A scale step reallocates the
        // offscreen targets in place and blanks ImGui for one frame (RenderManager one-frame skip,
        // the same contract as a window resize).
        void setDynamicResolutionSettings(const types::DynamicResolutionSettings& s);

        static void addPendingRenderWait(PendingRenderWait wait);

    private:
        void draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void tickDynamicResolution();
        static std::vector<PendingRenderWait> consumePendingRenderWaits();

        void createOffscreenResources();
        void cleanupOffscreenResources();
        void createUpscaleResources(uint32_t renderWidth, uint32_t renderHeight,
                                     uint32_t displayWidth, uint32_t displayHeight);
        void cleanupUpscaleResources();
        void createPrevFrameDepthResources(uint32_t width, uint32_t height);
        void cleanupPrevFrameDepthResources();
        void updateDescriptorSets(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const;
        void createSampler();
    };
}
