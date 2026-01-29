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
    };

    class ShadowPassPipeline
    {
    private:
        core::Device& device;

        vk::RenderPass shadowRenderPass;
        std::unique_ptr<core::Shader> shadowShader;
        vk::Pipeline shadowPipeline;
        vk::PipelineLayout shadowPipelineLayout;
        vk::Framebuffer atlasFramebuffer;

        vk::DescriptorSetLayout cachedPerDrawLayout;
        vk::DescriptorSetLayout cachedMeshletDataLayout;
        vk::DescriptorSetLayout cachedVertexDataLayout;
        vk::DescriptorSetLayout cachedBoneMatrixLayout;

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

        void cleanup();

        void createFramebuffer(vk::ImageView depthImageView, uint32_t width, uint32_t height);
        void destroyFramebuffer();

        [[nodiscard]] vk::RenderPass getRenderPass() const { return shadowRenderPass; }
        [[nodiscard]] vk::Pipeline getPipeline() const { return shadowPipeline; }
        [[nodiscard]] vk::PipelineLayout getPipelineLayout() const { return shadowPipelineLayout; }
        [[nodiscard]] vk::Framebuffer getFramebuffer() const { return atlasFramebuffer; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void createShadowRenderPass();
        void createShadowPipeline();
    };
}
