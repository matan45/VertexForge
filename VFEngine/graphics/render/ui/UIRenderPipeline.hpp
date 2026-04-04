#pragma once

#include "UIRenderTypes.hpp"
#include "UIRenderBufferManager.hpp"
#include "../../core/Texture.hpp"
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
        std::string texturePath;
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
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet defaultDescriptorSet;

        UIRenderBufferManager bufferManager;

        struct TextureEntry
        {
            std::unique_ptr<core::Texture> texture;
            vk::DescriptorSet descriptorSet;
        };
        std::unordered_map<std::string, TextureEntry> textureCache;
        std::unordered_map<std::string, vk::DescriptorSet> externalTextureCache;
        static constexpr uint32_t MAX_UI_TEXTURES = 64;
        static constexpr uint32_t MAX_EXTERNAL_TEXTURES = 8;

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

        void registerExternalTexture(const std::string& key, vk::ImageView imageView, vk::Sampler externalSampler);
        void unregisterExternalTexture(const std::string& key);
        void clearExternalTextures();

        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

        bool isInitialized() const { return initialized; }

    private:
        void loadShader();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void createDefaultDescriptorSet();
        void createPipeline();

        void updateDescriptorSet(vk::DescriptorSet dstSet, vk::ImageView imageView, vk::Sampler sampler);
        bool loadTexture(const std::string& texturePath);
    };
}
