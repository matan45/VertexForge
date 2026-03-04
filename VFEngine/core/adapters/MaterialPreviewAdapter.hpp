#pragma once
#include "../../services/providers/IMaterialPreviewProvider.hpp"
#include "../../graphics/controllers/preview/MaterialPreviewController.hpp"
#include <memory>
#include <unordered_map>

namespace core
{
    class MaterialPreviewAdapter : public services::IMaterialPreviewProvider
    {
    private:
        std::unordered_map<services::PreviewInstanceId, std::unique_ptr<::controllers::MaterialPreviewController>>
        controllers;

    public:
        explicit MaterialPreviewAdapter() = default;
        ~MaterialPreviewAdapter() noexcept override;

        void initMaterialPreview(services::PreviewInstanceId instanceId) override;
        void cleanUpMaterialPreview(services::PreviewInstanceId instanceId) override;
        bool isMaterialPreviewInitialized(services::PreviewInstanceId instanceId) const override;
        void setMaterialParams(services::PreviewInstanceId instanceId,
                               const services::MaterialPreviewParams& params) override;
        services::MaterialPreviewParams getMaterialParams(services::PreviewInstanceId instanceId) const override;
        void updateMaterialCamera(services::PreviewInstanceId instanceId, const glm::mat4& view,
                                  const glm::mat4& projection,
                                  const glm::vec3& cameraPos, float time = 0.0f) override;
        void* renderMaterialPreview(services::PreviewInstanceId instanceId) override;
        std::string getMaterialShaderError(services::PreviewInstanceId instanceId) const override;

    private:
        controllers::MaterialPreviewController* getController(services::PreviewInstanceId instanceId) const;
    };
}
