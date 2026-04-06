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
        uint32_t brushType;     // 0=Raise, 1=Lower, 2=Smooth, 3=Flatten, 4=Noise, 5=Stamp
        float deltaTime;
        float targetHeight;
        float minHeight;
        float maxHeight;
        uint32_t invertFlag;    // 0 or 1
        uint32_t stampWidth;
        uint32_t stampHeight;
        float stampRotation;    // radians
        float stampScale;
    };
    static_assert(sizeof(BrushComputePushConstants) == 80, "BrushComputePushConstants must be 80 bytes");

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

        // Stamp heightmap SSBO
        vk::Buffer stampBuffer;
        core::VulkanAllocation stampAllocation;
        uint32_t stampWidth = 0;
        uint32_t stampHeight = 0;
        bool hasStampData = false;

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

        // Upload stamp heightmap data for stamp brush
        void setStampData(const std::vector<float>& heights,
                          uint32_t width, uint32_t height);

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
