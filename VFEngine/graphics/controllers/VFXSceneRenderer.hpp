#pragma once

#include "../render/vfx/VFXBillboardTypes.hpp"
#include "../render/vfx/GPUVFXTypes.hpp"
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <memory>
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
    };

    struct VFXRuntimeInstance
    {
        VFXInstanceId id = 0;
        std::unique_ptr<render::vfx::VFXParticleSystem> particleSystem;
        glm::mat4 worldTransform{1.0f};
        render::vfx::VFXEmitterConfig config;
        bool loop = true;
        bool active = true;

        bool gpuDriven = false;
        uint32_t gpuEmitterIndex = UINT32_MAX;
        uint32_t gpuParticleOffset = 0;
        uint32_t gpuParticleCount = 0;
        float spawnAccumulator = 0.0f;
        float emissionTime = 0.0f;  // Tracks total emission time for looping control
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
        std::unique_ptr<render::mesh::MeshGPUCache> gpuMeshCache;

        std::unordered_map<VFXInstanceId, VFXRuntimeInstance> instances;

        // Deferred destruction queue to avoid per-instance waitIdle()
        // Each entry is (emitterIndex, frameWhenDestroyed)
        std::vector<std::pair<uint32_t, uint32_t>> pendingEmitterFrees;

        VFXInstanceId nextInstanceId = 1;
        bool initialized = false;
        bool gpuDrivenEnabled = true;  // Enable GPU mode by default
        uint32_t frameNumber = 0;
        static constexpr uint32_t FRAMES_BEFORE_FREE = 3;

        glm::mat4 currentView{1.0f};
        glm::mat4 currentProjection{1.0f};
        glm::vec3 currentCameraPos{0.0f};
        float currentTime = 0.0f;

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

        void update(float deltaTime);
        void setCamera(const glm::mat4& view, const glm::mat4& projection,
                       const glm::vec3& cameraPos, float time,
                       float nearPlane = 0.1f, float farPlane = 1000.0f);

        void setSceneDepthImageView(vk::ImageView depthView);

        void recordComputeCommands(vk::CommandBuffer cmd);

        void recordDrawCommands(vk::CommandBuffer cmd);

        size_t getInstanceCount() const { return instances.size(); }
        size_t getTotalParticleCount() const;

    private:
        void collectAllParticleInstances();
        void updateCPU(float deltaTime);
        void recordCPUDrawCommands(vk::CommandBuffer cmd);

        bool initGPUMode(vk::RenderPass renderPass);
        void cleanupGPUMode();
        void updateGPU(float deltaTime);
        void recordGPUDrawCommands(vk::CommandBuffer cmd);
        void processPendingEmitterFrees();

        render::vfx::GPUEmitterConfig toGPUConfig(
            const render::vfx::VFXEmitterConfig& cpuConfig,
            float deltaTime,
            uint32_t maxParticles,
            uint32_t seed) const;

        render::vfx::GPUEmitterState toGPUState(
            const VFXRuntimeInstance& instance) const;
    };
}
