#pragma once

#include "../../services/providers/vfx/IVFXRuntimeProvider.hpp"
#include "../../services/events/EventTypes.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace controllers
{
    class VFXSceneRenderer;
}

namespace core
{

    class VFXRuntimeAdapter : public services::IVFXRuntimeProvider
    {
    private:
        std::unique_ptr<controllers::VFXSceneRenderer> renderer;

        void updateSceneColliders();
        void updateTerrainHeightfield();
        void subscribeTerrainNotifications();
        void unsubscribeTerrainNotifications();

        bool terrainHeightfieldCached = false;
        uint32_t cachedMaxColliders = 32;
        uint32_t colliderSettingsRefreshCounter = 0;
        std::vector<events::SubscriptionToken> terrainSubscriptions;

        // VK-1453 — config-cache invalidations queued from the AssetSaved notification
        // (published on the editor thread) and applied on the update thread.
        std::mutex configInvalidationMutex;
        std::vector<std::string> pendingConfigInvalidations;

        // Cached lighting layouts (set before renderer exists)
        vk::DescriptorSetLayout pendingLightBufferLayout;
        vk::DescriptorSetLayout pendingClusterGridLayout;
        vk::DescriptorSetLayout pendingClusterLightGridLayout;
        bool hasPendingLightingLayouts = false;

    public:
        explicit VFXRuntimeAdapter();
        ~VFXRuntimeAdapter() noexcept override;

        // System lifecycle
        void init(vk::Format colorFormat, vk::Format depthFormat) override;
        void cleanUp() override;
        void recreate(vk::Format colorFormat, vk::Format depthFormat) override;
        bool isInitialized() const override;

        // Instance management
        services::VFXInstanceId createInstance(const services::VFXRuntimeParams& params) override;
        services::VFXInstanceId createChannel(const std::string& path, uint32_t particlesPerRequest) override;
        void emitToChannel(services::VFXInstanceId id, const services::VFXChannelEmitParams& params) override;
        void destroyInstance(services::VFXInstanceId id) override;

        // Instance control
        void applyInstanceOverrides(services::VFXInstanceId id, const services::VFXEmitterOverrides& overrides) override;
        void setInstanceTransform(services::VFXInstanceId id, const glm::mat4& worldTransform) override;
        void playInstance(services::VFXInstanceId id) override;
        void stopInstance(services::VFXInstanceId id) override;
        void resetInstance(services::VFXInstanceId id) override;
        bool isInstancePlaying(services::VFXInstanceId id) const override;

        // Frame update
        void update(float deltaTime) override;
        void setCamera(const services::VFXCameraParams& camera) override;
        void setSceneDepthImageView(vk::ImageView depthView) override;

        // Compute commands (call before render pass)
        void recordComputeCommands(const vk::CommandBuffer& cmd) override;

        // Draw commands (call during render pass)
        void recordDrawCommands(const vk::CommandBuffer& cmd) override;

        // Distortion pass
        bool hasDistortionEmitters() const override;
        void recordDistortionDrawCommands(const vk::CommandBuffer& cmd) override;
        void initDistortion(vk::Format colorFormat, vk::Format depthFormat) override;
        void recreateDistortion(vk::Format colorFormat, vk::Format depthFormat) override;

        size_t getInstanceCount() const override;

        void setLightingLayouts(vk::DescriptorSetLayout lightBufferLayout,
                                vk::DescriptorSetLayout clusterGridLayout,
                                vk::DescriptorSetLayout clusterLightGridLayout) override;
        void updateLightingDescriptorSets(vk::DescriptorSet lightBufferSet,
                                          vk::DescriptorSet clusterGridSet,
                                          vk::DescriptorSet clusterLightGridSet) override;

        void setDistanceCullingEnabled(bool enabled) override;
        void setMaxDrawDistance(float distance) override;

        std::vector<VFXProxyLight> getActiveProxyLights() const override;

        BudgetStats getBudgetStats() const override;

        LODConfig getLODConfig() const override;
        void setLODConfig(const LODConfig& config) override;

        std::optional<PlaybackState> capturePlaybackState(services::VFXInstanceId id) const override;
        void seekInstance(services::VFXInstanceId id, float emissionTime, float spawnAccumulator) override;

        // VK-1453 (Phase 4)
        CullState getCullState() const override;
        void setQualityTier(vfx::VFXQualityTier tier) override;
        std::vector<InstanceDebugInfo> getInstanceDebugInfo() const override;
    };
}
