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
        std::unordered_map<void*, std::unique_ptr<::controllers::MaterialPreviewController>> materialControllers;
        std::unordered_map<void*, std::unique_ptr<::controllers::MeshPreviewController>> meshControllers;

    public:
        PreviewAdapter();
        ~PreviewAdapter() noexcept override;

        // === Material Preview (IPreviewProvider) ===
        void initMaterialPreview(void* instanceId) override;
        void cleanUpMaterialPreview(void* instanceId) override;
        bool isMaterialPreviewInitialized(void* instanceId) const override;
        void setMaterialParams(void* instanceId, const services::MaterialPreviewParams& params) override;
        services::MaterialPreviewParams getMaterialParams(void* instanceId) const override;
        void updateMaterialCamera(void* instanceId, const glm::mat4& view, const glm::mat4& projection,
                                  const glm::vec3& cameraPos, float time = 0.0f) override;
        void* renderMaterialPreview(void* instanceId) override;
        std::string getMaterialShaderError(void* instanceId) const override;

        // === Mesh Preview (IPreviewProvider) ===
        void initMeshPreview(void* instanceId) override;
        void cleanUpMeshPreview(void* instanceId) override;
        bool isMeshPreviewInitialized(void* instanceId) const override;
        bool loadPreviewMesh(void* instanceId, const std::string& meshPath, math::AABB& outBounds) override;
        void unloadPreviewMesh(void* instanceId) override;
        bool isPreviewMeshLoaded(void* instanceId) const override;
        std::vector<services::SubMeshInfo> getPreviewMeshSubMeshInfo(void* instanceId) const override;
        math::AABB getPreviewMeshBounds(void* instanceId) const override;
        void setMeshPreviewParams(void* instanceId, const services::MeshPreviewParams& params) override;
        void updateMeshCamera(void* instanceId, const glm::mat4& view, const glm::mat4& projection,
                              const glm::vec3& cameraPos) override;
        void* renderMeshPreview(void* instanceId) override;

    private:
        controllers::MaterialPreviewController* getMaterialController(void* instanceId);
        controllers::MaterialPreviewController* getMaterialControllerConst(void* instanceId) const;
        controllers::MeshPreviewController* getMeshController(void* instanceId);
        controllers::MeshPreviewController* getMeshControllerConst(void* instanceId) const;
    };
}
