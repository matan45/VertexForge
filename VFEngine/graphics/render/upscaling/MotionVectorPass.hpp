#pragma once

#include "../../core/VulkanMemoryManager.hpp"
#include "../../core/PerFrameBuffer.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::upscaling
{
    struct alignas(16) MotionVectorParams
    {
        glm::mat4 invViewProjection;
        glm::mat4 prevViewProjection;
        glm::vec4 screenParams;     // xy = resolution, zw = 1/resolution
    };
    static_assert(sizeof(MotionVectorParams) == 144);

    class MotionVectorPass
    {
    public:
        explicit MotionVectorPass(core::Device& device);
        ~MotionVectorPass();

        MotionVectorPass(const MotionVectorPass&) = delete;
        MotionVectorPass& operator=(const MotionVectorPass&) = delete;

        void init();
        void cleanup();

        void dispatch(vk::CommandBuffer cmd,
                      vk::ImageView depthView,
                      vk::Image depthImage,
                      vk::ImageView motionVectorStorageView,
                      vk::Image motionVectorImage,
                      const glm::mat4& invViewProjection,
                      const glm::mat4& prevViewProjection,
                      uint32_t width, uint32_t height,
                      uint32_t frameIndex);

        bool isInitialized() const { return initialized; }

    private:
        core::Device& device;

        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        std::unique_ptr<core::Shader> shader;

        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSets[core::MAX_FRAMES_IN_FLIGHT];

        core::PerFrameBuffer paramsBuffer;
        vk::Sampler depthSampler;

        bool initialized = false;

        void createSampler();
        void createParamsBuffer();
        void createDescriptorLayout();
        void createDescriptorPool();
        void allocateDescriptorSets();
        void createComputePipeline();
    };
}
