#pragma once

#include "AtmosphereTypes.hpp"
#include "atmosphere/DayNightCycleController.hpp"
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
        vk::DeviceMemory paramsBufferMemory;
        void* paramsBufferMapped = nullptr;

        // Sampler (shared)
        vk::Sampler lutSampler;

        // --- Transmittance LUT (256x64, 2D) ---
        vk::Image transmittanceImage;
        vk::DeviceMemory transmittanceMemory;
        vk::ImageView transmittanceView;
        vk::DescriptorSetLayout transmittanceDSLayout;
        vk::DescriptorPool transmittanceDSPool;
        vk::DescriptorSet transmittanceDS;
        vk::PipelineLayout transmittancePipelineLayout;
        vk::Pipeline transmittancePipeline;
        std::shared_ptr<core::Shader> transmittanceShader;

        // --- Multi-Scatter LUT (32x32, 2D) ---
        vk::Image multiScatterImage;
        vk::DeviceMemory multiScatterMemory;
        vk::ImageView multiScatterView;
        vk::DescriptorSetLayout multiScatterDSLayout;
        vk::DescriptorPool multiScatterDSPool;
        vk::DescriptorSet multiScatterDS;
        vk::PipelineLayout multiScatterPipelineLayout;
        vk::Pipeline multiScatterPipeline;
        std::shared_ptr<core::Shader> multiScatterShader;

        // --- Sky-View LUT (192x108, 2D) ---
        vk::Image skyViewImage;
        vk::DeviceMemory skyViewMemory;
        vk::ImageView skyViewView;
        vk::DescriptorSetLayout skyViewDSLayout;
        vk::DescriptorPool skyViewDSPool;
        vk::DescriptorSet skyViewDS;
        vk::PipelineLayout skyViewPipelineLayout;
        vk::Pipeline skyViewPipeline;
        std::shared_ptr<core::Shader> skyViewShader;

        // --- Aerial Perspective LUT (32x32x32, 3D) ---
        vk::Image aerialImage;
        vk::DeviceMemory aerialMemory;
        vk::ImageView aerialView;
        vk::DescriptorSetLayout aerialDSLayout;
        vk::DescriptorPool aerialDSPool;
        vk::DescriptorSet aerialDS;
        vk::PipelineLayout aerialPipelineLayout;
        vk::Pipeline aerialPipeline;
        std::shared_ptr<core::Shader> aerialShader;

        // --- Sky Renderer (fullscreen graphics pass) ---
        vk::RenderPass skyRenderPass;
        std::vector<vk::Framebuffer> skyFramebuffers;
        vk::DescriptorSetLayout skyRendererDSLayout;
        vk::DescriptorPool skyRendererDSPool;
        vk::DescriptorSet skyRendererDS;
        vk::PipelineLayout skyRendererPipelineLayout;
        vk::Pipeline skyRendererPipeline;
        std::shared_ptr<core::Shader> skyRendererShader;

        // --- Aerial Perspective Composite (fullscreen graphics pass) ---
        vk::RenderPass compositeRenderPass;
        std::vector<vk::Framebuffer> compositeFramebuffers;
        vk::ImageView depthOnlyImageView;
        vk::DescriptorSetLayout compositeDSLayout;
        vk::DescriptorPool compositeDSPool;
        vk::DescriptorSet compositeDS;
        vk::PipelineLayout compositePipelineLayout;
        vk::Pipeline compositePipeline;
        std::shared_ptr<core::Shader> compositeShader;
        vk::Buffer compositeParamsBuffer;
        vk::DeviceMemory compositeParamsMemory;
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
        DayNightCycleController dayNightController;
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
        void setSunDirection(const glm::vec3& dir) { sunDirectionOverride = dir; hasSunOverride = true; }

        // Dispatch all compute LUTs
        void dispatchCompute(const vk::CommandBuffer& cmd);

        // Render procedural sky (replaces skybox)
        void renderSky(const vk::CommandBuffer& cmd, uint32_t imageIndex);

        // Apply aerial perspective composite
        void renderComposite(const vk::CommandBuffer& cmd, uint32_t imageIndex);

        void setEnabled(bool value) { enabled = value; }
        [[nodiscard]] bool isEnabled() const { return enabled && initialized; }
        [[nodiscard]] bool isInitialized() const { return initialized; }

        // Expose transmittance LUT for cloud lighting
        [[nodiscard]] vk::ImageView getTransmittanceView() const { return transmittanceView; }
        [[nodiscard]] vk::Sampler getLUTSampler() const { return lutSampler; }

    private:
        void createSampler();
        void createParamsBuffer();
        void updateParamsBuffer();

        // LUT image creation
        void create2DImage(uint32_t width, uint32_t height,
                           vk::Image& image, vk::DeviceMemory& memory, vk::ImageView& view);
        void create3DImage(uint32_t width, uint32_t height, uint32_t depth,
                           vk::Image& image, vk::DeviceMemory& memory, vk::ImageView& view);
        void destroyImage(vk::Image& image, vk::DeviceMemory& memory, vk::ImageView& view);

        // Compute pipeline helpers
        void createTransmittanceLUT();
        void createMultiScatterLUT();
        void createSkyViewLUT();
        void createAerialPerspectiveLUT();

        // Graphics pipeline helpers
        void createSkyRenderer();
        void createComposite();

        void createDepthOnlyView();
        void createSkyRenderPass();
        void createSkyFramebuffers();
        void createCompositeRenderPass();
        void createCompositeFramebuffers();

        void cleanupSkyFramebuffers();
        void cleanupCompositeFramebuffers();
        void cleanupComputePipelines();
        void cleanupGraphicsPipelines();
    };
}
