#pragma once
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_set>
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::occlusion
{
    class HiZBuffer;

    // Light types for occlusion culling
    enum class LightOcclusionType : uint32_t
    {
        Point = 0,
        Spot = 1
    };

    // GPU-aligned light bounds data for occlusion testing
    struct alignas(16) GPULightBounds
    {
        glm::vec4 positionRadius;  // xyz = world position, w = bounding radius
        glm::vec4 direction;       // xyz = direction (spot), w = cone angle (spot)
        uint32_t entityId;
        uint32_t lightType;        // 0 = point, 1 = spot
        uint32_t padding0;
        uint32_t padding1;
    };
    static_assert(sizeof(GPULightBounds) == 48, "GPULightBounds must be 48 bytes");

    // Camera data for light occlusion culling
    struct alignas(16) LightCullCameraData
    {
        glm::mat4 viewProj;
        glm::vec4 screenSize;   // xy = width/height, zw = 1/width, 1/height
        glm::vec4 cameraPos;    // xyz = camera position, w = near plane
        uint32_t lightCount;
        uint32_t hiZMipLevels;
        uint32_t padding0;
        uint32_t padding1;
    };
    static_assert(sizeof(LightCullCameraData) == 112, "LightCullCameraData must be 112 bytes");

    constexpr uint32_t INITIAL_MAX_LIGHTS = 256;
    constexpr uint32_t LIGHT_CULL_WORKGROUP_SIZE = 64;

    // State tracking for GPU readback synchronization
    enum class ReadbackState : uint8_t
    {
        Idle,       // No pending GPU work
        Pending,    // GPU work recorded but not yet completed
        Ready       // GPU work complete, safe to read results
    };

    /**
     * LightOcclusionCulling - Tests light bounding spheres against Hi-Z buffer.
     *
     * Uses the same Hi-Z pyramid as mesh occlusion culling to determine
     * which lights are potentially visible and which are fully occluded.
     * Occluded lights can skip shadow map rendering.
     */
    class LightOcclusionCulling
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

        // GPU buffers
        vk::Buffer lightBoundsBuffer;
        vk::DeviceMemory lightBoundsMemory;
        vk::Buffer visibilityBuffer;
        vk::DeviceMemory visibilityMemory;
        vk::Buffer cameraBuffer;
        vk::DeviceMemory cameraMemory;

        // Staging buffer for readback
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;

        // Persistent staging buffer for upload (avoids per-frame allocation)
        vk::Buffer uploadStagingBuffer;
        vk::DeviceMemory uploadStagingMemory;
        void* uploadStagingMapped = nullptr;  // Persistently mapped for efficiency
        bool uploadPending = false;           // True when staging has new data to upload
        vk::DeviceSize pendingUploadSize = 0; // Size of pending upload

        uint32_t maxLightCount = 0;
        uint32_t currentLightCount = 0;
        LightCullCameraData cameraData{};

        // Entity ID tracking for visibility mapping
        std::vector<uint32_t> lightEntityIds;

        // Cached visibility results
        std::vector<uint32_t> cachedVisibility;
        std::unordered_set<uint32_t> visibleLightIds;
        std::unordered_set<uint32_t> occludedLightIds;
        bool resultsCached = false;

        // GPU synchronization state
        ReadbackState readbackState = ReadbackState::Idle;

        bool initialized = false;
        bool needsDescriptorUpdate = true;

    public:
        explicit LightOcclusionCulling(core::Device& device, core::SwapChain& swapChain);
        ~LightOcclusionCulling();

        LightOcclusionCulling(const LightOcclusionCulling&) = delete;
        LightOcclusionCulling& operator=(const LightOcclusionCulling&) = delete;

        void init(HiZBuffer* hiZBuffer);

        // Update light bounds data for culling (CPU-side only, copies to staging buffer)
        // This does NOT block - call recordLightUpload() to record the GPU transfer
        void updateLights(const std::vector<GPULightBounds>& lights);

        // Record GPU commands to upload staged light data (call before cull())
        // Returns true if upload was recorded, false if no pending upload
        bool recordLightUpload(vk::CommandBuffer cmd);

        // Check if there's pending light data to upload
        bool hasUploadPending() const { return uploadPending; }

        // Update camera data
        void updateCamera(const glm::mat4& viewProj, const glm::vec3& cameraPos, float nearPlane);

        // Dispatch compute shader to cull lights (call after recordLightUpload if upload pending)
        void cull(vk::CommandBuffer cmd);

        // Copy visibility results to staging buffer (call after cull, before getVisibleLightIds)
        void copyResultsToStaging(vk::CommandBuffer cmd);

        // IMPORTANT: Call this after GPU work completes (e.g., after vkQueueWaitIdle or fence signal)
        // This marks results as safe to read from the CPU
        void markResultsReady();

        // Check if readback is pending (GPU work recorded but not yet marked complete)
        bool isReadbackPending() const { return readbackState == ReadbackState::Pending; }

        // Check current readback state
        ReadbackState getReadbackState() const { return readbackState; }

        // Get set of visible light entity IDs
        // REQUIRES: markResultsReady() must be called after GPU sync, before this method
        // Returns empty set and logs warning if called while readback is pending
        const std::unordered_set<uint32_t>& getVisibleLightIds();

        // Check if a specific light is visible (after calling getVisibleLightIds)
        bool isLightVisible(uint32_t entityId) const;

        // Get set of occluded light entity IDs (call after getVisibleLightIds)
        const std::unordered_set<uint32_t>& getOccludedLightIds() const { return occludedLightIds; }

        // Check if results have been cached (readback complete)
        bool hasResults() const { return resultsCached; }

        void cleanup();

        bool isInitialized() const { return initialized; }
        uint32_t getLightCount() const { return currentLightCount; }

    private:
        void createBuffers(uint32_t maxLights);
        void createComputePipeline();
        void createDescriptorSets();
        void resizeBuffers(uint32_t newMaxLights);
    };
}
