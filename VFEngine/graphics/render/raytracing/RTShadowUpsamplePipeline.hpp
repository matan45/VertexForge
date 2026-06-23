#pragma once

#include "UpsampleCore.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include "../../core/GraphicsConstants.hpp"
#include <vulkan/vulkan.hpp>

namespace core
{
    class Device;
}

namespace render::raytracing
{
    // VK-1430: edge-aware joint-bilateral upsample of the half-resolution directional RT shadow mask
    // back to full resolution. Consumes the half-res denoised R16Sfloat mask plus the full-res
    // depth/normal guides (the same buffers the half-res trace already sampled) and writes a full-res
    // R16Sfloat mask. Its fragment-stage sampler descriptor set + layout intentionally MATCH
    // RTShadowDenoiser's denoised-mask sampler (binding 0, combined image sampler, fragment stage,
    // R16Sfloat e2D view), so swapping the set-13 producer to this pipeline is a clean
    // vkCopyDescriptorSets repoint in the VK-1398 ring — no validation error.
    //
    // VK-1431: the byte-identical pipeline/layout/pool/sampler machinery shared with the layered
    // variant lives in UpsampleCore (by value); only the 2D output image + single-dispatch path are
    // here.
    class RTShadowUpsamplePipeline
    {
    public:
        explicit RTShadowUpsamplePipeline(core::Device& device);
        ~RTShadowUpsamplePipeline();

        RTShadowUpsamplePipeline(const RTShadowUpsamplePipeline&) = delete;
        RTShadowUpsamplePipeline& operator=(const RTShadowUpsamplePipeline&) = delete;

        // Allocates the full-res output image at (fullW, fullH); idempotent if unchanged.
        void resize(uint32_t fullW, uint32_t fullH);
        void cleanup();
        bool isInitialized() const { return initialized; }

        // Reads the half-res denoised mask and full-res depth/normal, writes the full-res output.
        // depth/normal arrive in eShaderReadOnlyOptimal (left there by the denoiser); the half-res
        // denoised view must be in eShaderReadOnlyOptimal. The output is left in
        // eShaderReadOnlyOptimal so the set-13 sampler can read it the same frame.
        void dispatch(vk::CommandBuffer cmd,
                      vk::ImageView halfResDenoisedView,
                      vk::ImageView fullDepthView,
                      vk::ImageView fullNormalView,
                      uint32_t halfW, uint32_t halfH,
                      uint32_t fullW, uint32_t fullH,
                      float depthThreshold,
                      float normalExp,
                      uint32_t frameIndex);

        // Consumer-side access (matches RTShadowDenoiser's denoised-mask sampler set/layout exactly).
        vk::DescriptorSetLayout getOutputSamplerLayout() const { return outputSamplerLayout; }
        vk::DescriptorSet getOutputSamplerDescriptorSet() const { return outputSamplerDescSet; }

    private:
        core::Device& device;
        UpsampleCore upsampleCore;          // shared pipeline/layout/pool/samplers (VK-1431)
        bool initialized = false;
        bool firstDispatch = true;          // first dispatch sees the output in eUndefined
        uint32_t outputWidth = 0;
        uint32_t outputHeight = 0;

        // Compute descriptor set: half-res mask (binding0) + full depth (1) + full normal (2) +
        // full-res output storage (3). Per-frame to avoid descriptor-update races. Allocated from
        // upsampleCore's pool/layout.
        vk::DescriptorSet computeDescSet[core::MAX_FRAMES_IN_FLIGHT];

        // Full-res output (R16Sfloat).
        vk::Image outputImage;
        core::VulkanAllocation outputAllocation;
        vk::ImageView outputStorageView;
        vk::ImageView outputSampledView;

        // Consumer sampler set (set 13), mirrors RTShadowDenoiser exactly.
        vk::DescriptorSetLayout outputSamplerLayout;
        vk::DescriptorPool outputSamplerPool;
        vk::DescriptorSet outputSamplerDescSet;

        void createOutputImage(uint32_t w, uint32_t h);
        void destroyOutputImage();
        void createDescriptorLayouts();
        void createDescriptorPools();
        void allocateDescriptorSets();
        void createOutputSamplerDescriptor();
    };
}
