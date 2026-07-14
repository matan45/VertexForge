#pragma once

#include "GPUVFXTypes.hpp"
#include "../../../core/GraphicsConstants.hpp"
#include "../../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <array>
#include <memory>
#include <glm/glm.hpp>

namespace core
{
    class Device;
    class Shader;
}

namespace render::vfx
{
    class GPUVFXComputePipeline
    {
    private:
        core::Device& device;

        std::unique_ptr<core::Shader> shader;

        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        bool initialized = false;
        bool descriptorsNeedUpdate = true;

        GPUVFXBufferSet cachedBuffers{};

        // VK-1502: depth-buffer collision. Camera is bound as a read-only SSBO (binding 12) and last-frame
        // scene depth as a per-frame-in-flight combined-image-sampler array (binding 13). When no valid
        // last-frame depth is available, all slots point at a 1x1 fallback (value 1.0 = far = "no hit").
        vk::Buffer cameraSSBO;
        core::VulkanAllocation cameraSSBOAllocation;
        void* cameraSSBOMapped = nullptr;
        GPUVFXCameraUBO cpuCamera{}; // CPU shadow so camera vs depth-state updates never clobber each other

        vk::Sampler depthSampler;
        vk::Image fallbackDepthImage;
        core::VulkanAllocation fallbackDepthAllocation;
        vk::ImageView fallbackDepthImageView;

        std::array<vk::ImageView, core::MAX_FRAMES_IN_FLIGHT> boundDepthViews{}; // real prevFrameDepth[] or null->fallback

    public:
        explicit GPUVFXComputePipeline(core::Device& device);
        ~GPUVFXComputePipeline();

        GPUVFXComputePipeline(const GPUVFXComputePipeline&) = delete;
        GPUVFXComputePipeline& operator=(const GPUVFXComputePipeline&) = delete;

        void init();
        void cleanup();
        bool isInitialized() const { return initialized; }

        void updateDescriptors(const GPUVFXBufferSet& buffers);

        // VK-1502: depth-buffer collision plumbing. The renderer feeds the same camera it already gives the
        // render pipelines, plus the last-frame depth views + the active slot for this frame.
        void updateCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                             const glm::vec3& cameraPos, float time, float nearPlane, float farPlane);
        void setPrevFrameDepthImages(const std::array<vk::ImageView, core::MAX_FRAMES_IN_FLIGHT>& views);
        void setDepthCollisionState(uint32_t prevDepthSlot, bool active);

        void dispatch(
            vk::CommandBuffer cmd,
            uint32_t emitterIndex,
            uint32_t particleCount,
            uint32_t frameNumber,
            uint32_t emitterCount,
            uint32_t channelRequestBase = 0,
            uint32_t particlesPerRequest = 0,
            uint32_t gpuChildRegion = 0xFFFFFFFFu // VK-1501: child region for a GPU event->child listener
        );

        void insertBarriersAfterCompute(vk::CommandBuffer cmd, const GPUVFXBufferSet& buffers);

        // VK-1501: compute->compute dependency so the previous frame's parent writes into the child
        // ring are visible to this frame's child-listener reads (the accepted 1-frame latency). Must be
        // issued once before the dispatch loop; the read/write halves are otherwise disjoint per frame.
        void insertChildSpawnComputeBarrier(vk::CommandBuffer cmd);

        void insertBarriersBeforeCompute(
            vk::CommandBuffer cmd,
            vk::Buffer stateBuffer,
            vk::Buffer drawCommandBuffer,
            vk::Buffer particleBuffer,
            vk::Buffer eventBuffer = nullptr
        );

        void insertBarriersBeforeTransfer(
            vk::CommandBuffer cmd,
            vk::Buffer stateBuffer,
            vk::Buffer drawCommandBuffer,
            vk::Buffer particleBuffer
        );

        void insertTransferToTransferBarrier(
            vk::CommandBuffer cmd,
            vk::Buffer stateBuffer
        );

    private:
        void createDescriptorSetLayout();
        void createPipelineLayout();
        void createComputePipeline();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void createDepthCollisionResources(); // VK-1502: camera SSBO + depth sampler + 1x1 fallback depth image
        void writeDescriptors();
    };
}
