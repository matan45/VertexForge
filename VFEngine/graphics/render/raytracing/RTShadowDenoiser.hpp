#pragma once

#include "../../core/VulkanMemoryManager.hpp"
#include "../../core/PerFrameBuffer.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace core
{
    class Device;
    class Shader;
}

namespace render::raytracing
{
    struct alignas(16) ShadowDenoiserUBO
    {
        glm::mat4 invViewProjection;
        glm::mat4 prevViewProjection;
        glm::vec4 screenParams;     // xy = resolution, zw = 1/resolution
        glm::vec4 temporalParams;   // x = blend, y = frameIndex, z = depthThreshold, w = normalThreshold
    };
    static_assert(sizeof(ShadowDenoiserUBO) == 160);

    struct ShadowSpatialPushConstants
    {
        int32_t stepSize;
        float phiDepth;
        float phiNormal;
        uint32_t passIndex;
    };
    static_assert(sizeof(ShadowSpatialPushConstants) == 16);

    class RTShadowDenoiser
    {
    public:
        explicit RTShadowDenoiser(core::Device& device);
        ~RTShadowDenoiser();

        RTShadowDenoiser(const RTShadowDenoiser&) = delete;
        RTShadowDenoiser& operator=(const RTShadowDenoiser&) = delete;

        void init(uint32_t width, uint32_t height);
        void cleanup();
        void resize(uint32_t width, uint32_t height);

        void dispatch(vk::CommandBuffer cmd,
                      vk::ImageView rawShadowView,
                      vk::Image rawShadowImage,
                      vk::ImageView depthView,
                      vk::Image depthImage,
                      vk::ImageView normalView,
                      vk::Image normalImage,
                      const glm::mat4& invViewProjection,
                      const glm::mat4& viewProjection,
                      uint32_t screenWidth,
                      uint32_t screenHeight,
                      uint32_t frameIndex,
                      uint32_t resourceFrameIndex);

        bool isInitialized() const { return initialized; }

        vk::DescriptorSetLayout getDenoisedMaskSamplerLayout() const { return denoisedMaskSamplerLayout; }
        vk::DescriptorSet getDenoisedMaskSamplerDescriptorSet() const { return denoisedMaskSamplerDescSet; }
        // VK-1430: sampled view of the denoised output (the half-res upsample reads it as its input).
        vk::ImageView getDenoisedMaskSamplerImageView() const { return denoisedOutputSampledView; }

        // Tunable parameters
        void setTemporalBlend(float v) { temporalBlend = v; }
        void setDepthThreshold(float v) { depthThreshold = v; }
        void setNormalThreshold(float v) { normalThreshold = v; }
        void setSpatialPhiDepth(float v) { spatialPhiDepth = v; }
        void setSpatialPhiNormal(float v) { spatialPhiNormal = v; }
        void setSpatialPasses(int v) { spatialPassCount = v; }

    private:
        core::Device& device;
        bool initialized = false;
        bool historyValid = false;
        uint32_t maskWidth = 0;
        uint32_t maskHeight = 0;
        bool spatialImagesReady = false;
        glm::mat4 prevViewProjection{1.0f};

        // Tunable parameters
        float temporalBlend = 0.9f;
        float depthThreshold = 0.01f;
        float normalThreshold = 0.9f;
        float spatialPhiDepth = 0.005f;
        float spatialPhiNormal = 32.0f;
        int spatialPassCount = 3;

        // Temporal pass
        vk::Pipeline temporalPipeline;
        vk::PipelineLayout temporalPipelineLayout;
        std::unique_ptr<core::Shader> temporalShader;
        vk::DescriptorSetLayout temporalDSLayout;
        vk::DescriptorPool temporalDSPool;
        vk::DescriptorSet temporalDescSets[2]; // ping-pong

        struct HistoryBuffer
        {
            vk::Image image;
            core::VulkanAllocation allocation;
            vk::ImageView storageView;
        };
        HistoryBuffer history[2]{};
        uint32_t currentHistoryIndex = 0;

        // Spatial pass
        vk::Pipeline spatialPipeline;
        vk::PipelineLayout spatialPipelineLayout;
        std::unique_ptr<core::Shader> spatialShader;
        vk::DescriptorSetLayout spatialDSLayout;
        vk::DescriptorPool spatialDSPool;
        static constexpr int MAX_SPATIAL_PASSES = 5;
        vk::DescriptorSet spatialDescSets[core::MAX_FRAMES_IN_FLIGHT][MAX_SPATIAL_PASSES];

        struct SpatialBuffer
        {
            vk::Image image;
            core::VulkanAllocation allocation;
            vk::ImageView storageView;
        };
        SpatialBuffer spatialBuf[2]{};

        // Denoised output (R8Unorm for fragment shader)
        vk::Image denoisedOutputImage;
        core::VulkanAllocation denoisedOutputAllocation;
        vk::ImageView denoisedOutputStorageView;
        vk::ImageView denoisedOutputSampledView;

        // Fragment shader sampler descriptor (set 13)
        vk::DescriptorSetLayout denoisedMaskSamplerLayout;
        vk::DescriptorPool denoisedMaskSamplerPool;
        vk::DescriptorSet denoisedMaskSamplerDescSet;
        vk::Sampler denoisedMaskSampler;

        // UBO (per-frame to avoid write-after-read hazards)
        core::PerFrameBuffer paramsBuffer;

        // Samplers
        vk::Sampler nearestSampler;

        void createImages(uint32_t w, uint32_t h);
        void createSamplers();
        void createParamsBuffer();
        void createDescriptorLayouts();
        void createDescriptorPools();
        void allocateDescriptorSets();
        void createTemporalPipeline();
        void createSpatialPipeline();
        void createDenoisedMaskSamplerDescriptor();
        void destroyImages();
    };
}
