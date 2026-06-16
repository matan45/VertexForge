#pragma once

#include <vulkan/vulkan.hpp>

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
    // raw view/sampler plumbing is needed; the set handle is stable for the lifetime of the renderer.
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
        vk::DescriptorSet getDescriptorSet() const { return descriptorSet; }

        // Copies a producer's combined-image-sampler descriptor (its binding 0) into one of this set's
        // bindings. Safe to call at producer create/resize/online transitions (not while a frame that
        // bound this set is in flight) — mirrors how the per-pipeline mask descriptors were updated.
        void copyInto(uint32_t dstBinding, vk::DescriptorSet srcSet);

    private:
        core::Device& device;
        bool initialized = false;

        vk::DescriptorSetLayout layout;
        vk::DescriptorPool pool;
        vk::DescriptorSet descriptorSet;
    };
}
