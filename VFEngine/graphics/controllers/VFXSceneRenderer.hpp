#pragma once

#include "../render/vfx/billboard/VFXBillboardTypes.hpp"
#include "../render/vfx/compute/GPUVFXTypes.hpp"
#include "../../services/data/VFXTypes.hpp"
#include "vfx/VFXScalability.hpp"
#include "vfx/VFXHandlePool.hpp"
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <string>

namespace core
{
    class Device;
    class SwapChain;
}

namespace render::vfx
{
    class VFXScenePipeline;
    class VFXParticleSystem;
    class GPUVFXBufferManager;
    class GPUVFXComputePipeline;
    class VFXSceneGPUPipeline;
    class VFXMeshGPUPipeline;
    class VFXRibbonGPUPipeline;
    class VFXEmitterPool;
    class VFXDistortionPipeline;
    class VFXBindlessTextures;
}

namespace render::mesh
{
    class MeshGPUCache;
}

namespace controllers
{
    using VFXInstanceId = uint32_t;

    struct VFXEmitterOverrides
    {
        std::optional<float> spawnRate;
        std::optional<float> lifetime;
        std::optional<float> startSize;
        std::optional<float> startSpeed;
        std::optional<float> stretchMultiplier;
        std::optional<glm::vec3> emitDirection;
        std::optional<glm::vec4> startColor;
        std::optional<glm::vec3> windDirection;
        std::optional<float> windStrength;
        std::optional<float> gravityStrength;
        std::optional<glm::vec3> gravityDirection;
        std::optional<int> renderMode;
        std::optional<float> softParticleDistance;
        std::optional<float> lightingInfluence;
        std::optional<bool> collisionEnabled;
        std::optional<float> collisionLifetimeLoss;
        std::optional<glm::vec3> shapeDimensions;
        std::optional<float> coneSpread;
    };

    struct VFXRuntimeParams
    {
        std::string vfxAssetPath;
        glm::mat4 worldTransform{1.0f};
        bool loop = true;
        uint32_t entityId = 0;
        services::VFXEmitterPriority priority = services::VFXEmitterPriority::Normal;
        bool cameraRelative = false;
        bool autoDestroy = false;
        uint32_t seed = 0; // VK-1451: 0 => random seed chosen once at creation
        bool poolable = false; // VK-1453: opt into dormant-instance reuse (fire-and-forget only)
        std::optional<glm::vec3> injectedEmitterVelocity;
        std::optional<glm::vec4> startColorMultiplier;
        std::optional<float> startSizeMultiplier;
    };

    struct VFXRuntimeInstance
    {
        VFXInstanceId id = 0;
        std::unique_ptr<render::vfx::VFXParticleSystem> particleSystem;
        glm::mat4 worldTransform{1.0f};
        glm::mat4 prevWorldTransform{1.0f};
        glm::vec3 emitterVelocity{0.0f};
        render::vfx::VFXEmitterConfig config;
        bool loop = true;
        bool active = true;
        uint32_t entityId = 0;

        bool gpuDriven = false;
        uint32_t gpuEmitterIndex = UINT32_MAX;
        uint32_t gpuParticleOffset = 0;
        uint32_t gpuParticleCount = 0;
        float spawnAccumulator = 0.0f;
        float emissionTime = 0.0f;
        bool firstFrame = true;
        services::VFXEmitterPriority priority = services::VFXEmitterPriority::Normal;
        bool cameraRelative = false;

        uint8_t currentLOD = 0;
        float lodSpawnMultiplier = 1.0f;
        float lodBias = 0.0f;
        bool burstClampWarned = false;
        bool autoDestroy = false;
        uint32_t seed = 0; // VK-1451: stable per-instance RNG seed (set once at creation)
        std::optional<glm::vec3> injectedEmitterVelocity;

        // VK-1453 (Phase 4) — pooling + scalability per-instance state.
        bool poolable = false;                // opt into dormant reuse (fire-and-forget only)
        bool dormant = false;                 // retired-but-retained for later revival
        std::string assetPath;                // source .vfVFX path (dormant-pool key)
        float cullDistanceSqOverride = -1.0f; // <0 => use the global VFX cull distance
        int updateInterval = 1;               // >1 => simulate only every Nth frame
        int updatePhase = 0;                  // frame offset so throttled emitters spread out

        // VK-1500: a channel listener owns one large GPU slice and consumes batched
        // world-space spawn requests instead of running the authored emission schedule.
        bool channelListener = false;
        uint32_t channelParticlesPerRequest = 0;
        uint32_t channelRequestBase = 0;
        uint32_t channelAcceptedRequests = 0;
        std::vector<render::vfx::GPUVFXSpawnRequest> pendingChannelRequests;
    };

    class VFXSceneRenderer
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<render::vfx::VFXScenePipeline> cpuPipeline;
        std::vector<render::vfx::VFXInstanceData> collectedInstances;

        std::unique_ptr<render::vfx::GPUVFXBufferManager> gpuBufferManager;
        std::unique_ptr<render::vfx::GPUVFXComputePipeline> gpuComputePipeline;
        // VK-1481: shared bindless texture table for all four GPU pipelines. Declared before them so
        // it is constructed first / destroyed last; each pipeline holds a raw pointer into it.
        std::unique_ptr<render::vfx::VFXBindlessTextures> bindlessTextures;
        std::unique_ptr<render::vfx::VFXSceneGPUPipeline> gpuRenderPipeline;
        std::unique_ptr<render::vfx::VFXMeshGPUPipeline> gpuMeshPipeline;
        std::unique_ptr<render::vfx::VFXRibbonGPUPipeline> gpuRibbonPipeline;
        std::unique_ptr<render::vfx::VFXDistortionPipeline> gpuDistortionPipeline;
        std::unique_ptr<render::mesh::MeshGPUCache> gpuMeshCache;
        std::unique_ptr<render::vfx::VFXEmitterPool> emitterPool;

        std::unordered_map<VFXInstanceId, VFXRuntimeInstance> instances;
        std::unordered_map<uint32_t, VFXInstanceId> emitterIndexToInstanceId;

        // Stable listener ordering makes global-ring overflow deterministic. The
        // rotating start index prevents one busy channel from starving later ones.
        std::unordered_map<std::string, VFXInstanceId> channelsByPath;
        std::vector<VFXInstanceId> channelOrder;
        size_t channelRoundRobinStart = 0;
        uint64_t channelEmitSequence = 0;
        uint32_t channelRawRequestsThisFrame = 0;
        uint32_t channelAcceptedRequestsThisFrame = 0;
        uint32_t channelRingDroppedThisFrame = 0;
        uint32_t channelParticleDroppedThisFrame = 0;

        // VK-1460: emitter slots whose draw command was left live last frame. Used by the
        // selective draw-command clear in recordComputeCommands so a temporally-throttled
        // emitter keeps its (persistent, per-emitter) command on frames it skips dispatch,
        // while slots that transitioned to hidden (culled/inactive) are zeroed.
        std::unordered_set<uint32_t> liveDrawSlots;

        // VK-1453 (Phase 4) — parsed-config cache (skip re-reading .vfVFX on repeat
        // spawns) + dormant fire-and-forget instance pool for cheap reuse.
        std::unordered_map<std::string, render::vfx::VFXEmitterConfig> configCache;
        vfx::VFXHandlePool instancePool;

        // Deferred destruction queue to avoid per-instance waitIdle()
        // Each entry is (emitterIndex, frameWhenDestroyed)
        std::vector<std::pair<uint32_t, uint32_t>> pendingEmitterFrees;

        VFXInstanceId nextInstanceId = 1;
        bool initialized = false;
        bool gpuDrivenEnabled = true;
        uint32_t frameNumber = 0;
        static constexpr uint32_t FRAMES_BEFORE_FREE = 3;

        std::vector<render::vfx::GPUVFXEvent> lastFrameEvents;
        uint32_t lastFrameEventCount = 0;
        uint32_t lastFrameRawEventCount = 0;

        static constexpr uint32_t MAX_SUB_EMITTERS_PER_PARENT = 32;

        struct SubEmitterInstance
        {
            VFXInstanceId parentId = 0;
            VFXInstanceId subId = 0;
            float lifetime = 0.0f;
            float maxLifetime = 5.0f;
            bool finished = false;
        };

        std::vector<SubEmitterInstance> activeSubEmitters;

        glm::mat4 currentView{1.0f};
        glm::mat4 currentProjection{1.0f};
        glm::vec3 currentCameraPos{0.0f};
        float currentTime = 0.0f;

        uint32_t activeDistortionCount = 0;

        bool distanceCullingEnabled = false;
        float maxVFXDistSq = 0.0f;

        // VK-1453 (Phase 4) — global quality tier applied to scalability profiles at
        // instance creation, plus per-frame cull/throttle counters for the debug UI.
        vfx::VFXQualityTier currentTier = vfx::VFXQualityTier::High;
        uint32_t culledEmittersThisFrame = 0;
        uint32_t throttledEmittersThisFrame = 0;

        glm::vec4 frustumPlanes[6]{};
        bool frustumPlanesValid = false;

        float LOD0_DIST = 50.0f;
        float LOD1_DIST = 100.0f;
        float LOD2_DIST = 200.0f;
        float LOD_TRANSITION_ZONE = 10.0f;

        std::vector<render::vfx::GPUCollider> sceneColliders;
        uint32_t sceneColliderCount = 0;

        render::vfx::GPUTerrainHeightfield terrainHeader{};
        std::vector<float> terrainHeights;
        bool terrainDirty = true;

        // Cached lighting layouts (set before init, applied when pipelines are created)
        vk::DescriptorSetLayout cachedLightBufferLayout;
        vk::DescriptorSetLayout cachedClusterGridLayout;
        vk::DescriptorSetLayout cachedClusterLightGridLayout;
        bool hasLightingLayouts = false;

    public:
        explicit VFXSceneRenderer(core::Device& device, core::SwapChain& swapChain);
        ~VFXSceneRenderer();

        void init(vk::Format colorFormat, vk::Format depthFormat);
        void recreate(vk::Format colorFormat, vk::Format depthFormat);
        void cleanUp();

        bool isInitialized() const { return initialized; }

        bool isGPUDrivenEnabled() const { return gpuDrivenEnabled; }
        void setGPUDrivenEnabled(bool enabled);

        VFXInstanceId createInstance(const VFXRuntimeParams& params);
        VFXInstanceId createChannel(const std::string& path, uint32_t particlesPerRequest = 0);
        void emitToChannel(VFXInstanceId id,
                           const glm::vec3& position,
                           float scale,
                           const glm::vec3& direction,
                           uint32_t packedTint,
                           bool hasTint);
        void destroyInstance(VFXInstanceId id);
        void destroyAllInstances();

        void applyInstanceOverrides(VFXInstanceId id, const VFXEmitterOverrides& overrides);
        void setInstanceTransform(VFXInstanceId id, const glm::mat4& worldTransform);
        void playInstance(VFXInstanceId id);
        void stopInstance(VFXInstanceId id);
        void resetInstance(VFXInstanceId id);
        bool isInstancePlaying(VFXInstanceId id) const;
        bool isInstanceActive(VFXInstanceId id) const;

        struct PlaybackState
        {
            float emissionTime = 0.0f;
            float spawnAccumulator = 0.0f;
            bool wasPlaying = true;
            bool wasActive = true;
        };
        std::optional<PlaybackState> capturePlaybackState(VFXInstanceId id) const;
        void seekInstance(VFXInstanceId id, float emissionTime, float spawnAccumulator);

        void update(float deltaTime);
        void setCamera(const services::VFXCameraParams& camera);

        void setSceneDepthImageView(vk::ImageView depthView);
        void setSceneColliders(const std::vector<render::vfx::GPUCollider>& colliders);
        void setTerrainHeightfield(const render::vfx::GPUTerrainHeightfield& header,
                                    std::vector<float> heights);

        void recordComputeCommands(vk::CommandBuffer cmd);

        void recordDrawCommands(vk::CommandBuffer cmd);

        bool hasDistortionEmitters() const;
        void initDistortion(vk::Format colorFormat, vk::Format depthFormat);
        void recreateDistortion(vk::Format colorFormat, vk::Format depthFormat);
        void recordDistortionDrawCommands(vk::CommandBuffer cmd);

        size_t getInstanceCount() const { return instances.size(); }
        size_t getTotalParticleCount() const;

        void setLightingLayouts(vk::DescriptorSetLayout lightBufferLayout,
                                vk::DescriptorSetLayout clusterGridLayout,
                                vk::DescriptorSetLayout clusterLightGridLayout);
        void updateLightingDescriptorSets(vk::DescriptorSet lightBufferSet,
                                          vk::DescriptorSet clusterGridSet,
                                          vk::DescriptorSet clusterLightGridSet);

        void setDistanceCullingEnabled(bool enabled) { distanceCullingEnabled = enabled; }
        void setMaxDrawDistance(float distance) { maxVFXDistSq = distance * distance; }

        // VK-1453 (Phase 4) — global VFX quality tier. Selects the scalability level for
        // each instance at creation; existing instances are not retroactively re-scaled.
        void setQualityTier(vfx::VFXQualityTier tier) { currentTier = tier; }

        // Camera + cull-state snapshot so the combo service can pre-cull off-screen
        // fire-and-forget effects before spawning them.
        struct VFXCullState
        {
            bool valid = false;
            glm::mat4 viewProj{1.0f};
            glm::vec3 cameraPos{0.0f};
            bool distanceCullEnabled = false;
            float maxDrawDistance = 0.0f;
        };
        VFXCullState getCullState() const;

        // Drop a cached parsed config so the next spawn of `path` re-reads it from disk.
        void invalidateConfigCache(const std::string& path);

        struct VFXProxyLight
        {
            glm::vec3 position{0.0f};
            glm::vec3 color{1.0f};
            float intensity = 1.0f;
            float radius = 10.0f;
        };
        std::vector<VFXProxyLight> getActiveProxyLights() const;

        struct VFXBudgetStats
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

        VFXBudgetStats getBudgetStats() const;

        // VK-1453 (Phase 4) — per-instance snapshot for the VFX debug window (capped).
        struct VFXInstanceDebugInfo
        {
            VFXInstanceId id = 0;
            glm::vec3 worldPosition{0.0f};
            glm::vec3 extents{0.0f};
            bool inFrustum = true;
            uint8_t lod = 0;
            uint32_t particleCount = 0;
            uint8_t priority = 2;
        };
        std::vector<VFXInstanceDebugInfo> getInstanceDebugInfo() const;

        struct VFXLODConfig
        {
            float lod0Distance = 50.0f;
            float lod1Distance = 100.0f;
            float lod2Distance = 200.0f;
            float transitionZone = 10.0f;
        };

        VFXLODConfig getLODConfig() const;
        void setLODConfig(const VFXLODConfig& config);

    private:
        void collectAllParticleInstances();
        void updateCPU(float deltaTime);
        void recordCPUDrawCommands(vk::CommandBuffer cmd);

        bool initGPUMode(vk::Format colorFormat, vk::Format depthFormat);
        void cleanupGPUMode();
        void updateGPU(float deltaTime);
        void recordGPUDrawCommands(vk::CommandBuffer cmd);
        void processPendingEmitterFrees();
        void processEvents();
        void cleanupFinishedSubEmitters(float deltaTime);
        void extractFrustumPlanes(const glm::mat4& viewProj);
        bool isEmitterInFrustum(const VFXRuntimeInstance& instance) const;
        bool isEmitterDistanceCulled(const VFXRuntimeInstance& instance) const;
        void updateInstanceLOD(VFXRuntimeInstance& instance) const;
        VFXInstanceId findLowestPriorityInstance(services::VFXEmitterPriority belowPriority) const;

        // VK-1453 (Phase 4) helpers.
        render::vfx::VFXEmitterConfig loadConfigCached(const std::string& path);
        uint32_t pickInstanceSeed(uint32_t explicitSeed);
        void configureInstanceEmitter(VFXInstanceId id, VFXRuntimeInstance& instance,
                                      services::VFXEmitterPriority priority, int maxParticlesCap);
        void reviveDormantInstance(VFXRuntimeInstance& instance, const VFXRuntimeParams& params,
                                   const render::vfx::VFXEmitterConfig& baseConfig,
                                   const vfx::VFXScalabilityLevel& level);
        void retireInstanceToDormant(VFXInstanceId id);

        render::vfx::GPUEmitterConfig toGPUConfig(
            const render::vfx::VFXEmitterConfig& cpuConfig,
            float deltaTime,
            uint32_t maxParticles,
            uint32_t seed) const;

        render::vfx::GPUEmitterState toGPUState(
            const VFXRuntimeInstance& instance) const;
    };
}
