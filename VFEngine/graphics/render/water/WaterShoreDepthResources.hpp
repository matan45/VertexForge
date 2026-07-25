#pragma once

#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <vector>

namespace core
{
    class Device;
}

namespace render::water
{
    // VK-1605: the GPU side of water::ShoreDepthField - a single R32F texture of
    // waterDepth = waterHeight - terrainHeight over a camera-following world window, sampled by the
    // water VERTEX stage (shoaling, breakers) and interpolated into the fragment stage.
    //
    // The resolution is a compile-time constant, so the image is created exactly once and never
    // recreated. That is deliberate: it means the descriptor writes into set 9 (and into the
    // refraction dummy set that RTT views bind) happen once at init and can never go stale, and a
    // rebake only ever rewrites image CONTENTS.
    class WaterShoreDepthResources
    {
    public:
        explicit WaterShoreDepthResources(core::Device& device);
        ~WaterShoreDepthResources();

        WaterShoreDepthResources(const WaterShoreDepthResources&) = delete;
        WaterShoreDepthResources& operator=(const WaterShoreDepthResources&) = delete;

        void init();
        void cleanup();

        // CPU-side: copy a completed bake into the persistently-mapped staging buffer. Cheap
        // (256 KB memcpy) and only happens when the field's version changes, i.e. every few
        // seconds of camera travel at most.
        void stage(const std::vector<float>& depths);

        [[nodiscard]] bool hasPendingUpload() const { return pendingUpload; }

        // Records the staging->image copy. MUST be called outside a render pass; it lives next to
        // dispatchOceanFFT, which is where the engine already does its per-frame out-of-pass GPU
        // writes. No-op when nothing is staged.
        void recordUpload(vk::CommandBuffer cmd);

        [[nodiscard]] vk::ImageView getImageView() const { return imageView; }
        [[nodiscard]] vk::Sampler getSampler() const { return sampler; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        core::Device& device;

        vk::Image image;
        core::VulkanAllocation allocation;
        vk::ImageView imageView;
        vk::Sampler sampler;

        vk::Buffer stagingBuffer;
        core::VulkanAllocation stagingAllocation;
        void* stagingMapped = nullptr;

        bool pendingUpload = false;
        bool initialized = false;
    };
}
