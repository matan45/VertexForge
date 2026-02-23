#pragma once

#include "../../services/providers/IVFXRuntimeProvider.hpp"
#include <memory>

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

        // Instance control
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

        size_t getInstanceCount() const override;
    };
}
