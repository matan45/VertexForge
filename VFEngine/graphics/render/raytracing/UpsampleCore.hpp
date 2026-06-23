#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::raytracing
{
    // Mirrors the push_constant block in rt_shadow_upsample.glsl (std430-compatible 40-byte layout).
    // Shared by both the directional (RTShadowUpsamplePipeline) and the layered
    // (RTLayeredShadowUpsamplePipeline) upsamplers — one declaration, one static_assert, so the two
    // can no longer drift.
    struct UpsamplePushConstants
    {
        glm::uvec2 dstExtent;   // offset 0
        glm::vec2 invHalfDims;  // offset 8
        glm::vec2 invFullDims;  // offset 16
        float depthThreshold;   // offset 24
        float normalExp;        // offset 28
        int32_t reserved0;      // offset 32 (padding to the shared 40-byte layout)
        int32_t reserved1;      // offset 36
    };
    static_assert(sizeof(UpsamplePushConstants) == 40);

    // VK-1431: the byte-identical Vulkan machinery shared by the two RT-shadow upsample pipelines —
    // the compute pipeline + its layout, the 4-binding compute descriptor-set layout and pool, and
    // the guide (nearest) + output (linear denoised-mask) samplers. Owned BY VALUE by each pipeline;
    // the divergent parts (output image / sampled-view type, the per-frame [/per-slice] compute
    // descriptor sets, the consumer sampler set, and dispatch()) stay in the pipelines themselves.
    class UpsampleCore
    {
    public:
        explicit UpsampleCore(core::Device& device);
        ~UpsampleCore();

        UpsampleCore(const UpsampleCore&) = delete;
        UpsampleCore& operator=(const UpsampleCore&) = delete;

        // Builds samplers + compute descriptor layout + compute pool + the compute pipeline.
        // computeSetCount is the only shape parameter: directional passes MAX_FRAMES_IN_FLIGHT,
        // layered passes MAX_FRAMES_IN_FLIGHT * MAX_SLICES (pool sizes derive from it). On
        // shader/pipeline failure it tears down everything it built and returns false.
        bool init(uint32_t computeSetCount);

        // Destroys everything init() built. Idempotent (guarded by built).
        void cleanup();

        bool valid() const { return static_cast<bool>(computePipeline); }
        vk::DescriptorSetLayout computeDescriptorLayout() const { return computeLayout; }
        vk::DescriptorPool computeDescriptorPool() const { return computePool; }
        vk::PipelineLayout layout() const { return pipelineLayout; }
        vk::Pipeline pipeline() const { return computePipeline; }
        vk::Sampler guide() const { return guideSampler; }
        vk::Sampler output() const { return outputSampler; }

    private:
        core::Device& device;
        bool built = false;

        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        std::unique_ptr<core::Shader> shader;

        vk::DescriptorSetLayout computeLayout;  // 3 combined-image-sampler + 1 storage, all compute
        vk::DescriptorPool computePool;

        vk::Sampler guideSampler;   // nearest clampToEdge (depth/normal/half-mask guide reads)
        vk::Sampler outputSampler;  // linear clampToEdge (canonical denoised-mask sampler)
    };
}
