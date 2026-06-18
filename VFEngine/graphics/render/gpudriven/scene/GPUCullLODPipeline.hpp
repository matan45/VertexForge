#pragma once

#include "../GPUDrivenTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class Shader;
}

namespace render::gpudriven
{
    class GPUCullLODPipeline
    {
    private:
        core::Device& device;

        // Shader
        std::unique_ptr<core::Shader> shader;

        // Pipeline resources
        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSet;

        bool initialized = false;
        bool descriptorsNeedUpdate = true;

        // Cached buffer info for descriptor updates
        vk::Buffer cachedObjectBuffer;
        vk::Buffer cachedCameraBuffer;
        vk::Buffer cachedDrawCommandBuffer;
        vk::Buffer cachedPerDrawDataBuffer;
        vk::Buffer cachedDrawCountBuffer;
        vk::Buffer cachedActiveIndexBuffer;

        // Cached Hi-Z descriptor info
        vk::ImageView cachedHiZView;
        vk::Sampler cachedHiZSampler;
        bool hiZDescriptorNeedsUpdate = false;

        // VK-1334: external descriptor sets (allocated by RenderTextureViewPort) that bind a
        // per-RTT camera buffer at binding 1 but share the other cached buffers/hiZ with the
        // main set. Tracked here so they get rewritten alongside the main set when any cached
        // buffer or hiZ binding changes.
        struct ExternalDescriptorRef
        {
            vk::DescriptorSet descriptorSet;
            vk::Buffer cameraBuffer;
        };
        std::vector<ExternalDescriptorRef> externalDescriptorSets;

    public:
        explicit GPUCullLODPipeline(core::Device& device);
        ~GPUCullLODPipeline();

        // Non-copyable
        GPUCullLODPipeline(const GPUCullLODPipeline&) = delete;
        GPUCullLODPipeline& operator=(const GPUCullLODPipeline&) = delete;

        void init();

        void cleanup();


        void updateDescriptors(
            vk::Buffer objectBuffer,
            vk::Buffer cameraBuffer,
            vk::Buffer drawCommandBuffer,
            vk::Buffer perDrawDataBuffer,
            vk::Buffer drawCountBuffer,
            vk::Buffer activeIndexBuffer
        );

        void updateHiZDescriptor(vk::ImageView hiZView, vk::Sampler hiZSampler);

        void dispatch(vk::CommandBuffer cmd, uint32_t objectCount);

        // VK-1334: dispatch using an explicit descriptor set (must be one previously returned by
        // allocateExternalDescriptorSet on this pipeline). Used by RTT pre-pass recording.
        void dispatchWithSet(vk::CommandBuffer cmd, uint32_t objectCount, vk::DescriptorSet externalSet);

        vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

        // VK-1334: allocate a descriptor set from `externalPool` that mirrors the main descriptor
        // set but binds `externalCameraBuffer` at binding 1. Caller owns the pool; the pipeline
        // tracks the returned set so it can rewrite it when cached buffers or hiZ change.
        vk::DescriptorSet allocateExternalDescriptorSet(vk::DescriptorPool externalPool,
                                                        vk::Buffer externalCameraBuffer);

        // Untrack a previously allocated external set. Does not free; caller frees via its pool.
        void releaseExternalDescriptorSet(vk::DescriptorSet externalSet);

    private:
        void createDescriptorSetLayout();
        void createPipelineLayout();
        void createComputePipeline();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void writeDescriptors();
        void writeBindingsToSet(const ExternalDescriptorRef& ref);
    };
}
