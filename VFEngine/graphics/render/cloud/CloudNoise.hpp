#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::cloud
{
    class CloudNoise
    {
    private:
        core::Device& device;

        bool initialized = false;
        bool generated = false;

        // Shape noise 3D (128^3, RGBA8)
        vk::Image shapeImage;
        vk::DeviceMemory shapeMemory;
        vk::ImageView shapeView;

        // Detail noise 3D (32^3, RGBA8)
        vk::Image detailImage;
        vk::DeviceMemory detailMemory;
        vk::ImageView detailView;

        // Weather map 2D (1024x1024, RGBA8)
        vk::Image weatherImage;
        vk::DeviceMemory weatherMemory;
        vk::ImageView weatherView;

        // Blue noise 2D (128x128, R8)
        vk::Image blueNoiseImage;
        vk::DeviceMemory blueNoiseMemory;
        vk::ImageView blueNoiseView;

        // Shared sampler
        vk::Sampler noiseSampler;

        // Noise generation compute pipeline
        vk::DescriptorSetLayout noiseGenDSLayout;
        vk::DescriptorPool noiseGenDSPool;
        vk::DescriptorSet noiseGenDS;
        vk::PipelineLayout noiseGenPipelineLayout;
        vk::Pipeline noiseGenPipeline;
        std::shared_ptr<core::Shader> noiseGenShader;

        // Weather generation compute pipeline
        vk::DescriptorSetLayout weatherGenDSLayout;
        vk::DescriptorPool weatherGenDSPool;
        vk::DescriptorSet weatherGenDS;
        vk::PipelineLayout weatherGenPipelineLayout;
        vk::Pipeline weatherGenPipeline;
        std::shared_ptr<core::Shader> weatherGenShader;

        // Image helpers
        void create3DImage(uint32_t width, uint32_t height, uint32_t depth,
                           vk::Image& image, vk::DeviceMemory& memory, vk::ImageView& view);
        void create2DImage(uint32_t width, uint32_t height,
                           vk::Image& image, vk::DeviceMemory& memory, vk::ImageView& view);
        void destroyImage(vk::Image& image, vk::DeviceMemory& memory, vk::ImageView& view);

        void createSampler();
        void createNoiseGenPipeline();
        void createWeatherGenPipeline();
        void createBlueNoiseTexture();

    public:
        explicit CloudNoise(core::Device& device);
        ~CloudNoise();

        CloudNoise(const CloudNoise&) = delete;
        CloudNoise& operator=(const CloudNoise&) = delete;

        void init();
        void cleanup();

        // Generate noise textures (one-time, dispatches compute)
        void generate(const vk::CommandBuffer& cmd);

        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] bool isGenerated() const { return generated; }

        [[nodiscard]] vk::ImageView getShapeView() const { return shapeView; }
        [[nodiscard]] vk::ImageView getDetailView() const { return detailView; }
        [[nodiscard]] vk::ImageView getWeatherView() const { return weatherView; }
        [[nodiscard]] vk::ImageView getBlueNoiseView() const { return blueNoiseView; }
        [[nodiscard]] vk::Sampler getSampler() const { return noiseSampler; }
    };
}
