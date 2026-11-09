#pragma once

#include "../../core/VulkanMemoryManager.hpp"
#include "../../core/PerFrameBuffer.hpp"
#include "TemporalUpscaleMath.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <cstddef>
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::upscaling
{
    // std140 UBO of motion_vectors_fullscreen.glsl. VK-1662: two camera-relative clip-to-clip matrices
    // (buildMotionVectorMatrices) instead of an inverse view-projection, so no fp32 world position is formed.
    struct alignas(16) MotionVectorParams
    {
        glm::mat4 jitteredToCurrentClip;
        glm::mat4 jitteredToPreviousClip;
        glm::vec4 screenParams;     // xy = resolution, zw = 1/resolution
    };
    static_assert(sizeof(MotionVectorParams) == 144);
    static_assert(offsetof(MotionVectorParams, jitteredToPreviousClip) == 64);
    static_assert(offsetof(MotionVectorParams, screenParams) == 128);

    class MotionVectorPass
    {
    public:
        explicit MotionVectorPass(core::Device& device);
        ~MotionVectorPass();

        MotionVectorPass(const MotionVectorPass&) = delete;
        MotionVectorPass& operator=(const MotionVectorPass&) = delete;

        void init();
        void cleanup();

        // Writes previous - current motion vectors (NDC, jitter-free) for every render pixel. depthView is this
        // frame's depth in DepthStencilReadOnlyOptimal (the Upscale pass's DepthSampledComputeRead).
        void dispatch(vk::CommandBuffer cmd,
                      vk::ImageView depthView,
                      vk::ImageView motionVectorStorageView,
                      vk::Image motionVectorImage,
                      const MotionVectorMatrices& matrices,
                      uint32_t width, uint32_t height,
                      uint32_t frameIndex);

        // False also when the shader failed to compile (init() still runs to completion, without a pipeline).
        bool isInitialized() const { return initialized && computePipeline; }

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
