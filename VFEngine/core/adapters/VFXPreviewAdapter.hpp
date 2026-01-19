#pragma once

#include "../../services/providers/IVFXPreviewProvider.hpp"
#include "../../graphics/controllers/VFXPreviewController.hpp"
#include <memory>
#include <unordered_map>

namespace core
{
    class VFXPreviewAdapter : public services::IVFXPreviewProvider
    {
    private:
        std::unordered_map<services::PreviewInstanceId, std::unique_ptr<::controllers::VFXPreviewController>>
            controllers;

    public:
        explicit VFXPreviewAdapter() = default;
        ~VFXPreviewAdapter() noexcept override;

        void initVFXPreview(services::PreviewInstanceId instanceId) override;
        void cleanUpVFXPreview(services::PreviewInstanceId instanceId) override;
        bool isVFXPreviewInitialized(services::PreviewInstanceId instanceId) const override;

        void setVFXParams(services::PreviewInstanceId instanceId, const services::VFXPreviewParams& params) override;
        services::VFXPreviewParams getVFXParams(services::PreviewInstanceId instanceId) const override;

        void updateVFXCamera(services::PreviewInstanceId instanceId, const glm::mat4& view,
                             const glm::mat4& projection, const glm::vec3& cameraPos, float time) override;
        void updateVFXSimulation(services::PreviewInstanceId instanceId, float deltaTime) override;

        void playVFX(services::PreviewInstanceId instanceId) override;
        void pauseVFX(services::PreviewInstanceId instanceId) override;
        void stopVFX(services::PreviewInstanceId instanceId) override;
        bool isVFXPlaying(services::PreviewInstanceId instanceId) const override;

        void* renderVFXPreview(services::PreviewInstanceId instanceId) override;

    private:
        controllers::VFXPreviewController* getController(services::PreviewInstanceId instanceId) const;
    };
}
