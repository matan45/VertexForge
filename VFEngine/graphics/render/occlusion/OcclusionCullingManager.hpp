#pragma once
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <vector>
#include "math/Frustum.hpp"

namespace core {
    class Device;
    class SwapChain;
}

namespace render::occlusion {

    class HiZBuffer;

    // Occlusion flags for GPUObjectData
    namespace OcclusionFlags {
        constexpr uint32_t None        = 0;
        constexpr uint32_t Transparent = 1 << 0;  // Object is translucent/masked - don't write to Hi-Z
        constexpr uint32_t NoOcclude   = 1 << 1;  // Object should never occlude others
        constexpr uint32_t NoCull      = 1 << 2;  // Object should never be culled (always visible)
    }

    // GPU object data for occlusion culling
    struct alignas(16) GPUObjectData {
        glm::vec4 aabbMin;  // xyz = min corner, w = entityId
        glm::vec4 aabbMax;  // xyz = max corner, w = flags (cast to float, reinterpret as uint)
        glm::mat4 modelMatrix;
    };

    // Camera data for occlusion culling
    struct alignas(16) CullCameraData {
        glm::mat4 viewProj;
        glm::vec4 screenSize;  // xy = width/height, zw = 1/width, 1/height
        float nearPlane;
        uint32_t objectCount;
        uint32_t hiZMipLevels;
        uint32_t padding;
    };

    // Manages GPU-based occlusion culling using Hi-Z
    class OcclusionCullingManager {
    public:
        OcclusionCullingManager(core::Device& device, core::SwapChain& swapChain);
        ~OcclusionCullingManager();

        // Initialize with Hi-Z buffer reference
        void init(HiZBuffer* hiZBuffer);

        // Update object data for culling (call before cull())
        void updateObjects(const std::vector<GPUObjectData>& objects);

        // Update camera data
        void updateCamera(const glm::mat4& viewProj, float nearPlane);

        // Perform GPU occlusion culling
        void cull(vk::CommandBuffer cmd);

        // Get visibility results (CPU readback - use sparingly)
        // Returns vector of visibility flags (1 = visible, 0 = occluded)
        std::vector<uint32_t> getVisibilityResults();

        // Get visibility buffer for GPU-side indirect drawing
        vk::Buffer getVisibilityBuffer() const { return visibilityBuffer; }

        // Cleanup
        void cleanup();

        bool isInitialized() const { return initialized; }
        uint32_t getObjectCount() const { return currentObjectCount; }

    private:
        void createBuffers(uint32_t maxObjects);
        void createComputePipeline();
        void createDescriptorSets();
        void resizeBuffers(uint32_t newMaxObjects);

        core::Device& device;
        core::SwapChain& swapChain;
        HiZBuffer* hiZBuffer = nullptr;

        // Compute pipeline
        vk::Pipeline cullPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        // GPU buffers
        vk::Buffer objectBuffer;
        vk::DeviceMemory objectBufferMemory;
        vk::Buffer visibilityBuffer;
        vk::DeviceMemory visibilityBufferMemory;
        vk::Buffer cameraBuffer;
        vk::DeviceMemory cameraBufferMemory;

        // Staging buffer for CPU readback
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingBufferMemory;

        uint32_t maxObjectCount = 0;
        uint32_t currentObjectCount = 0;
        CullCameraData cameraData{};

        bool initialized = false;
        bool needsDescriptorUpdate = true;
    };

}
