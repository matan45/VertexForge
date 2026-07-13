#pragma once

#include <vulkan/vulkan.hpp>
#include <optional>
#include <vector>
#include "../../data/VFXTypes.hpp"
#include <vfx/VFXScalability.hpp>

namespace services
{

    // Provider interface for runtime VFX rendering in the scene.
    // Unlike IVFXPreviewProvider (which renders offscreen for editor preview windows),
    // this provider integrates VFX directly into the scene render pass.
    class IVFXRuntimeProvider
    {
    public:
        virtual ~IVFXRuntimeProvider() = default;

        // System lifecycle (call once)
        virtual void init(vk::Format colorFormat, vk::Format depthFormat) = 0;
        virtual void cleanUp() = 0;
        virtual void recreate(vk::Format colorFormat, vk::Format depthFormat) = 0;
        virtual bool isInitialized() const = 0;

        // Instance management
        virtual VFXInstanceId createInstance(const VFXRuntimeParams& params) = 0;
        // Optional additive capability so existing test/plugin providers remain source
        // compatible; the runtime adapter overrides both methods.
        virtual VFXInstanceId createChannel(const std::string&, uint32_t) { return 0; }
        virtual void emitToChannel(VFXInstanceId, const VFXChannelEmitParams&) {}
        virtual void destroyInstance(VFXInstanceId id) = 0;

        // Instance control
        virtual void applyInstanceOverrides(VFXInstanceId id, const VFXEmitterOverrides& overrides) = 0;
        virtual void setInstanceTransform(VFXInstanceId id, const glm::mat4& worldTransform) = 0;
        virtual void playInstance(VFXInstanceId id) = 0;
        virtual void stopInstance(VFXInstanceId id) = 0;
        virtual void resetInstance(VFXInstanceId id) = 0;
        virtual bool isInstancePlaying(VFXInstanceId id) const = 0;

        // Frame update (call each frame)
        virtual void update(float deltaTime) = 0;
        virtual void setCamera(const VFXCameraParams& camera) = 0;

        virtual void setSceneDepthImageView(vk::ImageView depthView) = 0;

        // Called before render pass to dispatch compute shaders (GPU mode)
        virtual void recordComputeCommands(const vk::CommandBuffer& cmd) = 0;

        // Called during scene render pass to record VFX draw commands
        virtual void recordDrawCommands(const vk::CommandBuffer& cmd) = 0;

        // Distortion pass support
        virtual bool hasDistortionEmitters() const = 0;
        virtual void recordDistortionDrawCommands(const vk::CommandBuffer& cmd) = 0;
        virtual void initDistortion(vk::Format colorFormat, vk::Format depthFormat) = 0;
        virtual void recreateDistortion(vk::Format colorFormat, vk::Format depthFormat) = 0;

        virtual size_t getInstanceCount() const = 0;

        // Playback state capture/seek for sector streaming
        struct PlaybackState
        {
            float emissionTime = 0.0f;
            float spawnAccumulator = 0.0f;
            bool wasPlaying = true;
            bool wasActive = true;
        };
        virtual std::optional<PlaybackState> capturePlaybackState(VFXInstanceId id) const = 0;
        virtual void seekInstance(VFXInstanceId id, float emissionTime, float spawnAccumulator) = 0;

        // Lighting resources (shared from main renderer)
        virtual void setLightingLayouts(vk::DescriptorSetLayout lightBufferLayout,
                                        vk::DescriptorSetLayout clusterGridLayout,
                                        vk::DescriptorSetLayout clusterLightGridLayout) = 0;
        virtual void updateLightingDescriptorSets(vk::DescriptorSet lightBufferSet,
                                                  vk::DescriptorSet clusterGridSet,
                                                  vk::DescriptorSet clusterLightGridSet) = 0;

        // Distance culling
        virtual void setDistanceCullingEnabled(bool enabled) = 0;
        virtual void setMaxDrawDistance(float distance) = 0;

        // Proxy point lights emitted by playing instances with light emission
        // enabled (explosions/fires illuminating the scene). Collected each
        // frame and injected into the scene light list as transient lights.
        struct VFXProxyLight
        {
            glm::vec3 position{0.0f};
            glm::vec3 color{1.0f};
            float intensity = 1.0f;
            float radius = 10.0f;
        };
        virtual std::vector<VFXProxyLight> getActiveProxyLights() const = 0;

        // Budget stats for debug UI
        struct BudgetStats
        {
            uint32_t activeEmitters = 0;
            uint32_t maxEmitters = 0;
            uint32_t allocatedParticles = 0;
            uint32_t maxParticles = 0;
            uint32_t lodCounts[4] = {0, 0, 0, 0};
            float fragmentationPercent = 0.0f;
            uint32_t poolWarmSlots = 0;
            uint32_t poolUsedSlots = 0;
            uint32_t poolTotalSlots = 0;
            // VK-1453 (Phase 4)
            uint32_t culledEmitters = 0;    // emitters skipped by frustum/distance cull this frame
            uint32_t throttledEmitters = 0; // emitters whose sim was skipped by updateInterval
            float vfxCullDistance = 0.0f;   // active max VFX draw distance (0 => unlimited)
            uint32_t eventsThisFrame = 0;
            uint32_t rawEventsThisFrame = 0;
            uint32_t eventBudget = 0;
            bool eventsDropped = false;
            uint32_t channelListeners = 0;
            uint32_t channelRawRequests = 0;
            uint32_t channelAcceptedRequests = 0;
            uint32_t channelRingDroppedRequests = 0;
            uint32_t channelParticleDroppedRequests = 0;
            uint32_t channelRequestBudget = 0;
        };
        virtual BudgetStats getBudgetStats() const = 0;

        // LOD config
        struct LODConfig
        {
            float lod0Distance = 50.0f;
            float lod1Distance = 100.0f;
            float lod2Distance = 200.0f;
            float transitionZone = 10.0f;
        };
        virtual LODConfig getLODConfig() const = 0;
        virtual void setLODConfig(const LODConfig& config) = 0;

        // VK-1453 (Phase 4) — camera/cull state for service-side pre-spawn culling.
        // The renderer owns the camera; the combo service queries this once per update
        // to decide whether an off-screen fire-and-forget effect should be skipped.
        // Default (valid=false) => callers never cull (safe when there is no renderer).
        struct CullState
        {
            bool valid = false;
            glm::mat4 viewProj{1.0f};
            glm::vec3 cameraPos{0.0f};
            bool distanceCullEnabled = false;
            float maxDrawDistance = 0.0f;
        };
        virtual CullState getCullState() const { return {}; }

        // Global VFX quality tier applied to scalability profiles at instance creation.
        virtual void setQualityTier(vfx::VFXQualityTier tier) { (void)tier; }

        // Per-instance debug snapshot for the VFX debug window (capped by the impl).
        struct InstanceDebugInfo
        {
            VFXInstanceId id = 0;
            glm::vec3 worldPosition{0.0f};
            glm::vec3 extents{0.0f};
            bool inFrustum = true;
            uint8_t lod = 0;
            uint32_t particleCount = 0;
            uint8_t priority = 2;
        };
        virtual std::vector<InstanceDebugInfo> getInstanceDebugInfo() const { return {}; }
    };
}
