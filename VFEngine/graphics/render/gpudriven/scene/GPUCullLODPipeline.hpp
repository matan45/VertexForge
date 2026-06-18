#pragma once

#include "../GPUDrivenTypes.hpp"   // ShadowCullPushConstants
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
        //
        // Tier 4 extends this: a per-shadow-view cull set also overrides the OUTPUT buffers
        // (drawCommand/perDrawData/drawCount -> shared shadow buffers) and binds the camera at a
        // per-view offset. A null override buffer means "use the cached main buffer".
        struct ExternalDescriptorRef
        {
            vk::DescriptorSet descriptorSet;
            vk::Buffer cameraBuffer;
            vk::DeviceSize cameraOffset = 0;
            vk::DeviceSize cameraRange = 0;            // 0 => sizeof(GPUCameraData)
            vk::Buffer drawCommandOverride = nullptr;
            vk::Buffer perDrawDataOverride = nullptr;
            vk::Buffer drawCountOverride = nullptr;
        };
        std::vector<ExternalDescriptorRef> externalDescriptorSets;

        // Tier 4: second compute pipeline sharing descriptorSetLayout but with a push-constant
        // range, running gpu_cull_shadow.glsl (per-view flat-append shadow culling).
        vk::Pipeline shadowComputePipeline;
        vk::PipelineLayout shadowPipelineLayout;

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

        // Tier 4: dispatch the shadow-cull variant (gpu_cull_shadow.glsl) with a per-view set and
        // push constants selecting this view's flat region of the shared shadow buffers.
        void dispatchShadowWithSet(vk::CommandBuffer cmd, uint32_t objectCount,
                                   vk::DescriptorSet externalSet,
                                   const ShadowCullPushConstants& pc);

        vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

        // VK-1334: allocate a descriptor set from `externalPool` that mirrors the main descriptor
        // set but binds `externalCameraBuffer` at binding 1. Caller owns the pool; the pipeline
        // tracks the returned set so it can rewrite it when cached buffers or hiZ change.
        vk::DescriptorSet allocateExternalDescriptorSet(vk::DescriptorPool externalPool,
                                                        vk::Buffer externalCameraBuffer);

        // Tier 4: allocate a per-shadow-view cull set that overrides the camera (at a per-view
        // offset) AND the output buffers (drawCommand/perDrawData/drawCount -> shared shadow
        // buffers), sharing object/activeIndex/hiZ with the main cached set. Tracked for re-sync.
        vk::DescriptorSet allocateShadowDescriptorSet(vk::DescriptorPool externalPool,
                                                      vk::Buffer cameraBuffer,
                                                      vk::DeviceSize cameraOffset,
                                                      vk::Buffer drawCommandBuffer,
                                                      vk::Buffer perDrawDataBuffer,
                                                      vk::Buffer drawCountBuffer);

        // Untrack a previously allocated external set. Does not free; caller frees via its pool.
        void releaseExternalDescriptorSet(vk::DescriptorSet externalSet);

    private:
        void createDescriptorSetLayout();
        void createPipelineLayout();
        void createComputePipeline();
        void createShadowPipeline();   // Tier 4: shadow-cull variant sharing descriptorSetLayout
        void createDescriptorPool();
        void allocateDescriptorSet();
        void writeDescriptors();
        void writeBindingsToSet(const ExternalDescriptorRef& ref);
    };
}
