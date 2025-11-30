#pragma once
#include "IBLTypes.hpp"
#include "components/Components.hpp"
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
    public:
        SkyboxRenderer(core::Device& device, core::SwapChain& swapChain,
                       core::OffscreenResources& offscreenResources);
        ~SkyboxRenderer() = default;

        void init(const ImageData& irradianceCube);
        void recreate();
        void cleanUp();
        void cleanUpShader();

        void recordCommandBuffer(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

        void setCamera(components::CameraComponent* camera) {
            this->camera = camera;
            isDisplay = (camera != nullptr);
        }

        bool isDisplaying() const { return isDisplay; }

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

        components::CameraComponent* camera = nullptr;
        bool isDisplay = false;

        void updateUniformBuffer(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) const;
    };
}
