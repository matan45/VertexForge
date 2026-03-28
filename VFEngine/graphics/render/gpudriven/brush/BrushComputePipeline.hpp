#pragma once

#include "../../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <cstdint>
#include <vector>

namespace core
{
    class Device;
    class Shader;
}

namespace render::gpudriven
{
    struct BrushComputePushConstants
    {
        glm::vec2 brushCenter;
        glm::vec2 tileWorldOrigin;
        float brushRadius;
        float brushStrength;
        float vertexSpacing;
        uint32_t verticesPerSide;
        uint32_t falloffType;
        uint32_t shapeType;
        uint32_t brushType;     // 0=Raise, 1=Lower, 2=Smooth, 3=Flatten, 4=Noise
        float deltaTime;
        float targetHeight;
        float minHeight;
        float maxHeight;
        uint32_t invertFlag;    // 0 or 1
    };
    static_assert(sizeof(BrushComputePushConstants) == 64, "BrushComputePushConstants must be 64 bytes");

    class BrushComputePipeline
    {
    private:
        core::Device& device;

        std::unique_ptr<core::Shader> shader;

        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        // GPU buffers for height data
        vk::Buffer heightInputBuffer;
        core::VulkanAllocation heightInputAllocation;
        vk::Buffer heightOutputBuffer;
        core::VulkanAllocation heightOutputAllocation;

        // Host-visible staging buffers for upload/readback
        vk::Buffer stagingUploadBuffer;
        core::VulkanAllocation stagingUploadAllocation;
        vk::Buffer stagingReadbackBuffer;
        core::VulkanAllocation stagingReadbackAllocation;

        // Command pool for synchronous compute dispatches
        vk::CommandPool computeCommandPool;

        bool initialized = false;
        vk::DeviceSize currentBufferSize = 0;

        static constexpr uint32_t WORKGROUP_SIZE = 8;

    public:
        explicit BrushComputePipeline(core::Device& device);
        ~BrushComputePipeline();

        BrushComputePipeline(const BrushComputePipeline&) = delete;
        BrushComputePipeline& operator=(const BrushComputePipeline&) = delete;

        void init();
        void cleanup();
        bool isInitialized() const { return initialized; }

        // Synchronous GPU brush application:
        // Uploads heightData, dispatches compute shader, reads back modified heights.
        // Returns true on success, heightData is modified in-place.
        bool applyBrush(std::vector<float>& heightData,
                        const BrushComputePushConstants& constants);

    private:
        void createDescriptorSetLayout();
        void createPipelineLayout();
        void createComputePipeline();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void createCommandPool();
        void ensureBufferCapacity(vk::DeviceSize requiredSize);
        void destroyHeightBuffers();
    };
}
