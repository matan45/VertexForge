#pragma once

#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::volumetric
{
    class FogNoiseGenerator
    {
    private:
        core::Device& device;

        // 3D noise texture (64^3, RGBA8)
        vk::Image noiseImage;
        core::VulkanAllocation noiseAllocation;
        vk::ImageView noiseView;

        // Sampler with Repeat address mode for tiling
        vk::Sampler noiseSampler;

        // Compute pipeline for noise generation
        vk::DescriptorSetLayout genDSLayout;
        vk::DescriptorPool genDSPool;
        vk::DescriptorSet genDS;
        vk::PipelineLayout genPipelineLayout;
        vk::Pipeline genPipeline;
        std::shared_ptr<core::Shader> genShader;

        bool initialized = false;
        bool generated = false;

        void createImage();
        void destroyImage();
        void createSampler();
        void createComputePipeline();

    public:
        explicit FogNoiseGenerator(core::Device& device);
        ~FogNoiseGenerator();

        FogNoiseGenerator(const FogNoiseGenerator&) = delete;
        FogNoiseGenerator& operator=(const FogNoiseGenerator&) = delete;

        void init();
        void cleanup();

        // Generate noise texture (one-time, dispatches compute)
        void generate(const vk::UniqueCommandBuffer& cmd);

        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] bool isGenerated() const { return generated; }
        [[nodiscard]] vk::ImageView getNoiseView() const { return noiseView; }
        [[nodiscard]] vk::Sampler getNoiseSampler() const { return noiseSampler; }
    };
}
