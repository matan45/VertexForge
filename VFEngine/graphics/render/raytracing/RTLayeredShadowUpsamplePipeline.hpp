#pragma once

#include "UpsampleCore.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include "../../core/GraphicsConstants.hpp"
#include "../lighting/GPULightTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>

namespace core
{
    class Device;
}

namespace render::raytracing
{
    // One mask slice scheduled for upsampling this frame.
    struct RTLayeredUpsampleInfo
    {
        uint32_t slice;                       // array layer to write
        vk::ImageView halfResDenoisedLayerView; // half-res denoised layer view (e2D) for this slice
    };

    // VK-1430: array-slice variant of RTShadowUpsamplePipeline for the spot (VK-1175) and point
    // (VK-1176) RT shadow masks. Edge-aware joint-bilateral upsample of each half-res denoised mask
    // slice back to full resolution into a layered R16Sfloat output array. Its fragment-stage sampler
    // descriptor set + layout MATCH RTLayeredShadowDenoiser's denoised-mask sampler (binding 0,
    // combined image sampler, fragment stage, e2DArray view), so the set-15 / set-16 producer swap is
    // a clean vkCopyDescriptorSets repoint in the VK-1398 ring. The renderer owns one instance per
    // light type (spot/point), as it does for the layered pipeline + denoiser.
    //
    // VK-1431: the byte-identical pipeline/layout/pool/sampler machinery shared with the directional
    // variant lives in UpsampleCore (by value); only the 2D-array output + per-slice dispatch are here.
    class RTLayeredShadowUpsamplePipeline
    {
    public:
        static constexpr uint32_t MAX_SLICES = render::lighting::LightConstants::MAX_RT_SPOT_LIGHTS;

        explicit RTLayeredShadowUpsamplePipeline(core::Device& device);
        ~RTLayeredShadowUpsamplePipeline();

        RTLayeredShadowUpsamplePipeline(const RTLayeredShadowUpsamplePipeline&) = delete;
        RTLayeredShadowUpsamplePipeline& operator=(const RTLayeredShadowUpsamplePipeline&) = delete;

        void resize(uint32_t fullW, uint32_t fullH);
        void cleanup();
        bool isInitialized() const { return initialized; }

        // Upsamples each scheduled slice. depth/normal arrive in eShaderReadOnlyOptimal (left by the
        // denoiser); the half-res denoised layer views must be in eShaderReadOnlyOptimal. The whole
        // output array is left in eShaderReadOnlyOptimal so the set-15/16 sampler reads it this frame.
        void dispatch(vk::CommandBuffer cmd,
                      const std::vector<RTLayeredUpsampleInfo>& slices,
                      vk::ImageView fullDepthView,
                      vk::ImageView fullNormalView,
                      uint32_t halfW, uint32_t halfH,
                      uint32_t fullW, uint32_t fullH,
                      float depthThreshold,
                      float normalExp,
                      uint32_t frameIndex);

        vk::DescriptorSetLayout getOutputSamplerLayout() const { return outputSamplerLayout; }
        vk::DescriptorSet getOutputSamplerDescriptorSet() const { return outputSamplerDescSet; }

    private:
        core::Device& device;
        UpsampleCore upsampleCore;          // shared pipeline/layout/pool/samplers (VK-1431)
        bool initialized = false;
        bool firstDispatch = true;
        uint32_t outputWidth = 0;
        uint32_t outputHeight = 0;

        // Compute set: half mask array (binding0) + full depth (1) + full normal (2) + output array
        // storage (3). Per-frame * per-slice to avoid descriptor-update races within one frame.
        // Allocated from upsampleCore's pool/layout.
        vk::DescriptorSet computeDescSet[core::MAX_FRAMES_IN_FLIGHT][MAX_SLICES];

        // Full-res output (R16Sfloat array, one layer per slice).
        vk::Image outputImage;
        core::VulkanAllocation outputAllocation;
        vk::ImageView outputLayerStorageView[MAX_SLICES]; // e2D storage, baseArrayLayer = k
        vk::ImageView outputArraySampledView;             // e2DArray (fragment read)

        // Consumer sampler set (set 15/16), mirrors RTLayeredShadowDenoiser exactly.
        vk::DescriptorSetLayout outputSamplerLayout;
        vk::DescriptorPool outputSamplerPool;
        vk::DescriptorSet outputSamplerDescSet;

        void createOutputImage(uint32_t w, uint32_t h);
        void destroyOutputImage();
        void createDescriptorLayouts();
        void createDescriptorPools();
        void allocateDescriptorSets();
        void createOutputSamplerDescriptor();
        vk::ImageView makeLayerView(vk::Image image, vk::Format format, uint32_t layer);
    };
}
