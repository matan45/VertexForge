#pragma once

#include "UIRenderTypes.hpp"
#include "UIRenderBufferManager.hpp"
#include "../../core/Texture.hpp"
#include "../gpudriven/scene/BindlessTextureManager.hpp"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
    struct OffscreenResources;
}

namespace render::ui
{
    struct UITextureBatch
    {
        uint32_t firstInstance = 0;
        uint32_t instanceCount = 0;
        UIStencilOp stencilOp = UIStencilOp::None;
        uint8_t stencilRef = 0;
        bool discardColor = false;
        float alphaThreshold = 0.0f;
    };

    struct UIScissorGroup
    {
        glm::vec4 scissorRect{0.0f, 0.0f, 0.0f, 0.0f}; // 0,0,0,0 = full viewport
        std::vector<UITextureBatch> batches;
        bool overlay = false; // overlay groups record in the second UI pass
    };

    class UIRenderPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        bool initialized = false;

        std::shared_ptr<core::Shader> uiShader;

        vk::Pipeline pipelineNormal;
        vk::Pipeline pipelineStencilIncNoColor;
        vk::Pipeline pipelineStencilIncColor;
        vk::Pipeline pipelineStencilTest;
        vk::Pipeline pipelineStencilDecNoColor;
        vk::PipelineLayout pipelineLayout;

        UIRenderBufferManager bufferManager;

        // UI-owned bindless texture table (independent of the mesh-material instance).
        render::gpudriven::BindlessTextureManager uiBindless;

        struct TextureEntry
        {
            std::unique_ptr<core::Texture> texture;
            uint32_t bindlessIndex = 0;
        };
        struct ExternalTextureEntry
        {
            std::vector<uint32_t> bindlessIndices;  // one slot per swapchain image
            std::vector<vk::ImageView> imageViews;  // last-seen view per image (change detection)
            std::vector<vk::Sampler> samplers;      // last-seen sampler per image
        };
        std::unordered_map<std::string, TextureEntry> textureCache;
        std::unordered_map<std::string, ExternalTextureEntry> externalTextureCache;
        static constexpr uint32_t MAX_UI_TEXTURES = 64;
        static constexpr uint32_t MAX_EXTERNAL_TEXTURES = 8;
        static constexpr uint32_t UI_BINDLESS_CAPACITY = 2048;

        std::vector<UIScissorGroup> scissorGroups;
        uint32_t totalInstanceCount = 0;

    public:
        explicit UIRenderPipeline(core::Device& device, core::SwapChain& swapChain,
                                  core::OffscreenResources& offscreenResources);
        ~UIRenderPipeline();

        void init();
        void recreate();
        void cleanUp();
        void setDeletionQueue(core::DeferredDeletionQueue* queue) { bufferManager.setDeletionQueue(queue); }

        void setUIImageDrawList(const std::vector<UIImageRenderData>& images);

        void registerExternalTexture(const std::string& key, uint32_t imageIndex,
                                     vk::ImageView imageView, vk::Sampler externalSampler);
        void unregisterExternalTexture(const std::string& key);
        void clearExternalTextures();

        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        // overlayPass=false records the main UI groups; overlayPass=true records
        // overlay groups (tooltips, modal windows) — called again after UI text
        // so overlays cover underlying labels.
        // Forwards to the extent-parameterized overload below at the swapchain display extent,
        // so the main UI swapchain path is byte-identical.
        void recordCommandBufferGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                             bool overlayPass = false) const;
        // VK-1435 — extent-parameterized record for offscreen UI targets at a non-display
        // resolution (UI Layer Builder preview). targetExtent drives the render area, the dynamic
        // viewport, the viewportSize push constant, and the full-pass scissor fallback.
        void recordCommandBufferGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                             vk::Extent2D targetExtent, bool overlayPass) const;

        bool isInitialized() const { return initialized; }

    private:
        void loadShader();
        void createPipeline();

        bool loadTexture(const std::string& texturePath);
    };
}
