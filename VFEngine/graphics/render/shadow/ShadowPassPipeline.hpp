#pragma once

#include "ShadowTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <glm/glm.hpp>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::shadow
{
    struct ShadowPushConstants
    {
        glm::mat4 lightViewProjection;      // 64 bytes
        uint32_t baseDrawIndex;             // 4 bytes
        float depthBias;                    // 4 bytes
        float slopeBias;                    // 4 bytes
        float normalBias;                   // 4 bytes
    };  // 80 bytes total

    class ShadowPassPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        // Render pass (depth-only)
        vk::RenderPass shadowRenderPass;

        // Pipeline
        std::unique_ptr<core::Shader> shadowShader;
        vk::Pipeline shadowPipeline;
        vk::PipelineLayout shadowPipelineLayout;

        // Framebuffer for atlas rendering
        vk::Framebuffer atlasFramebuffer;

        // Cached layouts (external, not owned)
        vk::DescriptorSetLayout cachedPerDrawLayout;
        vk::DescriptorSetLayout cachedMeshletDataLayout;
        vk::DescriptorSetLayout cachedVertexDataLayout;
        vk::DescriptorSetLayout cachedBoneMatrixLayout;

        // State
        bool initialized = false;
        vk::Format depthFormat = vk::Format::eD32Sfloat;

    public:
        explicit ShadowPassPipeline(core::Device& device, core::SwapChain& swapChain);
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

        // Accessors
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
