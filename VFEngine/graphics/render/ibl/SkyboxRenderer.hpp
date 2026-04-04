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
        vk::Pipeline graphicsPipeline;
        vk::Buffer vertexBuffer;
        core::VulkanAllocation vertexBufferAllocation;
        vk::Buffer uniformBuffer;
        core::VulkanAllocation uniformBufferAllocation;
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
        
        void renderToTarget(const vk::CommandBuffer& commandBuffer,
                            const SkyboxTargetParams& target) const;

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
