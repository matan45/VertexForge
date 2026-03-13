#pragma once

#include "../PostProcessEffect.hpp"
#include <memory>
#include <string>

namespace core
{
    class Device;
    class Shader;
}

namespace render::postprocess
{
    struct ColorGradingParamsUBO
    {
        float lift[4];      // rgb + pad (vec4)
        float gamma[4];     // rgb + pad (vec4)
        float gain[4];      // rgb + pad (vec4)
        float saturation;
        float colorTemperature;
        float colorTint;
        float lutIntensity;
        float lutBlendFactor;
        float hasSecondaryLut;
        float lutSize;
        float pad;
    };

    class ColorGradingEffect : public PostProcessEffect
    {
    private:
        core::Device& device;
        std::shared_ptr<core::Shader> shader;

        vk::Pipeline graphicsPipeline;
        vk::PipelineLayout pipelineLayout;
        vk::DescriptorSetLayout inputDescriptorSetLayout;
        vk::DescriptorSetLayout lutDescriptorSetLayout;

        struct LUTTexture
        {
            vk::Image image;
            vk::DeviceMemory memory;
            vk::ImageView imageView;
            uint32_t size = 0;
        };

        LUTTexture primaryLut{};
        LUTTexture secondaryLut{};
        LUTTexture identityLut{};

        vk::Sampler lutSampler;

        vk::Buffer paramsBuffer;
        vk::DeviceMemory paramsBufferMemory;
        void* paramsBufferMapped = nullptr;

        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet lutDescriptorSet;

        std::string currentPrimaryPath;
        std::string currentSecondaryPath;
        std::string pendingPrimaryPath;
        std::string pendingSecondaryPath;
        bool primaryPathPending = false;
        bool secondaryPathPending = false;
        bool descriptorsDirty = true;

    public:
        explicit ColorGradingEffect(core::Device& device);

        void init(vk::RenderPass renderPass, vk::Extent2D extent) override;
        void cleanup() override;
        void recreate(vk::RenderPass renderPass, vk::Extent2D extent) override;

        void preRecord(const vk::CommandBuffer& commandBuffer,
                       vk::DescriptorSet inputDescriptorSet) override;

        void record(const vk::CommandBuffer& commandBuffer,
                    vk::DescriptorSet inputDescriptorSet) override;

        void updateParameters(const ::postprocess::PostProcessSettings& settings) override;

        ::postprocess::EffectType getType() const override { return ::postprocess::EffectType::ColorGrading; }
        uint32_t getPriority() const override { return 110; }

    private:
        void loadShader();
        void createDescriptorSetLayouts();
        void createPipeline(vk::RenderPass renderPass, vk::Extent2D extent);
        void createLUTSampler();
        void createUBO();
        void createDescriptorPool();
        void allocateDescriptorSet();
        void updateDescriptorSets();

        void generateIdentityLUT(uint32_t size = 32);
        void loadLUT(const std::string& path, LUTTexture& lut);
        void loadLUTFromCube(const std::string& path, LUTTexture& lut);
        void destroyLUT(LUTTexture& lut);
        void create3DImage(uint32_t size, LUTTexture& lut);
        void upload3DImageData(LUTTexture& lut, const void* data, size_t dataSize);
    };
}
