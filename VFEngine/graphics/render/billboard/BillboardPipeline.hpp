#pragma once

#include "BillboardTypes.hpp"
#include "BillboardBufferManager.hpp"
#include "BillboardAtlasManager.hpp"
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

namespace render::billboard
{
    struct CustomTextureBatch
    {
        std::string texturePath;
        uint32_t firstInstance = 0;
        uint32_t instanceCount = 0;
    };

    class BillboardPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        bool initialized = false;

        std::shared_ptr<core::Shader> billboardShader;

        vk::RenderPass renderPass;
        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet atlasDescriptorSet;

        std::vector<vk::Framebuffer> framebuffers;

        BillboardBufferManager bufferManager;
        BillboardAtlasManager atlasManager;

        struct CustomTextureEntry
        {
            std::unique_ptr<core::Texture> texture;
            vk::DescriptorSet descriptorSet;
        };
        std::unordered_map<std::string, CustomTextureEntry> customTextureCache;
        std::unordered_map<std::string, vk::DescriptorSet> externalTextureCache;
        static constexpr uint32_t MAX_CUSTOM_TEXTURES = 32;
        static constexpr uint32_t MAX_EXTERNAL_TEXTURES = 8;

        uint32_t atlasInstanceCount = 0;
        std::vector<CustomTextureBatch> customBatches;

        bool distanceCullingEnabled_ = false;
        float maxBillboardDistSq_ = 0.0f;
        glm::vec3 cameraPos_{0.0f};

    public:
        explicit BillboardPipeline(core::Device& device, core::SwapChain& swapChain,
                                   core::OffscreenResources& offscreenResources);
        ~BillboardPipeline();

        void init();
        void recreate();
        void cleanUp();
        void setDeletionQueue(core::DeferredDeletionQueue* queue) { bufferManager.setDeletionQueue(queue); }

        bool loadAtlas(const std::string& atlasPath);

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos);

        void setBillboardList(const std::vector<BillboardRenderData>& billboards);

        void registerExternalTexture(const std::string& key, vk::ImageView imageView, vk::Sampler externalSampler);
        void unregisterExternalTexture(const std::string& key);
        void clearExternalTextures();

        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

        bool isInitialized() const { return initialized; }

        void setDistanceCullingEnabled(bool enabled) { distanceCullingEnabled_ = enabled; }
        void setMaxDrawDistance(float distance) { maxBillboardDistSq_ = distance * distance; }

    private:
        void loadShader();
        void createRenderPass();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void createDescriptorSet();
        void createPipeline();
        void createFramebuffers();

        void updateDescriptorSet(vk::DescriptorSet dstSet, vk::ImageView imageView, vk::Sampler sampler);
        bool loadCustomTexture(const std::string& texturePath);
    };
}
