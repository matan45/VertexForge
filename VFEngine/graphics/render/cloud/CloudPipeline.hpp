#pragma once
#include "CloudTypes.hpp"
#include "cloud/CloudSettings.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
    struct OffscreenResources;
}

namespace render::atmosphere
{
    class AtmospherePipeline;
}

namespace render::cloud
{
    class CloudNoise;
    class CloudRayMarch;
    class CloudTemporal;
    class CloudComposite;

    class CloudPipeline
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        core::OffscreenResources& offscreenResources;

        CloudSettings settings;
        bool enabled = false;
        bool initialized = false;
        uint32_t frameIndex = 0;

        std::unique_ptr<CloudNoise> cloudNoise;
        std::unique_ptr<CloudRayMarch> cloudRayMarch;
        std::unique_ptr<CloudTemporal> cloudTemporal;
        std::unique_ptr<CloudComposite> cloudComposite;

        // Fallback 1x1 white texture (when atmosphere not available)
        vk::Image fallbackImage{};
        core::VulkanAllocation fallbackAllocation;
        vk::ImageView fallbackView{};
        vk::Sampler fallbackSampler{};

        void createFallbackTexture();
        void destroyFallbackTexture();

        // Cached camera data
        glm::mat4 cachedView{1.0f};
        glm::mat4 cachedProjection{1.0f};
        glm::mat4 prevViewProjection{1.0f};
        glm::vec3 cachedCameraPos{0.0f};
        float cachedNear = 0.1f;
        float cachedFar = 1000.0f;
        float cachedTime = 0.0f;
        glm::vec3 sunDirection{0.0f, 1.0f, 0.0f};
        glm::vec3 sunIrradiance{1.474f, 1.8504f, 1.91198f};

        // Atmosphere integration
        atmosphere::AtmospherePipeline* atmospherePipeline = nullptr;

        GPUCloudParams buildGPUParams() const;

    public:
        CloudPipeline(core::Device& device, core::SwapChain& swapChain,
                      core::OffscreenResources& offscreenResources);
        ~CloudPipeline();

        CloudPipeline(const CloudPipeline&) = delete;
        CloudPipeline& operator=(const CloudPipeline&) = delete;

        void init();
        void cleanup();
        void recreate();

        void updateSettings(const CloudSettings& newSettings);
        CloudSettings getSettings() const { return settings; }

        void setCameraData(const glm::mat4& view, const glm::mat4& projection,
                           const glm::vec3& cameraPos, float nearPlane, float farPlane,
                           float time = 0.0f);
        void setSunDirection(const glm::vec3& dir) { sunDirection = dir; }
        void setSunIrradiance(const glm::vec3& irr) { sunIrradiance = irr; }
        void setAtmospherePipeline(atmosphere::AtmospherePipeline* atmo) { atmospherePipeline = atmo; }

        // Dispatch compute (raymarch + temporal)
        void dispatchCompute(const vk::CommandBuffer& cmd);

        // Render composite (fullscreen graphics pass)
        void renderComposite(const vk::CommandBuffer& cmd, uint32_t imageIndex);
        void renderCompositeGraphManaged(const vk::CommandBuffer& cmd, uint32_t imageIndex);

        void setEnabled(bool value) { enabled = value; }
        [[nodiscard]] bool isEnabled() const { return enabled && initialized; }
        [[nodiscard]] bool isInitialized() const { return initialized; }
    };
}
