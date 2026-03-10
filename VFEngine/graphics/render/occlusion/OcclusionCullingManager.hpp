#pragma once
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include "math/Frustum.hpp"

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::occlusion
{
    class HiZBuffer;

    namespace OcclusionFlags
    {
        constexpr uint32_t None = 0;
        constexpr uint32_t Transparent = 1 << 0; // Object is translucent/masked - don't write to Hi-Z
        constexpr uint32_t NoOcclude = 1 << 1; // Object should never occlude others
        constexpr uint32_t NoCull = 1 << 2; // Object should never be culled (always visible)
    }

    struct alignas(16) GPUObjectData
    {
        glm::vec4 aabbMin; // xyz = min corner, w = entityId
        glm::vec4 aabbMax; // xyz = max corner, w = flags (cast to float, reinterpret as uint)
        glm::mat4 modelMatrix;
    };

    struct alignas(16) CullCameraData
    {
        glm::mat4 viewProj;
        glm::vec4 screenSize; // xy = width/height, zw = 1/width, 1/height
        float nearPlane;
        uint32_t objectCount;
        uint32_t hiZMipLevels;
        uint32_t padding;
    };

    constexpr uint32_t INITIAL_MAX_OBJECTS = 1024;
    constexpr uint32_t WORKGROUP_SIZE = 64;

    class OcclusionCullingManager
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        HiZBuffer* hiZBuffer = nullptr;

        vk::Pipeline cullPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;
        std::unique_ptr<core::Shader> shader;

        vk::Buffer objectBuffer;
        vk::DeviceMemory objectBufferMemory;
        vk::Buffer visibilityBuffer;
        vk::DeviceMemory visibilityBufferMemory;
        vk::Buffer cameraBuffer;
        vk::DeviceMemory cameraBufferMemory;

        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingBufferMemory;

        // Persistent staging buffer for object uploads (avoids per-frame alloc + waitIdle)
        vk::Buffer uploadStagingBuffer;
        vk::DeviceMemory uploadStagingBufferMemory;
        uint32_t uploadStagingCapacity = 0;
        bool pendingObjectUpload = false;

        uint32_t maxObjectCount = 0;
        uint32_t currentObjectCount = 0;
        CullCameraData cameraData{};

        bool initialized = false;
        bool needsDescriptorUpdate = true;

    public:
        explicit OcclusionCullingManager(core::Device& device, core::SwapChain& swapChain);
        ~OcclusionCullingManager();

        void init(HiZBuffer* hiZBuffer);

        void updateObjects(const std::vector<GPUObjectData>& objects);

        void uploadObjects(vk::CommandBuffer cmd);

        void updateCamera(const glm::mat4& viewProj, float nearPlane);

        void cull(vk::CommandBuffer cmd);

        std::vector<uint32_t> getVisibilityResults();

        vk::Buffer getVisibilityBuffer() const { return visibilityBuffer; }

        void cleanup();

        bool isInitialized() const { return initialized; }
        uint32_t getObjectCount() const { return currentObjectCount; }

    private:
        void createBuffers(uint32_t maxObjects);
        void createComputePipeline();
        void createDescriptorSets();
        void resizeBuffers(uint32_t newMaxObjects);
    };
}
