#pragma once

#include "../../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>
#include "terrain/TerrainHitResult.hpp"

namespace core
{
    class Device;
    class Shader;
}

namespace render::gpudriven
{
    struct RaycastPushConstants
    {
        glm::mat4 invViewProjection;
        glm::vec2 cursorUV;
        glm::vec2 texelSize;
    };
    static_assert(sizeof(RaycastPushConstants) == 80, "RaycastPushConstants must be 80 bytes");

    class TerrainRaycastPipeline
    {
    private:
        core::Device& device;

        std::unique_ptr<core::Shader> shader;

        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;
        vk::Sampler depthSampler;

        vk::Buffer resultBuffer;
        core::VulkanAllocation resultAllocation;

        vk::Buffer stagingBuffer;
        core::VulkanAllocation stagingAllocation;

        bool initialized = false;
        bool hasValidCursor = false;
        bool descriptorsNeedUpdate = true;
        glm::vec2 cursorUV{-1.0f};

        vk::ImageView cachedDepthImageView;

        terrain::TerrainHitResult lastResult;

        static constexpr vk::DeviceSize RESULT_BUFFER_SIZE = 32;

    public:
        explicit TerrainRaycastPipeline(core::Device& device);
        ~TerrainRaycastPipeline();

        TerrainRaycastPipeline(const TerrainRaycastPipeline&) = delete;
        TerrainRaycastPipeline& operator=(const TerrainRaycastPipeline&) = delete;

        void init();
        void cleanup();
        bool isInitialized() const { return initialized; }
        // True while a terrain brush cursor is active — the only time dispatch/copy/readback do
        // any work. Lets the caller skip the surrounding depth layout transitions when idle.
        [[nodiscard]] bool hasPendingRequest() const { return hasValidCursor; }

        void setCursorUV(const glm::vec2& uv);
        void clearCursor();

        void updateDepthImageView(vk::ImageView depthImageView);

        void dispatch(vk::CommandBuffer cmd,
                      const glm::mat4& invViewProjection,
                      uint32_t screenWidth, uint32_t screenHeight);

        void copyResultsToStaging(vk::CommandBuffer cmd);
        void readBackResults();

        const terrain::TerrainHitResult& getLastResult() const { return lastResult; }

    private:
        void createDescriptorSetLayout();
        void createPipelineLayout();
        void createComputePipeline();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void createResultBuffers();
        void createDepthSampler();
        void writeDescriptors();
    };
}
