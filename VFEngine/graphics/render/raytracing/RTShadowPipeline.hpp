#pragma once

#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::raytracing
{
    struct alignas(16) RTShadowParams
    {
        glm::mat4 invViewProjection;
        glm::vec4 screenParams;     // xy = resolution, zw = 1/resolution
        glm::vec4 cameraPosition;   // xyz = camera pos, w = far plane
    };
    static_assert(sizeof(RTShadowParams) == 96);

    struct RTShadowPushConstants
    {
        glm::vec4 lightDirection;   // xyz = toward-light dir (normalized), w = max ray distance
        glm::vec4 biasParams;       // x = normal bias, y = t_min, zw = unused
    };
    static_assert(sizeof(RTShadowPushConstants) == 32);

    class RTShadowPipeline
    {
    public:
        explicit RTShadowPipeline(core::Device& device);
        ~RTShadowPipeline();

        RTShadowPipeline(const RTShadowPipeline&) = delete;
        RTShadowPipeline& operator=(const RTShadowPipeline&) = delete;

        void init(uint32_t width, uint32_t height,
                  vk::DescriptorSetLayout tlasLayout);
        void cleanup();
        void resize(uint32_t width, uint32_t height);

        void dispatch(vk::CommandBuffer cmd,
                      vk::ImageView depthView,
                      vk::Image depthImage,
                      vk::ImageView normalView,
                      vk::Image normalImage,
                      vk::DescriptorSet tlasDescriptorSet,
                      const glm::mat4& invViewProjection,
                      const glm::vec3& cameraPos,
                      float farPlane,
                      const glm::vec3& lightDirection,
                      float maxRayDistance,
                      uint32_t screenWidth, uint32_t screenHeight);

        bool isInitialized() const { return initialized; }

        // For fragment shader consumption
        vk::DescriptorSetLayout getShadowMaskSamplerLayout() const { return shadowMaskSamplerLayout; }
        vk::DescriptorSet getShadowMaskSamplerDescriptorSet() const { return shadowMaskSamplerDescSet; }
        vk::ImageView getShadowMaskImageView() const { return shadowMaskSampledView; }

    private:
        core::Device& device;

        // Compute pipeline
        vk::Pipeline computePipeline;
        vk::PipelineLayout pipelineLayout;
        std::unique_ptr<core::Shader> shader;

        // Descriptor set layouts
        vk::DescriptorSetLayout cachedTlasLayout;
        vk::DescriptorSetLayout inputLayout;    // Set 1
        vk::DescriptorSetLayout outputLayout;   // Set 2

        // Descriptor pools and sets
        vk::DescriptorPool descriptorPool;
        vk::DescriptorSet inputDescSet;
        vk::DescriptorSet outputDescSet;

        // Params UBO
        vk::Buffer paramsBuffer;
        core::VulkanAllocation paramsAllocation;

        // Samplers
        vk::Sampler depthSampler;
        vk::Sampler normalSampler;

        // Shadow mask output image (R8Unorm)
        vk::Image shadowMaskImage;
        core::VulkanAllocation shadowMaskAllocation;
        vk::ImageView shadowMaskStorageView;   // for compute write (eGeneral)
        vk::ImageView shadowMaskSampledView;   // for fragment read (eShaderReadOnlyOptimal)

        // Fragment shader sampler descriptor (separate layout/pool for mesh pipeline binding)
        vk::DescriptorSetLayout shadowMaskSamplerLayout;
        vk::DescriptorPool shadowMaskSamplerPool;
        vk::DescriptorSet shadowMaskSamplerDescSet;
        vk::Sampler shadowMaskSampler;

        uint32_t maskWidth = 0;
        uint32_t maskHeight = 0;
        bool initialized = false;
        bool firstFrame = true;

        void createShadowMaskImage(uint32_t w, uint32_t h);
        void createSamplers();
        void createParamsBuffer();
        void createDescriptorLayouts(vk::DescriptorSetLayout tlasLayout);
        void createDescriptorPool();
        void allocateDescriptorSets();
        void createComputePipeline();
        void createShadowMaskSamplerDescriptor();
        void updateOutputDescriptor();
    };
}
