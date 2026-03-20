#pragma once
#include <vulkan/vulkan.hpp>
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::occlusion
{
    struct DepthPrepassPushConstants
    {
        uint32_t baseDrawIndex;
        uint32_t viewMode;
        float screenWidth;
        float screenHeight;
    };

    struct TerrainDepthPrepassPushConstants
    {
        uint32_t tileCount;
        uint32_t viewMode;
        float screenWidth;
        float screenHeight;
    };

    class DepthPrepassPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        // Scene depth prepass pipeline
        std::unique_ptr<core::Shader> sceneShader;
        vk::Pipeline scenePipeline;
        vk::PipelineLayout scenePipelineLayout;

        // Terrain depth prepass pipeline
        std::unique_ptr<core::Shader> terrainShader;
        vk::Pipeline terrainPipeline;
        vk::PipelineLayout terrainPipelineLayout;

        bool initialized = false;

    public:
        explicit DepthPrepassPipeline(core::Device& device, core::SwapChain& swapChain);
        ~DepthPrepassPipeline();

        DepthPrepassPipeline(const DepthPrepassPipeline&) = delete;
        DepthPrepassPipeline& operator=(const DepthPrepassPipeline&) = delete;

        void init(vk::RenderPass depthRenderPass,
                  vk::DescriptorSetLayout cameraLayout,
                  vk::DescriptorSetLayout perDrawLayout,
                  vk::DescriptorSetLayout bindlessTextureLayout,
                  vk::DescriptorSetLayout meshletDataLayout,
                  vk::DescriptorSetLayout vertexDataLayout,
                  vk::DescriptorSetLayout boneMatrixLayout,
                  vk::DescriptorSetLayout terrainDataLayout);

        void cleanup();

        void bindScenePipeline(vk::CommandBuffer cmd) const;
        void pushSceneConstants(vk::CommandBuffer cmd, const DepthPrepassPushConstants& pc) const;

        void bindTerrainPipeline(vk::CommandBuffer cmd) const;
        void pushTerrainConstants(vk::CommandBuffer cmd, const TerrainDepthPrepassPushConstants& pc) const;

        vk::PipelineLayout getScenePipelineLayout() const { return scenePipelineLayout; }
        vk::PipelineLayout getTerrainPipelineLayout() const { return terrainPipelineLayout; }
        bool isInitialized() const { return initialized; }

    private:
        void createScenePipeline(vk::RenderPass renderPass,
                                  vk::DescriptorSetLayout cameraLayout,
                                  vk::DescriptorSetLayout perDrawLayout,
                                  vk::DescriptorSetLayout bindlessTextureLayout,
                                  vk::DescriptorSetLayout meshletDataLayout,
                                  vk::DescriptorSetLayout vertexDataLayout,
                                  vk::DescriptorSetLayout boneMatrixLayout);

        void createTerrainPipeline(vk::RenderPass renderPass,
                                    vk::DescriptorSetLayout cameraLayout,
                                    vk::DescriptorSetLayout meshletDataLayout,
                                    vk::DescriptorSetLayout vertexDataLayout,
                                    vk::DescriptorSetLayout terrainDataLayout);
    };
}
