#pragma once

#include "../render/vfx/billboard/VFXBillboardTypes.hpp"
#include "../render/vfx/compute/GPUVFXTypes.hpp"
#include "../../services/data/VFXTypes.hpp"
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <memory>
#include <optional>
#include <unordered_map>
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
}

namespace render::mesh
{
    class MeshGPUCache;
}

namespace controllers
{
    using VFXInstanceId = uint32_t;

    struct VFXRuntimeParams
    {
        std::string vfxAssetPath;
        glm::mat4 worldTransform{1.0f};
        bool loop = true;
        uint32_t entityId = 0;
        services::VFXEmitterPriority priority = services::VFXEmitterPriority::Normal;
        bool cameraRelative = false;
    };

    struct VFXRuntimeInstance
    {
        VFXInstanceId id = 0;
        std::unique_ptr<render::vfx::VFXParticleSystem> particleSystem;
        glm::mat4 worldTransform{1.0f};
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
        std::unique_ptr<render::vfx::VFXSceneGPUPipeline> gpuRenderPipeline;
        std::unique_ptr<render::vfx::VFXMeshGPUPipeline> gpuMeshPipeline;
        std::unique_ptr<render::vfx::VFXRibbonGPUPipeline> gpuRibbonPipeline;
        std::unique_ptr<render::vfx::VFXDistortionPipeline> gpuDistortionPipeline;
        std::unique_ptr<render::mesh::MeshGPUCache> gpuMeshCache;
        std::unique_ptr<render::vfx::VFXEmitterPool> emitterPool;

        std::unordered_map<VFXInstanceId, VFXRuntimeInstance> instances;
        std::unordered_map<uint32_t, VFXInstanceId> emitterIndexToInstanceId;

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

        bool distanceCullingEnabled = false;
        float maxVFXDistSq = 0.0f;

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

        void init(vk::RenderPass sceneRenderPass);
        void recreate(vk::RenderPass sceneRenderPass);
        void cleanUp();

        bool isInitialized() const { return initialized; }

        bool isGPUDrivenEnabled() const { return gpuDrivenEnabled; }
        void setGPUDrivenEnabled(bool enabled);

        VFXInstanceId createInstance(const VFXRuntimeParams& params);
        void destroyInstance(VFXInstanceId id);
        void destroyAllInstances();

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
        void initDistortion(vk::RenderPass distortionRenderPass);
        void recreateDistortion(vk::RenderPass distortionRenderPass);
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
        };

        VFXBudgetStats getBudgetStats() const;

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

        bool initGPUMode(vk::RenderPass renderPass);
        void cleanupGPUMode();
        void updateGPU(float deltaTime);
        void recordGPUDrawCommands(vk::CommandBuffer cmd);
        void processPendingEmitterFrees();
        void processEvents();
        void cleanupFinishedSubEmitters(float deltaTime);
        void extractFrustumPlanes(const glm::mat4& viewProj);
        bool isEmitterInFrustum(const VFXRuntimeInstance& instance) const;
        void updateInstanceLOD(VFXRuntimeInstance& instance) const;
        VFXInstanceId findLowestPriorityInstance(services::VFXEmitterPriority belowPriority) const;

        render::vfx::GPUEmitterConfig toGPUConfig(
            const render::vfx::VFXEmitterConfig& cpuConfig,
            float deltaTime,
            uint32_t maxParticles,
            uint32_t seed) const;

        render::vfx::GPUEmitterState toGPUState(
            const VFXRuntimeInstance& instance) const;
    };
}
