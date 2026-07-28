#pragma once

#include "TextTypes.hpp"
#include "TextBufferManager.hpp"
#include "TextFontCache.hpp"
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
    struct FontBatch
    {
        std::string fontPath;
        uint32_t firstInstance = 0;
        uint32_t instanceCount = 0;
    };

    class TextPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        bool initialized = false;

        std::shared_ptr<core::Shader> textShader;

        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;

        TextBufferManager bufferManager;
        TextFontCache fontCache;

        // Per-font descriptor sets
        std::unordered_map<std::string, vk::DescriptorSet> fontDescriptorSets;
        // TextFontCache::atlasGeneration() as of the last refresh of the map above.
        uint64_t lastAtlasGeneration = 0;
        vk::DescriptorSet defaultDescriptorSet;
        static constexpr uint32_t MAX_FONT_DESCRIPTORS = 32;

        std::vector<FontBatch> fontBatches;
        uint32_t totalInstanceCount = 0;

    public:
        explicit TextPipeline(core::Device& device, core::SwapChain& swapChain,
                              core::OffscreenResources& offscreenResources);
        ~TextPipeline();

        void init();
        void recreate();
        void cleanUp();
        void setDeletionQueue(core::DeferredDeletionQueue* queue) { bufferManager.setDeletionQueue(queue); }

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos) const;

        void setExternalCameraBuffer(vk::Buffer buffer) { bufferManager.setExternalCameraBuffer(buffer); }

        void setTextDrawList(const std::vector<TextRenderData>& textEntities);

        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void recordCommandBufferGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

        bool isInitialized() const { return initialized; }

        TextFontCache& getFontCache() { return fontCache; }

    private:
        void loadShader();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void createDefaultDescriptorSet();
        void createPipeline();

        void updateDescriptorSet(vk::DescriptorSet dstSet, vk::ImageView imageView, vk::Sampler sampler);
        vk::DescriptorSet getOrCreateFontDescriptorSet(const std::string& fontPath);
        // VK-1638: a reimport keeps the font PATH but replaces the atlas behind it, so
        // the sets cached above point at a destroyed image view. Re-point rather than
        // free: the sets stay allocated, which keeps the descriptor budget honest.
        void refreshFontDescriptorSetsIfStale();
    };
}
