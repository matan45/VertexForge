#pragma once
#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace core { class Device; class SwapChain; class Shader; }

namespace render::cloud
{
    struct CloudTemporalUBO
    {
        glm::mat4 invViewProjection;
        glm::mat4 prevViewProjection;
        glm::vec4 screenParams;      // x=halfWidth, y=halfHeight, z=near, w=far
        glm::vec4 temporalParams;    // x=blendFactor, y=frameIndex, z=0, w=0
    };

    class CloudTemporal
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        bool initialized = false;

        // History image (RGBA16F, half-res)
        vk::Image historyImage;
        core::VulkanAllocation historyAllocation;
        vk::ImageView historyView;

        // UBO
        vk::Buffer paramsBuffer;
        core::VulkanAllocation paramsBufferAllocation;
        void* paramsBufferMapped = nullptr;

        // Compute pipeline
        vk::DescriptorSetLayout dsLayout;
        vk::DescriptorPool dsPool;
        vk::DescriptorSet descriptorSet;
        vk::PipelineLayout pipelineLayout;
        vk::Pipeline pipeline;
        std::shared_ptr<core::Shader> shader;

        vk::Extent2D halfExtent{};
        bool needsInitialTransition = true;

    public:
        CloudTemporal(core::Device& device, core::SwapChain& swapChain);
        ~CloudTemporal();

        CloudTemporal(const CloudTemporal&) = delete;
        CloudTemporal& operator=(const CloudTemporal&) = delete;

        void init(vk::ImageView currentResultView, vk::Image currentResultImage);
        void cleanup();
        void recreate(vk::ImageView currentResultView, vk::Image currentResultImage);

        void updateParams(const CloudTemporalUBO& params);
        void dispatch(const vk::CommandBuffer& cmd);

        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] vk::ImageView getHistoryView() const { return historyView; }
        [[nodiscard]] vk::Image getHistoryImage() const { return historyImage; }
    };
}
