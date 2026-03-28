#pragma once
#include "../../core/VulkanMemoryManager.hpp"
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

    enum class LightOcclusionType : uint32_t
    {
        Point = 0,
        Spot = 1
    };

    struct alignas(16) GPULightBounds
    {
        glm::vec4 positionRadius;  // xyz = position, w = radius
        glm::vec4 direction;       // xyz = direction, w = cone angle (spot only)
        uint32_t entityId;
        uint32_t lightType;
        uint32_t padding0;
        uint32_t padding1;
    };
    static_assert(sizeof(GPULightBounds) == 48, "GPULightBounds must be 48 bytes");

    struct alignas(16) LightCullCameraData
    {
        glm::mat4 viewProj;
        glm::vec4 screenSize;   // xy = size, zw = 1/size
        glm::vec4 cameraPos;    // xyz = position, w = near plane
        uint32_t lightCount;
        uint32_t hiZMipLevels;
        uint32_t padding0;
        uint32_t padding1;
    };
    static_assert(sizeof(LightCullCameraData) == 112, "LightCullCameraData must be 112 bytes");

    constexpr uint32_t INITIAL_MAX_LIGHTS = 256;
    constexpr uint32_t LIGHT_CULL_WORKGROUP_SIZE = 64;

    enum class ReadbackState : uint8_t
    {
        Idle,
        Pending,
        Ready
    };

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

        vk::Buffer lightBoundsBuffer;
        core::VulkanAllocation lightBoundsAllocation;
        vk::Buffer visibilityBuffer;
        core::VulkanAllocation visibilityAllocation;
        vk::Buffer cameraBuffer;
        core::VulkanAllocation cameraAllocation;

        vk::Buffer stagingBuffer;
        core::VulkanAllocation stagingAllocation;

        vk::Buffer uploadStagingBuffer;
        core::VulkanAllocation uploadStagingAllocation;
        void* uploadStagingMapped = nullptr;
        bool uploadPending = false;
        vk::DeviceSize pendingUploadSize = 0;

        uint32_t maxLightCount = 0;
        uint32_t currentLightCount = 0;
        LightCullCameraData cameraData{};

        std::vector<uint32_t> lightEntityIds;

        std::vector<uint32_t> cachedVisibility;
        std::unordered_set<uint32_t> visibleLightIds;
        std::unordered_set<uint32_t> occludedLightIds;
        bool resultsCached = false;

        ReadbackState readbackState = ReadbackState::Idle;

        bool initialized = false;
        bool needsDescriptorUpdate = true;

    public:
        explicit LightOcclusionCulling(core::Device& device, core::SwapChain& swapChain);
        ~LightOcclusionCulling();

        LightOcclusionCulling(const LightOcclusionCulling&) = delete;
        LightOcclusionCulling& operator=(const LightOcclusionCulling&) = delete;

        void init(HiZBuffer* hiZBuffer);
        void cleanup();

        void updateLights(const std::vector<GPULightBounds>& lights);
        bool recordLightUpload(vk::CommandBuffer cmd);
        void updateCamera(const glm::mat4& viewProj, const glm::vec3& cameraPos, float nearPlane);
        void cull(vk::CommandBuffer cmd);
        void copyResultsToStaging(vk::CommandBuffer cmd);

        // Call after GPU sync (vkQueueWaitIdle/fence) before reading results
        void markResultsReady();

        const std::unordered_set<uint32_t>& getVisibleLightIds();
        const std::unordered_set<uint32_t>& getOccludedLightIds() const { return occludedLightIds; }
        bool isLightVisible(uint32_t entityId) const;

        bool hasUploadPending() const { return uploadPending; }
        bool isReadbackPending() const { return readbackState == ReadbackState::Pending; }
        ReadbackState getReadbackState() const { return readbackState; }
        bool hasResults() const { return resultsCached; }

        bool isInitialized() const { return initialized; }
        uint32_t getLightCount() const { return currentLightCount; }

    private:
        void createBuffers(uint32_t maxLights);
        void createComputePipeline();
        void createDescriptorSets();
        void resizeBuffers(uint32_t newMaxLights);
    };
}
