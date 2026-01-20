#pragma once

#include "../../services/providers/IVFXRuntimeProvider.hpp"
#include <memory>

namespace controllers
{
    class VFXSceneRenderer;
}

namespace core
{
    // Adapter implementing IVFXRuntimeProvider by delegating to VFXSceneRenderer.
    // Unlike VFXPreviewAdapter (which manages multiple controllers for preview windows),
    // this adapter wraps a single VFXSceneRenderer that manages all VFX instances in the scene.
    class VFXRuntimeAdapter : public services::IVFXRuntimeProvider
    {
    private:
        std::unique_ptr<controllers::VFXSceneRenderer> renderer;

    public:
        explicit VFXRuntimeAdapter();
        ~VFXRuntimeAdapter() noexcept override;

        // System lifecycle
        void init(vk::RenderPass sceneRenderPass) override;
        void cleanUp() override;
        void recreate(vk::RenderPass sceneRenderPass) override;
        bool isInitialized() const override;

        // Instance management
        services::VFXInstanceId createInstance(const services::VFXRuntimeParams& params) override;
        void destroyInstance(services::VFXInstanceId id) override;
        void destroyAllInstances() override;

        // Instance control
        void setInstanceTransform(services::VFXInstanceId id, const glm::mat4& worldTransform) override;
        void playInstance(services::VFXInstanceId id) override;
        void stopInstance(services::VFXInstanceId id) override;
        void resetInstance(services::VFXInstanceId id) override;
        bool isInstancePlaying(services::VFXInstanceId id) const override;
        bool isInstanceActive(services::VFXInstanceId id) const override;

        // Frame update
        void update(float deltaTime) override;
        void setCamera(const glm::mat4& view, const glm::mat4& projection,
                       const glm::vec3& cameraPos, float time) override;

        // Compute commands (call before render pass)
        void recordComputeCommands(const vk::CommandBuffer& cmd) override;

        // Draw commands (call during render pass)
        void recordDrawCommands(const vk::CommandBuffer& cmd) override;

        // Stats
        size_t getInstanceCount() const override;
        size_t getTotalParticleCount() const override;
    };
}
