#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>
#include <glm/glm.hpp>

namespace core
{
    class Device;
    class Shader;
}

namespace render::shadow
{
    struct ShadowPushConstants
    {
        glm::mat4 lightViewProjection;
        uint32_t baseDrawIndex;
        float depthBias;
        float slopeBias;
        float normalBias;
        uint32_t objectFilterMask = 0;   // AND with PerDrawData.flags
        uint32_t objectFilterValue = 0;  // expected result after AND (0,0 = all objects pass)
    };

    class ShadowPassPipeline
    {
    private:
        core::Device& device;

        std::unique_ptr<core::Shader> shadowShader;
        vk::Pipeline shadowPipeline;
        vk::PipelineLayout shadowPipelineLayout;

        vk::DescriptorSetLayout cachedPerDrawLayout;
        vk::DescriptorSetLayout cachedMeshletDataLayout;
        vk::DescriptorSetLayout cachedVertexDataLayout;
        vk::DescriptorSetLayout cachedBoneMatrixLayout;
        vk::DescriptorSetLayout cameraUBOLayout;
        vk::DescriptorPool cameraDescriptorPool;
        vk::DescriptorSet cameraDescriptorSet;

        bool initialized = false;
        vk::Format depthFormat = vk::Format::eD32Sfloat;

    public:
        explicit ShadowPassPipeline(core::Device& device);
        ~ShadowPassPipeline();

        ShadowPassPipeline(const ShadowPassPipeline&) = delete;
        ShadowPassPipeline& operator=(const ShadowPassPipeline&) = delete;

        void init(vk::DescriptorSetLayout perDrawLayout,
                  vk::DescriptorSetLayout meshletDataLayout,
                  vk::DescriptorSetLayout vertexDataLayout,
                  vk::DescriptorSetLayout boneMatrixLayout,
                  vk::Format atlasDepthFormat);

        void updateCameraDescriptor(vk::Buffer cameraBuffer, vk::DeviceSize bufferSize);
        [[nodiscard]] vk::DescriptorSet getCameraDescriptorSet() const { return cameraDescriptorSet; }

        void cleanup();

        [[nodiscard]] vk::Pipeline getPipeline() const { return shadowPipeline; }
        [[nodiscard]] vk::PipelineLayout getPipelineLayout() const { return shadowPipelineLayout; }
        [[nodiscard]] bool isInitialized() const { return initialized; }
        [[nodiscard]] vk::Format getDepthFormat() const { return depthFormat; }

    private:
        void createShadowPipeline();
        void createCameraDescriptorResources();
    };
}
