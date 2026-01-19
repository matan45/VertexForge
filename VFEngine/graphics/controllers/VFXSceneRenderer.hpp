#pragma once

#include "../render/vfx/VFXBillboardTypes.hpp"
#include <glm/glm.hpp>
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
    };

    // VFXSceneRenderer manages all runtime VFX instances and coordinates
    // particle simulation with scene-integrated rendering.
    class VFXSceneRenderer
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<render::vfx::VFXScenePipeline> pipeline;
        std::unordered_map<VFXInstanceId, VFXRuntimeInstance> instances;

        VFXInstanceId nextInstanceId = 1;
        bool initialized = false;

        // Camera data for billboard orientation
        glm::mat4 currentView{1.0f};
        glm::mat4 currentProjection{1.0f};
        glm::vec3 currentCameraPos{0.0f};
        float currentTime = 0.0f;

        // Collected instance data for batch rendering
        std::vector<render::vfx::VFXInstanceData> collectedInstances;

    public:
        explicit VFXSceneRenderer(core::Device& device, core::SwapChain& swapChain);
        ~VFXSceneRenderer();

        // Initialize with scene's render pass
        void init(vk::RenderPass sceneRenderPass);
        void recreate(vk::RenderPass sceneRenderPass);
        void cleanUp();

        bool isInitialized() const { return initialized; }

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

        // Frame update
        void update(float deltaTime);
        void setCamera(const glm::mat4& view, const glm::mat4& projection,
                       const glm::vec3& cameraPos, float time);

        // Draw (called during scene render pass, after meshes)
        void recordDrawCommands(const vk::CommandBuffer& cmd);

        // Stats
        size_t getInstanceCount() const { return instances.size(); }
        size_t getTotalParticleCount() const;

    private:
        void collectAllParticleInstances();
    };
}
