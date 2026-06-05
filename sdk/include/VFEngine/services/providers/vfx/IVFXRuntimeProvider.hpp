#pragma once

#include <vulkan/vulkan.hpp>
#include <optional>
#include "../../data/VFXTypes.hpp"

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
    };
}
