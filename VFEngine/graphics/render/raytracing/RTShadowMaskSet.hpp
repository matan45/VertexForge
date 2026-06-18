#pragma once

#include <vulkan/vulkan.hpp>
#include <array>

#include "../../core/GraphicsConstants.hpp"

namespace core
{
    class Device;
}

namespace render::raytracing
{
    // Single descriptor set (bound at set 13) that gathers all three optional RT shadow masks into
    // one set with three bindings, instead of three separate sets at 13/15/16. Collapsing them frees
    // sets 15 and 16, dropping the fragment pipeline's max bound-descriptor-set requirement from 17
    // to 14 — so spot (VK-1175) and point (VK-1176) RT shadows become available on 16-bound-set GPUs.
    //
    //   binding 0 : directional RT mask  (sampler2D,      VK-1150)
    //   binding 1 : spot RT mask array   (sampler2DArray, VK-1175)
    //   binding 2 : point RT mask array  (sampler2DArray, VK-1176)
    //
    // Each binding is flagged PARTIALLY_BOUND (a feature the engine already requires for bindless),
    // so leaving a binding unwritten is legal while its RT type is inactive — the fragment shader only
    // declares (and statically uses) a binding when its RT_*_ENABLED macro is set, which only happens
    // once that producer is online and its binding has been copied in. Bindings are populated by
    // copying from each producer's own combined-image-sampler descriptor (vkCopyDescriptorSets), so no
    // raw view/sampler plumbing is needed.
    //
    // VK-1398: the destination set is RINGED per swapchain image (one set per image index, sized to
    // core::MAX_SWAPCHAIN_IMAGES). Per-frame producer re-points (resize / DLSS-D RR bypass) only ever
    // write the slot for the image currently being recorded — an image whose prior submission has
    // retired (per-image fence in RenderManager) — so a descriptor set is never rewritten while still
    // bound by an in-flight command buffer. Indexing per-image (not per MAX_FRAMES_IN_FLIGHT) is
    // required: the latter would alias distinct in-flight images (see GraphicsConstants.hpp).
    class RTShadowMaskSet
    {
    public:
        static constexpr uint32_t BINDING_DIRECTIONAL = 0;
        static constexpr uint32_t BINDING_SPOT = 1;
        static constexpr uint32_t BINDING_POINT = 2;

        explicit RTShadowMaskSet(core::Device& device);
        ~RTShadowMaskSet();

        RTShadowMaskSet(const RTShadowMaskSet&) = delete;
        RTShadowMaskSet& operator=(const RTShadowMaskSet&) = delete;

        void init();
        void cleanup();
        bool isInitialized() const { return initialized; }

        vk::DescriptorSetLayout getLayout() const { return layout; }

        // Returns the descriptor set for the given swapchain image index (the ring slot bound by that
        // image's command buffer).
        vk::DescriptorSet getDescriptorSet(uint32_t imageIndex) const
        {
            return imageIndex < core::MAX_SWAPCHAIN_IMAGES ? descriptorSets[imageIndex] : nullptr;
        }

        // Copies a producer's combined-image-sampler descriptor (its binding 0) into the given
        // binding of the ring slot for imageIndex. Caller must only write the slot for the image
        // currently being recorded (its prior submission has retired), never a slot bound by an
        // in-flight command buffer.
        void copyInto(uint32_t dstBinding, vk::DescriptorSet srcSet, uint32_t imageIndex);

    private:
        core::Device& device;
        bool initialized = false;

        vk::DescriptorSetLayout layout;
        vk::DescriptorPool pool;
        std::array<vk::DescriptorSet, core::MAX_SWAPCHAIN_IMAGES> descriptorSets{};
    };
}
