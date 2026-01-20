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

    // Internal structure for each VFX instance
    struct VFXRuntimeInstance
    {
        VFXInstanceId id = 0;
        std::unique_ptr<render::vfx::VFXParticleSystem> particleSystem;
        glm::mat4 worldTransform{1.0f};
        render::vfx::VFXEmitterConfig config;
        bool loop = true;
        bool active = true;

        // GPU mode tracking
        bool gpuDriven = false;
        uint32_t gpuEmitterIndex = UINT32_MAX;
        uint32_t gpuParticleOffset = 0;
        uint32_t gpuParticleCount = 0;
        float spawnAccumulator = 0.0f;
    };

    // VFXSceneRenderer manages all runtime VFX instances and coordinates
    // particle simulation with scene-integrated rendering.
    // Supports both CPU and GPU-driven modes.
    class VFXSceneRenderer
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        // CPU mode (fallback)
        std::unique_ptr<render::vfx::VFXScenePipeline> cpuPipeline;
        std::vector<render::vfx::VFXInstanceData> collectedInstances;

        // GPU mode (compute + indirect draw)
        std::unique_ptr<render::vfx::GPUVFXBufferManager> gpuBufferManager;
        std::unique_ptr<render::vfx::GPUVFXComputePipeline> gpuComputePipeline;
        std::unique_ptr<render::vfx::VFXSceneGPUPipeline> gpuRenderPipeline;

        std::unordered_map<VFXInstanceId, VFXRuntimeInstance> instances;

        VFXInstanceId nextInstanceId = 1;
        bool initialized = false;
        bool gpuDrivenEnabled = true;  // Enable GPU mode by default
        uint32_t frameNumber = 0;

        // Camera data for billboard orientation
        glm::mat4 currentView{1.0f};
        glm::mat4 currentProjection{1.0f};
        glm::vec3 currentCameraPos{0.0f};
        float currentTime = 0.0f;

    public:
        explicit VFXSceneRenderer(core::Device& device, core::SwapChain& swapChain);
        ~VFXSceneRenderer();

        // Initialize with scene's render pass
        void init(vk::RenderPass sceneRenderPass);
        void recreate(vk::RenderPass sceneRenderPass);
        void cleanUp();

        bool isInitialized() const { return initialized; }

        // GPU mode toggle
        bool isGPUDrivenEnabled() const { return gpuDrivenEnabled; }
        void setGPUDrivenEnabled(bool enabled);

        // Instance management
        VFXInstanceId createInstance(const VFXRuntimeParams& params);
        void destroyInstance(VFXInstanceId id);
        void destroyAllInstances();

        // Instance control
        void setInstanceTransform(VFXInstanceId id, const glm::mat4& worldTransform);
        void playInstance(VFXInstanceId id);
        void stopInstance(VFXInstanceId id);
        void resetInstance(VFXInstanceId id);
        bool isInstancePlaying(VFXInstanceId id) const;
        bool isInstanceActive(VFXInstanceId id) const;

        // Frame update (CPU simulation or GPU config upload)
        void update(float deltaTime);
        void setCamera(const glm::mat4& view, const glm::mat4& projection,
                       const glm::vec3& cameraPos, float time);

        // GPU compute (call before render pass)
        void recordComputeCommands(vk::CommandBuffer cmd);

        // Draw (called during scene render pass, after meshes)
        void recordDrawCommands(vk::CommandBuffer cmd);

        // Stats
        size_t getInstanceCount() const { return instances.size(); }
        size_t getTotalParticleCount() const;

    private:
        // CPU mode helpers
        void collectAllParticleInstances();
        void updateCPU(float deltaTime);
        void recordCPUDrawCommands(vk::CommandBuffer cmd);

        // GPU mode helpers
        bool initGPUMode(vk::RenderPass renderPass);
        void cleanupGPUMode();
        void updateGPU(float deltaTime);
        void recordGPUDrawCommands(vk::CommandBuffer cmd);

        // Config conversion
        render::vfx::GPUEmitterConfig toGPUConfig(
            const render::vfx::VFXEmitterConfig& cpuConfig,
            float deltaTime,
            uint32_t maxParticles,
            uint32_t seed) const;

        render::vfx::GPUEmitterState toGPUState(
            const VFXRuntimeInstance& instance) const;
    };
}
