#pragma once

#include "GPUDrivenTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

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
        bool distanceCullingEnabled;
        float categoryDistances[5]; // StaticMesh, Terrain, Foliage, VFX, Decals
        float shadowDistanceMultiplier;
        const IndirectBatchManager* batchManager;
    };

    class GPUDrivenCameraBuffer
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        vk::Buffer buffer;
        vk::DeviceMemory memory;
        void* mapped = nullptr;
        GPUCameraData data{};
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

    private:
        static void extractFrustumPlanes(const glm::mat4& viewProjection, glm::vec4 planes[6]);
    };
}
