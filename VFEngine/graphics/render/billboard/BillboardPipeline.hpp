#pragma once

#include "BillboardTypes.hpp"
#include "BillboardBufferManager.hpp"
#include "BillboardAtlasManager.hpp"
#include <memory>
#include <vector>
#include <string>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
    struct OffscreenResources;
}

namespace render::billboard
{
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
        vk::DescriptorSet descriptorSet;

        std::vector<vk::Framebuffer> framebuffers;

        BillboardBufferManager bufferManager;
        BillboardAtlasManager atlasManager;

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

        vk::RenderPass getRenderPass() const { return renderPass; }
        vk::Pipeline getGraphicsPipeline() const { return graphicsPipeline; }

    private:
        void loadShader();
        void createRenderPass();
        void createDescriptorSetLayout();
        void createDescriptorPool();
        void createDescriptorSet();
        void createPipeline();
        void createFramebuffers();

        void updateDescriptorSet();
    };
}
