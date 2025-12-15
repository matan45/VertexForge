#pragma once
#include "../../services/providers/IPreviewProvider.hpp"
#include <memory>
#include <unordered_map>

namespace controllers
{
    class MaterialPreviewController;
    class MeshPreviewController;
}

namespace core
{
    class PreviewAdapter : public services::IPreviewProvider
    {
    private:
        std::unordered_map<services::PreviewInstanceId, std::unique_ptr<::controllers::MaterialPreviewController>> materialControllers;
        std::unordered_map<services::PreviewInstanceId, std::unique_ptr<::controllers::MeshPreviewController>> meshControllers;

    public:
        PreviewAdapter();
        ~PreviewAdapter() noexcept override;

        // === Material Preview (IPreviewProvider) ===
        void initMaterialPreview(services::PreviewInstanceId instanceId) override;
        void cleanUpMaterialPreview(services::PreviewInstanceId instanceId) override;
        bool isMaterialPreviewInitialized(services::PreviewInstanceId instanceId) const override;
        void setMaterialParams(services::PreviewInstanceId instanceId, const services::MaterialPreviewParams& params) override;
        services::MaterialPreviewParams getMaterialParams(services::PreviewInstanceId instanceId) const override;
        void updateMaterialCamera(services::PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                  const glm::vec3& cameraPos, float time = 0.0f) override;
        void* renderMaterialPreview(services::PreviewInstanceId instanceId) override;
        std::string getMaterialShaderError(services::PreviewInstanceId instanceId) const override;

        // === Mesh Preview (IPreviewProvider) ===
        void initMeshPreview(services::PreviewInstanceId instanceId) override;
        void cleanUpMeshPreview(services::PreviewInstanceId instanceId) override;
        bool isMeshPreviewInitialized(services::PreviewInstanceId instanceId) const override;
        bool loadPreviewMesh(services::PreviewInstanceId instanceId, const std::string& meshPath, math::AABB& outBounds) override;
        void unloadPreviewMesh(services::PreviewInstanceId instanceId) override;
        bool isPreviewMeshLoaded(services::PreviewInstanceId instanceId) const override;
        std::vector<services::SubMeshInfo> getPreviewMeshSubMeshInfo(services::PreviewInstanceId instanceId) const override;
        math::AABB getPreviewMeshBounds(services::PreviewInstanceId instanceId) const override;
        void setMeshPreviewParams(services::PreviewInstanceId instanceId, const services::MeshPreviewParams& params) override;
        void updateMeshCamera(services::PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                              const glm::vec3& cameraPos) override;
        void* renderMeshPreview(services::PreviewInstanceId instanceId) override;

    private:
        controllers::MaterialPreviewController* getMaterialController(services::PreviewInstanceId instanceId);
        controllers::MaterialPreviewController* getMaterialControllerConst(services::PreviewInstanceId instanceId) const;
        controllers::MeshPreviewController* getMeshController(services::PreviewInstanceId instanceId);
        controllers::MeshPreviewController* getMeshControllerConst(services::PreviewInstanceId instanceId) const;
    };
}
