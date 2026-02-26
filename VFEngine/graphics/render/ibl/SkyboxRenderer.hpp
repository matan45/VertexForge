#pragma once
#include "IBLTypes.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
    struct OffscreenResources;
}

namespace render::ibl
{
    class SkyboxRenderer
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        std::shared_ptr<core::Shader> skyboxShader;
        vk::RenderPass renderPass;
        vk::Pipeline graphicsPipeline;
        std::vector<vk::Framebuffer> framebuffers;
        vk::Buffer vertexBuffer;
        vk::DeviceMemory vertexBufferMemory;
        vk::Buffer uniformBuffer;
        vk::DeviceMemory uniformBufferMemory;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorSet descriptorSet;
        vk::DescriptorPool descriptorPool;

        // Camera matrices (set via setCameraMatrices)
        glm::mat4 viewMatrix{1.0f};
        glm::mat4 projectionMatrix{1.0f};
        bool isDisplay = false;
        bool initialized = false;

    public:
        explicit SkyboxRenderer(core::Device& device, core::SwapChain& swapChain,
                                core::OffscreenResources& offscreenResources);
        ~SkyboxRenderer() = default;

        void init(const ImageData& irradianceCube);
        void recreate();
        void cleanUp();
        void cleanUpShader();

        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

        // Render skybox to an external target (e.g., RTT framebuffer).
        // The external render pass must be compatible (single color attachment, same format, no depth).
        void renderToTarget(const vk::CommandBuffer& commandBuffer,
                            vk::RenderPass targetPass, vk::Framebuffer targetFramebuffer,
                            uint32_t targetWidth, uint32_t targetHeight,
                            const glm::mat4& view, const glm::mat4& projection,
                            const glm::vec4& clearCol) const;

        bool isInitialized() const { return initialized; }

        // Set camera matrices directly - works with both EditorCamera and CameraComponent
        void setCameraMatrices(const glm::mat4& view, const glm::mat4& projection)
        {
            viewMatrix = view;
            projectionMatrix = projection;
            isDisplay = true;
        }

        void disable()
        {
            isDisplay = false;
        }

        bool isDisplaying() const { return isDisplay; }

    private:
        void updateUniformBuffer(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) const;
    };
}
