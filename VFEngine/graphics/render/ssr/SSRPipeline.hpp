#pragma once

#include "SSRTypes.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <array>
#include <cstdint>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
    struct OffscreenResources;
}

namespace render::ssr
{
    class SSRPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        // Intermediate images
        vk::Image ssrRawImage;
        core::VulkanAllocation ssrRawAllocation;
        vk::ImageView ssrRawImageView;

        vk::Image ssrDenoiseHorizImage;
        core::VulkanAllocation ssrDenoiseHorizAllocation;
        vk::ImageView ssrDenoiseHorizImageView;

        vk::Image ssrDenoisedImage;
        core::VulkanAllocation ssrDenoisedAllocation;
        vk::ImageView ssrDenoisedImageView;

        // Double-buffered history for temporal accumulation
        std::array<vk::Image, 2> ssrHistoryImages{};
        std::array<core::VulkanAllocation, 2> ssrHistoryAllocations{};
        std::array<vk::ImageView, 2> ssrHistoryImageViews{};
        uint32_t currentHistoryIdx = 0;

        // Color format for composite pipeline (dynamic rendering)
        vk::Format compositeColorFormat = vk::Format::eUndefined;

        // Shaders
        std::shared_ptr<core::Shader> traceShader;
        std::shared_ptr<core::Shader> temporalShader;
        std::shared_ptr<core::Shader> denoiseShader;
        std::shared_ptr<core::Shader> compositeShader;

        // Pipelines
        vk::Pipeline tracePipeline;
        vk::PipelineLayout tracePipelineLayout;

        vk::Pipeline temporalPipeline;
        vk::PipelineLayout temporalPipelineLayout;

        vk::Pipeline denoisePipeline;
        vk::PipelineLayout denoisePipelineLayout;

        vk::Pipeline compositePipeline;
        vk::PipelineLayout compositePipelineLayout;

        // Descriptor set layouts
        vk::DescriptorSetLayout traceSet0Layout;   // scene color
        vk::DescriptorSetLayout traceSet1Layout;   // depth + hiZ + normalRoughness + UBO

        vk::DescriptorSetLayout temporalSet0Layout; // ssrRaw
        vk::DescriptorSetLayout temporalSet1Layout; // history + depth + UBO

        vk::DescriptorSetLayout denoiseSet0Layout;  // ssrAccum + depth

        vk::DescriptorSetLayout compositeSet0Layout; // ssrDenoised + depth

        // Descriptor pool and sets
        vk::DescriptorPool descriptorPool;

        std::vector<vk::DescriptorSet> traceSet0PerImage; // per swap image
        vk::DescriptorSet traceSet1;

        vk::DescriptorSet temporalSet0;
        std::array<vk::DescriptorSet, 2> temporalSet1PerHistory;

        std::array<vk::DescriptorSet, 2> denoiseHorizSet0PerHistory;
        vk::DescriptorSet denoiseSet0;

        vk::DescriptorSet compositeSet0;

        // Depth image view (depth-only aspect)
        vk::ImageView depthOnlyImageView;
        vk::Sampler sampler;
        vk::ImageAspectFlags depthAspectMask;

        // External resources (set by RenderPassHandler)
        vk::ImageView hiZImageView;
        vk::Sampler hiZSampler;
        vk::ImageView normalRoughnessImageView;

        // UBO
        vk::Buffer paramsBuffer;
        core::VulkanAllocation paramsBufferAllocation;
        void* paramsBufferMapped = nullptr;

        // Push constants for denoise pass
        struct DenoisePushConstants
        {
            glm::vec2 texelSize;
            float nearPlane;
            float farPlane;
            glm::vec2 direction;
        };

        // Push constants for composite pass
        struct CompositePushConstants
        {
            float intensity;
            uint32_t halfResolution;
            float nearPlane;
            float farPlane;
            glm::vec2 texelSize;
        };

        // Cached camera data
        glm::mat4 cachedView{1.0f};
        glm::mat4 cachedProjection{1.0f};
        glm::mat4 cachedPrevView{1.0f};
        glm::mat4 cachedPrevProjection{1.0f};
        glm::vec3 cachedCameraPosition{0.0f};
        float cachedNear = 0.1f;
        float cachedFar = 1000.0f;
        uint32_t cachedFrameIndex = 0;

        // Settings
        float ssrMaxDistance = 100.0f;
        float ssrIntensity = 1.0f;
        float ssrRoughnessThreshold = 0.6f;
        float ssrEdgeFadeStart = 0.8f;
        float ssrTemporalBlend = 0.1f;
        uint32_t ssrMaxSteps = 64;
        bool ssrHalfResolution = true;

        vk::Extent2D currentExtent{};
        vk::Extent2D traceExtent{};
        bool historyValid = false;
        bool initialized = false;

        static constexpr vk::Format SSR_FORMAT = vk::Format::eR16G16B16A16Sfloat;

    public:
        SSRPipeline(core::Device& device, core::SwapChain& swapChain,
                    core::OffscreenResources& offscreenResources);
        ~SSRPipeline();

        SSRPipeline(const SSRPipeline&) = delete;
        SSRPipeline& operator=(const SSRPipeline&) = delete;

        void init();
        void cleanup();
        void recreate();

        void execute(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex);
        void executeGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex);

        void setCameraData(const glm::mat4& view, const glm::mat4& projection,
                           const glm::vec3& cameraPosition,
                           float nearPlane, float farPlane, uint32_t frameIndex);

        void updateSettings(const SSRSettings& settings);

        void setHiZResources(vk::ImageView hiZView, vk::Sampler hiZSmplr);
        void setNormalRoughnessView(vk::ImageView normalRoughnessView);

        [[nodiscard]] bool isInitialized() const { return initialized; }

    private:
        void createSampler();
        void createDepthImageView();
        void createIntermediateImages();
        void createParamsBuffer();

        void createDescriptorSetLayouts();
        void createDescriptorPool();
        void createDescriptorSets();
        void updateDescriptorSets();

        void loadShaders();

        void createTracePipeline();
        void createTemporalPipeline();
        void createDenoisePipeline();
        void createCompositePipeline();

        void updateParamsBuffer();

        void cleanupIntermediateImages();
        void cleanupPipelines();

        void createImageAndView(vk::Image& image, core::VulkanAllocation& alloc,
                                vk::ImageView& view, vk::Extent2D extent, vk::Format format);
        void destroyImageAndView(vk::Image& image, core::VulkanAllocation& alloc, vk::ImageView& view);

        vk::Pipeline createFullscreenPipeline(vk::PipelineLayout layout, vk::Format colorFormat,
                                               vk::Extent2D extent,
                                               const std::shared_ptr<core::Shader>& shdr,
                                               bool additiveBlend = false);
    };
}
