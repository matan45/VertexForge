#pragma once

#include "UITextRenderTypes.hpp"
#include "UITextBufferManager.hpp"
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

namespace render::text
{
    class TextFontCache;
}

namespace render::ui
{
    struct UITextFontBatch
    {
        std::string fontPath;
        uint32_t firstInstance = 0;
        uint32_t instanceCount = 0;
        UIStencilOp stencilOp = UIStencilOp::None;
        uint8_t stencilRef = 0;
    };

    struct UITextScissorGroup
    {
        glm::vec4 scissorRect{0.0f, 0.0f, 0.0f, 0.0f};
        std::vector<UITextFontBatch> batches;
        bool overlay = false; // overlay groups record in the second UI pass
    };

    class UITextPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;
        render::text::TextFontCache& fontCache;

        bool initialized = false;

        std::shared_ptr<core::Shader> uiTextShader;

        vk::Pipeline graphicsPipeline;
        vk::Pipeline pipelineStencilTest;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet defaultDescriptorSet;

        UITextBufferManager bufferManager;

        // Per-font descriptor sets
        std::unordered_map<std::string, vk::DescriptorSet> fontDescriptorSets;
        static constexpr uint32_t MAX_FONT_DESCRIPTORS = 32;

        std::vector<UITextScissorGroup> scissorGroups;
        uint32_t totalInstanceCount = 0;

    public:
        explicit UITextPipeline(core::Device& device, core::SwapChain& swapChain,
                                core::OffscreenResources& offscreenResources,
                                render::text::TextFontCache& fontCache);
        ~UITextPipeline();

        void init();
        void recreate();
        void cleanUp();
        void setDeletionQueue(core::DeferredDeletionQueue* queue) { bufferManager.setDeletionQueue(queue); }

        void setUITextDrawList(const std::vector<UITextRenderData>& labels);

        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        // overlayPass=false records main UI text; overlayPass=true records
        // overlay text (tooltips, modal windows) after the overlay image pass.
        void recordCommandBufferGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                             bool overlayPass = false) const;

        bool isInitialized() const { return initialized; }

    private:
        void loadShader();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void createDefaultDescriptorSet();
        void createPipeline();

        void updateDescriptorSet(vk::DescriptorSet dstSet, vk::ImageView imageView, vk::Sampler sampler);
        vk::DescriptorSet getOrCreateFontDescriptorSet(const std::string& fontPath);
    };
}
