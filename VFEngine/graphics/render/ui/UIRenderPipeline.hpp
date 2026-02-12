#pragma once

#include "UIRenderTypes.hpp"
#include "UIRenderBufferManager.hpp"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
    class Texture;
    struct OffscreenResources;
}

namespace render::ui
{
    struct UITextureBatch
    {
        std::string texturePath;
        uint32_t firstInstance = 0;
        uint32_t instanceCount = 0;
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

        vk::RenderPass renderPass;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet defaultDescriptorSet;

        std::vector<vk::Framebuffer> framebuffers;

        UIRenderBufferManager bufferManager;

        struct TextureEntry
        {
            std::unique_ptr<core::Texture> texture;
            vk::DescriptorSet descriptorSet;
        };
        std::unordered_map<std::string, TextureEntry> textureCache;
        static constexpr uint32_t MAX_UI_TEXTURES = 64;

        std::vector<UIScissorGroup> scissorGroups;
        uint32_t totalInstanceCount = 0;

    public:
        explicit UIRenderPipeline(core::Device& device, core::SwapChain& swapChain,
                                  core::OffscreenResources& offscreenResources);
        ~UIRenderPipeline();

        void init();
        void recreate();
        void cleanUp();

        void setUIImageDrawList(const std::vector<UIImageRenderData>& images);

        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

        bool isInitialized() const { return initialized; }

    private:
        void loadShader();
        void createRenderPass();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void createDefaultDescriptorSet();
        void createPipeline();
        void createFramebuffers();

        void updateDescriptorSet(vk::DescriptorSet dstSet, vk::ImageView imageView, vk::Sampler sampler);
        bool loadTexture(const std::string& texturePath);
    };
}
