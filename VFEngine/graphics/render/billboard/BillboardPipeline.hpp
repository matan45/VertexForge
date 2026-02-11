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
        static constexpr uint32_t MAX_CUSTOM_TEXTURES = 32;

        uint32_t atlasInstanceCount = 0;
        std::vector<CustomTextureBatch> customBatches;

    public:
        explicit BillboardPipeline(core::Device& device, core::SwapChain& swapChain,
                                   core::OffscreenResources& offscreenResources);
        ~BillboardPipeline();

        void init();
        void recreate();
        void cleanUp();

        bool loadAtlas(const std::string& atlasPath);

        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos) const;

        void setBillboardList(const std::vector<BillboardRenderData>& billboards);

        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

        bool isInitialized() const { return initialized; }

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
