#pragma once

#include "VSMTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class Shader;
}

namespace render::shadow
{
    enum class FeedbackReadbackState : uint8_t
    {
        Idle,
        Pending,
        Ready
    };

    struct alignas(16) FeedbackParams
    {
        glm::mat4 invViewProjection;
        glm::vec4 screenParams; // width, height, 1/width, 1/height
        uint32_t lightCount;
        uint32_t pad0;
        uint32_t pad1;
        uint32_t pad2;
    };
    static_assert(sizeof(FeedbackParams) == 96, "FeedbackParams must be 96 bytes");

    class VSMFeedbackPipeline
    {
    private:
        core::Device& device;

        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;
        std::unique_ptr<core::Shader> shader;

        // Feedback buffer (device-local, one uint per possible page entry)
        vk::Buffer feedbackBuffer;
        vk::DeviceMemory feedbackMemory;

        // Staging buffer for CPU readback
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;

        // Camera/params UBO
        vk::Buffer paramsBuffer;
        vk::DeviceMemory paramsMemory;

        // Depth sampler for reading depth buffer
        vk::Sampler depthSampler;

        uint32_t totalFeedbackEntries = 0;
        FeedbackReadbackState readbackState = FeedbackReadbackState::Idle;
        bool initialized = false;
        bool needsDescriptorUpdate = true;

    public:
        explicit VSMFeedbackPipeline(core::Device& device);
        ~VSMFeedbackPipeline();

        VSMFeedbackPipeline(const VSMFeedbackPipeline&) = delete;
        VSMFeedbackPipeline& operator=(const VSMFeedbackPipeline&) = delete;

        void init(uint32_t maxFeedbackEntries);
        void cleanup();

        void clearFeedbackBuffer(vk::CommandBuffer cmd);
        void dispatch(vk::CommandBuffer cmd,
                      vk::ImageView depthView,
                      vk::Buffer shadowDataBuffer, vk::DeviceSize shadowDataSize,
                      uint32_t lightCount,
                      const glm::mat4& invViewProjection,
                      uint32_t screenWidth, uint32_t screenHeight);
        void copyResultsToStaging(vk::CommandBuffer cmd);
        void markResultsReady();

        [[nodiscard]] std::vector<uint32_t> readbackResults(uint32_t count);

        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] FeedbackReadbackState getReadbackState() const { return readbackState; }

    private:
        void createBuffers();
        void createComputePipeline();
        void createDescriptorSets();
        void createDepthSampler();
    };
}
