#pragma once

#include "ResolutionManager.hpp"
#include "../../../utilities/postprocess/PostProcessTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>
#include <cstdint>

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
        vk::Image exposureImage;      // 1x1 R32_SFLOAT exposure value
        vk::ImageView exposureView;
        vk::Image output;             // Upscaled output at display resolution
        vk::ImageView outputView;
        vk::Extent2D renderExtent;
        vk::Extent2D displayExtent;
        glm::vec2 jitterOffset{0.0f};
        float deltaTime = 0.016f;
        float preExposure = 1.0f;
        bool resetAccumulation = false; // True on camera cuts / teleports
        VkFormat colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
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

    /// Manages DLSS upscaling via NVIDIA Streamline SDK.
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

        /// Free DLSS feature resources. Call before resolution change.
        void freeFeatureResources();

        /// Check feature availability (call after setVulkanDevice).
        bool isDLSSSupported() const { return dlssSupported; }
        bool isDLSSGSupported() const { return dlssGSupported; }

        /// Frame Generation (DLSS 3.x)
        void applyFrameGenSettings(const ::postprocess::FrameGenSettings& settings,
                                   uint32_t backBufferCount,
                                   uint32_t displayWidth, uint32_t displayHeight,
                                   uint32_t renderWidth, uint32_t renderHeight);
        bool isFrameGenActive() const { return frameGenActive; }

        /// Reflex low-latency mode (NVIDIA-only; required for DLSS Frame Gen).
        void applyReflexSettings(const ::postprocess::ReflexSettings& settings);
        bool isReflexSupported() const { return reflexSupported; }
        bool isReflexActive() const { return reflexActive; }
        ::postprocess::ReflexMode getActiveReflexMode() const { return activeReflexMode; }

        /// Per-frame latency markers for Reflex/PCL. Marker calls are no-ops when
        /// Reflex is inactive. Sim/sleep/state are main-thread only; submit/present
        /// markers run on the render thread (slPCLSetMarker/slReflexSleep are thread-safe).
        enum class FrameMarker
        {
            InputPing,
            SimulationStart,
            SimulationEnd,
            RenderSubmitStart,
            RenderSubmitEnd,
            PresentStart,
            PresentEnd
        };

        /// MAIN THREAD: acquire this frame's Streamline token (call once at frame start).
        void beginReflexFrame();
        /// MAIN THREAD: invoke the Reflex sleep for the current frame (after beginReflexFrame).
        void reflexSleep();
        /// MAIN THREAD: publish the current frame index to the render thread (before kicking it).
        void publishRenderFrameIndex();
        /// MAIN THREAD: emit a marker using the main-thread frame token.
        void setMarkerMain(FrameMarker marker);
        /// RENDER THREAD: emit a marker using the re-fetched render frame token.
        void setMarkerRender(FrameMarker marker);

        struct ReflexLatency
        {
            bool valid = false;
            uint32_t gpuFrameTimeUs = 0;
            uint32_t totalLatencyUs = 0;
        };
        /// MAIN THREAD ONLY: query the latest Reflex latency report (slReflexGetState).
        ReflexLatency getLatency() const;

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
        static bool isDLSSGAvailable() { return instance && instance->dlssGSupported; }
        static const UpscaleManager* getInstance() { return instance; }
        /// Mutable singleton accessor for the per-frame marker/sleep API (MainLoop, RenderManager).
        static UpscaleManager* getMutableInstance() { return instance; }

    private:
        ResolutionManager resolutionManager;

        ::postprocess::UpscaleMode activeMode = ::postprocess::UpscaleMode::Off;
        bool dlssSupported = false;
        bool dlssGSupported = false;
        bool deviceSet = false;
        bool frameGenActive = false;

        bool reflexSupported = false;
        bool reflexActive = false;
        ::postprocess::ReflexMode activeReflexMode = ::postprocess::ReflexMode::Off;

        // Shared per-frame Streamline frame token. The main thread is the authority:
        // it mints a token each frame (monotonic index) and publishes the index it
        // handed to the render thread. The render thread re-fetches the token by that
        // index (slGetNewFrameToken is thread-safe and idempotent per index), so both
        // threads agree on one token without sharing a recyclable pointer across the
        // ~1-frame pipeline. Stored as void* so sl.h stays out of this header.
        static constexpr uint32_t kReflexTokenRing = 3; // SL: <= 3 frames in flight
        void* reflexTokenRing[kReflexTokenRing] = {};
        uint32_t reflexTokenIndex[kReflexTokenRing] = {};
        std::atomic<uint32_t> reflexFrameIndex{0};
        std::atomic<uint32_t> lastKickedFrameIndex{0};
        // acquireFrameToken() reads/writes the ring slots from both the main thread
        // (beginReflexFrame/reflexSleep/setMarkerMain) and the render thread
        // (setMarkerRender), which can target the same slot in a frame. The two ring
        // fields (pointer + index) must be read/written as a unit, so guard them with
        // a small mutex — this is off the per-draw hot path (a handful of calls/frame).
        std::mutex reflexTokenMutex;

        // Resolve the FrameToken (void*) for a given monotonic frame index, fetching
        // and caching it in the ring if absent. Returns nullptr when Streamline is down.
        void* acquireFrameToken(uint32_t index);

        static inline bool streamlineAvailable = false;
        static inline bool streamlineInitialized = false;
        static inline UpscaleManager* instance = nullptr;

        void queryFeatureSupport();
    };
}
