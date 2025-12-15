#pragma once
#include "../../services/providers/IPreviewProvider.hpp"
#include <memory>
#include <unordered_map>

namespace controllers {
    class MaterialPreviewController;
    class MeshPreviewController;
}

namespace core {

    /**
     * @brief Adapter that implements IPreviewProvider by wrapping preview controllers.
     *
     * This class bridges the Services layer with the Graphics layer's preview controllers,
     * allowing Services to provide preview functionality without direct dependencies on Graphics.
     *
     * Supports multiple instances via instanceId parameter - each instance gets its own
     * independent controller (e.g., for multiple editor windows).
     */
    class PreviewAdapter : public services::IPreviewProvider {
    public:
        PreviewAdapter();
        ~PreviewAdapter() override;

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
        // Maps instanceId -> controller for multi-instance support
        std::unordered_map<void*, std::unique_ptr<::controllers::MaterialPreviewController>> materialControllers;
        std::unordered_map<void*, std::unique_ptr<::controllers::MeshPreviewController>> meshControllers;

        // Helper methods to get or create controllers
        ::controllers::MaterialPreviewController* getMaterialController(void* instanceId);
        ::controllers::MaterialPreviewController* getMaterialControllerConst(void* instanceId) const;
        ::controllers::MeshPreviewController* getMeshController(void* instanceId);
        ::controllers::MeshPreviewController* getMeshControllerConst(void* instanceId) const;
    };

}
