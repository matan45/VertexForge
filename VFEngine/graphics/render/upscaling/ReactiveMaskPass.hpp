#pragma once

#include "../../core/GraphicsConstants.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::upscaling
{
    /// Generates the upscaler reactive/transparency mask by diffing the final
    /// scene color against the opaque-only copy captured before the
    /// transparency passes (VFX, WBOIT, distortion).
    class ReactiveMaskPass
    {
    public:
        explicit ReactiveMaskPass(core::Device& device);
        ~ReactiveMaskPass();

        ReactiveMaskPass(const ReactiveMaskPass&) = delete;
        ReactiveMaskPass& operator=(const ReactiveMaskPass&) = delete;

        void init();
        void cleanup();

        /// Reads opaqueColorView + finalColorView (ShaderReadOnlyOptimal) and
        /// writes maskImage. Handles the mask image's layout transitions
        /// (Undefined -> General -> ShaderReadOnlyOptimal).
        void dispatch(vk::CommandBuffer cmd,
                      vk::ImageView opaqueColorView,
                      vk::ImageView finalColorView,
                      vk::ImageView maskStorageView,
                      vk::Image maskImage,
                      uint32_t width, uint32_t height,
                      uint32_t frameIndex);

        bool isInitialized() const { return initialized; }

    private:
        core::Device& device;

        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        std::unique_ptr<core::Shader> shader;

        vk::DescriptorSetLayout descriptorSetLayout;
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet descriptorSets[core::MAX_FRAMES_IN_FLIGHT];

        vk::Sampler colorSampler;

        bool initialized = false;

        void createSampler();
        void createDescriptorLayout();
        void createDescriptorPool();
        void allocateDescriptorSets();
        void createComputePipeline();
    };
}
