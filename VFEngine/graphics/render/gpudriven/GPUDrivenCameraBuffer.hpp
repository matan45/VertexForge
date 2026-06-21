#pragma once

#include "GPUDrivenTypes.hpp"
#include "../../core/RenderManager.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>

namespace core
{
    class Device;
    class SwapChain;
}

namespace render::gpudriven
{
    class IndirectBatchManager;

    struct CameraUpdateParams
    {
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec3 cameraPosition;
        float nearPlane;
        float farPlane;
        float time;
        uint32_t objectCount;
        uint32_t hiZMipLevels;
        bool frustumCullingEnabled;
        bool occlusionCullingEnabled;
        bool lodSelectionEnabled;
        bool lodCrossfadeEnabled = true;
        bool distanceCullingEnabled;
        float categoryDistances[services::CullingCategory::Count];
        float shadowDistanceMultiplier;
        float globalLodBias;
        const IndirectBatchManager* batchManager;
        uint32_t screenWidth = 0;   // 0 = use swapchain extent
        uint32_t screenHeight = 0;
        uint32_t cullingMask = 0xFFFFFFFFu; // VK-1415: per-camera render-layer mask (all layers by default)
    };

    class GPUDrivenCameraBuffer
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        vk::Buffer buffer;
        core::VulkanAllocation allocation;
        void* mapped = nullptr;
        GPUCameraData data{};
        glm::mat4 storedPrevViewProjection{1.0f};
        uint32_t frameIndex = 0;

    public:
        explicit GPUDrivenCameraBuffer(core::Device& device, core::SwapChain& swapChain);
        ~GPUDrivenCameraBuffer();

        GPUDrivenCameraBuffer(const GPUDrivenCameraBuffer&) = delete;
        GPUDrivenCameraBuffer& operator=(const GPUDrivenCameraBuffer&) = delete;

        void init();
        void cleanup();

        void update(const CameraUpdateParams& params);

        vk::Buffer getBuffer() const { return buffer; }
        const GPUCameraData& getData() const { return data; }
        const glm::mat4& getPrevViewProjection() const { return storedPrevViewProjection; }

    private:
        static void extractFrustumPlanes(const glm::mat4& viewProjection, glm::vec4 planes[6]);
    };
}
