#pragma once

#include "SVTTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::svt
{
    // GPU feedback params UBO (matches svt_feedback.glsl layout)
    struct alignas(16) SVTFeedbackParamsGPU
    {
        glm::mat4 invViewProjection;
        glm::vec4 screenParams;      // width, height, 1/width, 1/height
        glm::vec4 svtScaleOffset;    // xy = scale, zw = offset
        glm::uvec4 svtInfo;          // x = virtualSizeLog2, y = tileSizeLog2, z = unused, w = mipLevels
        glm::vec4 cameraPos;         // xyz = camera world position
    };

    // Compute pipeline that reads the depth buffer and generates tile requests
    // into a feedback SSBO. Pattern follows vsm_feedback.glsl.
    class SVTFeedbackPipeline
    {
    private:
        core::Device& device;
        SVTConfig config;

        std::unique_ptr<core::Shader> shader;
        vk::Pipeline pipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;

        // Double-buffered feedback: GPU writes to one, CPU reads the other
        static constexpr uint32_t FEEDBACK_BUFFER_COUNT = 3; // Triple-buffer for latency
        struct FeedbackFrame
        {
            vk::Buffer feedbackBuffer;
            vk::DeviceMemory feedbackMemory;
            void* feedbackMapped = nullptr;
            vk::DescriptorSet descriptorSet;
        };
        FeedbackFrame feedbackFrames[FEEDBACK_BUFFER_COUNT];

        // UBO for feedback params
        vk::Buffer paramsBuffer;
        vk::DeviceMemory paramsMemory;
        void* paramsMapped = nullptr;

        uint32_t totalPageTableEntries = 0;
        uint64_t currentFrame = 0;

        bool initialized = false;

    public:
        explicit SVTFeedbackPipeline(core::Device& device);
        ~SVTFeedbackPipeline();

        SVTFeedbackPipeline(const SVTFeedbackPipeline&) = delete;
        SVTFeedbackPipeline& operator=(const SVTFeedbackPipeline&) = delete;

        void init(const SVTConfig& config, vk::ImageView depthImageView, vk::Sampler depthSampler);
        void cleanup();

        // Update the depth image view (e.g., after resize)
        void updateDepthImage(vk::ImageView depthImageView, vk::Sampler depthSampler);

        // Dispatch the feedback compute pass
        void dispatch(vk::CommandBuffer cmd, uint64_t frameIndex,
                      const glm::mat4& invViewProjection,
                      const glm::vec4& screenParams,
                      const glm::vec3& cameraPos,
                      const glm::vec2& svtScale,
                      const glm::vec2& svtOffset);

        // Read back feedback data from N frames ago. Returns pointer to feedback uint array.
        // Caller should process and then clear.
        const uint32_t* readFeedback(uint64_t frameIndex) const;

        // Clear the feedback buffer for the current write frame
        void clearFeedbackBuffer(vk::CommandBuffer cmd, uint64_t frameIndex);

        uint32_t getTotalEntries() const { return totalPageTableEntries; }
        bool isInitialized() const { return initialized; }

    private:
        void createDescriptorSetLayout();
        void createPipeline();
        void createFeedbackBuffers();
        void createParamsBuffer();

        uint32_t writeFrameIndex(uint64_t frame) const { return frame % FEEDBACK_BUFFER_COUNT; }
        uint32_t readFrameIndex(uint64_t frame) const
        {
            return (frame + FEEDBACK_BUFFER_COUNT - config.feedbackLatencyFrames) % FEEDBACK_BUFFER_COUNT;
        }
    };
}
