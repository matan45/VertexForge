#pragma once

#include "AtmosphereTypes.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <cstdint>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
    struct OffscreenResources;
}

namespace render::atmosphere
{
    struct AtmosphereCompositeParams
    {
        float nearPlane;
        float farPlane;
        float aerialMaxDist;
        float intensity;
    };

    class AtmospherePipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        AtmosphereSettings settings;
        bool enabled = false;
        bool initialized = false;
        bool paramsDirty = true;
        bool needsInitialTransition = true;

        // GPU params UBO
        vk::Buffer paramsBuffer;
        core::VulkanAllocation paramsBufferAllocation;
        void* paramsBufferMapped = nullptr;

        // Sampler (shared)
        vk::Sampler lutSampler;

        // --- Transmittance LUT (256x64, 2D) ---
        vk::Image transmittanceImage;
        core::VulkanAllocation transmittanceAllocation;
        vk::ImageView transmittanceView;
        vk::DescriptorSetLayout transmittanceDSLayout;
        vk::DescriptorPool transmittanceDSPool;
        vk::DescriptorSet transmittanceDS;
        vk::PipelineLayout transmittancePipelineLayout;
        vk::Pipeline transmittancePipeline;
        std::shared_ptr<core::Shader> transmittanceShader;

        // --- Multi-Scatter LUT (32x32, 2D) ---
        vk::Image multiScatterImage;
        core::VulkanAllocation multiScatterAllocation;
        vk::ImageView multiScatterView;
        vk::DescriptorSetLayout multiScatterDSLayout;
        vk::DescriptorPool multiScatterDSPool;
        vk::DescriptorSet multiScatterDS;
        vk::PipelineLayout multiScatterPipelineLayout;
        vk::Pipeline multiScatterPipeline;
        std::shared_ptr<core::Shader> multiScatterShader;

        // --- Sky-View LUT (192x108, 2D) ---
        vk::Image skyViewImage;
        core::VulkanAllocation skyViewAllocation;
        vk::ImageView skyViewView;
        vk::DescriptorSetLayout skyViewDSLayout;
        vk::DescriptorPool skyViewDSPool;
        vk::DescriptorSet skyViewDS;
        vk::PipelineLayout skyViewPipelineLayout;
        vk::Pipeline skyViewPipeline;
        std::shared_ptr<core::Shader> skyViewShader;

        // --- Aerial Perspective LUT (32x32x32, 3D) ---
        vk::Image aerialImage;
        core::VulkanAllocation aerialAllocation;
        vk::ImageView aerialView;
        vk::DescriptorSetLayout aerialDSLayout;
        vk::DescriptorPool aerialDSPool;
        vk::DescriptorSet aerialDS;
        vk::PipelineLayout aerialPipelineLayout;
        vk::Pipeline aerialPipeline;
        std::shared_ptr<core::Shader> aerialShader;

        // --- Sky Renderer (fullscreen graphics pass) ---
        vk::DescriptorSetLayout skyRendererDSLayout;
        vk::DescriptorPool skyRendererDSPool;
        vk::DescriptorSet skyRendererDS;
        vk::PipelineLayout skyRendererPipelineLayout;
        vk::Pipeline skyRendererPipeline;
        std::shared_ptr<core::Shader> skyRendererShader;

        // --- Aerial Perspective Composite (fullscreen graphics pass) ---
        vk::ImageView depthOnlyImageView;
        vk::DescriptorSetLayout compositeDSLayout;
        vk::DescriptorPool compositeDSPool;
        vk::DescriptorSet compositeDS;
        vk::PipelineLayout compositePipelineLayout;
        vk::Pipeline compositePipeline;
        std::shared_ptr<core::Shader> compositeShader;
        vk::Buffer compositeParamsBuffer;
        core::VulkanAllocation compositeParamsAllocation;
        void* compositeParamsMapped = nullptr;

        vk::ImageAspectFlags depthAspectMask;
        vk::Extent2D currentExtent{};

        // Cached camera data
        glm::mat4 cachedView{1.0f};
        glm::mat4 cachedProjection{1.0f};
        glm::vec3 cachedCameraPos{0.0f};
        float cachedNear = 0.1f;
        float cachedFar = 1000.0f;
        float cachedTime = 0.0f;
        float lastFrameTime = 0.0f;
        glm::vec3 sunDirectionOverride{0.0f, 1.0f, 0.0f};
        bool hasSunOverride = false;

    public:
        AtmospherePipeline(core::Device& device, core::SwapChain& swapChain,
                           core::OffscreenResources& offscreenResources);
        ~AtmospherePipeline();

        AtmospherePipeline(const AtmospherePipeline&) = delete;
        AtmospherePipeline& operator=(const AtmospherePipeline&) = delete;

        void init();
        void cleanup();
        void recreate();

        void updateSettings(const AtmosphereSettings& newSettings);
        AtmosphereSettings getSettings() const { return settings; }

        void setCameraData(const glm::mat4& view, const glm::mat4& projection,
                           const glm::vec3& cameraPos, float nearPlane, float farPlane,
                           float time = 0.0f);

        // Override sun direction from directional light (takes priority over azimuth/elevation)
        // Ignored when day-night cycle is active (cycle controls sun position)
        void setSunDirection(const glm::vec3& dir)
        {
            if (!settings.dayNightEnabled)
            {
                sunDirectionOverride = dir;
                hasSunOverride = true;
            }
        }

        [[nodiscard]] bool isDayNightEnabled() const { return settings.dayNightEnabled; }

        // Dispatch all compute LUTs
        void dispatchCompute(const vk::CommandBuffer& cmd);

        // Render procedural sky (replaces skybox)
        void renderSky(const vk::CommandBuffer& cmd, uint32_t imageIndex);

        // Apply aerial perspective composite
        void renderComposite(const vk::CommandBuffer& cmd, uint32_t imageIndex);

        // Graph-managed variants (no layout transitions on scene color/depth)
        void renderSkyGraphManaged(const vk::CommandBuffer& cmd, uint32_t imageIndex);
        void renderCompositeGraphManaged(const vk::CommandBuffer& cmd, uint32_t imageIndex);

        void setEnabled(bool value) { enabled = value; }
        [[nodiscard]] bool isEnabled() const { return enabled && initialized; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

        // Expose transmittance LUT for cloud lighting
        [[nodiscard]] vk::ImageView getTransmittanceView() const { return transmittanceView; }
        [[nodiscard]] vk::Sampler getLUTSampler() const { return lutSampler; }

        // VK-1569: expose the sky-view LUT + params UBO for dynamic sky->IBL ambient capture.
        // Both are created in init() and untouched by recreate(), so the handles are stable.
        [[nodiscard]] vk::ImageView getSkyViewView() const { return skyViewView; }
        [[nodiscard]] vk::Buffer getParamsBuffer() const { return paramsBuffer; }

    private:
        void createSampler();
        void createParamsBuffer();
        void updateParamsBuffer();

        // LUT image creation
        void create2DImage(uint32_t width, uint32_t height,
                           vk::Image& image, core::VulkanAllocation& allocation, vk::ImageView& view);
        void create3DImage(uint32_t width, uint32_t height, uint32_t depth,
                           vk::Image& image, core::VulkanAllocation& allocation, vk::ImageView& view);
        void destroyImage(vk::Image& image, core::VulkanAllocation& allocation, vk::ImageView& view);

        // Compute pipeline helpers
        void createTransmittanceLUT();
        void createMultiScatterLUT();
        void createSkyViewLUT();
        void createAerialPerspectiveLUT();

        // Graphics pipeline helpers
        void createSkyRenderer();
        void createComposite();

        void createDepthOnlyView();

        void cleanupComputePipelines();
        void cleanupGraphicsPipelines();
    };
}
