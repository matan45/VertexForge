#pragma once

#include "ResolutionManager.hpp"
#include "../../../utilities/postprocess/PostProcessTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
}

namespace render::upscaling
{
    /// Input resources for a single upscale evaluation.
    struct UpscaleInputs
    {
        vk::Image colorInput;         // Jittered HDR color at render resolution
        vk::ImageView colorView;
        vk::Image depthInput;         // Depth at render resolution
        vk::ImageView depthView;
        vk::Image motionVectors;      // Screen-space velocity (R16G16_SFLOAT)
        vk::ImageView motionView;
        vk::Image reactiveMask;       // Transparency/particle mask (R8_UNORM)
        vk::ImageView reactiveView;
        vk::Image output;             // Upscaled output at display resolution
        vk::ImageView outputView;
        vk::Extent2D renderExtent;
        vk::Extent2D displayExtent;
        glm::vec2 jitterOffset{0.0f};
        float deltaTime = 0.016f;
        float preExposure = 1.0f;
        bool resetAccumulation = false; // True on camera cuts / teleports
        VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB;
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
        VkFormat motionFormat = VK_FORMAT_R16G16_SFLOAT;
        VkFormat outputFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkFormat reactiveFormat = VK_FORMAT_R8_UNORM;

        // Camera data required by Streamline common constants
        glm::mat4 viewMatrix{1.0f};
        glm::mat4 projectionMatrix{1.0f};
        glm::mat4 prevViewMatrix{1.0f};
        glm::mat4 prevProjectionMatrix{1.0f};
        glm::vec3 cameraPosition{0.0f};
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
    };

    /// Manages DLSS and FSR2 upscaling via NVIDIA Streamline SDK.
    /// All Streamline calls are behind #ifdef VF_STREAMLINE_ENABLED.
    class UpscaleManager
    {
    public:
        UpscaleManager() = default;
        ~UpscaleManager();

        UpscaleManager(const UpscaleManager&) = delete;
        UpscaleManager& operator=(const UpscaleManager&) = delete;

        /// Initialize Streamline. Call BEFORE Vulkan instance creation.
        /// Returns true if Streamline loaded successfully.
        static bool initStreamline();

        /// Provide Vulkan device info to Streamline. Call AFTER device creation.
        bool setVulkanDevice(core::Device& device);

        /// Shut down Streamline. Call before device destruction.
        void shutdown();

        /// Check feature availability (call after setVulkanDevice).
        bool isDLSSSupported() const { return dlssSupported; }
        bool isDirectSRSupported() const { return directSRSupported; }

        /// Determine the active upscale mode based on settings and hardware.
        ::postprocess::UpscaleMode resolveActiveMode(::postprocess::UpscaleMode requested) const;

        /// Set options for the active upscaler (mode, quality, output resolution).
        void applySettings(const ::postprocess::UpscaleSettings& settings,
                           uint32_t outputWidth, uint32_t outputHeight);

        /// Evaluate the upscaler for the current frame. Returns true on success.
        bool evaluate(vk::CommandBuffer cmd, uint32_t frameIndex,
                      const UpscaleInputs& inputs);

        /// Reset temporal history (camera cut, scene transition).
        void resetHistory();

        /// Resolution manager for internal vs display resolution.
        ResolutionManager& getResolutionManager() { return resolutionManager; }
        const ResolutionManager& getResolutionManager() const { return resolutionManager; }

        bool isActive() const { return activeMode != ::postprocess::UpscaleMode::Off; }
        ::postprocess::UpscaleMode getActiveMode() const { return activeMode; }

        /// Query Streamline's required Vulkan extensions (call after initStreamline, before device creation)
        static std::vector<const char*> getRequiredInstanceExtensions();
        static std::vector<const char*> getRequiredDeviceExtensions();

        static bool isStreamlineAvailable() { return streamlineAvailable; }
        static bool isDLSSAvailable() { return instance && instance->dlssSupported; }
        static bool isDirectSRAvailable() { return instance && instance->directSRSupported; }
        static const UpscaleManager* getInstance() { return instance; }

    private:
        ResolutionManager resolutionManager;

        ::postprocess::UpscaleMode activeMode = ::postprocess::UpscaleMode::Off;
        bool dlssSupported = false;
        bool directSRSupported = false;
        bool deviceSet = false;

        static inline bool streamlineAvailable = false;
        static inline bool streamlineInitialized = false;
        static inline UpscaleManager* instance = nullptr;

        void queryFeatureSupport();
    };
}
