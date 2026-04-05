#pragma once

#include "GITypes.hpp"
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

namespace render::gi
{
    class SSGIPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        // Intermediate images
        vk::Image ssgiRawImage;
        core::VulkanAllocation ssgiRawAllocation;
        vk::ImageView ssgiRawImageView;

        vk::Image ssgiDenoiseHorizImage;  // Horizontal blur output
        core::VulkanAllocation ssgiDenoiseHorizAllocation;
        vk::ImageView ssgiDenoiseHorizImageView;

        vk::Image ssgiDenoisedImage;  // Vertical blur output (final)
        core::VulkanAllocation ssgiDenoisedAllocation;
        vk::ImageView ssgiDenoisedImageView;

        // Double-buffered history for temporal accumulation
        std::array<vk::Image, 2> ssgiHistoryImages{};
        std::array<core::VulkanAllocation, 2> ssgiHistoryAllocations{};
        std::array<vk::ImageView, 2> ssgiHistoryImageViews{};
        uint32_t currentHistoryIdx = 0;

        // Color format for pipeline creation (dynamic rendering)
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
        vk::DescriptorSetLayout traceSet1Layout;   // depth + UBO

        vk::DescriptorSetLayout temporalSet0Layout; // ssgiRaw
        vk::DescriptorSetLayout temporalSet1Layout; // history + depth + UBO

        vk::DescriptorSetLayout denoiseSet0Layout;  // ssgiAccum + depth

        vk::DescriptorSetLayout compositeSet0Layout; // ssgiDenoised + depth

        // Descriptor pool and sets
        vk::DescriptorPool descriptorPool;

        std::vector<vk::DescriptorSet> traceSet0PerImage; // per swap image (scene color)
        vk::DescriptorSet traceSet1;

        vk::DescriptorSet temporalSet0;
        std::array<vk::DescriptorSet, 2> temporalSet1PerHistory; // per history buffer

        std::array<vk::DescriptorSet, 2> denoiseHorizSet0PerHistory;  // reads ssgiHistory[i]
        vk::DescriptorSet denoiseSet0;      // reads ssgiDenoiseHoriz

        vk::DescriptorSet compositeSet0;

        // Depth image view (depth-only aspect)
        vk::ImageView depthOnlyImageView;
        vk::Sampler sampler;
        vk::ImageAspectFlags depthAspectMask;

        // UBO
        vk::Buffer paramsBuffer;
        core::VulkanAllocation paramsBufferAllocation;
        void* paramsBufferMapped = nullptr;

        // Push constants for denoise pass (separable bilateral blur)
        struct DenoisePushConstants
        {
            glm::vec2 texelSize;
            float nearPlane;
            float farPlane;
            glm::vec2 direction;  // (1,0) horizontal, (0,1) vertical
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
        float ssgiRadius = 2.0f;
        float ssgiMaxDistance = 100.0f;
        float ssgiIntensity = 0.5f;
        float ssgiTemporalBlend = 0.1f;
        uint32_t ssgiSampleCount = 8;
        bool ssgiHalfResolution = true;

        vk::Extent2D currentExtent{};
        vk::Extent2D traceExtent{};
        bool historyValid = false;
        bool initialized = false;

        static constexpr vk::Format SSGI_FORMAT = vk::Format::eB10G11R11UfloatPack32;

    public:
        SSGIPipeline(core::Device& device, core::SwapChain& swapChain,
                     core::OffscreenResources& offscreenResources);
        ~SSGIPipeline();

        SSGIPipeline(const SSGIPipeline&) = delete;
        SSGIPipeline& operator=(const SSGIPipeline&) = delete;

        void init();
        void cleanup();
        void recreate();

        void execute(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex);
        void executeGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex);

        void setCameraData(const glm::mat4& view, const glm::mat4& projection,
                           const glm::vec3& cameraPosition,
                           float nearPlane, float farPlane, uint32_t frameIndex);

        void updateSettings(const gi::GISettings& settings);

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
