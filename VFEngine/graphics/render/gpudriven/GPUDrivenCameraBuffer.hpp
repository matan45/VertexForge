#pragma once

#include "GPUDrivenTypes.hpp"
#include "../../core/RenderManager.hpp"
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
    };

    class GPUDrivenCameraBuffer
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        struct FrameBuffer
        {
            vk::Buffer buffer;
            vk::DeviceMemory memory;
            void* mapped = nullptr;
        };

        std::array<FrameBuffer, core::MAX_FRAMES_IN_FLIGHT> frameBuffers{};
        GPUCameraData data{};
        uint32_t frameCounter = 0;
        uint32_t currentFrameSlot = 0;

    public:
        explicit GPUDrivenCameraBuffer(core::Device& device, core::SwapChain& swapChain);
        ~GPUDrivenCameraBuffer();

        GPUDrivenCameraBuffer(const GPUDrivenCameraBuffer&) = delete;
        GPUDrivenCameraBuffer& operator=(const GPUDrivenCameraBuffer&) = delete;

        void init();
        void cleanup();

        void update(const CameraUpdateParams& params);

        // Get buffer for the current frame slot (last updated)
        vk::Buffer getBuffer() const { return frameBuffers[currentFrameSlot].buffer; }

        // Get buffer for a specific frame slot
        vk::Buffer getBuffer(uint32_t frameSlot) const { return frameBuffers[frameSlot % core::MAX_FRAMES_IN_FLIGHT].buffer; }

        const GPUCameraData& getData() const { return data; }
        uint32_t getCurrentFrameSlot() const { return currentFrameSlot; }

    private:
        static void extractFrustumPlanes(const glm::mat4& viewProjection, glm::vec4 planes[6]);
    };
}
