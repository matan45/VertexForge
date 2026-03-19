#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>
#include <glm/glm.hpp>

namespace core
{
    class Device;
    class Shader;
}

namespace render::shadow
{
    struct ShadowPushConstants
    {
        glm::mat4 lightViewProjection;
        uint32_t baseDrawIndex;
        float depthBias;
        float slopeBias;
        float normalBias;
        uint32_t objectFilterMask = 0;   // AND with PerDrawData.flags
        uint32_t objectFilterValue = 0;  // expected result after AND (0,0 = all objects pass)
    };

    class ShadowPassPipeline
    {
    private:
        core::Device& device;

        vk::RenderPass shadowRenderPass;          // eClear - used when no cached tiles
        vk::RenderPass shadowRenderPassLoad;      // eLoad  - preserves cached tiles
        std::unique_ptr<core::Shader> shadowShader;
        vk::Pipeline shadowPipeline;
        vk::PipelineLayout shadowPipelineLayout;
        vk::Framebuffer atlasFramebuffer;
        vk::Framebuffer atlasFramebufferLoad;     // Framebuffer for the eLoad render pass

        vk::DescriptorSetLayout cachedPerDrawLayout;
        vk::DescriptorSetLayout cachedMeshletDataLayout;
        vk::DescriptorSetLayout cachedVertexDataLayout;
        vk::DescriptorSetLayout cachedBoneMatrixLayout;
        vk::DescriptorSetLayout cameraUBOLayout;
        vk::DescriptorPool cameraDescriptorPool;
        vk::DescriptorSet cameraDescriptorSet;

        bool initialized = false;
        vk::Format depthFormat = vk::Format::eD32Sfloat;

    public:
        explicit ShadowPassPipeline(core::Device& device);
        ~ShadowPassPipeline();

        ShadowPassPipeline(const ShadowPassPipeline&) = delete;
        ShadowPassPipeline& operator=(const ShadowPassPipeline&) = delete;

        void init(vk::DescriptorSetLayout perDrawLayout,
                  vk::DescriptorSetLayout meshletDataLayout,
                  vk::DescriptorSetLayout vertexDataLayout,
                  vk::DescriptorSetLayout boneMatrixLayout,
                  vk::Format atlasDepthFormat);

        void updateCameraDescriptor(vk::Buffer cameraBuffer, vk::DeviceSize bufferSize);
        [[nodiscard]] vk::DescriptorSet getCameraDescriptorSet() const { return cameraDescriptorSet; }

        void cleanup();

        void createFramebuffer(vk::ImageView depthImageView, uint32_t width, uint32_t height);
        void destroyFramebuffer();

        [[nodiscard]] vk::RenderPass getRenderPass() const { return shadowRenderPass; }
        [[nodiscard]] vk::RenderPass getRenderPassLoad() const { return shadowRenderPassLoad; }
        [[nodiscard]] vk::Pipeline getPipeline() const { return shadowPipeline; }
        [[nodiscard]] vk::PipelineLayout getPipelineLayout() const { return shadowPipelineLayout; }
        [[nodiscard]] vk::Framebuffer getFramebuffer() const { return atlasFramebuffer; }
        [[nodiscard]] vk::Framebuffer getFramebufferLoad() const { return atlasFramebufferLoad; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void createShadowRenderPass();
        void createShadowRenderPassLoad();
        void createShadowPipeline();
        void createCameraDescriptorResources();
    };
}
